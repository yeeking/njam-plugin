#pragma once

#include <JuceHeader.h>
#include <functional>
#include <map>
#include <optional>
#include <vector>

struct AgentStatusEvent
{
    juce::String state;
    juce::String detail;
};

using AgentStatusCallback = std::function<void (const AgentStatusEvent&)>;

struct ToolInvocation
{
    juce::String name;
    juce::var args;
};

struct ToolResult
{
    bool ok = false;
    juce::String content;
};

class MusicToolRegistry
{
public:
    using MidiPlaybackHandler = std::function<ToolResult (const juce::File&)>;

    MusicToolRegistry();

    ToolResult call (const ToolInvocation& invocation, AgentStatusCallback statusCallback = {});
    juce::String describeTools() const;
    juce::Array<juce::String> getToolNames() const;
    void setMidiPlaybackHandler (MidiPlaybackHandler handler);

private:
    using ToolHandler = std::function<ToolResult (const juce::var&)>;

    static int noteToMidi (const juce::var& note);
    static std::vector<int> notesFromVar (const juce::var& notes);
    static std::vector<double> numbersFromVar (const juce::var& values);
    static int instrumentToProgram (const juce::var& instrument);
    static juce::String expressionFromLambdaLikeText (juce::String text);
    static juce::File generatedMidiDirectory();
    static juce::File makeOutputFile (const juce::String& prefix);
    static juce::File resolveMidiFile (juce::String path);
    static void addNote (juce::MidiMessageSequence& sequence,
                         int note,
                         double startBeat,
                         double durationBeat,
                         int velocity,
                         int channel);
    static bool writeMidiFile (const juce::MidiMessageSequence& sequence,
                               const juce::File& outputFile,
                               int tempo);
    static int tempoFromMidiFile (const juce::MidiFile& midi, int fallback);
    static double getNumberProperty (const juce::DynamicObject& object,
                                     const juce::Identifier& key,
                                     double fallback);

    ToolResult generateMidi (const juce::var& args);
    ToolResult generateChordMidi (const juce::var& args);
    ToolResult generatePolyphonicMidi (const juce::var& args);
    ToolResult generateFormulaMidi (const juce::var& args);
    ToolResult combineMidi (const juce::var& args);
    ToolResult analyzeMidi (const juce::var& args);
    ToolResult fitMidiRegister (const juce::var& args);
    ToolResult fitMidiPitchRange (const juce::var& args);
    ToolResult fitMidiDuration (const juce::var& args);
    ToolResult humanizeMidi (const juce::var& args);
    ToolResult playMidi (const juce::var& args);

    std::map<juce::String, ToolHandler> tools;
    MidiPlaybackHandler midiPlaybackHandler;
};
