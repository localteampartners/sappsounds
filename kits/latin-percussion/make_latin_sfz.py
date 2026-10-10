#!/usr/bin/env python3
"""make_latin_sfz.py — generate Latin-Percussion.sfz (SappKit's "Latin Percussion").

GM-mapped Latin / Afro-Cuban / Brazilian percussion over three CC0 sources,
measured so every instrument lands at a musical level relative to the others:

  * Virtuosity Drums, percussion + snare cross-stick (Versilian Studios, CC0)
      https://github.com/sfzinstruments/virtuosity_drums
  * VCSL — Versilian Community Sample Library (Versilian Studios, CC0)
      https://github.com/sgossner/VCSL   (pinned in fetch-library.sh)
  * FreePats World Percussion (FreePats project, CC0)
      https://github.com/freepats/world-percussion
  * Body Percussion (sfzinstruments, CC0)
      https://github.com/sfzinstruments/body_percussion

Run on a box that has fetched them (`fetch-library.sh get latin-percussion`
puts them under ~/Samples). It reads the samples, measures them, and writes
Latin-Percussion.sfz next to this script; that file is committed and copied
into ~/Samples/latin-percussion/ by fetch-library.sh. Paths in the SFZ are
relative to ~/Samples/latin-percussion/ (siblings are reached with ../).

    python3 -I make_latin_sfz.py [--samples ~/Samples]

Requires ffmpeg on PATH (decode only). Deterministic: same samples, same file.
"""
from __future__ import annotations

import argparse
import array
import math
import os
import re
import subprocess
import sys
from dataclasses import dataclass, field

HERE = os.path.dirname(os.path.abspath(__file__))
KIT = "latin-percussion"
VP = "../virtuosity-drums/Samples/perc"          # Virtuosity percussion, by mic
VS = "../virtuosity-drums/Samples"               # Virtuosity kit (snare mic etc.)
FP = "../freepats-world-percussion/samples"
BP = "../body-percussion/Samples/body"
VC = "samples/vcsl"                              # fetched into the kit folder
KIT_HEADROOM_DB = 4.0                            # every target sits this far lower


# ----------------------------------------------------------------- measuring --
def decode(path: str) -> array.array:
    out = subprocess.run(["ffmpeg", "-v", "error", "-i", path, "-ac", "1", "-ar", "48000",
                          "-f", "f32le", "-"], check=True, capture_output=True).stdout
    a = array.array("f")
    a.frombytes(out)
    return a


_cache: dict[str, tuple[float, float]] = {}


def measure(path: str) -> tuple[float, float]:
    """(peak, rms) — rms over the 120 ms after the envelope peak, which is the
    part of a percussion hit the ear judges loudness by."""
    if path in _cache:
        return _cache[path]
    x = decode(path)
    if not x:
        raise SystemExit(f"empty or undecodable: {path}")
    peak_i = max(range(len(x)), key=lambda i: abs(x[i]))
    peak = abs(x[peak_i])
    seg = x[max(0, peak_i - 240):peak_i + 5760]
    rms = math.sqrt(sum(v * v for v in seg) / max(1, len(seg)))
    _cache[path] = (peak, rms)
    return _cache[path]


def db(v: float) -> float:
    return 20.0 * math.log10(max(v, 1e-9))


# ----------------------------------------------------------------- the model --
@dataclass
class Layer:
    """One velocity layer: its round robins (in sequence order)."""
    files: list[str]


@dataclass
class Mic:
    layers: list[Layer]          # soft -> loud
    gain_db: float = 0.0         # relative to the primary mic
    pan: float = 0.0


@dataclass
class Sound:
    note: int
    name: str
    mics: list[Mic]
    target_db: float             # RMS target of the loudest layer, dBFS
    extra: str = ""              # extra opcodes on every region (chokes, tune, envelopes)
    veltrack: int = 60
    alternate: bool = False      # layers are ALTERNATING strokes, not dynamics
    comment: str = ""
    extra_regions: list[str] = field(default_factory=list)  # layered extras (jingles)


def ls(rel_dir: str, pattern: str, root: str) -> list[str]:
    full = os.path.join(root, KIT, rel_dir)
    rx = re.compile(pattern)
    files = sorted(f for f in os.listdir(full) if rx.search(f))
    if not files:
        raise SystemExit(f"no files match {pattern!r} in {full}")
    return [f"{rel_dir}/{f}" for f in files]


def natural(s: str):
    return [int(t) if t.isdigit() else t for t in re.split(r"(\d+)", s)]


def vl_layers(rel_dir: str, stem: str, root: str, vel_rx=r"_v(?:l)?(\d+)", rr_rx=r"_rr(\d+)") -> list[Layer]:
    """Group files of one articulation into velocity layers by their vN/vlN tag."""
    files = ls(rel_dir, "^" + re.escape(stem), root)
    by: dict[int, list[str]] = {}
    for f in files:
        m = re.search(vel_rx, os.path.basename(f))
        v = int(m.group(1)) if m else 1
        by.setdefault(v, []).append(f)
    return [Layer(sorted(v, key=natural)) for _, v in sorted(by.items())]


def range_layers(rel_dir: str, stem: str, root: str) -> list[Layer]:
    """Virtuosity's 'Conga_22_HitN_1_50_rr1' style: velocity RANGE in the name."""
    files = ls(rel_dir, "^" + re.escape(stem) + r"_\d+_\d+_rr", root)
    by: dict[int, list[str]] = {}
    for f in files:
        m = re.search(r"_(\d+)_(\d+)_rr", os.path.basename(f))
        by.setdefault(int(m.group(1)), []).append(f)
    return [Layer(sorted(v, key=natural)) for _, v in sorted(by.items())]


def rr_only(rel_dir: str, stem: str, root: str) -> list[Layer]:
    return [Layer(sorted(ls(rel_dir, "^" + re.escape(stem), root), key=natural))]


def order_by_loudness(layers: list[Layer], root: str) -> list[Layer]:
    """File names say pp..fff; the audio decides. Layers are sorted soft->loud
    by measured mean RMS so a mislabelled layer cannot invert the dynamics."""
    def loud(l: Layer) -> float:
        return sum(measure(os.path.join(root, KIT, f))[1] for f in l.files) / len(l.files)
    return sorted(layers, key=loud)


def kit(root: str) -> list[Sound]:
    c, o = f"{VP}/close", f"{VP}/oh"
    S: list[Sound] = []

    def two_mic(sub: str, fn, stem: str, mix_db: float = -8.0):
        """close mic primary, overhead blended under it for air."""
        closes = fn(f"{c}/{sub}", stem.replace("{m}", "Close"), root)
        ohs = fn(f"{o}/{sub}", stem.replace("{m}", "Overhead"), root)
        return [Mic(closes), Mic(ohs, gain_db=mix_db)]

    # ---- cajon (VCSL) — the latin kit's own bass drum and slap -------------
    def cajon(hit: str) -> list[Layer]:
        files = ls(VC, rf"^Cajon_{hit}_", root)
        by: dict[str, list[str]] = {}
        for f in files:
            dyn = re.search(rf"Cajon_{hit}_([a-z]+)_rr", f).group(1)
            by.setdefault(dyn, []).append(f)
        return order_by_loudness([Layer(sorted(v, key=natural)) for v in by.values()], root)

    S.append(Sound(35, "Cajon Bass 2", [Mic(cajon("hit2"))], -19))
    S.append(Sound(36, "Cajon Bass", [Mic(cajon("hit3"))], -17,
                   comment="the latin layer's KICK: a cajon bass tone, not a drum-kit kick"))
    S.append(Sound(37, "Cross-Stick", [
        Mic([Layer([f]) for f in sorted(ls(f"{VS}/snaremic/snare", r"^snaremic_snare_crossstick_vl\d+\.flac$", root), key=natural)]),
        Mic([Layer([f]) for f in sorted(ls(f"{VS}/oh/snare", r"^oh_snare_crossstick_vl\d+\.flac$", root), key=natural)], gain_db=-9.0),
    ], -21, veltrack=40, comment="bossa nova cross-stick: 16 velocity layers, snare mic + overheads"))
    S.append(Sound(38, "Cajon Slap", [Mic(cajon("hit1"))], -18))
    S.append(Sound(39, "Hand Clap", [Mic(vl_layers(BP, "handclap_", root))], -20,
                   comment="palmas: body-percussion hand claps, 2 layers x 4-6 RR"))

    # ---- surdo (VCSL concert bass drum + mallet tom) ------------------------
    bd = vl_layers(VC, "BDrumNew_hit_", root)
    S.append(Sound(41, "Surdo Low", [Mic(bd)], -16, extra="tune=-200 group=41 off_by=43",
                   comment="primeira, a tone lower"))
    S.append(Sound(43, "Surdo Muted", [Mic(bd)], -17,
                   extra="ampeg_hold=0.05 ampeg_decay=0.22 ampeg_sustain=0 group=43",
                   comment="the hand-damped stroke; chokes the open surdos"))
    S.append(Sound(45, "Surdo", [Mic(bd)], -16, extra="group=45 off_by=43",
                   comment="open surdo, felt mallet (concert bass drum, VCSL)"))
    S.append(Sound(47, "Surdo High", [Mic(vl_layers(VC, "TomH_HitM_", root))], -18,
                   comment="terceira / cutting surdo: mallet tom"))

    # ---- Virtuosity hand percussion -----------------------------------------
    S.append(Sound(54, "Tambourine", two_mic("tambourine", rr_only, "Tamb1_Shake"), -22))
    S.append(Sound(56, "Cowbell", two_mic("cowbell", vl_layers, "Cowbell1_Normal_"), -20,
                   comment="bongo bell / campana"))
    S.append(Sound(58, "Vibraslap", two_mic("vibraslap", range_layers, "Vibraslap1_1_Hit"), -22))
    S.append(Sound(60, "High Bongo", two_mic("bongoh", vl_layers, "BongoH_Hit1_"), -18))
    S.append(Sound(61, "Low Bongo", two_mic("bongol", vl_layers, "BongoL_Hit1_"), -18))
    S.append(Sound(62, "Mute High Conga", two_mic("conga", range_layers, "Conga_17_HitHM1"), -18,
                   comment="closed / slap-side stroke"))
    S.append(Sound(63, "Open High Conga", two_mic("conga", range_layers, "Conga_22_HitN"), -17))
    S.append(Sound(64, "Low Conga", two_mic("tumba", range_layers, "Tumba_24_HitN"), -17, comment="tumba"))
    tim = lambda stem: [Mic(vl_layers(f"{o}/timbales", stem, root))]
    S.append(Sound(65, "High Timbale", tim("timb13_"), -18))
    S.append(Sound(66, "Low Timbale", tim("timb14_"), -18))
    S.append(Sound(67, "High Agogo", two_mic("agogo", vl_layers, "Agogo_High_"), -21))
    S.append(Sound(68, "Low Agogo", two_mic("agogo", vl_layers, "Agogo_Low_"), -21))
    S.append(Sound(69, "Cabasa", two_mic("cabasa", vl_layers, "Cabasa1_Rub_"), -24))
    fw = sorted(ls(f"{FP}/Maracas", r"^01_\d+\.flac$", root), key=natural)
    bw = sorted(ls(f"{FP}/Maracas", r"^02_\d+\.flac$", root), key=natural)
    S.append(Sound(70, "Maracas", [Mic([Layer(fw), Layer(bw)])], -24, alternate=True, veltrack=80,
                   comment="real maracas (FreePats): forward and back strokes alternate"))
    whistle = lambda kind: [Mic(rr_only(f"{o}/whistle", f"Overhead_BallWhistle_{kind}", root))]
    S.append(Sound(71, "Short Whistle", whistle("Short"), -24, veltrack=80))
    S.append(Sound(72, "Long Whistle", whistle("Long"), -24, veltrack=80))
    S.append(Sound(73, "Short Guiro", two_mic("guiro", rr_only, "Guiro_Fast"), -23, veltrack=80))
    S.append(Sound(74, "Long Guiro", two_mic("guiro", rr_only, "Guiro_Med"), -23, veltrack=80))
    S.append(Sound(75, "Claves", two_mic("claves", vl_layers, "Claves1_Hit_"), -20))
    wb = lambda stem: [Mic(vl_layers(f"{o}/woodblock", stem, root))]
    S.append(Sound(76, "High Woodblock", wb("woodblock_hi"), -21))
    S.append(Sound(77, "Low Woodblock", wb("woodblock_lo"), -21))
    cu = sorted(ls(f"{o}/cuica", r"\.(wav|flac)$", root), key=natural)
    cu_mute = [f for f in cu if "p7" in f] or cu
    cu_open = [f for f in cu if "p2" in f] or cu
    S.append(Sound(78, "Mute Cuica", [Mic([Layer(cu_mute)])], -22, extra="group=78 off_by=79", veltrack=80))
    S.append(Sound(79, "Open Cuica", [Mic([Layer(cu_open)])], -22, extra="group=79 off_by=78", veltrack=80))
    S.append(Sound(80, "Mute Triangle", two_mic("triangle", vl_layers, "Triangle1_HitM_"), -25, extra="group=80"))
    S.append(Sound(81, "Open Triangle", two_mic("triangle", vl_layers, "Triangle1_Hit_"), -25,
                   extra="group=81 off_by=80"))
    S.append(Sound(82, "Shaker", [Mic([Layer(rr_only(f"{c}/shaker", "LShaker_Shake1D", root)[0].files),
                                       Layer(rr_only(f"{c}/shaker", "LShaker_Shake1U", root)[0].files)])],
                   -24, alternate=True, veltrack=80, comment="down / up strokes alternate"))
    S.append(Sound(83, "Jingle Bell", two_mic("sleighbells", rr_only, "Sleighbells_Hit"), -25))
    S.append(Sound(84, "Bell Tree", [Mic(rr_only(f"{o}/belltree", "BellTree_Stroke", root))], -25))

    # ---- VCSL extras: muted bongos, bells, frame drums ----------------------
    S.append(Sound(85, "Mute High Bongo", [Mic(vl_layers(VC, "BongoH_HitMuted1_", root))], -19))
    S.append(Sound(86, "Mute Low Bongo", [Mic(vl_layers(VC, "BongoL_HitMuted2_", root))], -19))
    S.append(Sound(87, "Mute Cowbell", [Mic(vl_layers(VC, "Cowbell1_Muted_", root))], -21))
    S.append(Sound(88, "Cha-Cha Bell", [Mic(vl_layers(VC, "Cowbell2_Normal_", root))], -21,
                   comment="the smaller, brighter bell (cha-cha / mambo bell)"))
    jingle = rr_only(f"{c}/tambourine", "Tamb1_Shake_", root)[0].files
    S.append(Sound(89, "Pandeiro Open", [Mic(vl_layers(VC, "HDrumS_Hit_", root))], -19,
                   comment="frame drum head + tambourine jingles layered under it (pandeiro)",
                   extra_regions=[f"sample={f} volume={{J}} lorand={i / len(jingle):.4f} hirand={(i + 1) / len(jingle):.4f}"
                                  for i, f in enumerate(jingle)]))
    S.append(Sound(90, "Pandeiro Slap", [Mic(vl_layers(VC, "HDrumS_HitMuted_", root))], -19,
                   extra_regions=[f"sample={f} volume={{J}} lorand={i / len(jingle):.4f} hirand={(i + 1) / len(jingle):.4f}"
                                  for i, f in enumerate(jingle)]))
    S.append(Sound(91, "Pandeiro Bass", [Mic(vl_layers(VC, "HDrumL_Hit_", root))], -18,
                   comment="the thumb tone"))
    S.append(Sound(92, "Tamborim", [Mic(vl_layers(VC, "HDrumS_HitMuted_", root))], -19,
                   extra="tune=700 ampeg_hold=0.02 ampeg_decay=0.12 ampeg_sustain=0",
                   comment="small frame drum, tuned up a fifth and choked short (stick on a tamborim)"))
    egg = sorted(ls(f"{FP}/EggShaker", r"^fast_\d+\.(flac|wav)$", root), key=natural)
    S.append(Sound(93, "Egg Shaker", [Mic([Layer(egg)])], -24, veltrack=80))
    cas = sorted(ls(f"{FP}/Castanets", r"^\d+\.flac$", root), key=natural)
    S.append(Sound(94, "Castanets", [Mic([Layer(cas)])], -21, veltrack=80))
    S.append(Sound(95, "Conga Heel", [Mic(vl_layers(VC, "Tumba_HitFM_", root))], -20,
                   comment="tumba, fingers/heel muted (heel-toe fill)"))
    return S


# ------------------------------------------------------------------- writing --
def rand_ranges(n: int):
    return [(i / n, (i + 1) / n) for i in range(n)]


def emit(sounds: list[Sound], root: str) -> str:
    out = [
        "// Latin-Percussion.sfz — SappKit \"Latin Percussion\" (LATIN-PERCUSSION v2)",
        "//",
        "// GENERATED by make_latin_sfz.py (sappsounds/kits/latin-percussion). Do not",
        "// hand-edit: change the generator and regenerate. Paths are relative to",
        "// ~/Samples/latin-percussion/; siblings are reached with ../",
        "//",
        "// Sources, all CC0 1.0 (public domain):",
        "//   Virtuosity Drums percussion + cross-stick — Versilian Studios",
        "//     https://github.com/sfzinstruments/virtuosity_drums",
        "//   VCSL — Versilian Community Sample Library — Versilian Studios",
        "//     https://github.com/sgossner/VCSL",
        "//   FreePats World Percussion — FreePats project (maracas, egg shaker, castanets)",
        "//     https://github.com/freepats/world-percussion",
        "//   Body Percussion — sfzinstruments (hand claps)",
        "//     https://github.com/sfzinstruments/body_percussion",
        "//",
        "// Note map (GM where GM has a name; Brazilian voices on documented free notes):",
    ]
    for s in sounds:
        out.append(f"//   {s.note:3d}  {s.name}")
    out.append("")
    for s in sounds:
        prim = s.mics[0]
        loudest = prim.layers[-1] if not s.alternate else max(
            prim.layers, key=lambda l: sum(measure(os.path.join(root, KIT, f))[1] for f in l.files))
        ms = [measure(os.path.join(root, KIT, f)) for f in loudest.files]
        rms = sum(m[1] for m in ms) / len(ms)
        # KIT_HEADROOM_DB: a groove stacks a dozen of these at once (clave,
        # shaker, tumbao, martillo, bell, guiro) - measured 2026-10-10, the
        # targets alone summed to -0.6 dBFS through SappKit's bus.
        trim = s.target_db - KIT_HEADROOM_DB - db(rms)
        # Headroom: the hottest sample of the hottest layer of ANY mic, after its
        # trim, peaks no higher than -3 dBFS.
        hottest = -200.0
        for mic in s.mics:
            for layer in mic.layers:
                for f in layer.files:
                    hottest = max(hottest, db(measure(os.path.join(root, KIT, f))[0]) + mic.gain_db)
        trim = min(trim, -3.0 - hottest)
        trim = max(-24.0, min(24.0, trim))
        names = ", ".join(sorted({os.path.basename(l.files[0]).split('.')[0] for l in prim.layers}))
        out.append(f"// ---- {s.note} {s.name} — {len(prim.layers)} layer(s), "
                   f"{max(len(l.files) for l in prim.layers)} RR, {len(s.mics)} mic(s), trim {trim:+.1f} dB"
                   + (f" — {s.comment}" if s.comment else ""))
        out.append("<group>")
        head = f"key={s.note} loop_mode=one_shot amp_veltrack={s.veltrack}"
        if s.extra:
            head += " " + s.extra
        out.append(head)
        for mic in s.mics:
            n = len(mic.layers)
            for li, layer in enumerate(mic.layers):
                if s.alternate:
                    lo, hi = 1, 127
                else:
                    lo = 1 if li == 0 else int(round(127 * li / n)) + 1
                    hi = int(round(127 * (li + 1) / n))
                rr = len(layer.files)
                for k, f in enumerate(layer.files):
                    if " " in f:
                        raise SystemExit(f"space in sample path breaks the fetch check: {f}")
                    parts = [f"<region> sample={f}", f"volume={trim + mic.gain_db:+.1f}"]
                    if mic.pan:
                        parts.append(f"pan={mic.pan:.0f}")
                    if not s.alternate:
                        parts.append(f"lovel={lo} hivel={hi}")
                    if s.alternate:
                        parts.append(f"seq_length={len(mic.layers)} seq_position={li + 1}")
                        if rr > 1:
                            a, b = rand_ranges(rr)[k]
                            parts.append(f"lorand={a:.4f} hirand={b:.4f}")
                    elif rr > 1:
                        parts.append(f"seq_length={rr} seq_position={k + 1}")
                    out.append(" ".join(parts))
        for er in s.extra_regions:
            out.append("<region> " + er.replace("{J}", f"{trim - 12:+.1f}"))
        out.append("")
    return "\n".join(out) + "\n"


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--samples", default=os.path.expanduser("~/Samples"))
    ap.add_argument("--out", default=os.path.join(HERE, "Latin-Percussion.sfz"))
    a = ap.parse_args()
    sounds = kit(a.samples)
    text = emit(sounds, a.samples)
    with open(a.out, "w", newline="\n") as fh:
        fh.write(text)
    regions = text.count("<region>")
    print(f"wrote {a.out}: {len(sounds)} sounds, {regions} regions")
    return 0


if __name__ == "__main__":
    sys.exit(main())
