# Plugin Design Summary

`neural-jammer` is a JUCE MIDI effect that turns short incoming note phrases into an LLM prompt, runs inference off the audio thread, and emits generated MIDI back to the host.

## Signal Flow

- `PluginProcessor::processBlock` is the runtime hub.
- Incoming host MIDI plus the on-screen keyboard MIDI are merged into a temporary input buffer.
- Note on/off events are accumulated into `midiForPrompt` with absolute sample offsets.
- After a silence gap (`waitTimeSeconds`), the buffered phrase is converted from MIDI to the project text format (`NJamLanguage::MIDIToNJam`) and submitted for inference.
- Returned text is converted back to MIDI (`NJamLanguage::NJamToMIDI`) and emitted immediately or queued in `futureMidiFromLLM` if events land in later blocks.

## Runtime Model

- The audio thread never runs the model directly.
- `InferenceThreadManager` owns a worker thread that consumes the latest prompt and calls `LLMController`.
- `LLMController` wraps `llama.cpp` model loading, context reset, tokenization, sampling, generation, and inference stats.
- Prompt handoff is overwrite-style: if a newer prompt arrives before the worker consumes the old one, the old prompt is replaced.

## Control / Parameters

- Runtime controls live in `AudioProcessorValueTreeState`.
- Current user-facing controls are:
- `waitTimeSeconds`: silence duration before a phrase is considered complete.
- `midiThruEnabled`: whether input MIDI is preserved in the outgoing buffer.
- `contextLength`: model context preset (`128/256/512/1024`).
- Model path is loaded at runtime from the editor rather than exposed as a normal automatable parameter.

## Important Design Decisions

- MIDI generation is phrase-based, not token-by-token in real time.
- Prompt capture depends on note activity followed by silence; this keeps prompts short and avoids constant inference.
- Audio and inference are decoupled so `processBlock` stays lightweight and deterministic.
- Output scheduling is block-relative: late model notes are stored and released in future blocks.
- State persistence is partial: APVTS-backed controls persist, but model loading is still an explicit runtime action.
