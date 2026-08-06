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
  (longest-match substitution)
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
off_by off_mode(fast normal) polyphony note_polyphony`

**Round robin/random** `seq_length seq_position lorand hirand`

**Keyswitch/CC** `sw_lokey sw_hikey sw_last sw_default sw_label loccN hiccN`

**Control header** `default_path set_ccN label_ccN`

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
