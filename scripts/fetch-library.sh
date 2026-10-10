#!/usr/bin/env bash
# fetch-library.sh — download curated free sample libraries for the Sapp
# instruments (SappOrchestra, and future SappSounds-based products).
#
#   ./scripts/fetch-library.sh list
#   ./scripts/fetch-library.sh get <name> [--dest DIR]
#   ./scripts/fetch-library.sh get all [--dest DIR]
#   ./scripts/fetch-library.sh get drums [--dest DIR]    (drum kits only)
#
# Default destination: ~/Samples/<name>. Every library here is free to
# download and use; licenses vary per library — check LICENSE/readme files
# after download before redistribution. Samples are never committed to git.

set -euo pipefail

DEST_ROOT="${HOME}/Samples"
CMD="${1:-list}"
NAME="${2:-}"
if [ "${3:-}" = "--dest" ] && [ -n "${4:-}" ]; then DEST_ROOT="$4"; fi

libraries() {
cat <<'EOF'
sonatina|2.6 GB|Sonatina Symphonic Orchestra — full orchestra, 747 SFZ instruments, FLAC (CC Sampling Plus 1.0)
vpo|1.3 GB|Virtual Playing Orchestra 3 — orchestra with velocity layers + round robins, WAV (free)
vsco2-ce|3.3 GB|VSCO 2 Community Edition — chamber orchestra + percussion, public domain (CC0)
salamander|707 MB|Salamander Grand Piano — Yamaha C5, 16 velocity layers, SFZ+FLAC (CC-BY 3.0)
fm-piano1|24 MB|FreePats FM Piano 1 — classic DX-style FM electric piano, SFZ+FLAC (CC0)
upright-piano|34 MB|FreePats Upright Piano KW — Kawai upright, velocity layers, SFZ+FLAC (CC0)
old-piano-fb|39 MB|FreePats Old Piano FB — worn 1920s upright, honky-tonk character, SFZ+FLAC (CC0)
freepats-synth-choir|7 MB|FreePats Synth Pad Choir — GM #92 choir pad, SFZ+FLAC (CC0)
legato-vocal|160 MB|SFZ legato vocal tutorial — solo voice, vowel sustains + transitions + syllables, SFZ+FLAC (CC0)
avl-drumkits|29 MB|AVL Drumkits 1.0 — Black Pearl + Red Zeppelin acoustic kits + percussion, GM-ish SFZ maps (CC-BY-SA)
sm-drums|2.2 GB|SM MegaReaper Drumkit — deep-sampled acoustic kit, up to 127 vel layers + 4 RR, SFZ (royalty-free)
big-rusty-drums|600 MB|Karoryfer Big Rusty Drums — characterful 60s acoustic kit, chokes + RR, SFZ (CC0)
muldjord-kit|350 MB|Muldjord Kit — Tama Superstar rock/metal kit, DrumGizmo port, per-mic + stereo SFZ (CC-BY 4.0)
drs-kit|740 MB|DRS Kit — Sonor kit, DrumGizmo port, brushes variant included, SFZ (CC-BY 4.0)
naked-drums|1.3 GB|Naked Drums — modern multi-mic kit, 10 round robins, GM + TD-12 maps, SFZ (CC-BY 4.0)
virtuosity-drums|1.2 GB|Virtuosity Drums — Versilian/Karoryfer deep-sampled kit, 400+ SFZ programs (CC0)
swirly-drums|840 MB|Karoryfer Swirly Drums — punk/indie brushes kit + unusual percussion, SFZ (CC0)
unruly-drums|660 MB|Karoryfer Unruly Drums — experimental kit where every drum is a snare, SFZ (CC0)
frankensnare|340 MB|Karoryfer Frankensnare — snare collection from tiny to huge, SFZ (CC0)
gogodze-phu|135 MB|Karoryfer Gogodze Phu Vol II — variable-fidelity kit, lo-fi to hi-fi, SFZ (CC0)
latin-percussion|25 MB|Latin Percussion v2 — congas/bongos/timbales/cowbells/agogo/guiro/cuica/surdo/pandeiro, 8 RR + velocity layers, GM-mapped SFZ over Virtuosity + VCSL + FreePats (CC0; needs virtuosity-drums)
karoryfer-sneakybass|300 MB|Karoryfer Sneakybass — upright bass: pizz, ghost notes, mutes, fingering noise (CC-BY). midnight-trio's Upright Bass
dsmolken-double-bass|150 MB|D. Smolken Rubner double bass — arco, pizz, keyswitched (CC)
freepats-world-percussion|10 MB|FreePats World Percussion — bongos, cajon, egg shaker, maracas, castanets, darbuka, SFZ (CC0)
body-percussion|100 MB|Body Percussion — claps, snaps, chest/thigh slaps, 4-6 RR, SFZ (CC0)
sr-808|45 MB|SampleRadar 808 — TR-808 one-shots: kick/snare/clap/rim/hats/cowbell/congas/claves/maracas (royalty-free, no redistribution)
sr-classic-machines|10 MB|SampleRadar classic drum machines — TR-808 + TR-909 + SR-16 + DDM-110 single hits (royalty-free, no redistribution)
sr-hardware-machines|60 MB|SampleRadar hardware drum machines — Roland TR-8 (808/909/707), Jomox Airbase 99, DrumBrute, HR-16 hits (royalty-free, no redistribution)
sr-obscure-machines|30 MB|SampleRadar obscure drum machines — TR-707, TR-505, DR-110, RX11, RY8, Sessionmate hits (royalty-free, no redistribution)
sr-retro-machines|25 MB|SampleRadar retro drum machines — Casio MT40, SR-88, Pearl SY1 raw + tape-path hits (royalty-free, no redistribution)
sr-analogue-drums|55 MB|SampleRadar analogue drums — Rhythm Ace, Vermona DRM1, Selmer Auto-Rhythm, Volca Kick hits (royalty-free, no redistribution)
sr-80s-pop-drums|16 MB|SampleRadar 80s pop drums — four LinnDrum-era pop kits + 32 gated snares (royalty-free, no redistribution)
sr-drum-kits|320 MB|SampleRadar drum samples — 26 kits (vinyl, electro, acoustic, Kurzweil) + assorted hits (royalty-free, no redistribution)
sr-vinyl|30 MB|SampleRadar vinyl-style — dusty drum hits, hits, 20 vinyl crackles (royalty-free, no redistribution)
sr-90s-hiphop|35 MB|SampleRadar 90s hip hop — boom-bap drum hits + hits (royalty-free, no redistribution)
sr-dancehall|20 MB|SampleRadar dancehall — five kits' drum hits, dembow-friendly kicks/snares/rims (royalty-free, no redistribution)
EOF
}

need() { command -v "$1" >/dev/null || { echo "error: $1 required"; exit 1; }; }

fetch_zip() {  # url, dest-file
  echo "  downloading $(basename "$2") ..."
  curl -L --progress-bar -o "$2" "$1"
}

get_sonatina() {
  need git
  local dest="$DEST_ROOT/sonatina"
  [ -d "$dest" ] && { echo "already present: $dest"; return; }
  git clone --depth 1 https://github.com/peastman/sso.git "$dest"
  echo "done: $dest  (instruments in 'Sonatina Symphonic Orchestra/')"
}

get_vpo() {
  need unzip
  local dest="$DEST_ROOT/vpo"
  mkdir -p "$dest"
  if [ ! -d "$dest/Virtual-Playing-Orchestra3" ]; then
    [ -f "$dest/vpo-waves.zip" ] || fetch_zip \
      "https://virtualplaying.com/go/virtual-playing-orchestra-v3-2-wave-files-archive/" \
      "$dest/vpo-waves.zip"
    [ -f "$dest/vpo-standard-scripts.zip" ] || fetch_zip \
      "https://virtualplaying.com/go/virtual-playing-orchestra-v3-3-standard-scripts/" \
      "$dest/vpo-standard-scripts.zip"
    [ -f "$dest/vpo-performance-scripts.zip" ] || fetch_zip \
      "https://virtualplaying.com/go/virtual-playing-orchestra-v3-3-performance-scripts/" \
      "$dest/vpo-performance-scripts.zip"
    echo "  extracting (waves first, then SFZ scripts on top) ..."
    unzip -oq "$dest/vpo-waves.zip" -d "$dest"
    unzip -oq "$dest/vpo-standard-scripts.zip" -d "$dest"
    unzip -oq "$dest/vpo-performance-scripts.zip" -d "$dest"
  fi
  echo "done: $dest"
}

get_vsco2_ce() {
  need git
  local dest="$DEST_ROOT/vsco2-ce"
  if [ ! -d "$dest" ]; then
    git clone --depth 1 https://github.com/sgossner/VSCO-2-CE.git "$dest"
  fi
  # The SFZ mappings live on the repo's SFZ branch; overlay them at the root
  # (their default_path entries are relative to the sample tree root).
  if ! ls "$dest"/*.sfz >/dev/null 2>&1; then
    echo "  overlaying SFZ mappings (SFZ branch) ..."
    git -C "$dest" fetch --depth 1 origin SFZ
    git -C "$dest" checkout FETCH_HEAD -- "*.sfz" 2>/dev/null ||       git -C "$dest" checkout FETCH_HEAD -- $(git -C "$dest" ls-tree -r --name-only FETCH_HEAD | grep '\.sfz$')
  fi
  echo "done: $dest ($(ls "$dest"/*.sfz 2>/dev/null | wc -l | tr -d ' ') instruments)"
}

get_freepats_synth_choir() {
  need git
  local dest="$DEST_ROOT/freepats-synth-choir"
  [ -d "$dest" ] && { echo "already present: $dest"; return; }
  git clone --depth 1 https://github.com/freepats/synth-pad-choir.git "$dest"
  echo "done: $dest"
}

get_legato_vocal() {
  need git
  local dest="$DEST_ROOT/legato-vocal"
  [ -d "$dest" ] && { echo "already present: $dest"; return; }
  git clone --depth 1 https://github.com/sfzinstruments/legato_vocal_tutorial.git "$dest"
  echo "done: $dest  (playable programs in 'Programs/')"
}

get_avl_drumkits() {
  need unzip
  local dest="$DEST_ROOT/avl-drumkits"
  if find "$dest" -name "*.sfz" -print -quit 2>/dev/null | grep -q .; then
    echo "already present: $dest"
    return
  fi
  mkdir -p "$dest"
  local file="$dest/avl-drumkits-sfz.zip"
  # Canonical SFZ package from the author's site (Glen MacArthur, bandshed.net).
  [ -f "$file" ] || fetch_zip "http://www.bandshed.net/sounds/sfz/AVL_Drumkits_1.0.zip" "$file"
  echo "  extracting ..."
  unzip -oq "$file" -d "$dest"
  echo "done: $dest"
}

get_sm_drums() {
  need git
  local dest="$DEST_ROOT/sm-drums"
  [ -d "$dest" ] && { echo "already present: $dest"; return; }
  git clone --depth 1 https://github.com/sfzinstruments/SMDrums.git "$dest"
  echo "done: $dest"
}

get_muldjord_kit() {
  need git
  local dest="$DEST_ROOT/muldjord-kit"
  [ -d "$dest" ] && { echo "already present: $dest"; return; }
  git clone --depth 1 https://github.com/sfzinstruments/DrumGizmo.MuldjordKit.git "$dest"
  echo "done: $dest"
}

get_drs_kit() {
  need git
  local dest="$DEST_ROOT/drs-kit"
  [ -d "$dest" ] && { echo "already present: $dest"; return; }
  git clone --depth 1 https://github.com/sfzinstruments/DrumGizmo.DRSKit.git "$dest"
  echo "done: $dest"
}

get_naked_drums() {
  need git
  local dest="$DEST_ROOT/naked-drums"
  [ -d "$dest" ] && { echo "already present: $dest"; return; }
  git clone --depth 1 https://github.com/sfzinstruments/WilkinsonAudio.NakedDrums.git "$dest"
  echo "done: $dest"
}

get_virtuosity_drums() {
  need git
  local dest="$DEST_ROOT/virtuosity-drums"
  [ -d "$dest" ] && { echo "already present: $dest"; return; }
  git clone --depth 1 https://github.com/sfzinstruments/virtuosity_drums.git "$dest"
  echo "done: $dest"
}

get_swirly_drums() {
  need git
  local dest="$DEST_ROOT/swirly-drums"
  [ -d "$dest" ] && { echo "already present: $dest"; return; }
  git clone --depth 1 https://github.com/sfzinstruments/karoryfer.swirly-drums.git "$dest"
  echo "done: $dest"
}

get_unruly_drums() {
  need git
  local dest="$DEST_ROOT/unruly-drums"
  [ -d "$dest" ] && { echo "already present: $dest"; return; }
  git clone --depth 1 https://github.com/sfzinstruments/karoryfer.unruly-drums.git "$dest"
  echo "done: $dest"
}

get_frankensnare() {
  need git
  local dest="$DEST_ROOT/frankensnare"
  [ -d "$dest" ] && { echo "already present: $dest"; return; }
  git clone --depth 1 https://github.com/sfzinstruments/karoryfer.frankensnare.git "$dest"
  echo "done: $dest"
}

# Latin Percussion v2 — a GM-mapped SFZ over THREE CC0 sources, assembled here
# rather than downloaded as one zip, so every sample comes from its author:
#   - Virtuosity Drums' percussion section (Versilian, CC0): congas, bongos,
#     timbales, cowbell, agogo, cabasa, shaker, guiro, claves, woodblocks,
#     cuica, triangle, whistle, cross-stick — 8 round robins, close + overhead
#     mics. Referenced in place from ../virtuosity-drums (fetched first).
#   - VCSL (Versilian, CC0): cajon, muted bongos, cowbells, frame drums (the
#     pandeiro/tamborim voices), concert bass drum + mallet tom (surdo), claps.
#     Fetched file by file from a PINNED commit into samples/vcsl/.
#   - FreePats World Percussion (CC0): real maracas, egg shaker, castanets.
#     Referenced in place from ../freepats-world-percussion.
#   - Body Percussion (sfzinstruments, CC0): the hand claps (palmas).
#     Referenced in place from ../body-percussion.
# The mapping itself (Latin-Percussion.sfz) is committed in this repo under
# kits/latin-percussion/ and generated by make_latin_sfz.py there.
VCSL_SHA="c1ea7bcc3c7309650ab0da9d15c9cd1fbc4a4c7e"
LATIN_KIT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../kits/latin-percussion" && pwd)"

get_latin_percussion() {
  need curl
  local dest="$DEST_ROOT/latin-percussion"
  if grep -q "LATIN-PERCUSSION v2" "$dest/Latin-Percussion.sfz" 2>/dev/null; then
    # A regenerated mapping (same samples, new trims) is picked up in place.
    if ! cmp -s "$LATIN_KIT_DIR/Latin-Percussion.sfz" "$dest/Latin-Percussion.sfz"; then
      cp "$LATIN_KIT_DIR/Latin-Percussion.sfz" "$dest/Latin-Percussion.sfz"
      echo "updated mapping: $dest/Latin-Percussion.sfz"
    else
      echo "already present: $dest"
    fi
    return
  fi
  get_virtuosity_drums
  get_freepats_world_percussion
  get_body_percussion
  # v1 (the 6 MB release zip) is replaced wholesale; it has no user content.
  rm -rf "$dest"
  mkdir -p "$dest/samples/vcsl"
  local n=0 failed=0
  while IFS= read -r rel; do
    # A Windows checkout with core.autocrlf=true hands every line back with a
    # trailing CR, and curl rejects the URL ("(3) URL using bad/illegal
    # format"). That's how all 88 VCSL files failed on the station box.
    rel=${rel%$'\r'}
    [ -z "$rel" ] && continue
    local enc out
    enc=$(printf '%s' "$rel" | sed 's/ /%20/g; s/,/%2C/g')
    out="$dest/samples/vcsl/$(basename "$rel")"
    curl -fsSL --retry 3 -o "$out" "https://raw.githubusercontent.com/sgossner/VCSL/$VCSL_SHA/$enc" \
      || { echo "  FAILED: $rel"; failed=$((failed + 1)); }
    n=$((n + 1))
  done < "$LATIN_KIT_DIR/vcsl-files.txt"
  echo "  $n VCSL samples fetched ($failed failed)"
  cp "$LATIN_KIT_DIR/Latin-Percussion.sfz" "$dest/Latin-Percussion.sfz"
  # Prove the mapping resolves on THIS box: every sample= path must exist.
  local missing
  missing=$(cd "$dest" && grep -o 'sample=[^ ]*' Latin-Percussion.sfz | cut -d= -f2- | sort -u |
            while IFS= read -r f; do [ -f "$f" ] || echo "$f"; done | wc -l | tr -d ' ')
  if [ "$missing" != "0" ]; then
    echo "  WARNING: $missing sample file(s) referenced by Latin-Percussion.sfz are missing"
  fi
  echo "done: $dest"
}

get_freepats_world_percussion() {
  need git
  local dest="$DEST_ROOT/freepats-world-percussion"
  [ -d "$dest" ] && { echo "already present: $dest"; return; }
  git clone --depth 1 https://github.com/freepats/world-percussion.git "$dest"
  echo "done: $dest"
}

get_body_percussion() {
  need git
  local dest="$DEST_ROOT/body-percussion"
  [ -d "$dest" ] && { echo "already present: $dest"; return; }
  git clone --depth 1 https://github.com/sfzinstruments/body_percussion.git "$dest"
  echo "done: $dest"
}

# SampleRadar (MusicRadar) packs. LICENCE, from every pack's page: "Because
# they're royalty-free, you're welcome to use the samples in your music in any
# way you like - all we ask is that you don't re-distribute them." So they are
# fetched from MusicRadar's own CDN onto the box that plays them, never
# mirrored, never committed, never attached to a release. Only the ONE-SHOT
# folders are kept: loops and construction-kit stems are left in the zip,
# which is deleted after extraction. SappStep's build-kit reads these.
SR_CDN="https://cdn.mos.musicradar.com"
get_sampleradar() {  # key  zip-path  page-url  include-pattern...
  need curl; need unzip
  local key="$1" zip="$2" page="$3"; shift 3
  local dest="$DEST_ROOT/sampleradar/$key"
  if find "$dest" -type f \( -iname '*.wav' -o -iname '*.aif' -o -iname '*.aiff' \) -print -quit 2>/dev/null | grep -q .; then
    echo "already present: $dest"
    return
  fi
  mkdir -p "$dest"
  local file="$DEST_ROOT/sampleradar/.$key.zip"
  [ -f "$file" ] || curl -fL --retry 3 --progress-bar -o "$file" "$SR_CDN/$zip"
  echo "  extracting one-shots ..."
  # exit 11 = "an exclude pattern matched nothing" (most zips carry no
  # __MACOSX/), which is not an error; anything else is.
  #
  # Each include pattern is also tried with up to two extra folder levels in
  # FRONT and three BEHIND. The Info-ZIP unzip in Git for Windows doesn't let
  # `*` cross a `/`, so '*/Hits/*' matched only Hits/.DS_Store and none of the
  # one-shots in Hits/Bass Drum [BD]/... (station box, 2026-10-10: 0 of 219
  # 808 hits, and 7 of 10 SappStep kits stayed synthesised), and none of
  # Analogue_Drums/<machine>/Hits/... either. Where `*` does cross `/`, the
  # extra patterns match the same files again, which unzip -o shrugs off.
  local rc=0 pats=() p pre suf
  for p in "$@"; do
    for pre in "" "*/" "*/*/"; do
      for suf in "" "/*" "/*/*" "/*/*/*"; do pats+=("$pre$p$suf"); done
    done
  done
  unzip -oq "$file" "${pats[@]}" -x '__MACOSX/*' '*.DS_Store' '*_DS_Store' '*/.DS_Store' -d "$dest" 2>/dev/null || rc=$?
  if [ "$rc" -ne 0 ] && [ "$rc" -ne 11 ]; then echo "  unzip failed ($rc): $file"; return 1; fi
  # The -x excludes have the same one-level limit, and the leading-level
  # patterns above reach into __MACOSX/, whose AppleDouble "._name.wav" copies
  # look like one-shots to build-kit (the 808 pack came out as 219 hits +
  # 219 ._ files). Sweep them after extraction instead; this works with any
  # unzip.
  find "$dest" -depth \( -name '__MACOSX' -o -name '._*' -o -name '.DS_Store' \) -exec rm -rf {} + 2>/dev/null
  if ! find "$dest" -type f \( -iname '*.wav' -o -iname '*.aif' -o -iname '*.aiff' \) -print -quit | grep -q .; then
    # Keep the zip: a layout change is fixed in the pattern, not by
    # downloading the pack again.
    echo "  no one-shots matched $* in $zip - the pack layout changed? (zip kept: $file)"; return 1
  fi
  rm -f "$file"
  printf '%s\n' "SampleRadar / MusicRadar sample pack ($key)" \
    "Source: $page" \
    "Licence: royalty-free - use the samples in your music in any way you like;" \
    "do NOT re-distribute them (do not mirror, commit or attach these files)." > "$dest/LICENCE.txt"
  echo "done: $dest ($(find "$dest" -type f \( -iname '*.wav' -o -iname '*.aif*' \) | wc -l | tr -d ' ') one-shots)"
}

get_sr_808()              { get_sampleradar sr-808 audio/samples/musicradar-808-samples.zip \
  https://www.musicradar.com/news/sampleradar-378-free-808-drum-samples '*/Hits/*'; }
get_sr_classic_machines() { get_sampleradar sr-classic-machines audio/samples/musicradar-synth-samples.zip \
  https://www.musicradar.com/news/tech/sampleradar-225241 '*/Classic drum machines/*'; }
get_sr_hardware_machines() { get_sampleradar sr-hardware-machines audio/musicradar-hardware-drum-machine-samples.zip \
  https://www.musicradar.com/music-tech/samples/sampleradar-493-free-hardware-drum-machine-samples '*/Hits/*'; }
get_sr_obscure_machines() { get_sampleradar sr-obscure-machines audio/samples/sampleradar-obscure-drum-machines-samples.zip \
  https://musicradar.com/news/sampleradar-obscure-drum-machine-samples '*/Hits/*'; }
get_sr_retro_machines()   { get_sampleradar sr-retro-machines audio/samples/musicradar-retro-drum-machine-samples.zip \
  https://www.musicradar.com/news/sampleradar-275-free-retro-drum-machine-samples '*/Drums Hits/*'; }
get_sr_analogue_drums()   { get_sampleradar sr-analogue-drums audio/samples/sampleradar-analogue-drums-samples.zip \
  https://musicradar.com/news/sampleradar-298-free-analogue-drum-samples '*/Hits/*'; }
get_sr_80s_pop_drums()    { get_sampleradar sr-80s-pop-drums audio/samples/sampleradar-80s-pop-drums-samples.zip \
  https://www.musicradar.com/news/sampleradar-free-80s-pop-drums-samples '*/Drum Kits/*' '*/Gated Snares/*'; }
get_sr_drum_kits()        { get_sampleradar sr-drum-kits audio/samples/musicradar-drum-samples.zip \
  https://www.musicradar.com/news/drums/1000-free-drum-samples '*/Drum Kits/*' '*/Assorted Hits/*'; }
get_sr_vinyl()            { get_sampleradar sr-vinyl audio/samples/musicradar-vinyl-style-samples.zip \
  https://www.musicradar.com/news/tech/sampleradar-243-free-vinyl-style-samples-277010 '*/Crackle/*' '*/DrumHits/*' '*/Hits/*'; }
get_sr_90s_hiphop()       { get_sampleradar sr-90s-hiphop musicradar-90s-hip-hop-samples.zip \
  https://www.musicradar.com/music-tech/samples/want-to-recreate-the-classic-sound-of-golden-age-90s-hip-hop-download-this-pack-of-537-free-samples-from-sampleradar '*/Drum_Hits/*' '*/Hits/*'; }
get_sr_dancehall()        { get_sampleradar sr-dancehall audio/samples/musicradar-dancehall-samples.zip \
  https://musicradar.com/news/tech/sampleradar-250784 '*/Drums/Hits/*'; }

get_sampleradar_all() {
  get_sr_808; get_sr_classic_machines; get_sr_hardware_machines; get_sr_obscure_machines
  get_sr_retro_machines; get_sr_analogue_drums; get_sr_80s_pop_drums; get_sr_drum_kits
  get_sr_vinyl; get_sr_90s_hiphop; get_sr_dancehall
}

get_gogodze_phu() {
  need git
  local dest="$DEST_ROOT/gogodze-phu"
  [ -d "$dest" ] && { echo "already present: $dest"; return; }
  git clone --depth 1 https://github.com/sfzinstruments/karoryfer.gogodze-phu-vol-ii.git "$dest"
  echo "done: $dest"
}

get_big_rusty_drums() {
  need git
  local dest="$DEST_ROOT/big-rusty-drums"
  [ -d "$dest" ] && { echo "already present: $dest"; return; }
  git clone --depth 1 https://github.com/sfzinstruments/karoryfer.big-rusty-drums.git "$dest"
  echo "done: $dest"
}

get_karoryfer_sneakybass() {
  need git
  local dest="$DEST_ROOT/karoryfer-sneakybass"
  [ -d "$dest" ] && { echo "already present: $dest"; return; }
  git clone --depth 1 https://github.com/sfzinstruments/karoryfer.sneakybass.git "$dest"
  echo "done: $dest  (playable programs in 'Programs/')"
}

get_dsmolken_double_bass() {
  need git
  local dest="$DEST_ROOT/dsmolken-double-bass"
  [ -d "$dest" ] && { echo "already present: $dest"; return; }
  git clone --depth 1 https://github.com/sfzinstruments/dsmolken.double-bass.git "$dest"
  echo "done: $dest  (arco, pizz, keyswitched)"
}

get_salamander() {
  need curl
  local dest="$DEST_ROOT/salamander"
  [ -d "$dest/SalamanderGrandPiano" ] && { echo "already present: $dest"; return; }
  mkdir -p "$dest"
  # freepats hosts the canonical SFZ package; archive.org mirrors it.
  local url="https://freepats.zenvoid.org/Piano/SalamanderGrandPiano/SalamanderGrandPiano-SFZ+FLAC-V3+20200602.tar.gz"
  local file="$dest/salamander.tar.gz"
  [ -f "$file" ] || curl -fL --progress-bar -o "$file" "$url"
  echo "  extracting ..."
  tar -xzf "$file" -C "$dest"
  echo "done: $dest"
}

get_fm_piano1() {
  need git
  local dest="$DEST_ROOT/fm-piano1"
  [ -d "$dest" ] && { echo "already present: $dest"; return; }
  git clone --depth 1 https://github.com/freepats/fm-piano1.git "$dest"
  echo "done: $dest"
}

get_upright_piano() {
  need git
  local dest="$DEST_ROOT/upright-piano"
  [ -d "$dest" ] && { echo "already present: $dest"; return; }
  git clone --depth 1 https://github.com/freepats/upright-piano-KW.git "$dest"
  echo "done: $dest"
}

get_old_piano_fb() {
  need git
  local dest="$DEST_ROOT/old-piano-fb"
  [ -d "$dest" ] && { echo "already present: $dest"; return; }
  git clone --depth 1 https://github.com/freepats/old-piano-FB.git "$dest"
  echo "done: $dest"
}

case "$CMD" in
  list)
    printf "%-26s %-8s %s\n" "NAME" "SIZE" "DESCRIPTION"
    libraries | while IFS='|' read -r n s d; do printf "%-26s %-8s %s\n" "$n" "$s" "$d"; done
    echo
    echo "groups: all (everything), drums (every drum kit, no orchestra), sampleradar (the sr-* packs)"
    echo
    echo "usage: $0 get <name|all|drums|sampleradar> [--dest DIR]   (default dest: ~/Samples)"
    ;;
  get)
    mkdir -p "$DEST_ROOT"
    case "$NAME" in
      sonatina) get_sonatina ;;
      vpo) get_vpo ;;
      vsco2-ce) get_vsco2_ce ;;
      salamander) get_salamander ;;
      fm-piano1) get_fm_piano1 ;;
      upright-piano) get_upright_piano ;;
      old-piano-fb) get_old_piano_fb ;;
      freepats-synth-choir) get_freepats_synth_choir ;;
      legato-vocal) get_legato_vocal ;;
      avl-drumkits) get_avl_drumkits ;;
      sm-drums) get_sm_drums ;;
      big-rusty-drums) get_big_rusty_drums ;;
      muldjord-kit) get_muldjord_kit ;;
      drs-kit) get_drs_kit ;;
      naked-drums) get_naked_drums ;;
      virtuosity-drums) get_virtuosity_drums ;;
      swirly-drums) get_swirly_drums ;;
      unruly-drums) get_unruly_drums ;;
      frankensnare) get_frankensnare ;;
      gogodze-phu) get_gogodze_phu ;;
      latin-percussion) get_latin_percussion ;;
      karoryfer-sneakybass) get_karoryfer_sneakybass ;;
      dsmolken-double-bass) get_dsmolken_double_bass ;;
      freepats-world-percussion) get_freepats_world_percussion ;;
      body-percussion) get_body_percussion ;;
      sr-808) get_sr_808 ;;
      sr-classic-machines) get_sr_classic_machines ;;
      sr-hardware-machines) get_sr_hardware_machines ;;
      sr-obscure-machines) get_sr_obscure_machines ;;
      sr-retro-machines) get_sr_retro_machines ;;
      sr-analogue-drums) get_sr_analogue_drums ;;
      sr-80s-pop-drums) get_sr_80s_pop_drums ;;
      sr-drum-kits) get_sr_drum_kits ;;
      sr-vinyl) get_sr_vinyl ;;
      sr-90s-hiphop) get_sr_90s_hiphop ;;
      sr-dancehall) get_sr_dancehall ;;
      sampleradar) get_sampleradar_all ;;
      # Every DRUM library a sapptune style can ask for, without the orchestra:
      # the SappKit kits (acoustic + Latin) and the SampleRadar one-shots that
      # SappStep's build-kit turns into its named electronic kits. ~2.1 GB of
      # downloads, ~1.5 GB on disk (the SampleRadar zips are deleted after the
      # one-shots are extracted).
      drums) get_avl_drumkits; get_muldjord_kit; get_virtuosity_drums; get_gogodze_phu;
             get_latin_percussion; get_body_percussion; get_sampleradar_all ;;
      # `all` must cover every library a sapptune style can ask for, or the
      # station renders that track on a fallback sound and says nothing.
      # virtuosity-drums (tokyo-pop), gogodze-phu (tape-room) and
      # latin-percussion (congas/bongos) were missing and were silently
      # substituted on a fully-fetched box. muldjord-kit is a sapptune kit
      # (patch "Muldjord Kit") and was missing too; the SampleRadar packs are
      # what SappStep's named kits are built from (build-kit).
      all) get_sonatina; get_vpo; get_vsco2_ce; get_salamander;
           get_fm_piano1; get_upright_piano; get_old_piano_fb;
           get_freepats_synth_choir; get_legato_vocal;
           get_avl_drumkits; get_sm_drums; get_big_rusty_drums;
           get_virtuosity_drums; get_gogodze_phu; get_latin_percussion;
           get_karoryfer_sneakybass; get_dsmolken_double_bass;
           get_muldjord_kit; get_body_percussion; get_sampleradar_all ;;
      *) echo "unknown library '$NAME' — run: $0 list"; exit 2 ;;
    esac
    ;;
  *)
    echo "usage: $0 list | get <name|all> [--dest DIR]"; exit 2 ;;
esac
