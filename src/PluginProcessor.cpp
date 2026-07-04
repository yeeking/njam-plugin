/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin processor.

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>
#include <deque>
#include <limits>
#include <map>
#include <sstream>
#include <thread>
#include <tuple>

namespace
{
constexpr double contextHistoryWindowSeconds = 20.0;
constexpr double pianoRollWindowSeconds = 10.0;
constexpr double captureOverlayFadeSeconds = 4.0;
constexpr double captureOverlayHoldSeconds = 1.5;
constexpr int remoteChatContextLength = 32768;
constexpr int remoteChatMaxResponseTokens = -1;

struct NotePair
{
    juce::MidiMessage noteOn;
    int noteOnSample = 0;
    juce::MidiMessage noteOff;
    int noteOffSample = 1;
};

using NoteKey = std::tuple<int, int>;

juce::String shortenForTranscript (juce::String text, int maxLength = 180)
{
    text = text.trim().replaceCharacter ('\n', ' ');
    if (text.length() <= maxLength)
        return text;

    return text.substring (0, maxLength - 3).trim() + "...";
}

juce::String activitySymbolForState (const juce::String& state)
{
    if (state == "calling tool") return "->";
    if (state == "tool complete") return "ok";
    if (state == "tool failed") return "!!";
    if (state == "calling model") return "..";
    if (state == "thinking") return "..";
    if (state == "done") return "ok";
    return "-";
}

bool shouldRecordActivityState (const juce::String& state)
{
    return state == "calling tool"
        || state == "tool complete"
        || state == "tool failed"
        || state == "calling model"
        || state == "thinking"
        || state == "done";
}

juce::String appendActivityLine (juce::String activity, const AgentStatusEvent& event, int maxLines = 9)
{
    if (! shouldRecordActivityState (event.state))
        return activity;

    const auto line = activitySymbolForState (event.state) + " "
        + event.state
        + (event.detail.isNotEmpty() ? ": " + event.detail : juce::String());

    juce::StringArray lines;
    lines.addLines (activity);
    if (lines.isEmpty() || lines[lines.size() - 1] != line)
        lines.add (line);

    while (lines.size() > maxLines)
        lines.remove (0);

    return lines.joinIntoString ("\n");
}

juce::String extractParenthesizedDetails (const juce::String& text)
{
    const int open = text.indexOfChar ('(');
    const int close = text.lastIndexOfChar (')');
    if (open < 0 || close <= open)
        return {};

    return text.substring (open + 1, close);
}

juce::String extractDetailField (const juce::String& details, const juce::String& key)
{
    const auto prefix = key + "=";
    const int start = details.indexOf (prefix);
    if (start < 0)
        return {};

    const int valueStart = start + prefix.length();
    int valueEnd = details.indexOf (valueStart, ", ");
    if (valueEnd < 0)
        valueEnd = details.length();

    return details.substring (valueStart, valueEnd).trim();
}

juce::String makeToolSummaryLine (const ToolInvocation& call, const ToolResult* result)
{
    if (result == nullptr)
        return {};

    if (! result->ok)
        return "Latest tool issue: " + call.name + " failed";

    const auto content = result->content.trim();
    if (call.name == "analyze_midi")
    {
        const auto details = extractParenthesizedDetails (content);
        juce::StringArray parts;
        for (const auto& key : { "score", "assessment", "next", "notes", "tempo", "pitch_range", "duration", "density", "velocity_range", "gap_unique" })
        {
            const auto value = extractDetailField (details, key);
            if (value.isNotEmpty())
                parts.add (juce::String (key) + "=" + value);
        }

        return parts.isEmpty()
            ? "Latest analysis: " + shortenForTranscript (content, 220)
            : "Latest analysis: " + parts.joinIntoString (" | ");
    }

    if (call.name == "play_midi")
        return "Playback: " + shortenForTranscript (content, 220);

    if (call.name.startsWith ("fit_midi_") || call.name == "humanize_midi")
        return "Latest repair: " + call.name + " -> " + shortenForTranscript (content, 220);

    if (call.name == "generate_formula_midi" || call.name == "combine_midi")
        return "Latest MIDI: " + shortenForTranscript (content, 220);

    return {};
}

std::vector<NotePair> extractCompleteNotePairs (const juce::MidiBuffer& sourceBuffer,
                                                int defaultDurationSamples)
{
    std::map<NoteKey, std::deque<std::pair<juce::MidiMessage, int>>> openNotes;
    std::vector<NotePair> notePairs;

    for (const auto metadata : sourceBuffer)
    {
        const auto message = metadata.getMessage();
        const int samplePosition = metadata.samplePosition;

        if (message.isNoteOn())
        {
            openNotes[{ message.getChannel(), message.getNoteNumber() }].push_back ({ message, samplePosition });
        }
        else if (message.isNoteOff())
        {
            auto noteIt = openNotes.find ({ message.getChannel(), message.getNoteNumber() });
            if (noteIt == openNotes.end() || noteIt->second.empty())
                continue;

            const auto [noteOn, noteOnSample] = noteIt->second.front();
            noteIt->second.pop_front();
            if (noteIt->second.empty())
                openNotes.erase (noteIt);

            notePairs.push_back ({
                noteOn,
                noteOnSample,
                message,
                std::max (noteOnSample + 1, samplePosition)
            });
        }
    }

    for (auto& [key, pendingNotes] : openNotes)
    {
        juce::ignoreUnused (key);
        for (const auto& [noteOn, noteOnSample] : pendingNotes)
        {
            notePairs.push_back ({
                noteOn,
                noteOnSample,
                juce::MidiMessage::noteOff (noteOn.getChannel(), noteOn.getNoteNumber()),
                std::max (noteOnSample + 1, noteOnSample + std::max (1, defaultDurationSamples))
            });
        }
    }

    std::sort (notePairs.begin(), notePairs.end(),
               [] (const NotePair& lhs, const NotePair& rhs)
               {
                   if (lhs.noteOnSample != rhs.noteOnSample)
                       return lhs.noteOnSample < rhs.noteOnSample;

                   if (lhs.noteOffSample != rhs.noteOffSample)
                       return lhs.noteOffSample < rhs.noteOffSample;

                   if (lhs.noteOn.getChannel() != rhs.noteOn.getChannel())
                       return lhs.noteOn.getChannel() < rhs.noteOn.getChannel();

                   return lhs.noteOn.getNoteNumber() < rhs.noteOn.getNoteNumber();
               });

    return notePairs;
}

juce::MidiBuffer makeMidiBufferFromNotePairs (const std::vector<NotePair>& notePairs)
{
    juce::MidiBuffer buffer;
    for (const auto& notePair : notePairs)
    {
        buffer.addEvent (notePair.noteOn, notePair.noteOnSample);
        buffer.addEvent (notePair.noteOff, std::max (notePair.noteOnSample + 1, notePair.noteOffSample));
    }
    return buffer;
}

std::vector<PianoRollDisplayNote> makeDisplayNotes (const juce::MidiBuffer& sourceBuffer,
                                                    int64_t latestSample)
{
    std::map<NoteKey, std::deque<std::pair<juce::MidiMessage, int64_t>>> openNotes;
    std::vector<PianoRollDisplayNote> displayNotes;

    for (const auto metadata : sourceBuffer)
    {
        const auto message = metadata.getMessage();
        const int64_t samplePosition = metadata.samplePosition;

        if (message.isNoteOn())
        {
            openNotes[{ message.getChannel(), message.getNoteNumber() }].push_back ({ message, samplePosition });
        }
        else if (message.isNoteOff())
        {
            auto noteIt = openNotes.find ({ message.getChannel(), message.getNoteNumber() });
            if (noteIt == openNotes.end() || noteIt->second.empty())
                continue;

            const auto [noteOn, noteOnSample] = noteIt->second.front();
            noteIt->second.pop_front();
            if (noteIt->second.empty())
                openNotes.erase (noteIt);

            displayNotes.push_back ({
                noteOn.getNoteNumber(),
                noteOnSample,
                std::max (noteOnSample + 1, samplePosition)
            });
        }
    }

    for (auto& [key, pendingNotes] : openNotes)
    {
        juce::ignoreUnused (key);
        for (const auto& [noteOn, noteOnSample] : pendingNotes)
        {
            displayNotes.push_back ({
                noteOn.getNoteNumber(),
                noteOnSample,
                std::max (noteOnSample + 1, latestSample)
            });
        }
    }

    std::sort (displayNotes.begin(), displayNotes.end(),
               [] (const PianoRollDisplayNote& lhs, const PianoRollDisplayNote& rhs)
               {
                   if (lhs.startSample != rhs.startSample)
                       return lhs.startSample < rhs.startSample;

                   if (lhs.endSample != rhs.endSample)
                       return lhs.endSample < rhs.endSample;

                   return lhs.noteNumber < rhs.noteNumber;
               });

    return displayNotes;
}

}

juce::AudioProcessorValueTreeState::ParameterLayout NJamPluginProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> parameters;
    parameters.push_back (std::make_unique<juce::AudioParameterFloat> (waitTimeParamID,
                                                                        "Wait Time",
                                                                        juce::NormalisableRange<float> (0.1f, 2.0f, 0.01f),
                                                                        0.1f));
    parameters.push_back (std::make_unique<juce::AudioParameterBool> (midiThruParamID,
                                                                       "MIDI Thru",
                                                                       true));
    parameters.push_back (std::make_unique<juce::AudioParameterChoice> (contextLengthParamID,
                                                                        "Context Length",
                                                                        juce::StringArray { "256", "512", "1024", "2048" },
                                                                        2));
    parameters.push_back (std::make_unique<juce::AudioParameterChoice> (maxResponseTokensParamID,
                                                                        "Max Response Tokens",
                                                                        juce::StringArray { "64", "128", "256", "512" },
                                                                        2));
    parameters.push_back (std::make_unique<juce::AudioParameterFloat> (selfListenParamID,
                                                                        "Self Listen",
                                                                        juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f),
                                                                        0.0f));
    parameters.push_back (std::make_unique<juce::AudioParameterFloat> (lookBackTimeParamID,
                                                                        "Look Back Time",
                                                                        juce::NormalisableRange<float> (0.5f, 20.0f, 0.01f),
                                                                        4.0f));
    parameters.push_back (std::make_unique<juce::AudioParameterFloat> (maxNoteLengthParamID,
                                                                        "Max Note Length",
                                                                        juce::NormalisableRange<float> (0.25f, 3.0f, 0.01f),
                                                                        1.0f));
    parameters.push_back (std::make_unique<juce::AudioParameterFloat> (timingMultiplierParamID,
                                                                        "Timing Multiplier",
                                                                        juce::NormalisableRange<float> (0.25f, 3.0f, 0.01f),
                                                                        1.0f));
    return { parameters.begin(), parameters.end() };
}

//==============================================================================
NJamPluginProcessor::NJamPluginProcessor()
#ifndef JucePlugin_PreferredChannelConfigurations
     : AudioProcessor (BusesProperties()
                     #if ! JucePlugin_IsMidiEffect
                      #if ! JucePlugin_IsSynth
                       .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                      #endif
                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                     #endif
                       )
#endif
// ,  inferenceThread{},  llmThread{NJamPluginProcessor::infiniteInfer,  std::ref(keepRunningLLM)}
, apvts (*this, nullptr, "PluginState", createParameterLayout())
, llmControllerP{std::make_shared<LLMController>()}
, inferenceThread{llmControllerP}
{
    remoteLLM.setEndpoint ("http://localhost:2224/v1/");
    chatAgent.setMidiPlaybackHandler ([this] (const juce::File& midiFile)
    {
        return scheduleMidiFileForPlayback (midiFile);
    });
}

NJamPluginProcessor::~NJamPluginProcessor()
{
    if (chatThread.joinable())
        chatThread.join();
    llmControllerP->requestStop();
    inferenceThread.stop();
    llmControllerP->unloadModel();
}

//==============================================================================
const juce::String NJamPluginProcessor::getName() const
{
    return JucePlugin_Name;
}

bool NJamPluginProcessor::acceptsMidi() const
{
   #if JucePlugin_WantsMidiInput
    return true;
   #else
    return false;
   #endif
}

bool NJamPluginProcessor::producesMidi() const
{
   #if JucePlugin_ProducesMidiOutput
    return true;
   #else
    return false;
   #endif
}

bool NJamPluginProcessor::isMidiEffect() const
{
   #if JucePlugin_IsMidiEffect
    return true;
   #else
    return false;
   #endif
}

double NJamPluginProcessor::getTailLengthSeconds() const
{
    return 0.0;
}

int NJamPluginProcessor::getNumPrograms()
{
    return 1;   // NB: some hosts don't cope very well if you tell them there are 0 programs,
                // so this should be at least 1, even if you're not really implementing programs.
}

int NJamPluginProcessor::getCurrentProgram()
{
    return 0;
}

void NJamPluginProcessor::setCurrentProgram (int index)
{
}

const juce::String NJamPluginProcessor::getProgramName (int index)
{
    return {};
}

void NJamPluginProcessor::changeProgramName (int index, const juce::String& newName)
{
}

//==============================================================================
void NJamPluginProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    juce::ignoreUnused (sampleRate, samplesPerBlock);
    updateMaxSamplesWithoutNotes();

    // Use this method as the place to do any pre-playback
    // initialisation that you need..
}

void NJamPluginProcessor::releaseResources()
{
    // When playback stops, you can use this as an opportunity to free up any
    // spare memory, etc.
}

#ifndef JucePlugin_PreferredChannelConfigurations
bool NJamPluginProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
  #if JucePlugin_IsMidiEffect
    juce::ignoreUnused (layouts);
    return true;
  #else
    // This is the place where you check if the layout is supported.
    // In this template code we only support mono or stereo.
    // Some plugin hosts, such as certain GarageBand versions, will only
    // load plugins that support stereo bus layouts.
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono()
     && layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    // This checks if the input layout matches the output layout
   #if ! JucePlugin_IsSynth
    if (layouts.getMainOutputChannelSet() != layouts.getMainInputChannelSet())
        return false;
   #endif

    return true;
  #endif
}
#endif

void NJamPluginProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    updateMaxSamplesWithoutNotes();
    int64_t blockStartSamples = static_cast<int64_t> (samplesSincePromptStart);
    int64_t blockEndSamples = blockStartSamples + buffer.getNumSamples();
    juce::MidiBuffer inputMidiMessages;

    for (const MidiMessageMetadata metadata : midiMessages)
        inputMidiMessages.addEvent (metadata.getMessage(), metadata.samplePosition);

    // Retrieve any midi messages from the on-screen keyboard so they can
    // contribute to prompt generation and optionally be passed through.
    consumeKeyboardMidiMessages(inputMidiMessages);

    const bool inferenceInProgress = llmControllerP != nullptr
        && llmControllerP->getStatus() == LLMStatus::Generating;
    bool generatePrompt{false};
    if (bufferContainsNoteEvents(inputMidiMessages)){// we've got some note events to process
        // store the events over to the promptMessages buffer
        samplesWithoutNotes = 0;
        // add the events to the prompt buffer
        for (const MidiMessageMetadata metadata : inputMidiMessages){
            MidiMessage msg = metadata.getMessage();
            if (msg.isNoteOn() || msg.isNoteOff()){
                lastLiveInputNoteSample = blockStartSamples + metadata.samplePosition;
                msg.setTimeStamp (msg.getTimeStamp() + static_cast<double> (blockStartSamples));
                addMidiMessageToPrompt(msg);
            }
        } 
    }
    else{ // we did not get any notes this time 
        // no notes events
        samplesWithoutNotes += buffer.getNumSamples();
        // std::cout << "pb elapsed samples " << samplesWithoutNotes << " of  " << maxSamplesWithoutNotes << " prompt msgs " << countPromptMIDIMessages() << std::endl;
        const bool newLiveInputArrivedSinceLastTrigger = lastLiveInputNoteSample > lastTriggeredInputNoteSample;
        if (!inferenceInProgress &&
            samplesWithoutNotes > maxSamplesWithoutNotes &&  // silence was long enough
            newLiveInputArrivedSinceLastTrigger){
            // time to prompt
            generatePrompt = true;
            samplesWithoutNotes = 0;
        }
    }

    if (! isMidiThruEnabled())
    {
        midiMessages.clear();
    }
    else
    {
        midiMessages.swapWith(inputMidiMessages);
    }

    juce::MidiBuffer pendingPlayback;
    consumePendingPlaybackMidi (pendingPlayback);
    if (pendingPlayback.getNumEvents() > 0)
    {
        lastModelOutputBuffer = normalizeBufferToSampleZero (pendingPlayback);
        storeOutputPreview (pendingPlayback);
        futureMidiFromLLM.addEvents (pendingPlayback, 0, -1, 0);
    }

    pruneContextHistory (blockEndSamples);
    storeContextRollSnapshot (makeRollSnapshot (midiForPrompt, blockEndSamples, true));

    if (generatePrompt){
        const int64_t contextCaptureSample = std::max<int64_t> (0, lastLiveInputNoteSample);
        juce::MidiBuffer promptMsgs = makeRecentContextMidi (contextCaptureSample);
        promptMsgs = sanitizeNotePairs (promptMsgs);
        promptMsgs = normalizeBufferToSampleZero (promptMsgs);
        mergeSelfListenNotes (promptMsgs);
        promptMsgs = sanitizeNotePairs (promptMsgs);

        if (promptMsgs.getNumEvents() > 0)
        {
            auto capturedContextBuffer = sanitizeNotePairs (makeRecentContextMidi (contextCaptureSample));
            const auto capturedPairs = extractCompleteNotePairs (capturedContextBuffer, getDefaultPromptNoteDurationSamples());
            if (! capturedPairs.empty())
            {
                lastCapturedContextStartSample = std::max<int64_t> (0, contextCaptureSample - getLookBackSamples());
                lastCapturedContextEndSample = contextCaptureSample;
                lastCapturedContextTriggerSample = blockEndSamples;
                hasCapturedContextOverlay = true;
                storeContextRollSnapshot (makeRollSnapshot (midiForPrompt, blockEndSamples, true));
            }

            storePromptPreview (makeEstimatedPromptPreview (promptMsgs));

            // convert the messages to a string of njam... 
            std::string prompt = NJamLanguage::MIDIToNJam(promptMsgs, getSampleRate(), 120.0);
            {
                const std::lock_guard<std::mutex> lock (promptDebugMutex);
                lastPromptMidiEventCount = promptMsgs.getNumEvents();
                lastPromptNotePairCount = static_cast<int> (extractCompleteNotePairs (promptMsgs, getDefaultPromptNoteDurationSamples()).size());
                lastPromptCharCount = static_cast<int> (prompt.size());
            }
            llmControllerP->registerPromptSettings (prompt, ILLMController::PromptSettings {
                static_cast<uint32_t> (getEffectiveRuntimeContextLength()),
                static_cast<size_t> (getMaxResponseTokensForCurrentContext()),
                false
            });
            lastTriggeredInputNoteSample = lastLiveInputNoteSample;

            // std::string prompt{"p_52 c_0 v_35 d_4120 w_1520"};
            // send it over for processing - the thread will pick it up
            // when its ready 
            inferenceThread.setPrompt(prompt);
        }
    }
    // now we can check if there any fresh messages back from the LLM
    std::string llmResponse;
    InferenceThreadManager::InferenceResult inferenceResult;
    if (inferenceThread.consumeInferenceResult(inferenceResult))
    {
        llmResponse = inferenceResult.output;
        std::lock_guard<std::mutex> lock(inferenceStatsMutex);
        lastInferenceStats = inferenceResult.stats;
        haveInferenceStats = true;
        if (lastInferenceStats.num_tokens_in_prompt == 1)
        {
            int promptMidiEventCount = 0;
            int promptNotePairCount = 0;
            int promptCharCount = 0;
            {
                const std::lock_guard<std::mutex> promptLock (promptDebugMutex);
                promptMidiEventCount = lastPromptMidiEventCount;
                promptNotePairCount = lastPromptNotePairCount;
                promptCharCount = lastPromptCharCount;
            }

            DBG ("[prompt-debug] prompt_tokens=1 midi_events=" + juce::String (promptMidiEventCount)
                 + " note_pairs=" + juce::String (promptNotePairCount)
                 + " prompt_chars=" + juce::String (promptCharCount)
                 + " response_tokens=" + juce::String (static_cast<int> (lastInferenceStats.num_tokens_in_response))
                 + " prompt_ctx=" + juce::String (getContextLength())
                 + " runtime_ctx=" + juce::String (getEffectiveRuntimeContextLength())
                 + " max_response=" + juce::String (getMaxResponseTokensForCurrentContext()));
        }
    }
    if (llmResponse != ""){
        // we got something
        // convert it to midi messages
        // std::cout << "processblock got llm result " << llmResponse << std::endl;
        MidiBuffer llmMIDIBuffer = transformGeneratedMidiForPlayback (NJamLanguage::NJamToMIDI (llmResponse, getSampleRate(), 120.0));
        lastModelOutputBuffer = normalizeBufferToSampleZero (llmMIDIBuffer);
        if (llmMIDIBuffer.getNumEvents() > 0)
            storeOutputPreview (llmMIDIBuffer);

        // futureMidiFromLLM.clear();// ignore future midi 
        // print them out 
        for (const MidiMessageMetadata metadata : llmMIDIBuffer){
            MidiMessage msg = metadata.getMessage();
            if (msg.getTimeStamp() > buffer.getNumSamples()){// send it in the future 
                futureMidiFromLLM.addEvent(msg, msg.getTimeStamp() - buffer.getNumSamples());
            }
            else{ // send it now 
                midiMessages.addEvent(msg, msg.getTimeStamp());
                juce::MidiBuffer displayBuffer;
                displayBuffer.addEvent (msg, metadata.samplePosition);
                appendOutputDisplayEvents (displayBuffer, blockStartSamples);
            }
        }
    }// end of what we do if we get a response from the llm

    else{
        // what to do if we haven't got a new response from the llm
        // copy all future messages to a local buffer
        MidiBuffer futureMidiFromLLMCopy;
        futureMidiFromLLMCopy.swapWith(futureMidiFromLLM);
        // if a message occurs in the current buffer, put them into the outgoing midi
        for (const MidiMessageMetadata metadata : futureMidiFromLLMCopy){
            MidiMessage msg = metadata.getMessage();
            if (msg.getTimeStamp() > buffer.getNumSamples()){// send it in the future 
                futureMidiFromLLM.addEvent(msg, msg.getTimeStamp() - buffer.getNumSamples());
            }
            else{
                midiMessages.addEvent(msg, msg.getTimeStamp());
                juce::MidiBuffer displayBuffer;
                displayBuffer.addEvent (msg, metadata.samplePosition);
                appendOutputDisplayEvents (displayBuffer, blockStartSamples);
            }
        }
        // if a message occurs after the current buffer, store it back to the future midi 
    }

    pruneOutputDisplayHistory (blockEndSamples);
    storeOutputRollSnapshot (makeRollSnapshot (outputDisplayHistory, blockEndSamples, false));
    samplesSincePromptStart = blockEndSamples;
}

//==============================================================================
bool NJamPluginProcessor::hasEditor() const
{
    return true; // (change this to false if you choose to not supply an editor)
}

void NJamPluginProcessor::setMidiThruEnabled (bool shouldEnable)
{
    midiThruEnabled.store (shouldEnable);
    if (auto* parameter = apvts.getParameter (midiThruParamID))
        parameter->setValueNotifyingHost (shouldEnable ? 1.0f : 0.0f);
}

bool NJamPluginProcessor::isMidiThruEnabled() const
{
    if (const auto* parameter = apvts.getRawParameterValue (midiThruParamID))
        return parameter->load() >= 0.5f;

    return midiThruEnabled.load();
}

juce::AudioProcessorEditor* NJamPluginProcessor::createEditor()
{
    return new NJamPluginEditor (*this);
}

//==============================================================================
void NJamPluginProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto stateXml = apvts.copyState().createXml())
        copyXmlToBinary (*stateXml, destData);
}

void NJamPluginProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    const auto xmlState = getXmlFromBinary (data, sizeInBytes);
    if (xmlState == nullptr)
        return;

    const auto restoredState = juce::ValueTree::fromXml (*xmlState);
    if (! restoredState.isValid())
        return;

    apvts.replaceState (restoredState);
    midiThruEnabled.store (isMidiThruEnabled());
    updateMaxSamplesWithoutNotes();

    const auto savedModelPath = apvts.state.getProperty (modelPathParamID).toString();
    if (savedModelPath.isNotEmpty())
        loadModelFromPath (savedModelPath);
}

//==============================================================================
// This creates new instances of the plugin..
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new NJamPluginProcessor();

}


// // static function that runs the llm in an infinite loop 
// void NJamPluginProcessor::infiniteInfer(std::atomic<bool>& keepRunning)
// {
//     LLMController llm;
//     bool loaded = llm.loadModel("/Users/matthewyk/src/ai-music/neural-jammer/models/EleutherAI__pythia-70m-last-q8_0.gguf");
//         if (loaded){
//         std::cout << "Model loaded. Status " << llm.getStatusString() << std::endl;
//     }
//     else{
//         return; 
//     }

//     // llm.resetContext(512);
//     llm.prepareSampler();
//     while(keepRunning.load()){
//         llm.resetContext(512);

//         std::cout << "running inference" << std::endl;
//         std::cout << "runInference starting " << std::endl;
//         std::string prompt{"p_52 c_0 v_35 d_4120 w_1520"};
//         std::cout << "runInference calling llm generate " << std::endl;

//         std::string res = llm.generate(prompt, 256);

//         std::cout << "runInference done:: " << res << std::endl;
//     }
//     std::cout << "calling unload model" << std::endl;

//     llm.unloadModel();// ensure everything is freed. 
//     std::cout << "exiting infiniteInfer" << std::endl;
// }

void NJamPluginProcessor::handleKeyboardMidiMessage (const juce::MidiMessage& message)
{
    std::lock_guard<std::mutex> guard(midiFromGUIMutex);
    midiFromGUI.addEvent(message, 0);// we assume it is at position zero

}
void NJamPluginProcessor::consumeKeyboardMidiMessages( juce::MidiBuffer& putMessagesHere)
{
    std::lock_guard<std::mutex> guard(midiFromGUIMutex);
    // copy the messages from midiFromBUI to putMessagesHere
    for (const MidiMessageMetadata metadata : midiFromGUI){
        // std::cout << "consumeKeyboardMidiMessages found midi " << metadata.getMessage().getDescription() << std::endl;
        putMessagesHere.addEvent(metadata.getMessage(), metadata.samplePosition);
    }
    midiFromGUI.clear();
}

bool NJamPluginProcessor::bufferContainsNoteEvents(MidiBuffer& midiBuffer)
{
    for (const MidiMessageMetadata metadata : midiBuffer){
        if (metadata.getMessage().isNoteOn() || 
            metadata.getMessage().isNoteOff()){
                return true; 
            }
    }
    return false; 
}


void NJamPluginProcessor::addMidiMessageToPrompt(const juce::MidiMessage& message)
{
    std::lock_guard<std::mutex> guard(midiForPromptMutex);
    midiForPrompt.addEvent(message, message.getTimeStamp());
}

void NJamPluginProcessor::consumePromptMIDIMessages(juce::MidiBuffer& putMessagesHere)
{
    std::lock_guard<std::mutex> guard(midiForPromptMutex);
    DBG("Preparing to copy retained context midi. evemts: " << midiForPrompt.getNumEvents());
    for (const MidiMessageMetadata metadata : midiForPrompt){
        MidiMessage msg = metadata.getMessage();
        putMessagesHere.addEvent(msg, msg.getTimeStamp());// assume position zero in the buffer
    }
}

int NJamPluginProcessor::countPromptMIDIMessages()
{    
    std::lock_guard<std::mutex> guard(midiForPromptMutex);
    return midiForPrompt.getNumEvents();

}

ILLMController::InferenceStats NJamPluginProcessor::getLastInferenceStats() const
{
    std::lock_guard<std::mutex> lock(inferenceStatsMutex);
    return lastInferenceStats;
}

bool NJamPluginProcessor::hasInferenceStats() const
{
    std::lock_guard<std::mutex> lock(inferenceStatsMutex);
    return haveInferenceStats;
}

bool NJamPluginProcessor::loadModelFromPath (const juce::String& modelPath)
{
    const bool wasSuspended = isSuspended();
    if (! wasSuspended)
        suspendProcessing (true);

    const auto resumeProcessing = [this, wasSuspended]()
    {
        if (! wasSuspended)
            suspendProcessing (false);
    };

    if (modelPath.isEmpty())
    {
        postCachedModelTrainingContextLength (0);
        resumeProcessing();
        return false;
    }

    llmControllerP->unloadModel();
    postCachedModelTrainingContextLength (0);
    const bool loaded = llmControllerP->loadModel (modelPath.toStdString());

    if (loaded)
    {
        std::cout << "Model loaded. Status " << llmControllerP->getStatusString() << std::endl;
        llmControllerP->prepareSampler();
        apvts.state.setProperty (modelPathParamID, modelPath, nullptr);
        postCachedModelTrainingContextLength (llmControllerP->getModelTrainingContextLength());
    }

    resumeProcessing();
    return loaded;
}

juce::String NJamPluginProcessor::getLoadedModelFileName() const
{
    const auto modelPath = apvts.state.getProperty (modelPathParamID).toString();
    if (modelPath.isEmpty())
        return "No model loaded";

    return juce::File (modelPath).getFileName();
}

juce::String NJamPluginProcessor::getStatusText() const
{
    const auto chatStatus = getAgentStatusText();
    if (chatStatus.isNotEmpty() && chatStatus != "ready")
        return "Agent: " + chatStatus;

    if (isSuspended())
        return "Status: Suspended";

    if (llmControllerP == nullptr)
        return "Status: No controller";

    return "Status: " + juce::String (llmControllerP->getStatusString());
}

float NJamPluginProcessor::getWaitTimeSeconds() const
{
    if (const auto* parameter = apvts.getRawParameterValue (waitTimeParamID))
        return parameter->load();

    return 0.1f;
}

int NJamPluginProcessor::getContextLength() const
{
    if (const auto* parameter = dynamic_cast<juce::AudioParameterChoice*> (apvts.getParameter (contextLengthParamID)))
    {
        switch (parameter->getIndex())
        {
            case 0: return 256;
            case 1: return 512;
            case 2: return 1024;
            case 3: return 2048;
            
            default: break;
        }
    }

    return 512;
}

int NJamPluginProcessor::getMaxResponseTokens() const
{
    if (const auto* parameter = dynamic_cast<juce::AudioParameterChoice*> (apvts.getParameter (maxResponseTokensParamID)))
    {
        switch (parameter->getIndex())
        {
            case 0: return 64;
            case 1: return 128;
            case 2: return 256;
            case 3: return 512;
            default: break;
        }
    }

    return 128;
}

void NJamPluginProcessor::setContextLength (int contextLength)
{
    float choiceValue = 0.5f;
    switch (contextLength)
    {
        case 256: choiceValue = 0.0f; break;
        case 512: choiceValue = 1.0f / 3.0f; break;
        case 1024: choiceValue = 2.0f / 3.0f; break;
        case 2048: choiceValue = 1.0f; break;
        default: return;
    }

    if (auto* parameter = apvts.getParameter (contextLengthParamID))
        parameter->setValueNotifyingHost (choiceValue);
}

void NJamPluginProcessor::setMaxResponseTokens (int maxTokens)
{
    float choiceValue = 0.5f;
    switch (maxTokens)
    {
        case 64: choiceValue = 0.0f; break;
        case 128: choiceValue = 1.0f / 3.0f; break;
        case 256: choiceValue = 2.0f / 3.0f; break;
        case 512: choiceValue = 1.0f; break;
        default: return;
    }

    if (auto* parameter = apvts.getParameter (maxResponseTokensParamID))
        parameter->setValueNotifyingHost (choiceValue);
}

float NJamPluginProcessor::getSelfListen() const
{
    if (const auto* parameter = apvts.getRawParameterValue (selfListenParamID))
        return parameter->load();

    return 0.0f;
}

float NJamPluginProcessor::getLookBackSeconds() const
{
    if (const auto* parameter = apvts.getRawParameterValue (lookBackTimeParamID))
        return parameter->load();

    return 4.0f;
}

float NJamPluginProcessor::getMaxGeneratedNoteLengthSeconds() const
{
    if (const auto* parameter = apvts.getRawParameterValue (maxNoteLengthParamID))
        return parameter->load();

    return 1.0f;
}

float NJamPluginProcessor::getGeneratedTimingMultiplier() const
{
    if (const auto* parameter = apvts.getRawParameterValue (timingMultiplierParamID))
        return parameter->load();

    return 1.0f;
}


juce::AudioProcessorValueTreeState& NJamPluginProcessor::getValueTreeState()
{
    return apvts;
}

bool NJamPluginProcessor::getPromptPreviewIfNew (uint64_t& lastSeenRevision, juce::MidiBuffer& midiBuffer) const
{
    return getPreviewIfNew (lastPromptPreview, promptPreviewRevision, lastSeenRevision, midiBuffer);
}

bool NJamPluginProcessor::getOutputPreviewIfNew (uint64_t& lastSeenRevision, juce::MidiBuffer& midiBuffer) const
{
    return getPreviewIfNew (lastOutputPreview, outputPreviewRevision, lastSeenRevision, midiBuffer);
}

bool NJamPluginProcessor::getContextRollSnapshotIfNew (uint64_t& lastSeenRevision, PianoRollDisplaySnapshot& snapshot) const
{
    return getRollSnapshotIfNew (contextRollSnapshot, contextRollRevision, lastSeenRevision, snapshot);
}

bool NJamPluginProcessor::getOutputRollSnapshotIfNew (uint64_t& lastSeenRevision, PianoRollDisplaySnapshot& snapshot) const
{
    return getRollSnapshotIfNew (outputRollSnapshot, outputRollRevision, lastSeenRevision, snapshot);
}

void NJamPluginProcessor::sendChatPrompt (const juce::String& prompt)
{
    const auto trimmed = prompt.trim();
    if (trimmed.isEmpty())
        return;

    if (chatRunInProgress.exchange (true))
    {
        const juce::ScopedLock scopedLock (chatLock);
        chatTranscript << "Agent: [Still working on the previous prompt. Wait for the current model call to finish before sending another.]\n\n";
        agentStatus = "busy: waiting for model";
        ++chatRevision;
        return;
    }

    {
        const juce::ScopedLock scopedLock (chatLock);
        chatTranscript << "You: " << trimmed << "\n";
        agentStatus = "thinking: queued prompt";
        agentActivity = "-> queued prompt";
        latestToolSummary = "Queued: " + shortenForTranscript (trimmed, 140);
        ++chatRevision;
    }

    if (chatThread.joinable())
        chatThread.join();

    chatThread = std::thread ([this, trimmed]
    {
        auto statusCallback = [this] (const AgentStatusEvent& event)
        {
            const juce::ScopedLock scopedLock (chatLock);
            agentStatus = event.state + (event.detail.isNotEmpty() ? ": " + event.detail : "");
            agentActivity = appendActivityLine (agentActivity, event);
            ++chatRevision;
        };

        AgentRunResult result;
        try
        {
            result = chatAgent.run (remoteLLM, trimmed, remoteChatContextLength,
                                    remoteChatMaxResponseTokens, statusCallback);
        }
        catch (const std::exception& e)
        {
            result.finalText = "Agent error: " + juce::String (e.what());
        }
        catch (...)
        {
            result.finalText = "Agent error.";
        }

        {
            const juce::ScopedLock scopedLock (chatLock);
            for (size_t i = 0; i < result.toolCalls.size(); ++i)
            {
                const auto& call = result.toolCalls[i];
                const auto* toolResult = i < result.toolResults.size() ? &result.toolResults[i] : nullptr;
                const auto outcome = toolResult == nullptr ? juce::String ("done")
                    : (toolResult->ok ? juce::String ("ok") : juce::String ("failed"));
                const auto detail = toolResult == nullptr ? juce::String()
                    : shortenForTranscript (toolResult->content);
                const auto summary = makeToolSummaryLine (call, toolResult);
                if (summary.isNotEmpty())
                    latestToolSummary = summary;

                chatTranscript << "Agent: [Tool " << call.name << " -> " << outcome;
                if (detail.isNotEmpty())
                    chatTranscript << ": " << detail;
                chatTranscript << "]\n";
            }

            chatTranscript << "Agent: " << result.finalText << "\n\n";
            agentStatus = "ready";
            agentActivity = appendActivityLine (agentActivity, { "done", "ready" });
            ++chatRevision;
        }

        {
            std::lock_guard<std::mutex> statsLock (inferenceStatsMutex);
            lastInferenceStats = result.stats;
            haveInferenceStats = true;
        }

        chatRunInProgress.store (false);
    });
}

juce::String NJamPluginProcessor::getChatTranscript() const
{
    const juce::ScopedLock scopedLock (chatLock);
    return chatTranscript;
}

juce::String NJamPluginProcessor::getAgentStatusText() const
{
    const juce::ScopedLock scopedLock (chatLock);
    return agentStatus;
}

juce::String NJamPluginProcessor::getAgentActivityText() const
{
    const juce::ScopedLock scopedLock (chatLock);
    return agentActivity;
}

juce::String NJamPluginProcessor::getLatestToolSummaryText() const
{
    const juce::ScopedLock scopedLock (chatLock);
    return latestToolSummary;
}

void NJamPluginProcessor::clearChat()
{
    if (chatRunInProgress.load())
        return;

    chatAgent.reset();
    const juce::ScopedLock scopedLock (chatLock);
    chatTranscript.clear();
    agentActivity.clear();
    latestToolSummary.clear();
    agentStatus = "ready";
    ++chatRevision;
}

void NJamPluginProcessor::setUseRemoteModel (bool shouldUseRemote)
{
    useRemoteModel.store (shouldUseRemote);
}

bool NJamPluginProcessor::getUseRemoteModel() const
{
    return useRemoteModel.load();
}

void NJamPluginProcessor::setRemoteEndpoint (const juce::String& endpoint)
{
    remoteLLM.setEndpoint (endpoint);
}

juce::String NJamPluginProcessor::getRemoteEndpoint() const
{
    return remoteLLM.getEndpoint();
}

ToolResult NJamPluginProcessor::scheduleMidiFileForPlayback (const juce::File& midiFile)
{
    if (! midiFile.existsAsFile())
        return { false, "MIDI file not found: " + midiFile.getFullPathName() };

    juce::FileInputStream stream (midiFile);
    if (! stream.openedOk())
        return { false, "Could not open MIDI file: " + midiFile.getFullPathName() };

    juce::MidiFile loadedMidi;
    if (! loadedMidi.readFrom (stream))
        return { false, "Could not read MIDI file: " + midiFile.getFullPathName() };

    loadedMidi.convertTimestampTicksToSeconds();

    const auto playbackSampleRate = getSampleRate() > 0.0 ? getSampleRate() : 44100.0;
    juce::MidiBuffer scheduledMidi;
    int eventCount = 0;
    int lastSample = 0;

    for (int trackIndex = 0; trackIndex < loadedMidi.getNumTracks(); ++trackIndex)
    {
        const auto* track = loadedMidi.getTrack (trackIndex);
        if (track == nullptr)
            continue;

        for (int eventIndex = 0; eventIndex < track->getNumEvents(); ++eventIndex)
        {
            const auto* event = track->getEventPointer (eventIndex);
            if (event == nullptr)
                continue;

            auto message = event->message;
            if (message.isMetaEvent())
                continue;

            const int samplePosition = juce::jmax (0, juce::roundToInt (message.getTimeStamp() * playbackSampleRate));
            message.setTimeStamp (static_cast<double> (samplePosition));
            scheduledMidi.addEvent (message, samplePosition);
            lastSample = juce::jmax (lastSample, samplePosition);
            ++eventCount;
        }
    }

    if (eventCount == 0)
        return { false, "MIDI file contained no playable events: " + midiFile.getFullPathName() };

    {
        const std::lock_guard<std::mutex> lock (pendingPlaybackMidiMutex);
        pendingPlaybackMidi.addEvents (scheduledMidi, 0, -1, 0);
    }

    const auto durationSeconds = static_cast<double> (lastSample) / playbackSampleRate;
    return { true, "Queued MIDI playback: " + midiFile.getFullPathName()
                   + " (" + juce::String (eventCount) + " events, "
                   + juce::String (durationSeconds, 2) + " s)" };
}

void NJamPluginProcessor::updateMaxSamplesWithoutNotes()
{
    const auto sampleRateHz = getSampleRate();
    if (sampleRateHz <= 0.0)
        return;

    maxSamplesWithoutNotes = static_cast<unsigned long> (sampleRateHz * getWaitTimeSeconds());
    
}

void NJamPluginProcessor::postCachedModelTrainingContextLength (int contextLength)
{
    cachedModelTrainingContextLength.store (std::max (0, contextLength));
}

void NJamPluginProcessor::storePromptPreview (const juce::MidiBuffer& midiBuffer)
{
    std::lock_guard<std::mutex> lock (midiPreviewMutex);
    lastPromptPreview = midiBuffer;
    ++promptPreviewRevision;
}

void NJamPluginProcessor::storeOutputPreview (const juce::MidiBuffer& midiBuffer)
{
    std::lock_guard<std::mutex> lock (midiPreviewMutex);
    lastOutputPreview = midiBuffer;
    ++outputPreviewRevision;
}

void NJamPluginProcessor::storeContextRollSnapshot (const PianoRollDisplaySnapshot& snapshot)
{
    std::lock_guard<std::mutex> lock (midiPreviewMutex);
    contextRollSnapshot = snapshot;
    ++contextRollRevision;
}

void NJamPluginProcessor::storeOutputRollSnapshot (const PianoRollDisplaySnapshot& snapshot)
{
    std::lock_guard<std::mutex> lock (midiPreviewMutex);
    outputRollSnapshot = snapshot;
    ++outputRollRevision;
}

juce::MidiBuffer NJamPluginProcessor::makeEstimatedPromptPreview (const juce::MidiBuffer& midiBuffer) const
{
    auto njamMessages = NJamLanguage::MIDIToNJamVec (midiBuffer, getSampleRate(), 120.0);
    if (njamMessages.empty())
        return midiBuffer;

    const int maxPromptTokens = std::max (1, getEffectiveRuntimeContextLength() - getMaxResponseTokensForCurrentContext());
    int estimatedTokensUsed = 0;
    size_t startIndex = njamMessages.size();

    for (size_t i = njamMessages.size(); i-- > 0;)
    {
        const auto line = njamMessages[i].toString();
        const int estimatedTokensForLine = std::max (1, static_cast<int> ((line.size() + 2) / 3));
        if (estimatedTokensUsed + estimatedTokensForLine > maxPromptTokens && startIndex < njamMessages.size())
            break;

        estimatedTokensUsed += estimatedTokensForLine;
        startIndex = i;
    }

    if (startIndex >= njamMessages.size())
        startIndex = njamMessages.size() - 1;

    std::vector<NJamMessage> croppedMessages (njamMessages.begin() + static_cast<ptrdiff_t> (startIndex), njamMessages.end());
    return NJamLanguage::NJamVecToMIDI (croppedMessages, getSampleRate(), 120.0);
}

int NJamPluginProcessor::getAvailableModelContextLength() const
{
    const int trainedContext = cachedModelTrainingContextLength.load();
    return trainedContext > 0 ? trainedContext : std::numeric_limits<int>::max();
}

int NJamPluginProcessor::getEffectiveRuntimeContextLength() const
{
    const int requestedContext = std::max (1, getContextLength() + getMaxResponseTokens());
    return std::max (1, std::min (requestedContext, getAvailableModelContextLength()));
}

int NJamPluginProcessor::getMaxResponseTokensForCurrentContext() const
{
    return std::min (getMaxResponseTokens(), getEffectiveRuntimeContextLength() - 1);
}

int NJamPluginProcessor::getDefaultPromptNoteDurationSamples() const
{
    constexpr double bpm = 120.0;
    constexpr int targetTicksPerQuarter = 960;
    const auto sampleRateHz = getSampleRate();
    if (sampleRateHz <= 0.0)
        return 1;

    const double samplesPerTick = (60.0 / bpm) * sampleRateHz / static_cast<double> (targetTicksPerQuarter);
    return std::max (1, static_cast<int> (std::llround ((targetTicksPerQuarter / 2.0) * samplesPerTick)));
}

juce::MidiBuffer NJamPluginProcessor::sanitizeNotePairs (const juce::MidiBuffer& sourceBuffer) const
{
    return makeMidiBufferFromNotePairs (extractCompleteNotePairs (sourceBuffer, getDefaultPromptNoteDurationSamples()));
}

juce::MidiBuffer NJamPluginProcessor::normalizeBufferToSampleZero (const juce::MidiBuffer& sourceBuffer) const
{
    auto sanitizedBuffer = sanitizeNotePairs (sourceBuffer);
    if (sanitizedBuffer.getNumEvents() == 0)
        return sanitizedBuffer;

    int firstSample = -1;
    for (const auto metadata : sanitizedBuffer)
    {
        if (metadata.getMessage().isNoteOn())
        {
            firstSample = metadata.samplePosition;
            break;
        }
    }

    if (firstSample < 0)
    {
        for (const auto metadata : sanitizedBuffer)
        {
            firstSample = metadata.samplePosition;
            break;
        }
    }

    juce::MidiBuffer normalizedBuffer;
    for (const auto metadata : sanitizedBuffer)
        normalizedBuffer.addEvent (metadata.getMessage(), std::max (0, metadata.samplePosition - firstSample));

    return normalizedBuffer;
}

juce::MidiBuffer NJamPluginProcessor::transformGeneratedMidiForPlayback (const juce::MidiBuffer& sourceBuffer) const
{
    const auto notePairs = extractCompleteNotePairs (sourceBuffer, getDefaultPromptNoteDurationSamples());
    if (notePairs.empty())
        return {};

    const auto timingMultiplier = std::max (0.25f, getGeneratedTimingMultiplier());
    const auto maxNoteLengthSeconds = std::max (0.25f, getMaxGeneratedNoteLengthSeconds());
    const auto sampleRateHz = getSampleRate() > 0.0 ? getSampleRate() : 44100.0;
    const int maxNoteLengthSamples = std::max (1, static_cast<int> (std::round (sampleRateHz * maxNoteLengthSeconds)));

    std::vector<NotePair> transformedPairs;
    transformedPairs.reserve (notePairs.size());

    for (const auto& notePair : notePairs)
    {
        const int scaledNoteOnSample = std::max (0, static_cast<int> (std::round (static_cast<double> (notePair.noteOnSample) * timingMultiplier)));
        const int scaledNoteOffSample = std::max (scaledNoteOnSample + 1,
                                                  static_cast<int> (std::round (static_cast<double> (notePair.noteOffSample) * timingMultiplier)));
        const int cappedNoteOffSample = std::min (scaledNoteOffSample, scaledNoteOnSample + maxNoteLengthSamples);

        transformedPairs.push_back ({
            notePair.noteOn,
            scaledNoteOnSample,
            notePair.noteOff,
            std::max (scaledNoteOnSample + 1, cappedNoteOffSample)
        });
    }

    return makeMidiBufferFromNotePairs (transformedPairs);
}

void NJamPluginProcessor::mergeSelfListenNotes (juce::MidiBuffer& promptBuffer)
{
    const auto selfListen = getSelfListen();
    if (selfListen <= 0.0f || lastModelOutputBuffer.getNumEvents() == 0)
        return;

    const auto notePairs = extractCompleteNotePairs (lastModelOutputBuffer, getDefaultPromptNoteDurationSamples());

    std::vector<NotePair> selectedNotePairs;
    for (const auto& notePair : notePairs)
    {
        if (selfListenRandom.nextFloat() < selfListen)
        {
            promptBuffer.addEvent (notePair.noteOn, notePair.noteOnSample);
            promptBuffer.addEvent (notePair.noteOff, notePair.noteOffSample);
            selectedNotePairs.push_back (notePair);
        }
    }

}

juce::MidiBuffer NJamPluginProcessor::makeRecentContextMidi (int64_t latestSample) const
{
    juce::MidiBuffer recentBuffer;
    const int64_t earliestSample = std::max<int64_t> (0, latestSample - getLookBackSamples());

    std::lock_guard<std::mutex> guard (midiForPromptMutex);
    for (const auto metadata : midiForPrompt)
    {
        if (metadata.samplePosition >= earliestSample)
            recentBuffer.addEvent (metadata.getMessage(), metadata.samplePosition);
    }

    return recentBuffer;
}

void NJamPluginProcessor::consumePendingPlaybackMidi (juce::MidiBuffer& destination)
{
    const std::lock_guard<std::mutex> lock (pendingPlaybackMidiMutex);
    destination.swapWith (pendingPlaybackMidi);
}

int64_t NJamPluginProcessor::getLookBackSamples() const
{
    const auto sampleRateHz = getSampleRate();
    if (sampleRateHz <= 0.0)
        return 0;

    return static_cast<int64_t> (sampleRateHz * getLookBackSeconds());
}

void NJamPluginProcessor::pruneContextHistory (int64_t latestSample)
{
    const auto sampleRateHz = getSampleRate();
    if (sampleRateHz <= 0.0)
        return;

    const int64_t earliestSample = std::max<int64_t> (0, latestSample - static_cast<int64_t> (sampleRateHz * contextHistoryWindowSeconds));
    juce::MidiBuffer prunedBuffer;

    std::lock_guard<std::mutex> guard (midiForPromptMutex);
    for (const auto metadata : midiForPrompt)
    {
        if (metadata.samplePosition >= earliestSample)
            prunedBuffer.addEvent (metadata.getMessage(), metadata.samplePosition);
    }
    midiForPrompt.swapWith (prunedBuffer);
}

void NJamPluginProcessor::pruneOutputDisplayHistory (int64_t latestSample)
{
    const auto sampleRateHz = getSampleRate();
    if (sampleRateHz <= 0.0)
        return;

    const int64_t earliestSample = std::max<int64_t> (0, latestSample - static_cast<int64_t> (sampleRateHz * pianoRollWindowSeconds));
    juce::MidiBuffer prunedBuffer;
    for (const auto metadata : outputDisplayHistory)
    {
        if (metadata.samplePosition >= earliestSample)
            prunedBuffer.addEvent (metadata.getMessage(), metadata.samplePosition);
    }
    outputDisplayHistory.swapWith (prunedBuffer);
}

void NJamPluginProcessor::appendOutputDisplayEvents (const juce::MidiBuffer& sourceBuffer, int64_t blockStartSample)
{
    for (const auto metadata : sourceBuffer)
        outputDisplayHistory.addEvent (metadata.getMessage(), static_cast<int> (blockStartSample + metadata.samplePosition));
}

PianoRollDisplaySnapshot NJamPluginProcessor::makeRollSnapshot (const juce::MidiBuffer& sourceBuffer,
                                                                int64_t latestSample,
                                                                bool includeOverlay) const
{
    PianoRollDisplaySnapshot snapshot;
    snapshot.notes = makeDisplayNotes (sourceBuffer, latestSample);
    snapshot.latestSample = latestSample;
    snapshot.sampleRate = getSampleRate() > 0.0 ? getSampleRate() : 44100.0;

    if (includeOverlay && hasCapturedContextOverlay)
    {
        const int64_t fadeSamples = static_cast<int64_t> (snapshot.sampleRate * captureOverlayFadeSeconds);
        if (latestSample - lastCapturedContextTriggerSample <= fadeSamples)
        {
            snapshot.overlay.active = true;
            snapshot.overlay.startSample = lastCapturedContextStartSample;
            snapshot.overlay.endSample = lastCapturedContextEndSample;
            snapshot.overlay.triggerSample = lastCapturedContextTriggerSample;
        }
    }

    return snapshot;
}

juce::String NJamPluginProcessor::getModelContextSummary() const
{
    const int selectedContext = getContextLength();
    const int selectedResponse = getMaxResponseTokens();
    const int requestedRuntimeContext = selectedContext + selectedResponse;
    const int effectiveRuntimeContext = getEffectiveRuntimeContextLength();
    const int trainedContext = cachedModelTrainingContextLength.load();

    juce::String summary = "Prompt ctx: " + juce::String (selectedContext) + " tokens";
    if (trainedContext > 0)
        summary << " / " << juce::String (trainedContext) << " trained";
    else
        summary << " / model context unknown";

    summary << "\nMax response: " << juce::String (getMaxResponseTokensForCurrentContext()) << " tokens";
    summary << "\nRuntime ctx: " << juce::String (effectiveRuntimeContext);
    if (effectiveRuntimeContext != requestedRuntimeContext)
        summary << " (requested " << juce::String (requestedRuntimeContext) << ")";
    summary << "\nLook back: " << juce::String (getLookBackSeconds(), 2) << " s";
    return summary;
}

bool NJamPluginProcessor::getPreviewIfNew (const juce::MidiBuffer& sourceBuffer,
                                           uint64_t sourceRevision,
                                           uint64_t& lastSeenRevision,
                                           juce::MidiBuffer& destinationBuffer) const
{
    std::lock_guard<std::mutex> lock (midiPreviewMutex);
    if (sourceRevision == lastSeenRevision)
        return false;

    destinationBuffer = sourceBuffer;
    lastSeenRevision = sourceRevision;
    return true;
}

bool NJamPluginProcessor::getRollSnapshotIfNew (const PianoRollDisplaySnapshot& sourceSnapshot,
                                                uint64_t sourceRevision,
                                                uint64_t& lastSeenRevision,
                                                PianoRollDisplaySnapshot& destinationSnapshot) const
{
    std::lock_guard<std::mutex> lock (midiPreviewMutex);
    if (sourceRevision == lastSeenRevision)
        return false;

    destinationSnapshot = sourceSnapshot;
    lastSeenRevision = sourceRevision;
    return true;
}
