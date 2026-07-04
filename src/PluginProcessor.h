/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin processor.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include <thread>
#include <atomic>
#include <mutex>

#include "LLMController.h"
#include "InferenceThreadManager.h"
#include "NJamLanguage.h"

//==============================================================================
struct PianoRollDisplayNote
{
    int noteNumber = 60;
    int64_t startSample = 0;
    int64_t endSample = 1;
};

struct PianoRollDisplayOverlay
{
    bool active = false;
    int64_t startSample = 0;
    int64_t endSample = 0;
    int64_t triggerSample = 0;
};

struct PianoRollDisplaySnapshot
{
    std::vector<PianoRollDisplayNote> notes;
    PianoRollDisplayOverlay overlay;
    int64_t latestSample = 0;
    double sampleRate = 44100.0;
};

/** Main plugin processor that captures MIDI phrases, manages inference, and emits generated MIDI. */
class NJamPluginProcessor  : public juce::AudioProcessor
                            #if JucePlugin_Enable_ARA
                             , public juce::AudioProcessorARAExtension
                            #endif
{
public:
    //==============================================================================
    NJamPluginProcessor();
    ~NJamPluginProcessor() override;

    //==============================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

   #ifndef JucePlugin_PreferredChannelConfigurations
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
   #endif

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    //==============================================================================
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    //==============================================================================
    const juce::String getName() const override;

    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    //==============================================================================
    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int index, const juce::String& newName) override;

    //==============================================================================
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;
    
    /** called by the GUI when it wants to send midi messages to the processor
     * uses a mutex to protect the underlying MidiBuffer midiFromGUI
     */
    void handleKeyboardMidiMessage (const juce::MidiMessage& message);
    /** writes the current set of messages that have been accumulated
     * for the prompt and writes them to putMessagesHere, with time 
     * offsets relative to the start in samples. Uses a mutex for thread safety
     */
    void consumePromptMIDIMessages(juce::MidiBuffer& putMessagesHere);
    /**thread safe way to count how many messages we've built up for the prompt */
    int countPromptMIDIMessages();
    /** retrieve last completed inference statistics */
    ILLMController::InferenceStats getLastInferenceStats() const;
    bool hasInferenceStats() const;
    void setMidiThruEnabled (bool shouldEnable);
    bool isMidiThruEnabled() const;
    bool loadModelFromPath (const juce::String& modelPath);
    juce::String getLoadedModelFileName() const;
    juce::String getStatusText() const;
    float getWaitTimeSeconds() const;
    int getContextLength() const;
    void setContextLength (int contextLength);
    int getMaxResponseTokens() const;
    void setMaxResponseTokens (int maxTokens);
    float getSelfListen() const;
    float getLookBackSeconds() const;
    float getMaxGeneratedNoteLengthSeconds() const;
    float getGeneratedTimingMultiplier() const;
    juce::String getModelContextSummary() const;
    juce::AudioProcessorValueTreeState& getValueTreeState();
    bool getPromptPreviewIfNew (uint64_t& lastSeenRevision, juce::MidiBuffer& midiBuffer) const;
    bool getOutputPreviewIfNew (uint64_t& lastSeenRevision, juce::MidiBuffer& midiBuffer) const;
    bool getContextRollSnapshotIfNew (uint64_t& lastSeenRevision, PianoRollDisplaySnapshot& snapshot) const;
    bool getOutputRollSnapshotIfNew (uint64_t& lastSeenRevision, PianoRollDisplaySnapshot& snapshot) const;
private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    static constexpr auto modelPathParamID = "modelPath";
    static constexpr auto waitTimeParamID = "waitTimeSeconds";
    static constexpr auto midiThruParamID = "midiThruEnabled";
    static constexpr auto contextLengthParamID = "contextLength";
    static constexpr auto maxResponseTokensParamID = "maxResponseTokens";
    static constexpr auto selfListenParamID = "selfListen";
    static constexpr auto lookBackTimeParamID = "lookBackTimeSeconds";
    static constexpr auto maxNoteLengthParamID = "maxGeneratedNoteLengthSeconds";
    static constexpr auto timingMultiplierParamID = "generatedTimingMultiplier";

    void updateMaxSamplesWithoutNotes();
    void postCachedModelTrainingContextLength (int contextLength);
    void storePromptPreview (const juce::MidiBuffer& midiBuffer);
    void storeOutputPreview (const juce::MidiBuffer& midiBuffer);
    void storeContextRollSnapshot (const PianoRollDisplaySnapshot& snapshot);
    void storeOutputRollSnapshot (const PianoRollDisplaySnapshot& snapshot);
    juce::MidiBuffer makeEstimatedPromptPreview (const juce::MidiBuffer& midiBuffer) const;
    int getAvailableModelContextLength() const;
    int getEffectiveRuntimeContextLength() const;
    int getMaxResponseTokensForCurrentContext() const;
    int getDefaultPromptNoteDurationSamples() const;
    juce::MidiBuffer sanitizeNotePairs (const juce::MidiBuffer& sourceBuffer) const;
    juce::MidiBuffer normalizeBufferToSampleZero (const juce::MidiBuffer& sourceBuffer) const;
    juce::MidiBuffer transformGeneratedMidiForPlayback (const juce::MidiBuffer& sourceBuffer) const;
    void mergeSelfListenNotes (juce::MidiBuffer& promptBuffer);
    juce::MidiBuffer makeRecentContextMidi (int64_t latestSample) const;
    int64_t getLookBackSamples() const;
    void pruneContextHistory (int64_t latestSample);
    void pruneOutputDisplayHistory (int64_t latestSample);
    void appendOutputDisplayEvents (const juce::MidiBuffer& sourceBuffer, int64_t blockStartSample);
    PianoRollDisplaySnapshot makeRollSnapshot (const juce::MidiBuffer& sourceBuffer,
                                               int64_t latestSample,
                                               bool includeOverlay) const;
    bool getPreviewIfNew (const juce::MidiBuffer& sourceBuffer,
                          uint64_t sourceRevision,
                          uint64_t& lastSeenRevision,
                          juce::MidiBuffer& destinationBuffer) const;
    bool getRollSnapshotIfNew (const PianoRollDisplaySnapshot& sourceSnapshot,
                               uint64_t sourceRevision,
                               uint64_t& lastSeenRevision,
                               PianoRollDisplaySnapshot& destinationSnapshot) const;

    juce::AudioProcessorValueTreeState apvts;


    juce::MidiBuffer midiFromGUI;
    std::mutex midiFromGUIMutex;

    juce::MidiBuffer midiForPrompt;
    mutable std::mutex midiForPromptMutex;
    /** place to store midi from the llm that is played later than the current block */
    juce::MidiBuffer futureMidiFromLLM;
    juce::MidiBuffer lastModelOutputBuffer;
    juce::MidiBuffer outputDisplayHistory;
    
    /** copies messages from midiFromGUI to  putMessagesHere and clears midiFromGUI
     * counterpart to 'handleKeyboardMidiMessage'
    */
    void consumeKeyboardMidiMessages( juce::MidiBuffer& putMessagesHere);

    /** adds a message to the buffer that will ultimately be used to generate a prompt */
    void addMidiMessageToPrompt(const juce::MidiMessage& message);

    /** returns true  if the sent buffer contains midi note events on or off*/
    bool bufferContainsNoteEvents(MidiBuffer& midiBuffer);
    unsigned long promptReferenceTS{0};
    /** keeps track of samples that have passed since we started gathering messages for the prompt */
    unsigned long samplesSincePromptStart{0};
    
    unsigned long samplesWithoutNotes{0};
    unsigned long maxSamplesWithoutNotes{0};
    std::shared_ptr<LLMController> llmControllerP;
    InferenceThreadManager inferenceThread; 
    mutable std::mutex inferenceStatsMutex;
    ILLMController::InferenceStats lastInferenceStats{};
    bool haveInferenceStats{false};
    mutable std::mutex promptDebugMutex;
    int lastPromptMidiEventCount{0};
    int lastPromptNotePairCount{0};
    int lastPromptCharCount{0};
    mutable std::mutex midiPreviewMutex;
    juce::MidiBuffer lastPromptPreview;
    juce::MidiBuffer lastOutputPreview;
    PianoRollDisplaySnapshot contextRollSnapshot;
    PianoRollDisplaySnapshot outputRollSnapshot;
    uint64_t promptPreviewRevision{0};
    uint64_t outputPreviewRevision{0};
    uint64_t contextRollRevision{0};
    uint64_t outputRollRevision{0};
    int64_t lastCapturedContextStartSample{0};
    int64_t lastCapturedContextEndSample{0};
    int64_t lastCapturedContextTriggerSample{0};
    int64_t lastLiveInputNoteSample{-1};
    int64_t lastTriggeredInputNoteSample{-1};
    bool hasCapturedContextOverlay{false};
    std::atomic<bool> midiThruEnabled{true};
    std::atomic<int> cachedModelTrainingContextLength{0};
    juce::Random selfListenRandom;
    // LLMController llmController; 
    /** pass this to a thread - it creates an llmcontroller and keeps calling generate on it */
    // static void infiniteInfer(std::atomic<bool>& keepRunning);
    // std::atomic<bool> keepRunningLLM{true};
    // std::thread llmThread; 
    //==============================================================================
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NJamPluginProcessor)
};
