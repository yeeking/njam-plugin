/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin editor.

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <algorithm>
#include <iomanip>
#include <sstream>

class ChatTranscriptComponent final : public juce::Component
{
public:
    void setTranscript (const juce::String& newTranscript, int viewportWidth)
    {
        if (newTranscript == transcript && viewportWidth == lastLayoutWidth)
            return;

        transcript = newTranscript;
        lastLayoutWidth = viewportWidth;
        rebuildLayout (juce::jmax (160, viewportWidth));
        repaint();
    }

    int getPreferredHeight() const
    {
        return preferredHeight;
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colours::black.withAlpha (0.55f));

        for (const auto& block : blocks)
        {
            const auto bubble = block.bounds.toFloat();
            g.setColour (block.role == "You"
                             ? juce::Colour (0xff20343b)
                             : juce::Colour (0xff1c2528));
            g.fillRoundedRectangle (bubble, 7.0f);

            g.setColour (block.role == "You"
                             ? juce::Colour (0xff92d8ff)
                             : juce::Colour (0xffa7f3c4));
            g.drawRoundedRectangle (bubble, 7.0f, 1.0f);

            block.layout.draw (g, block.textArea.toFloat());
        }
    }

private:
    struct MessageBlock
    {
        juce::String role;
        juce::String content;
        juce::TextLayout layout;
        juce::Rectangle<int> bounds;
        juce::Rectangle<int> textArea;
    };

    static bool isRoleLine (const juce::String& line, const juce::String& role)
    {
        return line.startsWith (role + ":");
    }

    std::vector<MessageBlock> parseTranscript() const
    {
        std::vector<MessageBlock> parsed;
        juce::StringArray lines;
        lines.addLines (transcript);

        MessageBlock* current = nullptr;
        for (const auto& line : lines)
        {
            if (isRoleLine (line, "You") || isRoleLine (line, "Agent"))
            {
                MessageBlock block;
                block.role = line.upToFirstOccurrenceOf (":", false, false).trim();
                block.content = line.fromFirstOccurrenceOf (":", false, false).trimStart();
                parsed.push_back (std::move (block));
                current = &parsed.back();
                continue;
            }

            if (current != nullptr)
            {
                if (current->content.isNotEmpty())
                    current->content << "\n";
                current->content << line;
            }
        }

        return parsed;
    }

    void rebuildLayout (int viewportWidth)
    {
        blocks = parseTranscript();
        constexpr int outerPadding = 8;
        constexpr int bubblePadding = 10;
        constexpr int gap = 8;
        constexpr int minEmptyHeight = 44;

        int y = outerPadding;
        const int availableWidth = juce::jmax (120, viewportWidth - (outerPadding * 2));

        for (auto& block : blocks)
        {
            juce::AttributedString text;
            text.setWordWrap (juce::AttributedString::byWord);
            text.setJustification (juce::Justification::topLeft);

            text.append (block.role + "\n",
                         juce::FontOptions (15.0f).withStyle ("Bold"),
                         block.role == "You" ? juce::Colour (0xffb9e6ff) : juce::Colour (0xffbbf7d0));

            const auto content = block.content.trim().isEmpty()
                ? juce::String ("...")
                : block.content.trim();

            const bool looksDiagnostic = content.startsWithChar ('[');
            text.append (content,
                         juce::FontOptions (15.0f),
                         looksDiagnostic ? juce::Colour (0xffffd28a) : juce::Colours::white.withAlpha (0.94f));

            block.layout.createLayout (text, static_cast<float> (availableWidth - (bubblePadding * 2)));

            const int textHeight = juce::roundToInt (block.layout.getHeight());
            const int bubbleHeight = juce::jmax (minEmptyHeight, textHeight + (bubblePadding * 2));
            block.bounds = { outerPadding, y, availableWidth, bubbleHeight };
            block.textArea = block.bounds.reduced (bubblePadding);
            y += bubbleHeight + gap;
        }

        preferredHeight = juce::jmax (y + outerPadding, getParentHeight());
        setSize (viewportWidth, preferredHeight);
    }

    juce::String transcript;
    std::vector<MessageBlock> blocks;
    int lastLayoutWidth = 0;
    int preferredHeight = 80;
};

//==============================================================================
NJamPluginEditor::NJamPluginEditor (NJamPluginProcessor& p)
    : AudioProcessorEditor (&p),
      audioProcessor (p)
{
    chatLabel.setText ("Music Agent Chat", juce::dontSendNotification);
    chatLabel.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (chatLabel);

    chatTranscriptComponent = std::make_unique<ChatTranscriptComponent>();
    chatTranscriptViewport.setViewedComponent (chatTranscriptComponent.get(), false);
    chatTranscriptViewport.setScrollBarsShown (true, false);
    addAndMakeVisible (chatTranscriptViewport);

    chatPromptEditor.setMultiLine (true);
    chatPromptEditor.setReturnKeyStartsNewLine (true);
    chatPromptEditor.setTextToShowWhenEmpty ("Ask for a musical idea, arrangement, MIDI file, or playback step...", juce::Colours::grey);
    addAndMakeVisible (chatPromptEditor);

    sendChatButton.setButtonText ("Send");
    sendChatButton.onClick = [this]
    {
        const auto prompt = chatPromptEditor.getText();
        if (prompt.trim().isNotEmpty())
        {
            audioProcessor.sendChatPrompt (prompt);
            chatPromptEditor.clear();
        }
    };
    addAndMakeVisible (sendChatButton);

    clearChatButton.setButtonText ("Clear");
    clearChatButton.onClick = [this] { audioProcessor.clearChat(); };
    addAndMakeVisible (clearChatButton);

    remoteModelToggle.setButtonText ("Remote");
    remoteModelToggle.setToggleState (true, juce::dontSendNotification);
    remoteModelToggle.setEnabled (false);
    audioProcessor.setUseRemoteModel (true);
    addAndMakeVisible (remoteModelToggle);

    endpointLabel.setText ("Endpoint", juce::dontSendNotification);
    addAndMakeVisible (endpointLabel);

    remoteEndpointEditor.setText (audioProcessor.getRemoteEndpoint(), juce::dontSendNotification);
    remoteEndpointEditor.onReturnKey = [this] { audioProcessor.setRemoteEndpoint (remoteEndpointEditor.getText()); };
    remoteEndpointEditor.onFocusLost = [this] { audioProcessor.setRemoteEndpoint (remoteEndpointEditor.getText()); };
    addAndMakeVisible (remoteEndpointEditor);

    statusLabel.setJustificationType (juce::Justification::centredLeft);
    statusLabel.setText (audioProcessor.getStatusText(), juce::dontSendNotification);
    addAndMakeVisible (statusLabel);

    statsLabel.setJustificationType (juce::Justification::centredLeft);
    statsLabel.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.72f));
    addAndMakeVisible (statsLabel);
    updateStatsLabel();

    activityEditor.setMultiLine (true);
    activityEditor.setReadOnly (true);
    activityEditor.setScrollbarsShown (false);
    activityEditor.setCaretVisible (false);
    activityEditor.setPopupMenuEnabled (false);
    activityEditor.setColour (juce::TextEditor::backgroundColourId, juce::Colour (0xff11191b));
    activityEditor.setColour (juce::TextEditor::outlineColourId, juce::Colours::white.withAlpha (0.16f));
    activityEditor.setColour (juce::TextEditor::textColourId, juce::Colours::white.withAlpha (0.78f));
    activityEditor.setFont (juce::FontOptions (13.0f));
    addAndMakeVisible (activityEditor);

    toolSummaryEditor.setMultiLine (true);
    toolSummaryEditor.setReadOnly (true);
    toolSummaryEditor.setScrollbarsShown (false);
    toolSummaryEditor.setCaretVisible (false);
    toolSummaryEditor.setPopupMenuEnabled (false);
    toolSummaryEditor.setTextToShowWhenEmpty ("Latest MIDI/tool summary will appear here.", juce::Colours::grey);
    toolSummaryEditor.setColour (juce::TextEditor::backgroundColourId, juce::Colour (0xff142025));
    toolSummaryEditor.setColour (juce::TextEditor::outlineColourId, juce::Colours::white.withAlpha (0.18f));
    toolSummaryEditor.setColour (juce::TextEditor::textColourId, juce::Colour (0xffd8f7e4));
    toolSummaryEditor.setFont (juce::FontOptions (13.0f));
    addAndMakeVisible (toolSummaryEditor);

    setResizable (true, true);
    setResizeLimits (420, 300, 1800, 1400);
    setSize (900, 620);
    startTimerHz (20);
}

NJamPluginEditor::~NJamPluginEditor()
{
    stopTimer();
}

void NJamPluginEditor::paint (juce::Graphics& g)
{
    g.fillAll (getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId));

    auto bounds = getLocalBounds().reduced (10);

    g.setColour (juce::Colours::darkgrey.withAlpha (0.45f));
    g.fillRoundedRectangle (bounds.toFloat(), 10.0f);

    g.setColour (juce::Colours::white);
    g.setFont (15.0f);
    g.drawFittedText ("Chat", bounds.reduced (12, 10).removeFromTop (24), juce::Justification::centredLeft, 1);
}

void NJamPluginEditor::resized()
{
    auto bounds = getLocalBounds();
    const int outerMargin = bounds.getWidth() < 520 || bounds.getHeight() < 360 ? 6 : 10;
    bounds = bounds.reduced (outerMargin);

    auto chatArea = bounds.reduced (12);
    chatArea.removeFromTop (28);

    chatLabel.setBounds (chatArea.removeFromTop (26));

    const bool compactEndpoint = chatArea.getWidth() < 560;
    if (compactEndpoint)
    {
        auto endpointTopRow = chatArea.removeFromTop (28);
        remoteModelToggle.setBounds (endpointTopRow.removeFromLeft (92));
        endpointLabel.setBounds (endpointTopRow);
        remoteEndpointEditor.setBounds (chatArea.removeFromTop (30));
    }
    else
    {
        auto endpointRow = chatArea.removeFromTop (30);
        remoteModelToggle.setBounds (endpointRow.removeFromLeft (90));
        endpointLabel.setBounds (endpointRow.removeFromLeft (76));
        remoteEndpointEditor.setBounds (endpointRow);
    }

    chatArea.removeFromTop (6);

    statusLabel.setBounds (chatArea.removeFromTop (24));
    statsLabel.setBounds (chatArea.removeFromTop (24));
    const int activityHeight = juce::jlimit (42, 88, chatArea.getHeight() / 7);
    activityEditor.setBounds (chatArea.removeFromTop (activityHeight));
    chatArea.removeFromTop (6);
    const int summaryHeight = juce::jlimit (36, 62, chatArea.getHeight() / 10);
    toolSummaryEditor.setBounds (chatArea.removeFromTop (summaryHeight));
    chatArea.removeFromTop (6);

    const int promptHeight = juce::jlimit (64, 104, chatArea.getHeight() / 5);
    auto promptRow = chatArea.removeFromBottom (promptHeight);
    chatArea.removeFromBottom (8);

    chatTranscriptViewport.setBounds (chatArea);
    if (chatTranscriptComponent != nullptr)
        chatTranscriptComponent->setTranscript (audioProcessor.getChatTranscript(),
                                                chatTranscriptViewport.getMaximumVisibleWidth());

    const int buttonWidth = juce::jlimit (72, 104, promptRow.getWidth() / 5);
    auto promptButtons = promptRow.removeFromRight (buttonWidth);
    promptRow.removeFromRight (4);

    const int buttonHeight = juce::jmax (28, (promptButtons.getHeight() - 6) / 2);
    sendChatButton.setBounds (promptButtons.removeFromTop (buttonHeight).reduced (0, 1));
    promptButtons.removeFromTop (4);
    clearChatButton.setBounds (promptButtons.removeFromTop (buttonHeight).reduced (0, 1));
    chatPromptEditor.setBounds (promptRow.reduced (0, 1));
}

void NJamPluginEditor::updateStatsLabel()
{
    if (audioProcessor.hasInferenceStats())
    {
        const auto stats = audioProcessor.getLastInferenceStats();
        std::ostringstream oss;
        oss << std::fixed << std::setprecision (2)
            << "Prompt " << stats.num_tokens_in_prompt
            << " | Response " << stats.num_tokens_in_response
            << " | Prefill " << stats.prompt_tokens_per_second << " tok/s"
            << " | Inference " << stats.inference_tokens_per_second << " tok/s"
            << " | Total " << stats.total_time_taken_for_inference << " s";
        statsLabel.setText (oss.str(), juce::dontSendNotification);
        return;
    }

    statsLabel.setText ("No inference stats yet.", juce::dontSendNotification);
}

void NJamPluginEditor::timerCallback()
{
    remoteModelToggle.setToggleState (true, juce::dontSendNotification);
    if (! remoteEndpointEditor.hasKeyboardFocus (true))
        remoteEndpointEditor.setText (audioProcessor.getRemoteEndpoint(), juce::dontSendNotification);
    if (chatTranscriptComponent != nullptr)
    {
        const auto previousMax = chatTranscriptViewport.getViewHeight() - chatTranscriptViewport.getHeight();
        const bool wasNearBottom = chatTranscriptViewport.getViewPositionY() >= previousMax - 12;
        chatTranscriptComponent->setTranscript (audioProcessor.getChatTranscript(),
                                                chatTranscriptViewport.getMaximumVisibleWidth());
        if (wasNearBottom)
            chatTranscriptViewport.setViewPosition (0, juce::jmax (0, chatTranscriptComponent->getHeight() - chatTranscriptViewport.getHeight()));
    }
    statusLabel.setText (audioProcessor.getStatusText(), juce::dontSendNotification);
    activityEditor.setText (audioProcessor.getAgentActivityText(), false);
    toolSummaryEditor.setText (audioProcessor.getLatestToolSummaryText(), false);
    updateStatsLabel();
    repaint();
}
