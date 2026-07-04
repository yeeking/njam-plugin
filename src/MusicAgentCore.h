#pragma once

#include <JuceHeader.h>
#include "ILLMController.h"
#include "MusicTools.h"

struct ChatMessage
{
    juce::String role;
    juce::String content;
};

struct AgentRunResult
{
    juce::String finalText;
    std::vector<ToolInvocation> toolCalls;
    std::vector<ToolResult> toolResults;
    ILLMController::InferenceStats stats{};
};

class MusicAgentCore
{
public:
    MusicAgentCore();

    AgentRunResult run (ILLMController& llm,
                        const juce::String& userPrompt,
                        int contextLength,
                        int maxTokens,
                        AgentStatusCallback statusCallback = {});

    void reset();
    std::vector<ChatMessage> getMessages() const;
    void setMidiPlaybackHandler (MusicToolRegistry::MidiPlaybackHandler handler);
    juce::String buildPromptForUserMessage (const juce::String& userPrompt) const;
    juce::String buildPromptForUserMessage (const juce::String& userPrompt, int contextLength, int maxTokens) const;
    std::vector<ToolInvocation> parseToolCalls (const juce::String& modelText) const;

private:
    ToolResult callSpecialistTool (const ToolInvocation& invocation);
    juce::String stripToolTags (const juce::String& modelText) const;
    juce::String renderTranscript() const;
    juce::String renderTranscript (int maxTranscriptChars) const;
    static int estimateTranscriptBudgetChars (int contextLength, int maxTokens);
    juce::String runSpecialist (const juce::String& name, const juce::String& userMusicInterest) const;

    mutable juce::CriticalSection lock;
    std::vector<ChatMessage> messages;
    MusicToolRegistry tools;
};
