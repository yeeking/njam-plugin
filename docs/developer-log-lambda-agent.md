# Developer Log: Mathematical Lambda Music Agent

Date: 2026-07-03

## Context

The current goal is to move the Python/PydanticAI multi-agent music system into the JUCE plugin, with native chat history, OpenAI-compatible remote model support, MIDI generation/playback tools, and a new mathematical music agent. The mathematical agent should make complex musical patterns with fewer tokens by emitting compact formulas instead of long note lists.

The C++ system now has a manager agent, specialist tool calls, remote OpenAI-compatible chat/tool requests, a formula MIDI generator, reusable motifs, section chaining, MIDI playback scheduling, MIDI analysis, live UI activity reporting, and a repeatable remote probe target for testing against `http://localhost:2224/v1/`.

Update 2026-07-03:

- Live probing against `google/gemma-4-26b-a4b-qat` produced initial wrapper-only responses: an empty assistant message with `finish_reason=stop`, and a reasoning-only response with no visible assistant/tool text.
- `MusicAgentCore` now treats those wrapper messages as retryable and immediately asks the model again inside the same run, emitting the compact status event `thinking / retrying empty model response`.
- The retry was verified live: the model recovered, called the style and mathematical specialists, generated formula MIDI, analyzed it, and repaired the register.
- The same live probe also showed that the previous 10-round tool budget could be exhausted before duration repair and playback after several malformed tool attempts. The manager loop is now allowed 14 tool rounds before the reserved final-summary pass.
- Some model tool calls used `{}` for specialist arguments even though the schema requests `user_music_interest`. The core now fills missing specialist interest from the active user prompt before running the specialist, so the blueprint/mathematical agents stay grounded in the user's actual request instead of producing blank generic suggestions.
- `analyze_midi` now appends a compact `next=` hint. Examples: `next=play_midi` for acceptable files, `next=fit_midi_register` for clipped/high/low/very-wide range, `next=fit_midi_pitch_range` for narrow material, `next=fit_midi_duration` for over/under length, and `next=regenerate_density` for overly sparse or dense material. This keeps tool reporting readable for the UI while giving the manager an explicit repair cue.
- `fit_midi_pitch_range` was added as the deterministic counterpart to `narrow_range`. It octave-spreads note events inside a target register while preserving timing, channels, velocities, and program changes. Repair tools now mark the latest MIDI as needing analysis again so the manager loop naturally checks the repaired file before playback.
- The chat UI now has a compact latest-tool summary strip below the live activity log. The processor derives it from important completed tools: generated MIDI paths, `analyze_midi` score/assessment/next hints, repair results, and playback scheduling. This keeps the transcript readable while leaving the latest MIDI state visible during long multi-tool runs.
- Formula motif references now support numeric post-evaluation overrides: `transpose`, `duration_scale`, and `velocity_offset`. These sit alongside existing voice overrides such as `start`, `root`, `channel`, `steps`, and `gate_lambda`, giving the mathematical agent a lower-token way to reuse a motif across sections without composing fragile formula strings.
- Formula voices now support `chord_offsets`, a compact semitone array applied around the evaluated pitch. Examples: `[0,4,7]` for triads, `[0,5,10]` for quartal/minor-colour stabs, or `[0,7,14]` for open fifth stacks. This lets one rhythm/pitch lambda create chordal textures without extra voices or long note lists.
- Live chordal prompts showed that scaling a very-long chordal file can inflate density and trigger another regeneration loop. The manager and tool schema now prefer `fit_midi_duration` with `mode="trim"` for `very_long` material and reserve 18 bounded tool rounds before the final-summary pass.
- `fit_midi_duration` now has a deterministic density guard: if the model requests `mode="scale"` on a long file and the projected result would exceed the dense threshold, the tool switches to trim and reports `mode=trim(auto_dense_from_scale)`. This protects chordal/stab material from becoming a compressed note cloud.
- A live run exposed a `fit_midi_pitch_range` edge case where a repeated single pitch stayed narrow after repair. The tool now spreads repeated note-on occurrences across octave choices while keeping a queue of transformed notes so matching note-offs are preserved.
- A live remote probe returned a bare tool-call sentinel (`<tool_call|>`) with no parseable tool arguments as the first response. The core now retries tool-only/sentinel responses the same way it retries empty wrapper responses, instead of ending with a no-tool deterministic summary.
- The core now refuses `play_midi` after generated or repaired MIDI if the latest file has not been analyzed yet. The tool result tells the manager to call `analyze_midi` first, preventing playback of unverified repair outputs.
- Tests now cover recovery from that refusal: after a blocked premature `play_midi`, the manager can call `analyze_midi` on the generated path and then successfully call `play_midi`.
- Formula voices now support deterministic swing timing via `swing` and `swing_grid`. Odd grid subdivisions are delayed while the formula clock keeps advancing normally, so the model can request shuffled eighth-note feel with a couple of numeric fields instead of spelling out many delayed start times.
- Formula voices now support compact echo/canon expansion with `echoes`, `echo_delay`, `echo_transpose`, and `echo_velocity_decay`. This lets a single lambda voice create delayed transposed repeats, delay-line textures, and canons without duplicating near-identical voices or formulas.
- The expression VM now includes `euclid(pulses, steps, i[, rotation])`, returning `1` for hits and `0` for rests. This is intended for `gate_lambda` in clave, techno, additive percussion, polymeter, and sparse ostinato prompts, giving the agent compact musically even rhythms without bracket-list gates.
- A live Afro-Cuban polymeter probe reached a successful chain through blueprint, mathematical specialist, formula generation, analysis, register/duration/range repair, re-analysis, and playback. It also exposed unnecessary prompt bloat: specialist tool results were echoing the full specialist instructions. Specialist tools now return concise handoff briefs instead, and the mathematical handoff explicitly recommends sane degree-mode roots, `pitch_mode="midi"` for percussion constants, and `euclid(3,8,i)`-style gates.
- A follow-up live probe showed prompt tokens dropping from roughly 53k to roughly 21k after concise specialist results. It also showed the model sometimes calls `analyze_midi` with only the generated filename instead of the absolute path. MIDI-consuming tools now resolve bare/relative filenames through the generated MIDI directory before checking the current working directory, avoiding a failed retry round and a JUCE relative-path assertion.
- The same probe showed two musical robustness issues: occasional missing closing parentheses in `euclid(...)` gate expressions, and repair tools transposing channel-10 percussion note numbers as if they were pitches. Formula cleanup now balances small missing parenthesis counts, and `analyze_midi`, `fit_midi_register`, and `fit_midi_pitch_range` treat channel 10 as percussion. Register/range flags are based on pitched channels when present, while percussion note identities are preserved.
- Re-running the short Afro-Cuban Euclidean prompt after these fixes produced a clean first-pass chain: style blueprint, mathematical specialist, `generate_formula_midi`, `analyze_midi` with `score=1.00 assessment=ok`, then `play_midi`. No repair loop was needed, percussion stayed on channel 10, and prompt usage dropped further to roughly 6.7k tokens.
- `combine_midi` now reports compact layer metadata: note count, program changes, notes by channel, per-source length/note summaries, mode, and total duration. This gives the manager and UI enough context to decide whether a layered/combined result still needs analysis, repair, or playback without re-reading the raw files immediately.
- A live layered prompt for separate phased marimba and bass ostinato validated the intended combine workflow: two `generate_formula_midi` calls, `combine_midi` overlay, `analyze_midi` with `score=1.00`, then `play_midi`. The combine metadata clearly showed two source files, 192 combined notes, three channels, source lengths, and a 48-beat overlay duration.
- The expression VM now has deterministic variation helpers: `rand(i, seed)`, `chance(prob, i, seed)`, and `pick(i, seed, value)`. They are pure repeatable functions, intended for subtle gate, pitch, and velocity variation without long arrays or true runtime randomness.
- A live generative-marimba prompt with repeatable random accents completed cleanly through mathematical specialist, formula generation, analysis, and playback with `score=1.00`. The specialist handoff now explicitly advertises `rand/chance/pick` so future variation prompts are more likely to use the deterministic helpers directly.
- `analyze_midi` now reports `velocity_range`, `velocity_unique`, and `gap_unique`, and lightly flags `flat_velocity` or `clockwork_rhythm` with `next=humanize_midi`. These are low-severity musicality hints, intended to nudge the manager toward deterministic timing/velocity repair or formula-level `rand/chance/pick`, swing, or gate variation when a generated pattern is technically valid but too mechanical.
- A live “mechanical marimba, then improve variation” probe showed the model could burn the tool budget by generating again after a repair without analyzing the repaired file. The core now blocks any new MIDI creation or combine call while the latest generated/repaired MIDI is unanalyzed, just as it already blocks premature playback. The tool result explicitly tells the manager to call `analyze_midi` first.
- `humanize_midi` was added as the deterministic repair counterpart to `flat_velocity` and `clockwork_rhythm`. It applies repeatable note-on timing offsets and velocity variation while preserving note-off pairing, channels, pitches, program changes, and reproducibility. `analyze_midi` now recommends `next=humanize_midi` for these low-severity musicality flags.
- MIDI repair and combine tools now preserve source tempo metadata instead of writing every repaired file at a fixed 120 BPM. `combine_midi` uses the first source file tempo, and register/range/duration/humanize repairs reuse the source file tempo in their outputs and result summaries.
- `analyze_midi` now reports `tempo=...`, and the compact UI latest-analysis strip includes tempo. This makes tempo part of the visible musical state after generation, repair, combine, and playback preparation.
- Tool results stored back into the model transcript are now compacted. Full `AgentRunResult` data is still available to the UI/test caller, but follow-up model prompts get concise tool lines with filenames and high-signal musical fields instead of full paths and bulky result text. This should reduce prompt tokens during long generate/analyze/repair loops.
- The expression VM now includes `cycle(i, value1, value2, ...)` and `choose(i, value1, value2, ...)` for compact repeating pitch, rhythm, velocity, or percussion cells. These are equivalent to simple bracket lookup patterns but are often easier for the model to write and for humans to read.

## Prompt Design

The manager prompt is intentionally direct:

- If the user asks for generated music, the model should call MIDI tools, not merely describe them.
- Mood, genre, chord-style, or form requests should call `style_blueprint_agent_tool` before `mathematical_music_agent_tool`.
- Complex patterned music should prefer `generate_formula_midi` with compact lambda-style formulas, reusable motifs, and sections.
- After complex MIDI generation, the manager should call `analyze_midi`.
- If analysis flags obvious problems, the manager should adjust/regenerate when possible before playback.
- If a MIDI example is generated, the manager should call `play_midi`.

The intended chain is:

```text
user prompt
  -> manager
  -> style_blueprint_agent_tool
  -> mathematical_music_agent_tool
  -> generate_formula_midi
  -> analyze_midi
  -> play_midi
  -> concise final reply
```

The style blueprint specialist is deliberately not asked to write formulas. Its role is to compress musical intent into pattern briefs: section names, chord palette, register, density, rhythmic feel, texture roles, and transitions.

The mathematical specialist is asked to write compact C++ lambda-style ideas. It is told to use:

- `i` or `n` for note index
- `t` or `beat` for global beat
- `local_t`, `lt`, or `section_t` for beat within a section
- `voice` or `v` for voice index
- `section` or `s` for section index
- `rhythm_lambda` for inter-onset beat duration
- `gate_lambda` for rests/syncopation
- reusable `motifs` when a figure recurs
- `sections` when form matters

Specialist tool results are intentionally concise. They should not repeat the full specialist system instructions into the manager transcript, because those tool results are carried into subsequent OpenAI-compatible chat messages and can otherwise dominate prompt tokens during long tool loops.

The mathematical prompt explicitly says that JavaScript and native C++ JIT backends are not enabled yet. The current executable backend is `expr`, a constrained expression VM.

## Why Not Execute Arbitrary C++ Lambdas Yet?

The user-facing concept is “C++ lambda functions”, but the current implementation executes a safe expression subset rather than compiling arbitrary C++ at runtime.

Reasons:

- A JUCE plugin runs inside a DAW or standalone audio process; arbitrary JIT/native compilation would be fragile and risky.
- Shipping a C++ compiler or JIT backend raises deployment, sandboxing, crash, and security concerns.
- The expression VM is deterministic, testable, and enough for many useful musical patterns.
- The syntax can still look lambda-like to the model:

```cpp
[](int i){ return [60,64,67][i % 3] + floor(2 * sin(i * 0.37)); }
```

The tool strips the lambda wrapper and executes the expression body.

Future backends could include:

- `expr`: current safe deterministic expression VM
- `javascript`: possible future QuickJS/Duktape-style sandbox for richer control flow
- `cpp_jit`: possible experimental backend only, not a default plugin path

## Formula Tool Design

Main tool:

```text
generate_formula_midi
```

It accepts either top-level `voices` or `sections`.

Each voice can define:

- `pitch_lambda`
- `rhythm_lambda`
- `gate_lambda`
- `duration_lambda`
- `velocity_lambda`
- `pitch_mode`: `degree` or `midi`
- `instrument`
- `channel`
- `steps`
- `root`
- `scale`
- `start`
- `transpose`
- `duration_scale`
- `velocity_offset`
- `chord_offsets`
- `swing`
- `swing_grid`
- `echoes`
- `echo_delay`
- `echo_transpose`
- `echo_velocity_decay`

`pitch_mode = "degree"` maps formula output through a root/scale. This is often better for musical patterns. `pitch_mode = "midi"` treats the formula result as an absolute MIDI note.

Supported expression features currently include:

- arithmetic: `+ - * / %`
- comparisons: `< <= > >= == !=`
- logical operations: `&& || !`
- ternary expressions: `condition ? a : b`
- bracket lookup: `[60,64,67][i % 3]`
- functions: `sin`, `cos`, `tan`, `abs`, `floor`, `ceil`, `round`, `sqrt`, `pow`, `min`, `max`, `clamp`, `fract`, `mod`, `fmod`, `ifgt`, `iflt`, `ifeq`
- rhythm helper: `euclid(pulses, steps, i[, rotation])`
- cycle helpers: `cycle(i, value1, value2, ...)`, `choose(i, value1, value2, ...)`
- deterministic variation helpers: `rand(i, seed)`, `chance(prob, i, seed)`, `pick(i, seed, value)`
- constants: `pi`, `tau`, `e`
- `std::sin` style names are accepted by stripping `std::`

## Motifs

Motifs were added to reduce token use and make sectioned pattern generation more compositional.

Example shape:

```json
{
  "backend": "expr",
  "tempo": 126,
  "motifs": [
    {
      "name": "bass",
      "pitch_lambda": "[0, 0, 3, 5][i % 4]",
      "rhythm_lambda": "[0.5, 0.25, 0.25, 1.0][i % 4]",
      "gate_lambda": "(i % 8) != 6",
      "duration_lambda": "0.2",
      "instrument": "bass"
    },
    {
      "name": "chime",
      "pitch_lambda": "[14, 17, 21, 24][(i + section) % 4]",
      "rhythm_lambda": "0.25",
      "gate_lambda": "(i % 3) != 1",
      "duration_lambda": "0.08",
      "instrument": "vibraphone"
    }
  ],
  "sections": [
    {
      "name": "intro",
      "start": 0,
      "steps": 8,
      "root": 48,
      "voices": [
        { "motif": "bass", "channel": 1 },
        { "motif": "chime", "channel": 2, "start": 0.25 }
      ]
    },
    {
      "name": "body",
      "start": 8,
      "steps": 12,
      "root": 53,
      "voices": [
        { "motif": "bass", "channel": 1, "gate_lambda": "(i % 5) != 3" },
        { "motif": "chime", "channel": 2 }
      ]
    }
  ]
}
```

Voice-level fields override motif fields. This lets the model keep a repeated figure stable while changing channel, root, start offset, gate, or density per section.

## Tool Calling and OpenAI-Compatible Request Design

The remote LLM wrapper sends OpenAI-compatible `messages` rather than a single prompt blob. The internal transcript is rendered as role blocks:

```text
[system]
...
[user]
...
[assistant]
...
[tool]
...
```

The OpenAI-compatible wrapper converts these to request messages. Tool result blocks are currently sent back as user-visible context:

```text
Tool result:
tool generate_formula_midi returned: ...
```

Native OpenAI-style `tool_calls` from the server are converted into the internal XML-ish tool tag form:

```xml
<tool name="generate_formula_midi">{...}</tool>
```

This keeps the C++ core compatible with both:

- native OpenAI tool-call responses
- fallback text/tool-tag responses
- function-style `generate_formula_midi(...)` and `play_midi(...)` responses in prose/code blocks

`tool_choice="required"` is used only for the first model call when the latest real user message clearly asks for generated/algorithmic music. It is disabled once tool-result context exists, otherwise the final answer pass can get stuck being forced to call another tool.

## Multi-Round Tool Loop

The first version only supported one tool round. That was not enough for:

```text
blueprint -> math specialist -> generate -> analyze -> repair -> play
```

`MusicAgentCore::run()` now uses a bounded multi-round loop. Current limit: 18 model/tool rounds before the reserved final-summary pass.

State tracked inside the loop:

- whether any MIDI has been generated
- whether the latest generated MIDI has been analyzed
- whether the latest generated MIDI has been played

The follow-up system instruction changes based on this state. For example:

- if MIDI exists but has not been analyzed, ask for `analyze_midi`
- if MIDI has been analyzed but not played, ask for `play_midi` if analysis is acceptable
- if analysis reports register flags, ask for `fit_midi_register` and analyze again
- if analysis reports duration flags, ask for `fit_midi_duration` and analyze again
- if analysis reports density flags or a low score, ask the model to repair/regenerate and analyze again

This is intentionally still model-led. It does not yet hard-code automatic regeneration from analysis flags.

If the loop exhausts its tool-round budget, the core now reserves a final text-only model pass. If the model returns a tool-call sentinel anyway, the C++ side falls back to a deterministic summary of the latest successful tool result.

## MIDI Analysis Tool

New tool:

```text
analyze_midi
```

Input:

```json
{ "midi_file": "/path/to/file.mid" }
```

Output includes:

- note count
- note-off count
- channel count
- program change count
- tempo
- pitch range
- duration in beats
- first note beat
- density in notes/beat
- velocity range and velocity uniqueness
- rhythmic gap uniqueness
- average/min/max inter-onset gap
- notes by channel
- pitch class counts
- quality score from `0.00` to `1.00`
- assessment flags

Example:

```text
/path/file.mid (notes=416, note_offs=521, channels=3, programs=9,
pitch_range=101-127, duration=262.00 beats, first_note=0.00,
density=1.59 notes/beat, avg_gap=1.020, min_gap=0.500, max_gap=11.500,
by_channel={1:160,2:192,3:64}, pitch_classes={2:96,5:32,7:288},
score=0.32, assessment=too_high|clipped_top|very_long)
```

Assessment flags currently include:

- `ok`
- `too_high`
- `clipped_top`
- `too_low`
- `narrow_range`
- `very_wide_range`
- `very_sparse`
- `very_dense`
- `very_short`
- `very_long`
- `single_channel_texture`

This tool was added because live tests showed the model could produce syntactically valid MIDI that was musically suspect, especially patterns clipped into the top register.

The score is deliberately simple. It penalizes hard register problems, extreme density, extreme duration, very narrow/wide ranges, and single-channel textures. Current prompt policy treats `score < 0.70` as a repair/regenerate cue.

## MIDI Repair Tools

Two deterministic repair tools now exist so the model does not have to rewrite formulas for every common analysis failure.

### `fit_midi_register`

Input:

```json
{ "midi_file": "/path/to/file.mid", "low": 36, "high": 84 }
```

The tool octave-transposes note events into the target range and clamps only when octave fitting cannot get a note inside range. It preserves timing, velocity, channels, and program changes.

Use this when `analyze_midi` reports:

- `too_high`
- `clipped_top`
- `too_low`

### `fit_midi_duration`

Input:

```json
{ "midi_file": "/path/to/file.mid", "target_beats": 64, "mode": "trim" }
```

Modes:

- `trim`: keep timing and remove/shorten material after `target_beats`
- `scale`: compress or expand event times into `target_beats`

Use this when `analyze_midi` reports:

- `very_long`
- `very_short`

Live tests showed `scale` can solve length while creating dense output. The score calibration now penalizes `very_dense` enough to fall below the repair threshold.

### `humanize_midi`

Input:

```json
{ "midi_file": "/path/to/file.mid", "timing_ticks": 18, "velocity_amount": 10, "seed": 3 }
```

The tool applies deterministic micro-timing and velocity variation. It is intended for valid-but-mechanical files where `analyze_midi` reports:

- `flat_velocity`
- `clockwork_rhythm`

Because timing offsets and velocity changes are seed-based, the same input file and seed produce the same repaired MIDI.

All repair tools preserve tempo metadata from the source MIDI when writing the repaired file. This matters for generated examples whose tempo is part of the musical request.

## Percussion Formula Support

The expression VM now includes GM-style percussion constants for channel-10 drum/percussion voices. This helps prompts such as Afro-Cuban, clave, hand percussion, or polymeter patterns.

Useful constants:

```text
kick, bass_drum, rim, side_stick, snare, hand_clap, clap,
closed_hat, open_hat, low_tom, high_tom, cowbell,
bongo_high, bongo_low, conga_high, conga_low,
timbale_high, timbale_low, agogo_high, agogo_low,
shaker, maracas, clave
```

Example:

```json
{
  "pitch_lambda": "[clave, conga_low, conga_high, shaker][i % 4]",
  "rhythm_lambda": "[0.5, 0.25, 0.75][i % 3]",
  "duration_lambda": "0.08",
  "pitch_mode": "midi",
  "channel": 10
}
```

## UI Reporting

The chat UI now has:

- dynamically resizing chat layout
- transcript rendered with `AttributedString`/`TextLayout`
- compact status label
- inference stats
- compact rolling activity strip

The activity strip updates live from `AgentStatusEvent` callbacks and shows a short trail such as:

```text
.. calling model: manager
-> calling tool: style_blueprint_agent_tool
ok tool complete: style_blueprint_agent_tool
-> calling tool: generate_formula_midi
ok tool complete: generate_formula_midi
```

The full transcript still gets final tool summaries after the background run completes.

## Live Testing Notes

Test server:

```text
http://localhost:2224/v1/
```

Preferred model:

```text
google/gemma-4-26b-a4b-qat
```

Repeatable probe target:

```text
music-agent-remote-probe
```

Example command:

```bash
./build/music-agent-remote-probe_artefacts/Release/music-agent-remote-probe \
  "make nocturnal dub techno with quartal chords and two sections; use reusable mathematical motifs and generate then play the midi"
```

Successful chain observed:

```text
style_blueprint_agent_tool
mathematical_music_agent_tool
generate_formula_midi
play_midi
final answer
```

Another successful chain observed after adding analysis:

```text
style_blueprint_agent_tool
mathematical_music_agent_tool
generate_formula_midi
analyze_midi
play_midi
final answer
```

Important failure/recovery observation:

- Prompt: compact phased marimba and bass canon
- First `generate_formula_midi` call failed
- Improved formula diagnostics reported the failing lambda name/location
- The model repaired the formula and generated a valid MIDI

Important unresolved observation:

- Prompt: compact evolving minimal piano and bass pattern
- `analyze_midi` correctly flagged `too_high|clipped_top|very_long`
- Earlier versions attempted multiple regenerations and often failed to lower the pitch range
- `fit_midi_register` and `fit_midi_duration` now give deterministic repair paths
- The final-summary fallback prevents raw tool-call sentinels or raw tool results from being surfaced when the model exhausts the tool budget

Recent successful observed chain:

```text
style_blueprint_agent_tool
mathematical_music_agent_tool
generate_formula_midi
analyze_midi
fit_midi_register
analyze_midi
fit_midi_duration
analyze_midi
play_midi
final answer
```

## Current Weak Spots

1. Degree vs MIDI pitch mode can still confuse the model.

For musical material, `degree` mode is good, but if the model writes high absolute values while using degree mode, the scale mapping can climb into the top of the MIDI range. The schema/prompt should emphasize:

- use small degree numbers like `[-7, 0, 3, 5]`
- set `root` per section
- use `pitch_mode="midi"` only when returning actual MIDI note numbers

2. Analysis score is heuristic, not musical taste.

The score catches obvious problems, but it cannot know whether a narrow range is artistically intentional. Live minimal/polymeter prompts sometimes produce acceptable `narrow_range` output. The manager should treat score and flags as guidance, not absolute truth.

3. The analysis/regeneration loop could become more deterministic.

Right now the model is strongly guided by state-aware follow-up prompts, but the C++ loop still lets the model choose between repair, regeneration, playback, and final response. A stricter state machine could enforce:

```text
generate -> analyze -> if bad, regenerate -> analyze -> if ok, play
```

or:

```text
generate -> analyze -> register repair -> duration repair -> analyze -> play
```

4. Style specialists are still generic.

The style blueprint tool currently returns useful structure but not deep idiomatic detail for all genres. Afro-Cuban prompts now have percussion constants available, but the style expert could still be improved with more genre-specific pattern vocabulary.

## Test Coverage Added

Core tests now cover:

- XML-ish tool parsing
- function-style fallback parsing for `generate_formula_midi(...)`
- manager prompt advertises real MIDI generation
- prompt classifier requires music tools for relevant prompts
- multi-round chain:
  - style blueprint
  - mathematical specialist
  - formula MIDI generation
  - final response
- formula generation:
  - lambda wrapper stripping
  - bracket lookup
  - ternary expressions
  - `std::sin`
  - `clamp`
  - gate lambdas/rests
  - duration/velocity lambdas
  - section variables
  - motif reuse and voice overrides
  - chord offsets
  - swing timing
  - echo/canon expansion
  - Euclidean rhythm gates
  - cycle/choose helpers
  - deterministic variation helpers
  - unsupported backend rejection
  - detailed formula error reporting
  - percussion constants on channel 10
- combine MIDI:
  - overlay
  - sequence
  - missing file failure
  - compact note/channel/source metadata
- analyze MIDI:
  - reads generated files
  - reports notes, pitch range, density, score
  - flags high/clipped register
  - flags dense files below score threshold
- repair MIDI:
  - `fit_midi_register` removes high/clipped register flags
  - `fit_midi_duration` removes very-long duration flags
  - `humanize_midi` removes flat velocity/clockwork rhythm flags
- conversation history retention
- status event emission

## Next Recommended Steps

Status update, 2026-07-04:

- The chat/tool path now uses the remote OpenAI-compatible server by default and keeps the prompt history/tool-result context inside `MusicAgentCore`.
- The formula MIDI tool has grown into the main "executable lambda" surface: safe expressions, sections, motifs, chord offsets, swing, echo/canon expansion, Euclidean gates, deterministic variation helpers, percussion constants, and compact `cycle`/`choose` helpers.
- The manager now pushes the model toward a more musical loop: generate or repair, analyze, then only play once the latest MIDI has been analyzed.
- Tool results are compacted before being fed back into the model context, while the UI can still show higher-level status/tool activity.
- The old NJam UI controls have been removed from the chat surface; the remaining application is chat-first with endpoint, transcript, input, send/clear, status, inference stats, and tool activity.
- A live prompt listening set was generated against `http://localhost:2224/v1/` using `google/gemma-4-26b-a4b-qat` and copied to `example_midis/`. The set covers phased marimba, Afro-Cuban groove, nocturnal ambient, dub techno, jazz quartet, and classical chamber orchestration. This exposed useful behavior: the model can recover from malformed formula attempts, analysis can trigger register repair, and orchestration prompts need stronger program/channel guidance.

1. Re-run live prompt probes against the local server.

Use `http://localhost:2224/v1/` with `google/gemma-4-26b-a4b-qat` and check realistic multi-turn flows:

- mechanical marimba pattern -> analyze -> humanize -> analyze -> play
- Afro-Cuban layered groove with percussion constants and Euclidean gates
- dub techno chord stab plus bass plus delay/echo expansion
- Reich-style phase pattern using sections or multiple voices
- "make it better" follow-up that repairs rather than replacing useful material

2. Add stricter formula examples for degree mode.

Examples should use small degree offsets and roots:

```json
{
  "root": 48,
  "pitch_lambda": "[-7, 0, 3, 5, 7][i % 5]",
  "pitch_mode": "degree"
}
```

3. Make the analysis state machine stricter.

Instead of only nudging the model, the core can track analysis flags and say:

```text
The latest analysis still reports clipped_top and too_high.
Do not call play_midi yet. Regenerate with lower root or smaller pitch degrees.
```

This should stay lightweight: enforce obvious safety/quality gates, but still let the model make creative choices when the analysis is musically acceptable.

4. Improve genre specialists.

Add more idiomatic guidance for common prompts: Afro-Cuban clave/tumbao, dub techno stabs, Reich-style phasing, jazz walking bass, classical counterpoint, etc.

5. Add a small MIDI preview/inspection surface in the GUI.

Now that generated MIDI is being analyzed and repaired, the UI could expose the latest path, score, flags, note count, and maybe a compact piano-roll preview for the final generated file.

6. Add prompt fixtures for the lambda chain.

Keep a small fixture set of user prompts and expected tool-call shapes. These should verify that the manager delegates to the style/mathematical specialists, requests formula generation, analyzes the file, repairs where needed, and produces a concise final response.

7. Decide the next executable-lambda backend.

The safe C++ expression VM is the right current default for MIDI generation. For richer future lambdas, compare:

- extending the current VM with named local variables/macros
- embedding a small JavaScript engine for dynamic pattern scripts
- using a native JIT only later if script expressiveness becomes a real blocker

8. Later cleanup: remove dormant NJam-era processor/APVTS/CMake pieces.

Keep this until the chat app behavior is stable. The current removal depth is intentionally UI and wiring, not full source deletion.
