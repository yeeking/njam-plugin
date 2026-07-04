# Example MIDI Listening Set

Generated on 2026-07-04 with the remote OpenAI-compatible server at `http://localhost:2224/v1/` using `google/gemma-4-26b-a4b-qat`.

These files were produced through the agent tool loop: style blueprint, mathematical music planning, formula MIDI generation, analysis, and repair/humanization when requested by the analysis.

## Files

1. `01_bright_phased_marimba_canon_two_voices_humanized.mid`
   - Prompt: short bright phased marimba canon with two voices and subtle human timing.
   - Final tool path: generated -> analyzed -> humanized.

2. `02_afro_cuban_montuno_bass_clave_congas_shaker_humanized.mid`
   - Prompt: four-bar Afro-Cuban piano bass and percussion groove with clave, congas, shaker, and syncopated montuno.
   - Final tool path: generated -> analyzed -> humanized.

3. `03_nocturnal_ambient_chords_arps_counterline_humanized.mid`
   - Prompt: nocturnal ambient chord progression with slow evolving arpeggios, gentle counterline, wide voicings, and calm ending.
   - Final tool path: generated -> analyzed -> humanized.

4. `04_dub_techno_minor_stabs_sub_bass_hats_register_fitted.mid`
   - Prompt: dub techno loop with minor chord stabs, sub bass, offbeat hats, echoes, and restrained variation.
   - Final tool path: generated -> analyzed -> register fitted -> analyzed.
   - Note: the first generated version was too high/clipped, and the agent repaired the register.

5. `05_jazz_quartet_ii_v_i_trumpet_piano_bass_drums_humanized.mid`
   - Prompt: jazz quartet cue for piano, walking bass, brushed drums, and muted trumpet with ii-V-I harmony, swing feel, comping chords, and a short melodic head.
   - Final tool path: generated attempts -> analyzed -> regenerated -> register fitted -> analyzed -> humanized.

6. `06_classical_chamber_strings_woodwinds_cadence_humanized.mid`
   - Prompt: classical chamber orchestration for string quartet and woodwinds, with cello ostinato, viola inner motion, violin melody, flute counterline, clarinet harmony, and cadence.
   - Final tool path: generated retry -> analyzed -> humanized.

## Notes

- The jazz and classical prompts are useful stress tests because the model asked for richer orchestration and had to recover from malformed formula attempts.
- The dub techno prompt is useful because analysis caught a bad register and the agent repaired it before finalizing.
