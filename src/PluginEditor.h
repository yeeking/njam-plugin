/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin editor.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

class ChatTranscriptComponent;

/** Editor component for the agent chat surface. */
class NJamPluginEditor  : public juce::AudioProcessorEditor,
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
    void updateStatsLabel();

    juce::TextButton sendChatButton;
    juce::TextButton clearChatButton;
    juce::ToggleButton remoteModelToggle;
    juce::Viewport chatTranscriptViewport;
    std::unique_ptr<ChatTranscriptComponent> chatTranscriptComponent;
    juce::TextEditor chatPromptEditor;
    juce::TextEditor remoteEndpointEditor;
    juce::TextEditor activityEditor;
    juce::TextEditor toolSummaryEditor;
    juce::Label chatLabel;
    juce::Label endpointLabel;
    juce::Label statusLabel;
    juce::Label statsLabel;

    // This reference is provided as a quick way for your editor to
    // access the processor object that created it.
    NJamPluginProcessor& audioProcessor;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NJamPluginEditor)
};
