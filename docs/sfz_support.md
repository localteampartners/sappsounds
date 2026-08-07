# SFZ Support

The parser handles the structural format fully; opcode coverage is phased.
Unsupported opcodes are **recorded and reported** (validator/inspector output,
`InstrumentDefinition::unsupportedOpcodes`), never fatal.

## Structure

- Headers: `<control>`, `<global>`, `<master>`, `<group>`, `<region>`
  (unknown headers warn and are skipped)
- Inheritance: global ← master ← group ← region, snapshotted at the point
  each `<region>` appears; new `<master>` clears group scope, new `<group>`
  clears the previous group
- `//` comments, values containing spaces (sample paths), mixed `\`/`/`
  separators, case-insensitive opcode names
- `#include "file"` (depth-limited, cycle-detected), `#define $VAR value`
  (longest-match substitution, applied inside include paths too:
  `#include "$DIR/$DYN.txt"`)
- Note names (`c4`, `c#4`, `db3`) or numbers wherever a key is expected;
  `c4 == 60`
- Safe numerics: invalid values warn and keep defaults; everything is clamped
- Source file + line attached to every diagnostic and region

## Limits (untrusted input)

`SfzParserLimits`: include depth 8, 65 536 regions, 32 MiB per file, 4 096
chars per token. WAV decode caps: 2 GiB file, 2³¹ frames, sane rates.

## Phase 1 opcodes (implemented)

**Mapping/pitch** `sample key lokey hikey pitch_keycenter transpose tune
lovel hivel volume pan amp_veltrack offset end`

**Loop** `loop_mode(no_loop one_shot loop_continuous loop_sustain)
loop_start/loopstart loop_end/loopend loop_crossfade` — regions without an
explicit mode use the WAV `smpl` loop when present (`loop_continuous`
default per convention)

**Envelope** `ampeg_delay ampeg_start ampeg_attack ampeg_hold ampeg_decay
ampeg_sustain ampeg_release`

**Trigger/groups** `trigger(attack release release_key first legato) group
off_by off_mode(fast normal time) off_time polyphony note_polyphony delay`

**Round robin/random** `seq_length seq_position lorand hirand`

**Humanize** `amp_random pitch_random delay_random` (deterministic under the
engine seed) · **CC gain** `gain_ccN` (live: region gain follows the
controller, e.g. VPO/Sonatina CC1 dynamics)

**Keyswitch/CC** `sw_lokey sw_hikey sw_last sw_default sw_label loccN hiccN`

**Crossfades** `xfin_lovel xfin_hivel xfout_lovel xfout_hivel xfin_lokey
xfin_hikey xfout_lokey xfout_hikey xfin_loccN xfin_hiccN xfout_loccN
xfout_hiccN xf_velcurve xf_cccurve xf_keycurve` — CC crossfades track the
controller live while the note sounds (dynamic-layer morphing)

**Control header** `default_path set_ccN label_ccN` — default_path is
POSITIONAL (per SFZ v2): each `<control>` re-points it for the regions that
follow, baked into region paths at parse time (VSCO2-CE style)

**Scope volumes** `group_volume global_volume master_volume` (additive with `volume`)

## Phase 2 (planned, in priority order from real library failures)

Filters (`fil_type cutoff resonance`), pitch/filter envelopes, LFOs,
crossfade opcodes (`xfin/xfout` key/vel/CC), key/vel tracking beyond
amp_veltrack, `rt_decay`, pedal opcodes beyond CC64 semantics, output
routing, curves, `delay/sample randomization`.

## Behavior notes

- A region without `sample=` is dropped with a warning.
- Regions whose sample is missing/undecodable are disabled and reported;
  the rest of the instrument stays playable. Zero playable regions → load
  fails with an error diagnostic.
- Articulations are derived from `sw_last` groups (label from `sw_label`,
  else the keyswitch note name); instruments without keyswitches get one
  "Default" articulation.
