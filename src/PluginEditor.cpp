/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin editor.

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>

namespace
{
constexpr double pianoRollWindowSeconds = 10.0;
constexpr int lowestDisplayedMidiNote = 24;
constexpr int highestDisplayedMidiNote = 108;
}

class OpenGLPianoRollComponent final : public juce::Component,
                                       private juce::OpenGLRenderer
{
public:
    explicit OpenGLPianoRollComponent (juce::Colour defaultNoteColour)
        : noteColour (defaultNoteColour)
    {
        openGLContext.setRenderer (this);
        openGLContext.setContinuousRepainting (false);
        openGLContext.attachTo (*this);
    }

    ~OpenGLPianoRollComponent() override
    {
        shutdown();
    }

    void setSnapshot (const PianoRollDisplaySnapshot& newSnapshot)
    {
        const juce::ScopedLock lock (snapshotLock);
        snapshot = newSnapshot;
        snapshotReceivedMs = juce::Time::getMillisecondCounterHiRes();
        openGLContext.triggerRepaint();
    }

    void requestRender()
    {
        openGLContext.triggerRepaint();
    }

    void shutdown()
    {
        openGLContext.setContinuousRepainting (false);
        openGLContext.detach();
    }

private:
    void newOpenGLContextCreated() override {}
    void openGLContextClosing() override {}

    void renderOpenGL() override
    {
        juce::OpenGLHelpers::clear (juce::Colours::transparentBlack);

        const auto scale = openGLContext.getRenderingScale();
        const int width = getWidth();
        const int height = getHeight();
        juce::gl::glViewport (0, 0, juce::roundToInt (width * scale), juce::roundToInt (height * scale));

        juce::gl::glMatrixMode (juce::gl::GL_PROJECTION);
        juce::gl::glLoadIdentity();
        juce::gl::glOrtho (0.0, static_cast<double> (width), static_cast<double> (height), 0.0, -1.0, 1.0);
        juce::gl::glMatrixMode (juce::gl::GL_MODELVIEW);
        juce::gl::glLoadIdentity();

        PianoRollDisplaySnapshot localSnapshot;
        double receivedMs = 0.0;
        {
            const juce::ScopedLock lock (snapshotLock);
            localSnapshot = snapshot;
            receivedMs = snapshotReceivedMs;
        }

        drawBackground (width, height);
        if (localSnapshot.sampleRate <= 0.0)
            return;

        const double elapsedSeconds = std::max (0.0, (juce::Time::getMillisecondCounterHiRes() - receivedMs) / 1000.0);
        const int64_t estimatedLatestSample = localSnapshot.latestSample
            + static_cast<int64_t> (elapsedSeconds * localSnapshot.sampleRate);
        const int64_t visibleWindowSamples = static_cast<int64_t> (localSnapshot.sampleRate * pianoRollWindowSeconds);
        const int64_t visibleStartSample = estimatedLatestSample - visibleWindowSamples;

        drawGrid (width, height);
        drawNotes (localSnapshot, visibleStartSample, visibleWindowSamples, width, height);
        drawOverlay (localSnapshot, estimatedLatestSample, visibleStartSample, visibleWindowSamples, width, height);
    }

    void drawBackground (int width, int height) const
    {
        const auto colour = juce::Colours::black;
        juce::gl::glColor4f (colour.getFloatRed(), colour.getFloatGreen(), colour.getFloatBlue(), colour.getFloatAlpha());
        juce::gl::glBegin (juce::gl::GL_QUADS);
        juce::gl::glVertex2f (0.0f, 0.0f);
        juce::gl::glVertex2f (static_cast<float> (width), 0.0f);
        juce::gl::glVertex2f (static_cast<float> (width), static_cast<float> (height));
        juce::gl::glVertex2f (0.0f, static_cast<float> (height));
        juce::gl::glEnd();
    }

    void drawGrid (int width, int height) const
    {
        const auto gridColour = juce::Colours::black;
        juce::gl::glColor4f (gridColour.getFloatRed(), gridColour.getFloatGreen(), gridColour.getFloatBlue(), gridColour.getFloatAlpha());
        juce::gl::glBegin (juce::gl::GL_LINES);
        for (int i = 0; i <= 8; ++i)
        {
            const float x = static_cast<float> (width * i) / 8.0f;
            juce::gl::glVertex2f (x, 0.0f);
            juce::gl::glVertex2f (x, static_cast<float> (height));
        }

        constexpr int noteRange = highestDisplayedMidiNote - lowestDisplayedMidiNote + 1;
        for (int i = 0; i <= noteRange; ++i)
        {
            const float y = static_cast<float> (height * i) / static_cast<float> (noteRange);
            juce::gl::glVertex2f (0.0f, y);
            juce::gl::glVertex2f (static_cast<float> (width), y);
        }
        juce::gl::glEnd();
    }

    void drawNotes (const PianoRollDisplaySnapshot& localSnapshot,
                    int64_t visibleStartSample,
                    int64_t visibleWindowSamples,
                    int width,
                    int height) const
    {
        constexpr int noteRange = highestDisplayedMidiNote - lowestDisplayedMidiNote + 1;
        juce::gl::glColor4f (noteColour.getFloatRed(), noteColour.getFloatGreen(), noteColour.getFloatBlue(), noteColour.getFloatAlpha());
        juce::gl::glBegin (juce::gl::GL_QUADS);
        for (const auto& note : localSnapshot.notes)
        {
            const int64_t clippedStart = std::max<int64_t> (note.startSample, visibleStartSample);
            const int64_t clippedEnd = std::min<int64_t> (note.endSample, visibleStartSample + visibleWindowSamples);
            if (clippedEnd <= clippedStart)
                continue;

            const float x1 = static_cast<float> (clippedStart - visibleStartSample) * static_cast<float> (width)
                / static_cast<float> (visibleWindowSamples);
            const float x2 = static_cast<float> (clippedEnd - visibleStartSample) * static_cast<float> (width)
                / static_cast<float> (visibleWindowSamples);

            const int clampedNote = juce::jlimit (lowestDisplayedMidiNote, highestDisplayedMidiNote, note.noteNumber);
            const float noteTop = static_cast<float> (height)
                - static_cast<float> (clampedNote - lowestDisplayedMidiNote + 1) * static_cast<float> (height) / static_cast<float> (noteRange);
            const float noteHeight = std::max (2.0f, static_cast<float> (height) / static_cast<float> (noteRange));

            juce::gl::glVertex2f (x1, noteTop);
            juce::gl::glVertex2f (std::max (x1 + 2.0f, x2), noteTop);
            juce::gl::glVertex2f (std::max (x1 + 2.0f, x2), noteTop + noteHeight);
            juce::gl::glVertex2f (x1, noteTop + noteHeight);
        }
        juce::gl::glEnd();
    }

    void drawOverlay (const PianoRollDisplaySnapshot& localSnapshot,
                      int64_t estimatedLatestSample,
                      int64_t visibleStartSample,
                      int64_t visibleWindowSamples,
                      int width,
                      int height) const
    {
        if (! localSnapshot.overlay.active || localSnapshot.sampleRate <= 0.0)
            return;

        const double ageSeconds = static_cast<double> (estimatedLatestSample - localSnapshot.overlay.triggerSample) / localSnapshot.sampleRate;
        if (ageSeconds < 0.0 || ageSeconds > 4.0)
            return;

        const float alpha = [&]()
        {
            if (ageSeconds <= 1.5)
                return 1.0f;

            const double fadeProgress = juce::jlimit (0.0, 1.0, (ageSeconds - 1.5) / (4.0 - 1.5));
            return static_cast<float> ((1.0 - fadeProgress) * (1.0 - fadeProgress));
        }();

        const int64_t overlayStart = std::max<int64_t> (localSnapshot.overlay.startSample, visibleStartSample);
        const int64_t overlayEnd = std::min<int64_t> (localSnapshot.overlay.endSample, visibleStartSample + visibleWindowSamples);
        if (overlayEnd <= overlayStart)
            return;

        const float x1 = static_cast<float> (overlayStart - visibleStartSample) * static_cast<float> (width)
            / static_cast<float> (visibleWindowSamples);
        const float x2 = static_cast<float> (overlayEnd - visibleStartSample) * static_cast<float> (width)
            / static_cast<float> (visibleWindowSamples);
        const auto borderColour = juce::Colours::gold.withAlpha (0.85f * alpha);

        juce::gl::glColor4f (borderColour.getFloatRed(), borderColour.getFloatGreen(), borderColour.getFloatBlue(), borderColour.getFloatAlpha());
        juce::gl::glLineWidth (3.0f);
        juce::gl::glBegin (juce::gl::GL_LINE_LOOP);
        juce::gl::glVertex2f (x1, 1.0f);
        juce::gl::glVertex2f (x2, 1.0f);
        juce::gl::glVertex2f (x2, static_cast<float> (height - 1));
        juce::gl::glVertex2f (x1, static_cast<float> (height - 1));
        juce::gl::glEnd();
        juce::gl::glLineWidth (1.0f);
    }

    juce::OpenGLContext openGLContext;
    juce::CriticalSection snapshotLock;
    PianoRollDisplaySnapshot snapshot;
    double snapshotReceivedMs = 0.0;
    juce::Colour noteColour;
};

//==============================================================================
NJamPluginEditor::NJamPluginEditor (NJamPluginProcessor& p)
    : AudioProcessorEditor (&p),
      keyboardComponent (keyboardState, juce::MidiKeyboardComponent::horizontalKeyboard),
      audioProcessor (p)
{
    keyboardState.addListener (this);
    keyboardComponent.setAvailableRange (24, 108);
    keyboardComponent.setWantsKeyboardFocus (false);
    addAndMakeVisible (keyboardComponent);

    contextRollComponent = std::make_unique<OpenGLPianoRollComponent> (juce::Colours::skyblue);
    outputRollComponent = std::make_unique<OpenGLPianoRollComponent> (juce::Colours::lightgreen);
    addAndMakeVisible (*contextRollComponent);
    addAndMakeVisible (*outputRollComponent);

    btn.setButtonText ("MIDI Thru");
    btn.setClickingTogglesState (true);
    btn.setToggleState (audioProcessor.isMidiThruEnabled(), juce::dontSendNotification);
    btn.setColour (juce::TextButton::buttonOnColourId, juce::Colours::limegreen);
    btn.setColour (juce::TextButton::buttonColourId, juce::Colours::darkgrey);
    btn.onClick = [this] { audioProcessor.setMidiThruEnabled (btn.getToggleState()); };
    addAndMakeVisible (btn);

    loadModelButton.setButtonText ("Load GGUF");
    loadModelButton.onClick = [this]
    {
        modelChooser = std::make_unique<juce::FileChooser> ("Select a GGUF model", juce::File{}, "*.gguf");
        modelChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                                   [this] (const juce::FileChooser& chooser)
                                   {
                                       const auto result = chooser.getResult();
                                       if (result.existsAsFile())
                                           audioProcessor.loadModelFromPath (result.getFullPathName());

                                       modelChooser.reset();
                                   });
    };
    addAndMakeVisible (loadModelButton);

    auto configureContextButton = [this] (juce::TextButton& button, const juce::String& text, int contextLength)
    {
        button.setButtonText (text);
        button.setClickingTogglesState (true);
        button.setRadioGroupId (1001);
        button.setColour (juce::TextButton::buttonColourId, juce::Colours::darkslategrey);
        button.setColour (juce::TextButton::buttonOnColourId, juce::Colours::red);
        button.onClick = [this, contextLength] { audioProcessor.setContextLength (contextLength); };
        addAndMakeVisible (button);
    };

    configureContextButton (ctx128Button, "256", 256);
    configureContextButton (ctx256Button, "512", 512);
    configureContextButton (ctx512Button, "1024", 1024);
    configureContextButton (ctx1024Button, "2048", 2048);

    auto configureTokenButton = [this] (juce::TextButton& button, const juce::String& text, int maxTokens)
    {
        button.setButtonText (text);
        button.setClickingTogglesState (true);
        button.setRadioGroupId (1002);
        button.setColour (juce::TextButton::buttonColourId, juce::Colours::darkolivegreen);
        button.setColour (juce::TextButton::buttonOnColourId, juce::Colours::orange);
        button.onClick = [this, maxTokens] { audioProcessor.setMaxResponseTokens (maxTokens); };
        addAndMakeVisible (button);
    };

    configureTokenButton (tokens32Button, "64", 64);
    configureTokenButton (tokens64Button, "128", 128);
    configureTokenButton (tokens128Button, "256", 256);
    configureTokenButton (tokens256Button, "512", 512);

    waitTimeLabel.setText ("Wait Time (s)", juce::dontSendNotification);
    addAndMakeVisible (waitTimeLabel);

    contextLengthLabel.setText ("Memory", juce::dontSendNotification);
    addAndMakeVisible (contextLengthLabel);

    maxTokensLabel.setText ("Gen Length", juce::dontSendNotification);
    addAndMakeVisible (maxTokensLabel);

    selfListenLabel.setText ("Self Listen", juce::dontSendNotification);
    addAndMakeVisible (selfListenLabel);

    lookBackTimeLabel.setText ("Look Back", juce::dontSendNotification);
    addAndMakeVisible (lookBackTimeLabel);

    maxNoteLengthLabel.setText ("Max Note Len", juce::dontSendNotification);
    addAndMakeVisible (maxNoteLengthLabel);

    timingMultiplierLabel.setText ("Timing Mult", juce::dontSendNotification);
    addAndMakeVisible (timingMultiplierLabel);

    waitTimeSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    waitTimeSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 60, 20);
    waitTimeSlider.setNumDecimalPlacesToDisplay (2);
    waitTimeSlider.setTextValueSuffix (" s");
    addAndMakeVisible (waitTimeSlider);

    selfListenSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    selfListenSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 60, 20);
    selfListenSlider.setNumDecimalPlacesToDisplay (2);
    addAndMakeVisible (selfListenSlider);

    lookBackTimeSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    lookBackTimeSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 60, 20);
    lookBackTimeSlider.setNumDecimalPlacesToDisplay (2);
    lookBackTimeSlider.setTextValueSuffix (" s");
    addAndMakeVisible (lookBackTimeSlider);

    maxNoteLengthSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    maxNoteLengthSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 60, 20);
    maxNoteLengthSlider.setNumDecimalPlacesToDisplay (2);
    maxNoteLengthSlider.setTextValueSuffix (" s");
    addAndMakeVisible (maxNoteLengthSlider);

    timingMultiplierSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    timingMultiplierSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 60, 20);
    timingMultiplierSlider.setNumDecimalPlacesToDisplay (2);
    addAndMakeVisible (timingMultiplierSlider);

    waitTimeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        audioProcessor.getValueTreeState(), "waitTimeSeconds", waitTimeSlider);
    selfListenAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        audioProcessor.getValueTreeState(), "selfListen", selfListenSlider);
    lookBackTimeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        audioProcessor.getValueTreeState(), "lookBackTimeSeconds", lookBackTimeSlider);
    maxNoteLengthAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        audioProcessor.getValueTreeState(), "maxGeneratedNoteLengthSeconds", maxNoteLengthSlider);
    timingMultiplierAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        audioProcessor.getValueTreeState(), "generatedTimingMultiplier", timingMultiplierSlider);

    statusLabel.setJustificationType (juce::Justification::centredLeft);
    statusLabel.setText (audioProcessor.getStatusText(), juce::dontSendNotification);
    addAndMakeVisible (statusLabel);

    setSize (820, 640);
    startTimerHz (20);
}

NJamPluginEditor::~NJamPluginEditor()
{
    stopTimer();
    modelChooser.reset();
    if (contextRollComponent != nullptr)
        contextRollComponent->shutdown();
    if (outputRollComponent != nullptr)
        outputRollComponent->shutdown();
    contextRollComponent.reset();
    outputRollComponent.reset();
    keyboardState.removeListener (this);
}

void NJamPluginEditor::paint (juce::Graphics& g)
{
    g.fillAll (getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId));

    auto bounds = getLocalBounds().reduced (10);
    auto leftPanel = bounds.removeFromLeft (static_cast<int> (bounds.getWidth() * 0.42f));
    bounds.removeFromLeft (10);
    auto rightPanel = bounds;

    g.setColour (juce::Colours::darkgrey.withAlpha (0.45f));
    g.fillRoundedRectangle (leftPanel.toFloat(), 10.0f);
    g.fillRoundedRectangle (rightPanel.toFloat(), 10.0f);

    g.setColour (juce::Colours::white);
    g.setFont (15.0f);
    g.drawFittedText ("Controls", leftPanel.reduced (12, 10).removeFromTop (24), juce::Justification::centredLeft, 1);
    g.drawFittedText ("Monitor", rightPanel.reduced (12, 10).removeFromTop (24), juce::Justification::centredLeft, 1);

    auto infoArea = rightPanel.reduced (12);
    infoArea.removeFromTop (28);

    auto modelArea = infoArea.removeFromTop (38);
    g.setFont (13.0f);
    g.drawFittedText ("Model: " + audioProcessor.getLoadedModelFileName(), modelArea, juce::Justification::centredLeft, 2);

    infoArea.removeFromTop (4);
    auto statusArea = infoArea.removeFromTop (24);
    juce::ignoreUnused (statusArea);

    infoArea.removeFromTop (6);
    auto contextInfoArea = infoArea.removeFromTop (44);
    g.drawFittedText (audioProcessor.getModelContextSummary(), contextInfoArea, juce::Justification::centredLeft, 3);

    infoArea.removeFromTop (12);
    g.drawFittedText ("Live Context", infoArea.removeFromTop (20), juce::Justification::centredLeft, 1);
    infoArea.removeFromTop (120);
    infoArea.removeFromTop (10);
    g.drawFittedText ("Generated Output", infoArea.removeFromTop (20), juce::Justification::centredLeft, 1);
    infoArea.removeFromTop (120);
    infoArea.removeFromTop (12);
    auto statsArea = infoArea;

    if (audioProcessor.hasInferenceStats())
    {
        const auto stats = audioProcessor.getLastInferenceStats();
        std::ostringstream oss;
        oss << std::fixed << std::setprecision (2)
            << "Prompt tokens: " << stats.num_tokens_in_prompt << "\n"
            << "Response tokens: " << stats.num_tokens_in_response << "\n"
            << "Prefill tok/s: " << stats.prompt_tokens_per_second << "\n"
            << "Inference tok/s: " << stats.inference_tokens_per_second << "\n"
            << "Total time (s): " << stats.total_time_taken_for_inference;
        g.drawFittedText (oss.str(), statsArea, juce::Justification::topLeft, 6);
    }
    else
    {
        g.drawFittedText ("No inference stats yet.", statsArea, juce::Justification::topLeft, 1);
    }
}

void NJamPluginEditor::resized()
{
    auto bounds = getLocalBounds().reduced (10);
    auto leftPanel = bounds.removeFromLeft (static_cast<int> (bounds.getWidth() * 0.42f));
    bounds.removeFromLeft (10);
    auto rightPanel = bounds;

    auto controlsArea = leftPanel.reduced (12);
    controlsArea.removeFromTop (28);

    auto keyboardArea = controlsArea.removeFromTop (92);
    keyboardComponent.setBounds (keyboardArea);

    controlsArea.removeFromTop (10);
    auto buttonRow = controlsArea.removeFromTop (30);
    btn.setBounds (buttonRow.removeFromLeft (120));
    buttonRow.removeFromLeft (8);
    loadModelButton.setBounds (buttonRow.removeFromLeft (120));

    controlsArea.removeFromTop (14);
    auto sliderRow = controlsArea.removeFromTop (30);
    waitTimeLabel.setBounds (sliderRow.removeFromLeft (100));
    waitTimeSlider.setBounds (sliderRow);

    controlsArea.removeFromTop (8);
    auto contextRow = controlsArea.removeFromTop (30);
    contextLengthLabel.setBounds (contextRow.removeFromLeft (100));
    auto buttonWidth = contextRow.getWidth() / 4;
    ctx128Button.setBounds (contextRow.removeFromLeft (buttonWidth).reduced (1, 0));
    ctx256Button.setBounds (contextRow.removeFromLeft (buttonWidth).reduced (1, 0));
    ctx512Button.setBounds (contextRow.removeFromLeft (buttonWidth).reduced (1, 0));
    ctx1024Button.setBounds (contextRow.reduced (1, 0));

    controlsArea.removeFromTop (8);
    auto tokensRow = controlsArea.removeFromTop (30);
    maxTokensLabel.setBounds (tokensRow.removeFromLeft (100));
    auto tokenButtonWidth = tokensRow.getWidth() / 4;
    tokens32Button.setBounds (tokensRow.removeFromLeft (tokenButtonWidth).reduced (1, 0));
    tokens64Button.setBounds (tokensRow.removeFromLeft (tokenButtonWidth).reduced (1, 0));
    tokens128Button.setBounds (tokensRow.removeFromLeft (tokenButtonWidth).reduced (1, 0));
    tokens256Button.setBounds (tokensRow.reduced (1, 0));

    controlsArea.removeFromTop (8);
    auto selfListenRow = controlsArea.removeFromTop (30);
    selfListenLabel.setBounds (selfListenRow.removeFromLeft (100));
    selfListenSlider.setBounds (selfListenRow);

    controlsArea.removeFromTop (8);
    auto lookBackTimeRow = controlsArea.removeFromTop (30);
    lookBackTimeLabel.setBounds (lookBackTimeRow.removeFromLeft (100));
    lookBackTimeSlider.setBounds (lookBackTimeRow);

    controlsArea.removeFromTop (8);
    auto maxNoteLengthRow = controlsArea.removeFromTop (30);
    maxNoteLengthLabel.setBounds (maxNoteLengthRow.removeFromLeft (100));
    maxNoteLengthSlider.setBounds (maxNoteLengthRow);

    controlsArea.removeFromTop (8);
    auto timingMultiplierRow = controlsArea.removeFromTop (30);
    timingMultiplierLabel.setBounds (timingMultiplierRow.removeFromLeft (100));
    timingMultiplierSlider.setBounds (timingMultiplierRow);

    auto infoArea = rightPanel.reduced (12);
    infoArea.removeFromTop (28);
    infoArea.removeFromTop (38);
    infoArea.removeFromTop (4);
    statusLabel.setBounds (infoArea.removeFromTop (24));
    infoArea.removeFromTop (6);
    infoArea.removeFromTop (44);
    infoArea.removeFromTop (12);
    infoArea.removeFromTop (20);
    if (contextRollComponent != nullptr)
        contextRollComponent->setBounds (infoArea.removeFromTop (120));
    else
        infoArea.removeFromTop (120);
    infoArea.removeFromTop (10);
    infoArea.removeFromTop (20);
    if (outputRollComponent != nullptr)
        outputRollComponent->setBounds (infoArea.removeFromTop (120));
    else
        infoArea.removeFromTop (120);
}

void NJamPluginEditor::timerCallback()
{
    btn.setToggleState (audioProcessor.isMidiThruEnabled(), juce::dontSendNotification);
    statusLabel.setText (audioProcessor.getStatusText(), juce::dontSendNotification);
    const auto contextLength = audioProcessor.getContextLength();
    ctx128Button.setToggleState (contextLength == 256, juce::dontSendNotification);
    ctx256Button.setToggleState (contextLength == 512, juce::dontSendNotification);
    ctx512Button.setToggleState (contextLength == 1024, juce::dontSendNotification);
    ctx1024Button.setToggleState (contextLength == 2048, juce::dontSendNotification);
    const auto maxResponseTokens = audioProcessor.getMaxResponseTokens();
    tokens32Button.setToggleState (maxResponseTokens == 64, juce::dontSendNotification);
    tokens64Button.setToggleState (maxResponseTokens == 128, juce::dontSendNotification);
    tokens128Button.setToggleState (maxResponseTokens == 256, juce::dontSendNotification);
    tokens256Button.setToggleState (maxResponseTokens == 512, juce::dontSendNotification);
    refreshPianoRolls();
    if (contextRollComponent != nullptr)
        contextRollComponent->requestRender();
    if (outputRollComponent != nullptr)
        outputRollComponent->requestRender();
    repaint();
}

void NJamPluginEditor::handleNoteOn (juce::MidiKeyboardState* source,
                                     int midiChannel,
                                     int midiNoteNumber,
                                     float velocity)
{
    juce::ignoreUnused (source);
    handleKeyboardMidiMessage (juce::MidiMessage::noteOn (midiChannel, midiNoteNumber, velocity));
}

void NJamPluginEditor::handleNoteOff (juce::MidiKeyboardState* source,
                                      int midiChannel,
                                      int midiNoteNumber,
                                      float velocity)
{
    juce::ignoreUnused (source);
    handleKeyboardMidiMessage (juce::MidiMessage::noteOff (midiChannel, midiNoteNumber, velocity));
}

void NJamPluginEditor::handleKeyboardMidiMessage (const juce::MidiMessage& message)
{
    juce::ignoreUnused (audioProcessor);
    this->audioProcessor.handleKeyboardMidiMessage (message);
}

void NJamPluginEditor::refreshPianoRolls()
{
    PianoRollDisplaySnapshot snapshot;
    if (audioProcessor.getContextRollSnapshotIfNew (lastContextRollRevision, snapshot))
        if (contextRollComponent != nullptr)
            contextRollComponent->setSnapshot (snapshot);

    if (audioProcessor.getOutputRollSnapshotIfNew (lastOutputRollRevision, snapshot))
        if (outputRollComponent != nullptr)
            outputRollComponent->setSnapshot (snapshot);
}
