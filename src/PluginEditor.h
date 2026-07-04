/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin editor.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

//==============================================================================
class OpenGLPianoRollComponent;

/** Editor component that exposes plugin controls and visualises prompts, outputs, and runtime status. */
class NJamPluginEditor  : public juce::AudioProcessorEditor,
                          private juce::MidiKeyboardState::Listener,
                          private juce::Timer
{
public:
    NJamPluginEditor (NJamPluginProcessor&);
    ~NJamPluginEditor() override;

    //==============================================================================
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void handleNoteOn (juce::MidiKeyboardState* source,
                       int midiChannel,
                       int midiNoteNumber,
                       float velocity) override;
    void handleNoteOff (juce::MidiKeyboardState* source,
                        int midiChannel,
                        int midiNoteNumber,
                        float velocity) override;
    void handleKeyboardMidiMessage (const juce::MidiMessage& message);
    void refreshPianoRolls();

    juce::MidiKeyboardState keyboardState;
    juce::MidiKeyboardComponent keyboardComponent;
    juce::TextButton btn;
    juce::TextButton loadModelButton;
    juce::TextButton ctx128Button;
    juce::TextButton ctx256Button;
    juce::TextButton ctx512Button;
    juce::TextButton ctx1024Button;
    juce::TextButton tokens32Button;
    juce::TextButton tokens64Button;
    juce::TextButton tokens128Button;
    juce::TextButton tokens256Button;
    juce::Slider waitTimeSlider;
    juce::Slider selfListenSlider;
    juce::Slider lookBackTimeSlider;
    juce::Slider maxNoteLengthSlider;
    juce::Slider timingMultiplierSlider;
    juce::Label waitTimeLabel;
    juce::Label selfListenLabel;
    juce::Label lookBackTimeLabel;
    juce::Label maxNoteLengthLabel;
    juce::Label timingMultiplierLabel;
    juce::Label contextLengthLabel;
    juce::Label maxTokensLabel;
    juce::Label statusLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> waitTimeAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> selfListenAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> lookBackTimeAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> maxNoteLengthAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> timingMultiplierAttachment;
    std::unique_ptr<juce::FileChooser> modelChooser;
    std::unique_ptr<OpenGLPianoRollComponent> contextRollComponent;
    std::unique_ptr<OpenGLPianoRollComponent> outputRollComponent;
    uint64_t lastContextRollRevision{0};
    uint64_t lastOutputRollRevision{0};

    // This reference is provided as a quick way for your editor to
    // access the processor object that created it.
    NJamPluginProcessor& audioProcessor;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NJamPluginEditor)
};
