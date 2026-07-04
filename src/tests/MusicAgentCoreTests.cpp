#include "../MusicAgentCore.h"
#include "../OpenAICompatibleLLM.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <iostream>
#include <map>
#include <set>

class ScriptedLLM final : public ILLMController
{
public:
    explicit ScriptedLLM (std::vector<std::string> outputsIn) : outputs (std::move (outputsIn)) {}

    bool readyToGenerate() override { return true; }
    bool loadModel (const std::string&) override { return true; }
    void unloadModel() override {}
    void resetContext (uint32_t ctxLenIn) override { ctxLen = ctxLenIn; }
    void prepareSampler() override {}
    std::string generate (std::string prompt, size_t maxLength) override
    {
        InferenceStats stats;
        return generateWithStats (prompt, maxLength, stats);
    }
    std::string generateWithStats (const std::string& prompt, size_t, InferenceStats& stats) override
    {
        prompts.push_back (prompt);
        stats.num_tokens_in_prompt = prompt.size() / 4;
        stats.num_tokens_in_response = 4;
        if (outputIndex >= outputs.size())
            return "done";
        return outputs[outputIndex++];
    }
    void addPromptResponse (const std::string& prompt, const std::string& response) override
    {
        history.emplace_back (prompt, response);
    }
    std::vector<std::pair<std::string, std::string>> getPromptResponseHistory() override { return history; }
    bool popPromptResponse (std::pair<std::string, std::string>&) override { return false; }
    LLMStatus getStatus() const override { return LLMStatus::WaitingForJobs; }
    std::string getStatusString() const override { return "Waiting for jobs"; }
    bool isReady() const override { return true; }
    void setThreadCount (int) override {}
    int getModelTrainingContextLength() const override { return 2048; }
    void requestStop() override {}
    void clearStopRequest() override {}
    void registerPromptSettings (const std::string&, const PromptSettings&) override {}
    PromptSettings consumePromptSettings (const std::string&) override { return {}; }

    std::vector<std::string> prompts;
    uint32_t ctxLen = 0;

private:
    std::vector<std::string> outputs;
    size_t outputIndex = 0;
    std::vector<std::pair<std::string, std::string>> history;
};

class PrematurePlaybackRecoveryLLM final : public ILLMController
{
public:
    bool readyToGenerate() override { return true; }
    bool loadModel (const std::string&) override { return true; }
    void unloadModel() override {}
    void resetContext (uint32_t) override {}
    void prepareSampler() override {}
    std::string generate (std::string prompt, size_t maxLength) override
    {
        InferenceStats stats;
        return generateWithStats (std::move (prompt), maxLength, stats);
    }
    std::string generateWithStats (const std::string& prompt, size_t, InferenceStats& stats) override
    {
        prompts.push_back (prompt);
        stats.num_tokens_in_prompt = prompt.size() / 4;
        stats.num_tokens_in_response = 4;

        if (callIndex++ == 0)
            return R"txt(<tool name="generate_formula_midi">{
                "backend": "expr",
                "steps": 4,
                "voices": [
                    {
                        "pitch_lambda": "[60, 64][i % 2]",
                        "rhythm_lambda": "0.5",
                        "pitch_mode": "midi"
                    }
                ]
            }</tool>)txt";

        if (callIndex == 2)
            return "play_midi(\"/tmp/definitely_missing_music_agent.mid\")";

        const auto midiPath = latestMidiPathFromPrompt (prompt);
        if (callIndex == 3)
            return "<tool name=\"analyze_midi\">{\"midi_file\":\"" + midiPath + "\"}</tool>";
        if (callIndex == 4)
            return "play_midi(\"" + midiPath + "\")";

        return "I analyzed the generated file before playback.";
    }
    void addPromptResponse (const std::string& prompt, const std::string& response) override
    {
        history.emplace_back (prompt, response);
    }
    std::vector<std::pair<std::string, std::string>> getPromptResponseHistory() override { return history; }
    bool popPromptResponse (std::pair<std::string, std::string>&) override { return false; }
    LLMStatus getStatus() const override { return LLMStatus::WaitingForJobs; }
    std::string getStatusString() const override { return "Waiting for jobs"; }
    bool isReady() const override { return true; }
    void setThreadCount (int) override {}
    int getModelTrainingContextLength() const override { return 2048; }
    void requestStop() override {}
    void clearStopRequest() override {}
    void registerPromptSettings (const std::string&, const PromptSettings&) override {}
    PromptSettings consumePromptSettings (const std::string&) override { return {}; }

    std::vector<std::string> prompts;

private:
    static std::string latestMidiPathFromPrompt (const std::string& prompt)
    {
        auto marker = prompt.rfind ("/generated_midis/");
        if (marker != std::string::npos)
            marker = prompt.find (".mid", marker);
        else if (const auto generated = prompt.rfind ("formula_"); generated != std::string::npos)
            marker = prompt.find (".mid", generated);
        else
            marker = prompt.rfind (".mid");
        if (marker == std::string::npos)
            return {};

        auto start = marker;
        while (start > 0
               && ! std::isspace (static_cast<unsigned char> (prompt[start - 1]))
               && prompt[start - 1] != '"'
               && prompt[start - 1] != '\''
               && prompt[start - 1] != '<'
               && prompt[start - 1] != '>')
            --start;

        return prompt.substr (start, marker + 4 - start);
    }

    int callIndex = 0;
    std::vector<std::pair<std::string, std::string>> history;
};

class PrematureRegenerationRecoveryLLM final : public ILLMController
{
public:
    bool readyToGenerate() override { return true; }
    bool loadModel (const std::string&) override { return true; }
    void unloadModel() override {}
    void resetContext (uint32_t) override {}
    void prepareSampler() override {}
    std::string generate (std::string prompt, size_t maxLength) override
    {
        InferenceStats stats;
        return generateWithStats (std::move (prompt), maxLength, stats);
    }
    std::string generateWithStats (const std::string& prompt, size_t, InferenceStats& stats) override
    {
        prompts.push_back (prompt);
        stats.num_tokens_in_prompt = prompt.size() / 4;
        stats.num_tokens_in_response = 4;

        if (callIndex++ == 0)
            return generateToolCall (60);

        if (callIndex == 2)
        {
            lastMidiPath = latestMidiPathFromPrompt (prompt);
            return generateToolCall (72);
        }

        if (lastMidiPath.empty())
            lastMidiPath = latestMidiPathFromPrompt (prompt);
        if (callIndex == 3)
            return "<tool name=\"analyze_midi\">{\"midi_file\":\"" + lastMidiPath + "\"}</tool>";
        if (callIndex == 4)
            return "play_midi(\"" + lastMidiPath + "\")";

        return "I analyzed the first generated MIDI before branching.";
    }
    void addPromptResponse (const std::string& prompt, const std::string& response) override
    {
        history.emplace_back (prompt, response);
    }
    std::vector<std::pair<std::string, std::string>> getPromptResponseHistory() override { return history; }
    bool popPromptResponse (std::pair<std::string, std::string>&) override { return false; }
    LLMStatus getStatus() const override { return LLMStatus::WaitingForJobs; }
    std::string getStatusString() const override { return "Waiting for jobs"; }
    bool isReady() const override { return true; }
    void setThreadCount (int) override {}
    int getModelTrainingContextLength() const override { return 2048; }
    void requestStop() override {}
    void clearStopRequest() override {}
    void registerPromptSettings (const std::string&, const PromptSettings&) override {}
    PromptSettings consumePromptSettings (const std::string&) override { return {}; }

    std::vector<std::string> prompts;

private:
    static std::string generateToolCall (int pitch)
    {
        return "<tool name=\"generate_formula_midi\">{\"backend\":\"expr\",\"steps\":4,\"voices\":[{\"pitch_lambda\":\""
            + std::to_string (pitch)
            + "\",\"rhythm_lambda\":\"0.5\",\"pitch_mode\":\"midi\"}]}</tool>";
    }

    static std::string latestMidiPathFromPrompt (const std::string& prompt)
    {
        auto marker = prompt.rfind ("/generated_midis/");
        if (marker != std::string::npos)
            marker = prompt.find (".mid", marker);
        else if (const auto generated = prompt.rfind ("formula_"); generated != std::string::npos)
            marker = prompt.find (".mid", generated);
        else
            marker = prompt.rfind (".mid");
        if (marker == std::string::npos)
            return {};

        auto start = marker;
        while (start > 0
               && ! std::isspace (static_cast<unsigned char> (prompt[start - 1]))
               && prompt[start - 1] != '"'
               && prompt[start - 1] != '\''
               && prompt[start - 1] != '<'
               && prompt[start - 1] != '>')
            --start;

        return prompt.substr (start, marker + 4 - start);
    }

    int callIndex = 0;
    std::string lastMidiPath;
    std::vector<std::pair<std::string, std::string>> history;
};

static void expect (bool condition, const char* message)
{
    if (! condition)
    {
        std::cerr << "FAIL: " << message << "\n";
        std::exit (1);
    }
}

static juce::File extractMidiPath (const juce::String& toolResult)
{
    const int marker = toolResult.indexOf (".mid");
    if (marker < 0)
        return {};

    int start = marker;
    while (start > 0 && ! juce::CharacterFunctions::isWhitespace (toolResult[start - 1]))
        --start;

    return juce::File (toolResult.substring (start, marker + 4));
}

static double extractScore (const juce::String& toolResult)
{
    const int marker = toolResult.indexOf ("score=");
    if (marker < 0)
        return -1.0;

    int end = marker + 6;
    while (end < toolResult.length()
           && (juce::CharacterFunctions::isDigit (toolResult[end]) || toolResult[end] == '.'))
        ++end;

    return toolResult.substring (marker + 6, end).getDoubleValue();
}

static int readTempo (const juce::File& file)
{
    juce::FileInputStream stream (file);
    juce::MidiFile midi;
    if (! stream.openedOk() || ! midi.readFrom (stream))
        return 0;

    for (int trackIndex = 0; trackIndex < midi.getNumTracks(); ++trackIndex)
        if (const auto* track = midi.getTrack (trackIndex))
            for (int eventIndex = 0; eventIndex < track->getNumEvents(); ++eventIndex)
            {
                const auto message = track->getEventPointer (eventIndex)->message;
                if (message.isTempoMetaEvent())
                    return juce::roundToInt (60.0 / message.getTempoSecondsPerQuarterNote());
            }

    return 0;
}

struct MidiAnalysis
{
    int noteOns = 0;
    int noteOffs = 0;
    int programChanges = 0;
    int minNote = 128;
    int maxNote = -1;
    int timeFormat = 0;
    double firstNoteBeat = 0.0;
    double lastNoteBeat = 0.0;
    std::set<int> pitches;
    std::set<int> channels;
    std::set<int> startTicks;
    std::set<int> velocities;
    std::vector<double> durations;
};

static MidiAnalysis analyseMidi (const juce::File& file)
{
    MidiAnalysis analysis;
    juce::FileInputStream stream (file);
    juce::MidiFile midi;
    if (! stream.openedOk() || ! midi.readFrom (stream))
        return analysis;
    analysis.timeFormat = midi.getTimeFormat();

    for (int trackIndex = 0; trackIndex < midi.getNumTracks(); ++trackIndex)
        if (const auto* track = midi.getTrack (trackIndex))
            for (int eventIndex = 0; eventIndex < track->getNumEvents(); ++eventIndex)
            {
                const auto message = track->getEventPointer (eventIndex)->message;
                if (message.isProgramChange())
                    ++analysis.programChanges;

                if (message.isNoteOn())
                {
                    ++analysis.noteOns;
                    analysis.pitches.insert (message.getNoteNumber());
                    analysis.channels.insert (message.getChannel());
                    analysis.velocities.insert (message.getVelocity());
                    analysis.startTicks.insert (juce::roundToInt (message.getTimeStamp()));
                    analysis.minNote = juce::jmin (analysis.minNote, message.getNoteNumber());
                    analysis.maxNote = juce::jmax (analysis.maxNote, message.getNoteNumber());
                    analysis.lastNoteBeat = juce::jmax (analysis.lastNoteBeat, message.getTimeStamp() / 960.0);
                    if (analysis.noteOns == 1)
                        analysis.firstNoteBeat = message.getTimeStamp() / 960.0;
                }
                if (message.isNoteOff() && ! message.isAllNotesOff())
                    ++analysis.noteOffs;
            }

    std::map<std::pair<int, int>, std::vector<double>> activeNoteStarts;
    for (int trackIndex = 0; trackIndex < midi.getNumTracks(); ++trackIndex)
        if (const auto* track = midi.getTrack (trackIndex))
        {
            for (int eventIndex = 0; eventIndex < track->getNumEvents(); ++eventIndex)
            {
                const auto message = track->getEventPointer (eventIndex)->message;
                const auto key = std::make_pair (message.getChannel(), message.getNoteNumber());
                if (message.isNoteOn())
                    activeNoteStarts[key].push_back (message.getTimeStamp());
                else if (message.isNoteOff() && ! message.isAllNotesOff())
                {
                    auto& starts = activeNoteStarts[key];
                    if (! starts.empty())
                    {
                        analysis.durations.push_back ((message.getTimeStamp() - starts.back()) / 960.0);
                        starts.pop_back();
                    }
                }
            }
        }

    return analysis;
}

int main()
{
    {
        MusicAgentCore agent;
        const auto calls = agent.parseToolCalls (
            "<tool name=\"classical_agent_tool\">{\"user_music_interest\":\"icy strings\"}</tool>");
        expect (calls.size() == 1, "tool tag parsed");
        expect (calls[0].name == "classical_agent_tool", "tool name parsed");

        const auto prompt = agent.buildPromptForUserMessage ("make a phased marimba canon", 4096, 512);
        expect (prompt.contains ("actually call the available MIDI generation tools"), "manager prompt asks for real tool calls");
        expect (prompt.contains ("generate_formula_midi"), "manager prompt advertises formula midi path");
    }

    {
        expect (OpenAICompatibleLLM::promptShouldRequireMusicToolForTesting (
                    "make nocturnal dub techno with quartal chords and two sections"),
                "mood genre chord prompt requires music tool");
        expect (! OpenAICompatibleLLM::promptShouldRequireMusicToolForTesting ("hello what is going on?"),
                "ordinary chat does not require music tool");
    }

    {
        MusicAgentCore agent;
        ScriptedLLM llm ({
            "<tool name=\"style_blueprint_agent_tool\">{\"user_music_interest\":\"nocturnal dub techno with quartal chords\"}</tool>",
            "The style blueprint suggests sparse nocturnal sections for the lambda composer."
        });

        const auto result = agent.run (llm, "make nocturnal dub techno with quartal chords", 512, 128);
        expect (result.toolCalls.size() == 1, "style blueprint tool was executed");
        expect (result.toolCalls[0].name == "style_blueprint_agent_tool", "style blueprint tool name retained");
        expect (! result.toolResults.empty() && result.toolResults[0].content.contains ("blueprint sections"),
                "style blueprint tool returns section guidance");
        expect (! result.toolResults[0].content.contains ("You are a style blueprint expert"),
                "style blueprint tool result stays concise");
    }

    {
        MusicToolRegistry tools;
        const auto args = juce::JSON::parse (R"json({
            "tempo": 132,
            "steps": 48,
            "root": 60,
            "scale": [0, 2, 3, 5, 7, 10],
            "voices": [
                {
                    "pitch_lambda": "[](int i){ return (i * 3 + floor(2 * sin(i * 0.31))) % 18; }",
                    "rhythm_lambda": "[](int i){ return (i % 5) > 2 ? 0.375 : 0.25; }",
                    "duration_lambda": "[](int i){ return (i % 4) < 1 ? 0.26 : 0.18; }",
                    "velocity_lambda": "[](int i){ return 78 + 22 * abs(sin(i * 0.5)); }",
                    "instrument": "marimba",
                    "pitch_mode": "degree"
                },
                {
                    "pitch_lambda": "[](int i){ return [72, 79, 86, 91, 98][i % 5] + floor(3 * sin(i * 0.17)); }",
                    "rhythm_lambda": "[](int i){ return (i % 7) == 0 ? 0.5 : 0.125; }",
                    "duration_lambda": "[](int i){ return 0.11 + 0.06 * ((i % 3) == 0); }",
                    "velocity_lambda": "[](int i){ return 62 + 18 * abs(cos(i * 0.37)); }",
                    "instrument": "celeste",
                    "pitch_mode": "midi"
                }
            ]
        })json");
        const auto result = tools.call ({ "generate_formula_midi", args });
        if (! result.ok)
            std::cerr << "formula tool error: " << result.content << "\n";
        expect (result.ok, "formula midi tool succeeds");
        expect (result.content.contains (".mid"), "formula midi tool returns midi path");

        const auto analysis = analyseMidi (extractMidiPath (result.content));
        expect (analysis.noteOns == 96, "formula midi contains expected note count");
        expect (analysis.noteOffs >= analysis.noteOns, "formula midi terminates notes");
        expect (analysis.pitches.size() >= 18, "formula midi contains varied pitches");
        expect (analysis.channels.size() == 2, "formula midi keeps voices on distinct channels");
        expect (analysis.maxNote - analysis.minNote >= 30, "formula midi spans multiple registers");
    }

    {
        MusicToolRegistry tools;
        const auto args = juce::JSON::parse (R"json({
            "tempo": 118,
            "steps": 32,
            "voices": [
                {
                    "pitch_lambda": "[](int i){ return [84, 88, 91, 96][i % 4] + floor(2 * std::sin(i * 0.41)); }",
                    "rhythm_lambda": "[](int i){ return (i % 3) == 0 ? 0.375 : 0.25; }",
                    "gate_lambda": "[](int i){ return (i % 5) != 2 && !((i % 11) == 7); }",
                    "duration_lambda": "[](int i){ return 0.14 + 0.04 * ((i % 4) == 0); }",
                    "velocity_lambda": "[](int i){ return clamp(62 + 28 * abs(cos(i * 0.23)), 40, 105); }",
                    "instrument": "glockenspiel",
                    "pitch_mode": "midi"
                }
            ]
        })json");
        const auto result = tools.call ({ "generate_formula_midi", args });
        if (! result.ok)
            std::cerr << "gated formula tool error: " << result.content << "\n";
        expect (result.ok, "gated formula midi tool succeeds");

        const auto midiPath = extractMidiPath (result.content);
        const auto analysis = analyseMidi (midiPath);
        expect (analysis.noteOns == 24, "gate lambda creates expected rests");
        expect (analysis.noteOffs >= analysis.noteOns, "gated formula midi terminates notes");
        expect (analysis.pitches.size() >= 7, "gated formula midi contains varied pitches");
        expect (analysis.startTicks.size() == static_cast<size_t> (analysis.noteOns), "gated formula has sparse unique starts");

        juce::DynamicObject::Ptr analyzeArgs = new juce::DynamicObject();
        analyzeArgs->setProperty ("midi_file", midiPath.getFullPathName());
        const auto midiSummary = tools.call ({ "analyze_midi", juce::var (analyzeArgs.get()) });
        expect (midiSummary.ok, "analyze_midi reads generated formula file");
        expect (midiSummary.content.contains ("notes=24"), "analyze_midi reports note count");
        expect (midiSummary.content.contains ("tempo="), "analyze_midi reports tempo");
        expect (midiSummary.content.contains ("pitch_range="), "analyze_midi reports pitch range");
        expect (midiSummary.content.contains ("density="), "analyze_midi reports density");
        expect (midiSummary.content.contains ("velocity_range="), "analyze_midi reports velocity range");
        expect (midiSummary.content.contains ("velocity_unique="), "analyze_midi reports velocity uniqueness");
        expect (midiSummary.content.contains ("gap_unique="), "analyze_midi reports rhythmic uniqueness");
        expect (midiSummary.content.contains ("score="), "analyze_midi reports quality score");
        expect (midiSummary.content.contains ("assessment="), "analyze_midi reports assessment flags");
        expect (midiSummary.content.contains ("next=play_midi"), "analyze_midi recommends playback for acceptable files");

        juce::DynamicObject::Ptr basenameAnalyzeArgs = new juce::DynamicObject();
        basenameAnalyzeArgs->setProperty ("midi_file", extractMidiPath (result.content).getFileName());
        const auto basenameSummary = tools.call ({ "analyze_midi", juce::var (basenameAnalyzeArgs.get()) });
        expect (basenameSummary.ok, "analyze_midi resolves generated MIDI basename");
        expect (basenameSummary.content.contains ("notes=24"), "basename analyze reports same note count");

        const auto high = tools.call ({ "generate_formula_midi", juce::JSON::parse (R"json({
            "tempo": 137,
            "steps": 4,
            "voices": [
                {
                    "pitch_lambda": "124 + i",
                    "rhythm_lambda": "0.5",
                    "pitch_mode": "midi"
                }
            ]
        })json") });
        expect (high.ok, "high-register test MIDI generated");
        expect (readTempo (extractMidiPath (high.content)) == 137, "generated high-register MIDI keeps requested tempo");
        juce::DynamicObject::Ptr highAnalyzeArgs = new juce::DynamicObject();
        highAnalyzeArgs->setProperty ("midi_file", extractMidiPath (high.content).getFullPathName());
        const auto highSummary = tools.call ({ "analyze_midi", juce::var (highAnalyzeArgs.get()) });
        expect (highSummary.ok, "analyze_midi reads high-register file");
        expect (highSummary.content.contains ("tempo=137"), "analyze_midi reports requested tempo");
        expect (highSummary.content.contains ("too_high"), "analyze_midi flags too-high register");
        expect (highSummary.content.contains ("clipped_top"), "analyze_midi flags clipped top register");
        expect (highSummary.content.contains ("next=fit_midi_register"), "analyze_midi recommends register fitting");
        const auto highScore = extractScore (highSummary.content);
        expect (highScore >= 0.0 && highScore < 0.7, "high/clipped file gets low analysis score");

        juce::DynamicObject::Ptr fitArgs = new juce::DynamicObject();
        fitArgs->setProperty ("midi_file", extractMidiPath (high.content).getFullPathName());
        fitArgs->setProperty ("low", 36);
        fitArgs->setProperty ("high", 84);
        const auto fitted = tools.call ({ "fit_midi_register", juce::var (fitArgs.get()) });
        expect (fitted.ok, "fit_midi_register repairs high-register MIDI");
        expect (fitted.content.contains ("target=36-84"), "fit_midi_register reports target range");
        expect (fitted.content.contains ("tempo=137"), "fit_midi_register reports preserved tempo");
        expect (readTempo (extractMidiPath (fitted.content)) == 137, "fit_midi_register preserves source tempo");

        juce::DynamicObject::Ptr fittedAnalyzeArgs = new juce::DynamicObject();
        fittedAnalyzeArgs->setProperty ("midi_file", extractMidiPath (fitted.content).getFullPathName());
        const auto fittedSummary = tools.call ({ "analyze_midi", juce::var (fittedAnalyzeArgs.get()) });
        expect (fittedSummary.ok, "analyze_midi reads fitted file");
        expect (! fittedSummary.content.contains ("too_high"), "fitted file no longer reports too_high");
        expect (! fittedSummary.content.contains ("clipped_top"), "fitted file no longer reports clipped_top");
        expect (extractScore (fittedSummary.content) > highScore, "register fitting improves analysis score");

        const auto mixedPercussion = tools.call ({ "generate_formula_midi", juce::JSON::parse (R"json({
            "steps": 4,
            "voices": [
                {
                    "pitch_lambda": "124",
                    "rhythm_lambda": "0.5",
                    "pitch_mode": "midi",
                    "channel": 1
                },
                {
                    "pitch_lambda": "clave",
                    "rhythm_lambda": "0.5",
                    "pitch_mode": "midi",
                    "channel": 10
                }
            ]
        })json") });
        expect (mixedPercussion.ok, "mixed pitched/percussion test MIDI generated");

        juce::DynamicObject::Ptr mixedFitArgs = new juce::DynamicObject();
        mixedFitArgs->setProperty ("midi_file", extractMidiPath (mixedPercussion.content).getFullPathName());
        mixedFitArgs->setProperty ("low", 36);
        mixedFitArgs->setProperty ("high", 84);
        const auto mixedFitted = tools.call ({ "fit_midi_register", juce::var (mixedFitArgs.get()) });
        expect (mixedFitted.ok, "fit_midi_register handles mixed pitched/percussion MIDI");
        const auto mixedFitAnalysis = analyseMidi (extractMidiPath (mixedFitted.content));
        expect (mixedFitAnalysis.pitches.count (75) == 1, "fit_midi_register preserves channel-10 clave note");
        expect (mixedFitAnalysis.maxNote <= 84, "fit_midi_register lowers high pitched voice");

        juce::DynamicObject::Ptr mixedRangeArgs = new juce::DynamicObject();
        mixedRangeArgs->setProperty ("midi_file", extractMidiPath (mixedFitted.content).getFullPathName());
        mixedRangeArgs->setProperty ("low", 36);
        mixedRangeArgs->setProperty ("high", 84);
        mixedRangeArgs->setProperty ("target_span", 18);
        const auto mixedRangeFit = tools.call ({ "fit_midi_pitch_range", juce::var (mixedRangeArgs.get()) });
        expect (mixedRangeFit.ok, "fit_midi_pitch_range handles mixed pitched/percussion MIDI");
        const auto mixedRangeAnalysis = analyseMidi (extractMidiPath (mixedRangeFit.content));
        expect (mixedRangeAnalysis.pitches.count (75) == 1, "fit_midi_pitch_range preserves channel-10 clave note");

        const auto longMidi = tools.call ({ "generate_formula_midi", juce::JSON::parse (R"json({
            "steps": 300,
            "voices": [
                {
                    "pitch_lambda": "[48, 52, 55, 60][i % 4]",
                    "rhythm_lambda": "1.0",
                    "pitch_mode": "midi"
                }
            ]
        })json") });
        expect (longMidi.ok, "long test MIDI generated");
        juce::DynamicObject::Ptr longAnalyzeArgs = new juce::DynamicObject();
        longAnalyzeArgs->setProperty ("midi_file", extractMidiPath (longMidi.content).getFullPathName());
        const auto longSummary = tools.call ({ "analyze_midi", juce::var (longAnalyzeArgs.get()) });
        expect (longSummary.ok, "analyze_midi reads long file");
        expect (longSummary.content.contains ("very_long"), "analyze_midi flags very-long file");
        expect (longSummary.content.contains ("next=fit_midi_duration"), "analyze_midi recommends duration fitting");

        juce::DynamicObject::Ptr durationArgs = new juce::DynamicObject();
        durationArgs->setProperty ("midi_file", extractMidiPath (longMidi.content).getFullPathName());
        durationArgs->setProperty ("target_beats", 64.0);
        durationArgs->setProperty ("mode", "trim");
        const auto durationFit = tools.call ({ "fit_midi_duration", juce::var (durationArgs.get()) });
        expect (durationFit.ok, "fit_midi_duration repairs long MIDI");
        expect (durationFit.content.contains ("target=64.00"), "fit_midi_duration reports target beats");

        juce::DynamicObject::Ptr durationAnalyzeArgs = new juce::DynamicObject();
        durationAnalyzeArgs->setProperty ("midi_file", extractMidiPath (durationFit.content).getFullPathName());
        const auto durationSummary = tools.call ({ "analyze_midi", juce::var (durationAnalyzeArgs.get()) });
        expect (durationSummary.ok, "analyze_midi reads duration-fitted file");
        expect (! durationSummary.content.contains ("very_long"), "duration-fitted file no longer reports very_long");

        const auto mechanicalMidi = tools.call ({ "generate_formula_midi", juce::JSON::parse (R"json({
            "steps": 32,
            "voices": [
                {
                    "pitch_lambda": "[60, 64, 67, 72][i % 4]",
                    "rhythm_lambda": "0.5",
                    "duration_lambda": "0.2",
                    "velocity_lambda": "80",
                    "pitch_mode": "midi"
                }
            ]
        })json") });
        expect (mechanicalMidi.ok, "mechanical test MIDI generated");
        juce::DynamicObject::Ptr mechanicalAnalyzeArgs = new juce::DynamicObject();
        mechanicalAnalyzeArgs->setProperty ("midi_file", extractMidiPath (mechanicalMidi.content).getFullPathName());
        const auto mechanicalSummary = tools.call ({ "analyze_midi", juce::var (mechanicalAnalyzeArgs.get()) });
        expect (mechanicalSummary.ok, "analyze_midi reads mechanical file");
        expect (mechanicalSummary.content.contains ("flat_velocity"), "analyze_midi flags flat velocity");
        expect (mechanicalSummary.content.contains ("clockwork_rhythm"), "analyze_midi flags clockwork rhythm");
        expect (mechanicalSummary.content.contains ("next=humanize_midi"), "analyze_midi recommends deterministic humanization");

        juce::DynamicObject::Ptr humanizeArgs = new juce::DynamicObject();
        humanizeArgs->setProperty ("midi_file", extractMidiPath (mechanicalMidi.content).getFullPathName());
        humanizeArgs->setProperty ("timing_ticks", 18.0);
        humanizeArgs->setProperty ("velocity_amount", 10);
        humanizeArgs->setProperty ("seed", 3.0);
        const auto humanized = tools.call ({ "humanize_midi", juce::var (humanizeArgs.get()) });
        if (! humanized.ok)
            std::cerr << "humanize_midi tool error: " << humanized.content << "\n";
        expect (humanized.ok, "humanize_midi repairs mechanical MIDI");
        expect (humanized.content.contains ("velocities changed"), "humanize_midi reports velocity changes");
        expect (readTempo (extractMidiPath (humanized.content)) == readTempo (extractMidiPath (mechanicalMidi.content)),
                "humanize_midi preserves source tempo");

        juce::DynamicObject::Ptr humanizedAnalyzeArgs = new juce::DynamicObject();
        humanizedAnalyzeArgs->setProperty ("midi_file", extractMidiPath (humanized.content).getFullPathName());
        const auto humanizedSummary = tools.call ({ "analyze_midi", juce::var (humanizedAnalyzeArgs.get()) });
        expect (humanizedSummary.ok, "analyze_midi reads humanized MIDI");
        expect (! humanizedSummary.content.contains ("flat_velocity"), "humanized MIDI no longer reports flat velocity");
        expect (! humanizedSummary.content.contains ("clockwork_rhythm"), "humanized MIDI no longer reports clockwork rhythm");

        const auto longChordMidi = tools.call ({ "generate_formula_midi", juce::JSON::parse (R"json({
            "steps": 300,
            "voices": [
                {
                    "pitch_lambda": "[48, 52, 55, 60][i % 4]",
                    "rhythm_lambda": "1.0",
                    "pitch_mode": "midi",
                    "chord_offsets": [0, 4, 7]
                }
            ]
        })json") });
        expect (longChordMidi.ok, "long chordal test MIDI generated");
        juce::DynamicObject::Ptr chordDurationArgs = new juce::DynamicObject();
        chordDurationArgs->setProperty ("midi_file", extractMidiPath (longChordMidi.content).getFullPathName());
        chordDurationArgs->setProperty ("target_beats", 64.0);
        chordDurationArgs->setProperty ("mode", "scale");
        const auto chordDurationFit = tools.call ({ "fit_midi_duration", juce::var (chordDurationArgs.get()) });
        expect (chordDurationFit.ok, "fit_midi_duration repairs long chordal MIDI");
        expect (chordDurationFit.content.contains ("mode=trim(auto_dense_from_scale)"),
                "fit_midi_duration auto-trims dense scale compression");

        juce::DynamicObject::Ptr chordDurationAnalyzeArgs = new juce::DynamicObject();
        chordDurationAnalyzeArgs->setProperty ("midi_file", extractMidiPath (chordDurationFit.content).getFullPathName());
        const auto chordDurationSummary = tools.call ({ "analyze_midi", juce::var (chordDurationAnalyzeArgs.get()) });
        expect (chordDurationSummary.ok, "analyze_midi reads auto-trimmed chordal file");
        expect (! chordDurationSummary.content.contains ("very_dense"), "auto-trimmed chordal file avoids very_dense");

        const auto narrowMidi = tools.call ({ "generate_formula_midi", juce::JSON::parse (R"json({
            "steps": 24,
            "voices": [
                {
                    "pitch_lambda": "[60, 64][i % 2]",
                    "rhythm_lambda": "0.5",
                    "pitch_mode": "midi"
                }
            ]
        })json") });
        expect (narrowMidi.ok, "narrow-range test MIDI generated");
        juce::DynamicObject::Ptr narrowAnalyzeArgs = new juce::DynamicObject();
        narrowAnalyzeArgs->setProperty ("midi_file", extractMidiPath (narrowMidi.content).getFullPathName());
        const auto narrowSummary = tools.call ({ "analyze_midi", juce::var (narrowAnalyzeArgs.get()) });
        expect (narrowSummary.ok, "analyze_midi reads narrow file");
        expect (narrowSummary.content.contains ("narrow_range"), "analyze_midi flags narrow-range file");
        expect (narrowSummary.content.contains ("next=fit_midi_pitch_range"), "analyze_midi recommends pitch-range fitting");

        juce::DynamicObject::Ptr rangeFitArgs = new juce::DynamicObject();
        rangeFitArgs->setProperty ("midi_file", extractMidiPath (narrowMidi.content).getFullPathName());
        rangeFitArgs->setProperty ("low", 36);
        rangeFitArgs->setProperty ("high", 96);
        rangeFitArgs->setProperty ("target_span", 18);
        const auto rangeFit = tools.call ({ "fit_midi_pitch_range", juce::var (rangeFitArgs.get()) });
        expect (rangeFit.ok, "fit_midi_pitch_range repairs narrow MIDI");
        expect (rangeFit.content.contains ("target_span=18"), "fit_midi_pitch_range reports target span");

        juce::DynamicObject::Ptr rangeFitAnalyzeArgs = new juce::DynamicObject();
        rangeFitAnalyzeArgs->setProperty ("midi_file", extractMidiPath (rangeFit.content).getFullPathName());
        const auto rangeFitSummary = tools.call ({ "analyze_midi", juce::var (rangeFitAnalyzeArgs.get()) });
        expect (rangeFitSummary.ok, "analyze_midi reads pitch-range-fitted file");
        expect (! rangeFitSummary.content.contains ("narrow_range"), "pitch-range-fitted file no longer reports narrow_range");

        const auto singlePitchMidi = tools.call ({ "generate_formula_midi", juce::JSON::parse (R"json({
            "steps": 12,
            "voices": [
                {
                    "pitch_lambda": "84",
                    "rhythm_lambda": "0.5",
                    "pitch_mode": "midi"
                }
            ]
        })json") });
        expect (singlePitchMidi.ok, "single-pitch test MIDI generated");
        juce::DynamicObject::Ptr singlePitchFitArgs = new juce::DynamicObject();
        singlePitchFitArgs->setProperty ("midi_file", extractMidiPath (singlePitchMidi.content).getFullPathName());
        singlePitchFitArgs->setProperty ("low", 36);
        singlePitchFitArgs->setProperty ("high", 84);
        singlePitchFitArgs->setProperty ("target_span", 18);
        const auto singlePitchFit = tools.call ({ "fit_midi_pitch_range", juce::var (singlePitchFitArgs.get()) });
        expect (singlePitchFit.ok, "fit_midi_pitch_range repairs repeated single pitch");

        juce::DynamicObject::Ptr singlePitchAnalyzeArgs = new juce::DynamicObject();
        singlePitchAnalyzeArgs->setProperty ("midi_file", extractMidiPath (singlePitchFit.content).getFullPathName());
        const auto singlePitchSummary = tools.call ({ "analyze_midi", juce::var (singlePitchAnalyzeArgs.get()) });
        expect (singlePitchSummary.ok, "analyze_midi reads single-pitch fitted file");
        expect (! singlePitchSummary.content.contains ("narrow_range"), "single-pitch fitted file no longer reports narrow_range");

        const auto denseMidi = tools.call ({ "generate_formula_midi", juce::JSON::parse (R"json({
            "steps": 80,
            "voices": [
                {
                    "pitch_lambda": "[60, 64, 67, 72][i % 4]",
                    "rhythm_lambda": "0.05",
                    "duration_lambda": "0.03",
                    "pitch_mode": "midi"
                }
            ]
        })json") });
        expect (denseMidi.ok, "dense test MIDI generated");
        juce::DynamicObject::Ptr denseAnalyzeArgs = new juce::DynamicObject();
        denseAnalyzeArgs->setProperty ("midi_file", extractMidiPath (denseMidi.content).getFullPathName());
        const auto denseSummary = tools.call ({ "analyze_midi", juce::var (denseAnalyzeArgs.get()) });
        expect (denseSummary.ok, "analyze_midi reads dense file");
        expect (denseSummary.content.contains ("very_dense"), "analyze_midi flags very-dense file");
        expect (denseSummary.content.contains ("regenerate_density"), "analyze_midi recommends density regeneration");
        expect (extractScore (denseSummary.content) < 0.7, "very-dense file gets repair-threshold score");
    }

    {
        MusicToolRegistry tools;
        const auto args = juce::JSON::parse (R"json({
            "steps": 64,
            "tempo": 75,
            "voices": [
                {
                    "duration_lambda": "1.0",
                    "gate_lambda": "1.0",
                    "pitch_lambda": "[48, 52, 55, 60][i % 4]",
                    "pitch_mode": "midi",
                    "rhythm_lambda": "2.0",
                    "velocity_lambda": "80"
                },
                {
                    "duration_lambda": "1.0",
                    "gate_lambda": "[1, 0, 1, 1][i % 4]",
                    "pitch_lambda": "[72, 74, 76, 79][i % 4]",
                    "pitch_mode": "midi",
                    "rhythm_lambda": "[0.75, 1.25, 0.5, 1.5][i % 4]",
                    "velocity_lambda": "70"
                },
                {
                    "duration_lambda": "1.0",
                    "gate_lambda": "[1, 0, 0, 1][i % 4]",
                    "pitch_lambda": "[84, 88, 91, 96][i % 4]",
                    "pitch_mode": "midi",
                    "rhythm_lambda": "[0.33, 0.66, 1.0, 0.33][i % 4]",
                    "velocity_lambda": "90"
                }
            ]
        })json");
        const auto result = tools.call ({ "generate_formula_midi", args });
        if (! result.ok)
            std::cerr << "live-style formula tool error: " << result.content << "\n";
        expect (result.ok, "live-style formula midi tool succeeds");

        const auto analysis = analyseMidi (extractMidiPath (result.content));
        expect (analysis.noteOns == 144, "live-style gated formula creates expected note count");
        expect (analysis.noteOffs >= analysis.noteOns, "live-style formula terminates notes");
        expect (analysis.pitches.size() >= 9, "live-style formula covers gated pitch groups");
        expect (analysis.channels.size() == 3, "live-style formula keeps three voice channels");
        expect (analysis.lastNoteBeat > 20.0, "live-style formula creates a substantial musical span");
    }

    {
        MusicAgentCore agent;
        const auto calls = agent.parseToolCalls (R"txt(
To capture the essence of Steve Reich's minimalist style, here is a three-voice marimba canon:

```python
generate_formula_midi(
    steps=128,
    tempo=120,
    voices=[
        {
            "instrument": "marimba",
            "pitch_lambda": "[60, 64, 67, 64, 60, 64, 67, 69, 67, 64, 62, 60][i % 12]",
            "rhythm_lambda": "0.25",
            "gate_lambda": "0.7"
        },
        {
            "instrument": "marimba",
            "pitch_lambda": "[60, 64, 67, 64, 60, 64, 67, 69, 67, 64, 62, 60][(i + 3) % 12]",
            "rhythm_lambda": "0.25",
            "gate_lambda": "0.7"
        },
        {
            "instrument": "marimba",
            "pitch_lambda": "[48, 55][i % 2]",
            "rhythm_lambda": "1.0",
            "gate_lambda": "1.0"
        }
    ]
)
```
)txt");
        expect (calls.size() == 1, "function-style formula call parsed");
        expect (calls[0].name == "generate_formula_midi", "function-style formula call name parsed");

        MusicToolRegistry tools;
        const auto result = tools.call (calls[0]);
        if (! result.ok)
            std::cerr << "function-style formula tool error: " << result.content << "\n";
        expect (result.ok, "function-style formula call executes");

        const auto analysis = analyseMidi (extractMidiPath (result.content));
        expect (analysis.noteOns == 384, "function-style formula call creates expected note count");
        expect (analysis.channels.size() == 3, "function-style formula call keeps three channels");
        expect (analysis.lastNoteBeat >= 120.0, "function-style formula call creates long phased span");

        const auto playCalls = agent.parseToolCalls (
            "I made it. play_midi(\"/tmp/example_music_agent.mid\")");
        expect (playCalls.size() == 1, "function-style play_midi call parsed");
        expect (playCalls[0].name == "play_midi", "function-style play_midi call name parsed");
        expect (playCalls[0].args.getDynamicObject()->getProperty ("midi_file").toString()
                    == "/tmp/example_music_agent.mid",
                "function-style play_midi positional path parsed");
    }

    {
        MusicToolRegistry tools;
        const auto args = juce::JSON::parse (R"json({
            "tempo": 104,
            "sections": [
                {
                    "name": "intro",
                    "start": 0,
                    "steps": 8,
                    "voices": [
                        {
                            "pitch_lambda": "[60, 63, 67, 70][i % 4]",
                            "rhythm_lambda": "0.5",
                            "duration_lambda": "0.22",
                            "velocity_lambda": "62",
                            "instrument": "electric_piano",
                            "pitch_mode": "midi"
                        }
                    ]
                },
                {
                    "name": "body",
                    "start": 8,
                    "steps": 8,
                    "voices": [
                        {
                            "pitch_lambda": "[72, 75, 79, 82][i % 4]",
                            "rhythm_lambda": "0.5",
                            "gate_lambda": "(i % 4) != 1",
                            "duration_lambda": "0.18",
                            "velocity_lambda": "78",
                            "instrument": "vibraphone",
                            "pitch_mode": "midi"
                        },
                        {
                            "pitch_lambda": "[48, 55][i % 2]",
                            "rhythm_lambda": "1.0",
                            "duration_lambda": "0.4",
                            "velocity_lambda": "72",
                            "instrument": "bass",
                            "pitch_mode": "midi",
                            "channel": 2
                        }
                    ]
                }
            ]
        })json");

        const auto result = tools.call ({ "generate_formula_midi", args });
        if (! result.ok)
            std::cerr << "sectioned formula tool error: " << result.content << "\n";
        expect (result.ok, "sectioned formula midi succeeds");

        const auto analysis = analyseMidi (extractMidiPath (result.content));
        expect (analysis.noteOns == 22, "sectioned formula creates expected chained note count");
        expect (analysis.channels.size() == 2, "sectioned formula keeps body voices on distinct channels");
        expect (analysis.minNote <= 48 && analysis.maxNote >= 82, "sectioned formula changes register across sections");
        expect (analysis.lastNoteBeat >= 15.0, "sectioned formula body starts after intro");
    }

    {
        MusicToolRegistry tools;
        const auto args = juce::JSON::parse (R"json({
            "backend": "expr",
            "tempo": 96,
            "sections": [
                {
                    "name": "low",
                    "start": 0,
                    "steps": 6,
                    "voices": [
                        {
                            "pitch_lambda": "60 + (section * 12) + [0, 3, 7][i % 3]",
                            "rhythm_lambda": "0.5",
                            "gate_lambda": "local_t < 2.5",
                            "duration_lambda": "0.2",
                            "pitch_mode": "midi"
                        }
                    ]
                },
                {
                    "name": "high",
                    "start": 4,
                    "steps": 6,
                    "voices": [
                        {
                            "pitch_lambda": "60 + (s * 12) + [0, 3, 7][i % 3]",
                            "rhythm_lambda": "0.5",
                            "gate_lambda": "lt < 2.5",
                            "duration_lambda": "0.2",
                            "pitch_mode": "midi"
                        }
                    ]
                }
            ]
        })json");

        const auto result = tools.call ({ "generate_formula_midi", args });
        if (! result.ok)
            std::cerr << "section variable formula tool error: " << result.content << "\n";
        expect (result.ok, "section variable formula midi succeeds");
        expect (result.content.contains ("backend=expr"), "formula result reports backend");
        expect (result.content.contains ("2 sections"), "formula result reports section count");

        const auto analysis = analyseMidi (extractMidiPath (result.content));
        expect (analysis.noteOns == 10, "section/local time gates notes as expected");
        expect (analysis.minNote == 60, "section zero keeps low register");
        expect (analysis.maxNote >= 79, "section one transposes higher");
        expect (analysis.lastNoteBeat >= 6.0, "section local variables preserve section starts");

        const auto unsupported = tools.call ({ "generate_formula_midi", juce::JSON::parse (R"json({
            "backend": "cpp_jit",
            "steps": 4,
            "voices": [
                {
                    "pitch_lambda": "60 + i",
                    "rhythm_lambda": "0.5"
                }
            ]
        })json") });
        expect (! unsupported.ok, "unsupported formula backend is rejected");
        expect (unsupported.content.contains ("Supported backend: expr"), "unsupported backend reports expr backend");

        const auto badFormula = tools.call ({ "generate_formula_midi", juce::JSON::parse (R"json({
            "backend": "expr",
            "steps": 4,
            "voices": [
                {
                    "pitch_lambda": "60 + unknown_fn(i)",
                    "rhythm_lambda": "0.5"
                }
            ]
        })json") });
        expect (! badFormula.ok, "bad formula is rejected");
        expect (badFormula.content.contains ("pitch_lambda"), "bad formula reports failed lambda name");
        expect (badFormula.content.contains ("section 0"), "bad formula reports section index");
        expect (badFormula.content.contains ("voice 0"), "bad formula reports voice index");
    }

    {
        MusicToolRegistry tools;
        const auto args = juce::JSON::parse (R"json({
            "backend": "expr",
            "tempo": 124,
            "motifs": [
                {
                    "name": "sync_bass",
                    "pitch_lambda": "[0, 0, 3, 5, 7, 5][i % 6]",
                    "rhythm_lambda": "[0.5, 0.25, 0.25, 0.5][i % 4]",
                    "gate_lambda": "(i % 7) != 5",
                    "duration_lambda": "0.18",
                    "velocity_lambda": "76 + 12 * ((i % 4) == 0)",
                    "instrument": "bass"
                },
                {
                    "name": "spark",
                    "pitch_lambda": "[14, 16, 19, 23, 26][(i + section) % 5]",
                    "rhythm_lambda": "0.25",
                    "gate_lambda": "fract((i * 3) / 8) < 0.76",
                    "duration_lambda": "0.08",
                    "velocity_lambda": "60 + 18 * abs(sin(i * 0.33))",
                    "instrument": "vibraphone"
                }
            ],
            "sections": [
                {
                    "name": "a",
                    "start": 0,
                    "steps": 12,
                    "root": 48,
                    "voices": [
                        { "motif": "sync_bass", "channel": 1 },
                        { "motif": "spark", "channel": 2, "start": 0.25 }
                    ]
                },
                {
                    "name": "b",
                    "start": 8,
                    "steps": 12,
                    "root": 53,
                    "voices": [
                        { "motif": "sync_bass", "channel": 1, "gate_lambda": "(i % 5) != 2" },
                        { "motif": "spark", "channel": 2, "start": 0.125 }
                    ]
                }
            ]
        })json");

        const auto result = tools.call ({ "generate_formula_midi", args });
        if (! result.ok)
            std::cerr << "motif formula tool error: " << result.content << "\n";
        expect (result.ok, "motif-based formula midi succeeds");
        expect (result.content.contains ("4 voices"), "motif formula reports expanded section voices");
        expect (result.content.contains ("2 sections"), "motif formula reports sections");

        const auto analysis = analyseMidi (extractMidiPath (result.content));
        expect (analysis.noteOns > 30, "motif reuse creates dense multi-section pattern");
        expect (analysis.channels.size() == 2, "motif voices retain overridden channels");
        expect (std::abs (analysis.firstNoteBeat) < 1.0e-9, "motif first section starts immediately");
        expect (analysis.lastNoteBeat >= 10.0, "motif second section starts later");

        const auto overrideResult = tools.call ({ "generate_formula_midi", juce::JSON::parse (R"json({
            "backend": "expr",
            "motifs": [
                {
                    "name": "cell",
                    "pitch_lambda": "[60, 64][i % 2]",
                    "rhythm_lambda": "0.5",
                    "duration_lambda": "0.4",
                    "velocity_lambda": "70",
                    "pitch_mode": "midi"
                }
            ],
            "voices": [
                { "motif": "cell", "channel": 1, "steps": 2 },
                {
                    "motif": "cell",
                    "channel": 2,
                    "steps": 2,
                    "start": 0.125,
                    "transpose": 12,
                    "duration_scale": 0.5,
                    "velocity_offset": 20
                }
            ]
        })json") });
        if (! overrideResult.ok)
            std::cerr << "motif override tool error: " << overrideResult.content << "\n";
        expect (overrideResult.ok, "motif numeric overrides succeed");

        const auto overrideAnalysis = analyseMidi (extractMidiPath (overrideResult.content));
        expect (overrideAnalysis.pitches.count (60) == 1 && overrideAnalysis.pitches.count (64) == 1,
                "motif override keeps base notes");
        expect (overrideAnalysis.pitches.count (72) == 1 && overrideAnalysis.pitches.count (76) == 1,
                "motif transpose override creates octave notes");
        expect (overrideAnalysis.velocities.count (70) == 1 && overrideAnalysis.velocities.count (90) == 1,
                "motif velocity_offset changes velocity");
        expect (std::find_if (overrideAnalysis.durations.begin(), overrideAnalysis.durations.end(),
                              [] (double duration) { return std::abs (duration - 0.2) < 0.01; }) != overrideAnalysis.durations.end(),
                "motif duration_scale shortens notes");

        const auto echoResult = tools.call ({ "generate_formula_midi", juce::JSON::parse (R"json({
            "backend": "expr",
            "steps": 2,
            "voices": [
                {
                    "pitch_lambda": "60",
                    "rhythm_lambda": "1.0",
                    "duration_lambda": "0.2",
                    "velocity_lambda": "90",
                    "pitch_mode": "midi",
                    "echoes": 2,
                    "echo_delay": 0.5,
                    "echo_transpose": 7,
                    "echo_velocity_decay": 10
                }
            ]
        })json") });
        if (! echoResult.ok)
            std::cerr << "echo formula tool error: " << echoResult.content << "\n";
        expect (echoResult.ok, "formula echoes succeed");

        const auto echoAnalysis = analyseMidi (extractMidiPath (echoResult.content));
        expect (echoAnalysis.noteOns == 6, "echoes duplicate each formula step");
        expect (echoAnalysis.pitches.count (60) == 1 && echoAnalysis.pitches.count (67) == 1 && echoAnalysis.pitches.count (74) == 1,
                "echo_transpose creates delayed canon pitches");
        expect (echoAnalysis.startTicks.count (0) == 1 && echoAnalysis.startTicks.count (480) == 1 && echoAnalysis.startTicks.count (960) == 1,
                "echo_delay creates delayed copies");
        expect (echoAnalysis.velocities.count (90) == 1 && echoAnalysis.velocities.count (80) == 1 && echoAnalysis.velocities.count (70) == 1,
                "echo_velocity_decay lowers copy velocities");

        const auto chordResult = tools.call ({ "generate_formula_midi", juce::JSON::parse (R"json({
            "backend": "expr",
            "steps": 2,
            "voices": [
                {
                    "pitch_lambda": "[60, 62][i % 2]",
                    "rhythm_lambda": "1.0",
                    "duration_lambda": "0.5",
                    "velocity_lambda": "80",
                    "pitch_mode": "midi",
                    "chord_offsets": [0, 4, 7]
                }
            ]
        })json") });
        if (! chordResult.ok)
            std::cerr << "chord_offsets tool error: " << chordResult.content << "\n";
        expect (chordResult.ok, "formula chord_offsets succeeds");

        const auto chordAnalysis = analyseMidi (extractMidiPath (chordResult.content));
        expect (chordAnalysis.noteOns == 6, "chord_offsets expands each formula step into chord notes");
        expect (chordAnalysis.startTicks.size() == 2, "chord_offsets keeps chord notes simultaneous per step");
        expect (chordAnalysis.pitches.count (60) == 1 && chordAnalysis.pitches.count (64) == 1 && chordAnalysis.pitches.count (67) == 1,
                "chord_offsets creates first triad");
        expect (chordAnalysis.pitches.count (62) == 1 && chordAnalysis.pitches.count (66) == 1 && chordAnalysis.pitches.count (69) == 1,
                "chord_offsets creates second transposed triad");

        const auto swingResult = tools.call ({ "generate_formula_midi", juce::JSON::parse (R"json({
            "backend": "expr",
            "steps": 4,
            "voices": [
                {
                    "pitch_lambda": "60",
                    "rhythm_lambda": "0.5",
                    "duration_lambda": "0.1",
                    "pitch_mode": "midi",
                    "swing": 0.5,
                    "swing_grid": 0.5
                }
            ]
        })json") });
        if (! swingResult.ok)
            std::cerr << "swing formula tool error: " << swingResult.content << "\n";
        expect (swingResult.ok, "formula swing succeeds");

        const auto swingAnalysis = analyseMidi (extractMidiPath (swingResult.content));
        expect (swingAnalysis.startTicks.count (0) == 1, "swing leaves first eighth on grid");
        expect (swingAnalysis.startTicks.count (720) == 1, "swing delays second eighth");
        expect (swingAnalysis.startTicks.count (960) == 1, "swing leaves third eighth on grid");
        expect (swingAnalysis.startTicks.count (1680) == 1, "swing delays fourth eighth");

        const auto euclidResult = tools.call ({ "generate_formula_midi", juce::JSON::parse (R"json({
            "backend": "expr",
            "steps": 8,
            "voices": [
                {
                    "pitch_lambda": "clave",
                    "rhythm_lambda": "0.25",
                    "gate_lambda": "euclid(3, 8, i)",
                    "duration_lambda": "0.05",
                    "pitch_mode": "midi",
                    "channel": 10
                }
            ]
        })json") });
        if (! euclidResult.ok)
            std::cerr << "euclid formula tool error: " << euclidResult.content << "\n";
        expect (euclidResult.ok, "formula euclid gate succeeds");

        const auto euclidAnalysis = analyseMidi (extractMidiPath (euclidResult.content));
        expect (euclidAnalysis.noteOns == 3, "euclid gate creates requested pulse count");
        expect (euclidAnalysis.channels.count (10) == 1, "euclid percussion keeps channel 10");
        expect (euclidAnalysis.startTicks.count (0) == 1, "euclid pulse at step 0");
        expect (euclidAnalysis.startTicks.count (720) == 1, "euclid pulse at step 3");
        expect (euclidAnalysis.startTicks.count (1440) == 1, "euclid pulse at step 6");

        const auto forgivingEuclidResult = tools.call ({ "generate_formula_midi", juce::JSON::parse (R"json({
            "backend": "expr",
            "steps": 8,
            "voices": [
                {
                    "pitch_lambda": "clave",
                    "rhythm_lambda": "0.25",
                    "gate_lambda": "euclid(3, 8, i",
                    "duration_lambda": "0.05",
                    "pitch_mode": "midi",
                    "channel": 10
                }
            ]
        })json") });
        if (! forgivingEuclidResult.ok)
            std::cerr << "forgiving euclid formula tool error: " << forgivingEuclidResult.content << "\n";
        expect (forgivingEuclidResult.ok, "formula repairs common missing euclid parenthesis");
        expect (analyseMidi (extractMidiPath (forgivingEuclidResult.content)).noteOns == 3,
                "repaired euclid expression keeps pulse count");

        const auto variationResult = tools.call ({ "generate_formula_midi", juce::JSON::parse (R"json({
            "backend": "expr",
            "steps": 16,
            "voices": [
                {
                    "pitch_lambda": "60 + pick(i, 2, 7) + floor(rand(i, 5) * 2)",
                    "rhythm_lambda": "0.25",
                    "gate_lambda": "chance(0.5, i, 4)",
                    "duration_lambda": "0.08",
                    "velocity_lambda": "70 + floor(rand(i, 9) * 20)",
                    "pitch_mode": "midi"
                }
            ]
        })json") });
        if (! variationResult.ok)
            std::cerr << "variation formula tool error: " << variationResult.content << "\n";
        expect (variationResult.ok, "formula deterministic variation succeeds");

        const auto variationAnalysis = analyseMidi (extractMidiPath (variationResult.content));
        expect (variationAnalysis.noteOns > 0 && variationAnalysis.noteOns < 16,
                "chance gate creates sparse deterministic variation");
        expect (variationAnalysis.pitches.size() > 1, "rand/pick pitch formula creates pitch variation");
        expect (variationAnalysis.velocities.size() > 1, "rand velocity formula creates velocity variation");

        const auto cycleResult = tools.call ({ "generate_formula_midi", juce::JSON::parse (R"json({
            "backend": "expr",
            "steps": 6,
            "voices": [
                {
                    "pitch_lambda": "cycle(i, 60, 64, 67)",
                    "rhythm_lambda": "cycle(i, 0.25, 0.5)",
                    "duration_lambda": "0.1",
                    "velocity_lambda": "choose(i, 70, 80, 90)",
                    "pitch_mode": "midi"
                }
            ]
        })json") });
        if (! cycleResult.ok)
            std::cerr << "cycle formula tool error: " << cycleResult.content << "\n";
        expect (cycleResult.ok, "formula cycle/choose helpers succeed");

        const auto cycleAnalysis = analyseMidi (extractMidiPath (cycleResult.content));
        expect (cycleAnalysis.noteOns == 6, "cycle helper generates requested note count");
        expect (cycleAnalysis.pitches.count (60) == 1 && cycleAnalysis.pitches.count (64) == 1 && cycleAnalysis.pitches.count (67) == 1,
                "cycle helper rotates pitch values");
        expect (cycleAnalysis.velocities.count (70) == 1 && cycleAnalysis.velocities.count (80) == 1 && cycleAnalysis.velocities.count (90) == 1,
                "choose helper rotates velocity values");

        const auto missing = tools.call ({ "generate_formula_midi", juce::JSON::parse (R"json({
            "motifs": [],
            "voices": [
                { "motif": "missing", "channel": 1 }
            ]
        })json") });
        expect (! missing.ok, "missing motif is rejected");
        expect (missing.content.contains ("Unknown formula motif"), "missing motif reports useful error");
    }

    {
        MusicToolRegistry tools;
        const auto result = tools.call ({ "generate_formula_midi", juce::JSON::parse (R"json({
            "steps": 12,
            "tempo": 100,
            "voices": [
                {
                    "pitch_lambda": "[clave, conga_low, conga_high, shaker][i % 4]",
                    "rhythm_lambda": "[0.5, 0.25, 0.75][i % 3]",
                    "duration_lambda": "0.08",
                    "gate_lambda": "(i % 5) != 3",
                    "pitch_mode": "midi",
                    "channel": 10
                }
            ]
        })json") });
        if (! result.ok)
            std::cerr << "percussion formula tool error: " << result.content << "\n";
        expect (result.ok, "percussion constants formula midi succeeds");

        const auto analysis = analyseMidi (extractMidiPath (result.content));
        expect (analysis.channels.count (10) == 1, "percussion constants keep channel 10");
        expect (analysis.pitches.count (75) == 1, "percussion constants include clave note");
        expect (analysis.pitches.count (64) == 1, "percussion constants include low conga note");
        expect (analysis.pitches.count (62) == 1, "percussion constants include high conga note");
        expect (analysis.pitches.count (70) == 1, "percussion constants include shaker note");
    }

    {
        MusicToolRegistry tools;
        const auto first = tools.call ({ "generate_formula_midi", juce::JSON::parse (R"json({
            "steps": 16,
            "tempo": 120,
            "voices": [
                {
                    "pitch_lambda": "[60, 64, 67, 72][i % 4]",
                    "rhythm_lambda": "0.5",
                    "duration_lambda": "0.2",
                    "instrument": "marimba",
                    "pitch_mode": "midi"
                }
            ]
        })json") });
        const auto second = tools.call ({ "generate_formula_midi", juce::JSON::parse (R"json({
            "steps": 8,
            "tempo": 120,
            "voices": [
                {
                    "pitch_lambda": "[48, 55][i % 2]",
                    "rhythm_lambda": "1.0",
                    "duration_lambda": "0.35",
                    "instrument": "bass",
                    "pitch_mode": "midi"
                }
            ]
        })json") });

        expect (first.ok && second.ok, "source formula files for combine succeed");
        const auto firstAnalysis = analyseMidi (extractMidiPath (first.content));
        const auto secondAnalysis = analyseMidi (extractMidiPath (second.content));

        auto makeCombineArgs = [] (const juce::String& firstPath, const juce::String& secondPath, const juce::String& mode)
        {
            juce::DynamicObject::Ptr args = new juce::DynamicObject();
            juce::Array<juce::var> files;
            files.add (firstPath);
            files.add (secondPath);
            args->setProperty ("midi_files", files);
            args->setProperty ("mode", mode);
            return juce::var (args.get());
        };

        const auto firstPath = extractMidiPath (first.content).getFullPathName();
        const auto secondPath = extractMidiPath (second.content).getFullPathName();
        const auto overlay = tools.call ({ "combine_midi", makeCombineArgs (firstPath, secondPath, "overlay") });
        if (! overlay.ok)
            std::cerr << "overlay combine error: " << overlay.content << "\n";
        expect (overlay.ok, "overlay combine succeeds");
        expect (overlay.content.contains ("overlay"), "overlay combine reports mode");
        expect (overlay.content.contains ("notes=") || overlay.content.contains (" notes,"),
                "overlay combine reports note count");
        expect (overlay.content.contains ("by_channel={"), "overlay combine reports channel summary");
        expect (overlay.content.contains ("sources=["), "overlay combine reports source lengths");

        const auto overlayAnalysis = analyseMidi (extractMidiPath (overlay.content));
        expect (overlayAnalysis.noteOns == firstAnalysis.noteOns + secondAnalysis.noteOns, "overlay combine preserves note count");
        if (overlayAnalysis.lastNoteBeat > juce::jmax (firstAnalysis.lastNoteBeat, secondAnalysis.lastNoteBeat) + 1.0)
            std::cerr << "combine beat spans/timeformats first=" << firstAnalysis.lastNoteBeat << "/" << firstAnalysis.timeFormat
                      << " second=" << secondAnalysis.lastNoteBeat << "/" << secondAnalysis.timeFormat
                      << " overlay=" << overlayAnalysis.lastNoteBeat << "/" << overlayAnalysis.timeFormat << "\n";
        expect (overlayAnalysis.lastNoteBeat <= juce::jmax (firstAnalysis.lastNoteBeat, secondAnalysis.lastNoteBeat) + 1.0,
                "overlay combine keeps files simultaneous");

        const auto sequence = tools.call ({ "combine_midi", makeCombineArgs (firstPath, secondPath, "sequence") });
        if (! sequence.ok)
            std::cerr << "sequence combine error: " << sequence.content << "\n";
        expect (sequence.ok, "sequence combine succeeds");
        expect (sequence.content.contains ("sequence"), "sequence combine reports mode");
        expect (sequence.content.contains ("by_channel={"), "sequence combine reports channel summary");

        const auto sequenceAnalysis = analyseMidi (extractMidiPath (sequence.content));
        expect (sequenceAnalysis.noteOns == firstAnalysis.noteOns + secondAnalysis.noteOns, "sequence combine preserves note count");
        expect (sequenceAnalysis.lastNoteBeat > overlayAnalysis.lastNoteBeat + 4.0, "sequence combine offsets later files");

        const auto missing = tools.call ({ "combine_midi", makeCombineArgs (firstPath, "/tmp/definitely_missing_music_agent.mid", "overlay") });
        expect (! missing.ok, "combine fails on missing input");
    }

    {
        MusicAgentCore agent;
        ScriptedLLM llm ({
            "<tool name=\"classical_agent_tool\">{\"user_music_interest\":\"icy strings\"}</tool>",
            "The classical delegate suggests icy string counterpoint."
        });

        int statusCount = 0;
        const auto result = agent.run (llm, "make icy string music", 512, 128,
                                       [&] (const AgentStatusEvent&) { ++statusCount; });

        expect (result.toolCalls.size() == 1, "agent executed one tool");
        expect (result.finalText.contains ("classical delegate"), "agent produced final answer");
        expect (llm.prompts.size() == 2, "tool call produced follow-up model request");
        expect (agent.getMessages().size() >= 4, "conversation history retained");
        expect (statusCount >= 3, "status events emitted");
    }

    {
        MusicAgentCore agent;
        ScriptedLLM llm ({
            R"txt(<tool name="generate_formula_midi">{
                "backend": "expr",
                "tempo": 123,
                "steps": 4,
                "voices": [
                    {
                        "pitch_lambda": "[60, 64][i % 2]",
                        "rhythm_lambda": "0.5",
                        "velocity_lambda": "80 + i",
                        "pitch_mode": "midi"
                    }
                ]
            }</tool>)txt",
            "<tool name=\"analyze_midi\">{\"midi_file\":\"formula_placeholder.mid\"}</tool>",
            "I checked the compact transcript."
        });

        const auto result = agent.run (llm, "make a compact transcript test", 4096, 256);
        expect (result.toolCalls.size() == 2, "compact transcript test executes generate and analyze attempt");

        juce::String transcript;
        for (const auto& message : agent.getMessages())
            transcript << "[" << message.role << "]\n" << message.content << "\n";
        expect (transcript.contains ("tool generate_formula_midi ok:"), "tool transcript stores compact generation summary");
        expect (! transcript.contains ("/generated_midis/formula_"),
                "tool transcript avoids full generated path in compact tool context");
    }

    {
        MusicAgentCore agent;
        ScriptedLLM llm ({
            "[The model returned an empty assistant message. finish_reason=stop]",
            "<tool name=\"classical_agent_tool\">{\"user_music_interest\":\"retry strings\"}</tool>",
            "Recovered after an empty model response."
        });

        int retryStatusCount = 0;
        const auto result = agent.run (llm, "make retry string music", 1024, 128,
                                       [&] (const AgentStatusEvent& event)
                                       {
                                           if (event.detail.contains ("retrying empty"))
                                               ++retryStatusCount;
                                       });

        expect (result.toolCalls.size() == 1, "empty model response is retried into a tool call");
        expect (result.finalText.contains ("Recovered"), "empty model response retry reaches final answer");
        expect (retryStatusCount == 1, "empty model response emits retry status");
        expect (llm.prompts.size() == 3, "empty model response retry uses another model call");
    }

    {
        MusicAgentCore agent;
        ScriptedLLM llm ({
            "[The model returned reasoning but no visible assistant text. finish_reason=stop]\nI should call a tool next.",
            "<tool name=\"classical_agent_tool\">{\"user_music_interest\":\"hidden reasoning retry\"}</tool>",
            "Recovered after a reasoning-only model response."
        });

        int retryStatusCount = 0;
        const auto result = agent.run (llm, "make hidden reasoning retry music", 1024, 128,
                                       [&] (const AgentStatusEvent& event)
                                       {
                                           if (event.detail.contains ("retrying empty"))
                                               ++retryStatusCount;
                                       });

        expect (result.toolCalls.size() == 1, "reasoning-only remote response is retried into a tool call");
        expect (result.finalText.contains ("Recovered"), "reasoning-only retry reaches final answer");
        expect (retryStatusCount == 1, "reasoning-only retry emits retry status");
        expect (llm.prompts.size() == 3, "reasoning-only retry uses another model call");
    }

    {
        MusicAgentCore agent;
        ScriptedLLM llm ({
            "<tool_call|>",
            "<tool name=\"classical_agent_tool\">{\"user_music_interest\":\"sentinel retry\"}</tool>",
            "Recovered after a tool-only sentinel."
        });

        int retryStatusCount = 0;
        const auto result = agent.run (llm, "make sentinel retry music", 1024, 128,
                                       [&] (const AgentStatusEvent& event)
                                       {
                                           if (event.detail.contains ("retrying tool-only"))
                                               ++retryStatusCount;
                                       });

        expect (result.toolCalls.size() == 1, "tool-only sentinel response is retried into a tool call");
        expect (result.finalText.contains ("Recovered"), "tool-only sentinel retry reaches final answer");
        expect (retryStatusCount == 1, "tool-only sentinel retry emits retry status");
        expect (llm.prompts.size() == 3, "tool-only sentinel retry uses another model call");
    }

    {
        MusicAgentCore agent;
        ScriptedLLM llm ({
            "<tool name=\"style_blueprint_agent_tool\">{}</tool>",
            "The style expert kept the real prompt context."
        });

        const auto prompt = juce::String ("make Afro-Cuban piano bass and percussion with clave");
        const auto result = agent.run (llm, prompt, 2048, 128);

        expect (result.toolCalls.size() == 1, "specialist with missing interest still executes");
        expect (result.toolResults[0].content.contains (prompt), "specialist missing interest falls back to active user prompt");
        expect (result.finalText.contains ("real prompt context"), "specialist fallback continues to final response");
    }

    {
        MusicAgentCore agent;
        ScriptedLLM llm ({
            "<tool name=\"mathematical_music_agent_tool\">{\"user_music_interest\":\"Afro-Cuban Euclidean percussion and bass\"}</tool>",
            "The math expert produced a compact formula plan."
        });

        const auto result = agent.run (llm, "make Afro-Cuban Euclidean percussion and bass", 2048, 128);
        expect (result.toolCalls.size() == 1, "mathematical specialist executes");
        expect (result.toolResults[0].content.contains ("formula plan"), "mathematical specialist returns formula plan");
        expect (result.toolResults[0].content.contains ("euclid(3,8,i)"), "mathematical specialist suggests euclidean gate");
        expect (result.toolResults[0].content.contains ("roots around 36-60"), "mathematical specialist suggests sane register roots");
        expect (result.toolResults[0].content.contains ("rand(i,seed)") && result.toolResults[0].content.contains ("chance(prob,i,seed)"),
                "mathematical specialist suggests deterministic variation helpers");
        expect (! result.toolResults[0].content.contains ("You are a mathematical music expert"),
                "mathematical specialist tool result stays concise");
    }

    {
        MusicAgentCore agent;
        ScriptedLLM llm ({
            "<tool name=\"style_blueprint_agent_tool\">{\"user_music_interest\":\"nocturnal quartal dub techno\"}</tool>",
            "<tool name=\"mathematical_music_agent_tool\">{\"user_music_interest\":\"convert the blueprint into reusable motif lambdas\"}</tool>",
            R"txt(<tool name="generate_formula_midi">{
                "backend": "expr",
                "tempo": 126,
                "motifs": [
                    {
                        "name": "bass",
                        "pitch_lambda": "[0, 0, 3, 5][i % 4]",
                        "rhythm_lambda": "[0.5, 0.25, 0.25, 1.0][i % 4]",
                        "duration_lambda": "0.2",
                        "gate_lambda": "(i % 8) != 6"
                    },
                    {
                        "name": "chime",
                        "pitch_lambda": "[14, 17, 21, 24][(i + section) % 4]",
                        "rhythm_lambda": "0.25",
                        "duration_lambda": "0.08",
                        "gate_lambda": "(i % 3) != 1"
                    }
                ],
                "sections": [
                    { "name": "intro", "start": 0, "steps": 8, "root": 48, "voices": [
                        { "motif": "bass", "channel": 1 },
                        { "motif": "chime", "channel": 2, "start": 0.25 }
                    ] },
                    { "name": "body", "start": 8, "steps": 12, "root": 53, "voices": [
                        { "motif": "bass", "channel": 1, "gate_lambda": "(i % 5) != 3" },
                        { "motif": "chime", "channel": 2 }
                    ] }
                ]
            }</tool>)txt",
            "Generated a two-section motif-based MIDI file and queued playback."
        });

        const auto result = agent.run (llm, "make nocturnal quartal dub techno with reusable mathematical motifs", 4096, 512);

        expect (result.toolCalls.size() == 3, "agent can execute a multi-round specialist-to-midi chain");
        expect (result.toolCalls[0].name == "style_blueprint_agent_tool", "multi-round chain starts with style blueprint");
        expect (result.toolCalls[1].name == "mathematical_music_agent_tool", "multi-round chain calls mathematical specialist");
        expect (result.toolCalls[2].name == "generate_formula_midi", "multi-round chain generates formula MIDI");
        expect (result.toolResults.size() == 3 && result.toolResults[2].ok, "multi-round formula MIDI tool succeeds");
        expect (result.toolResults[2].content.contains ("2 sections"), "multi-round formula MIDI reports sections");
        expect (result.finalText.contains ("two-section"), "multi-round chain ends with final answer");
        expect (llm.prompts.size() == 4, "multi-round chain keeps calling model until final text");
    }

    {
        MusicAgentCore agent;
        std::vector<std::string> outputs;
        for (int i = 0; i < 18; ++i)
            outputs.push_back ("<tool name=\"classical_agent_tool\">{\"user_music_interest\":\"round "
                               + std::to_string (i) + "\"}</tool>");
        outputs.push_back ("The tool budget was used, so here is the concise final summary.");
        ScriptedLLM llm (outputs);

        const auto result = agent.run (llm, "keep calling tools until the cap", 1024, 128);
        expect (result.toolCalls.size() == 18, "round limit executes bounded number of tools");
        expect (result.finalText.contains ("concise final summary"), "round limit uses reserved final summary pass");
        expect (llm.prompts.size() == 19, "round limit reserves one final model call");
    }

    {
        MusicAgentCore agent;
        std::vector<std::string> outputs;
        for (int i = 0; i < 18; ++i)
            outputs.push_back ("<tool name=\"classical_agent_tool\">{\"user_music_interest\":\"sentinel "
                               + std::to_string (i) + "\"}</tool>");
        outputs.push_back ("<tool_call|>");
        ScriptedLLM llm (outputs);

        const auto result = agent.run (llm, "force sentinel final response", 1024, 128);
        expect (result.toolCalls.size() == 18, "sentinel fallback executes bounded number of tools");
        expect (result.finalText.contains ("Latest successful tool result"), "sentinel fallback writes deterministic summary");
        expect (! result.finalText.contains ("<tool_call"), "sentinel fallback removes tool-call marker");
    }

    {
        MusicAgentCore agent;
        ScriptedLLM llm ({
            "I made something. play_midi(\"/tmp/definitely_missing_music_agent.mid\")",
            "I attempted playback and reported the result."
        });

        const auto result = agent.run (llm, "play the generated file", 1024, 128);
        expect (result.toolCalls.size() == 1, "function-style play_midi executes as tool");
        expect (result.toolCalls[0].name == "play_midi", "function-style play_midi executes correct tool");
        expect (! result.toolResults[0].ok, "function-style play_midi reports missing file through tool result");
        expect (result.finalText.contains ("attempted playback"), "function-style play_midi gets follow-up final answer");
    }

    {
        MusicAgentCore agent;
        ScriptedLLM llm ({
            R"txt(<tool name="generate_formula_midi">{
                "backend": "expr",
                "steps": 4,
                "voices": [
                    {
                        "pitch_lambda": "[60, 64][i % 2]",
                        "rhythm_lambda": "0.5",
                        "pitch_mode": "midi"
                    }
                ]
            }</tool>)txt",
            "play_midi(\"/tmp/definitely_missing_music_agent.mid\")",
            "I will analyze before playback next time."
        });

        const auto result = agent.run (llm, "generate then prematurely play", 2048, 128);
        expect (result.toolCalls.size() == 2, "premature playback test executes generate and play attempt");
        expect (result.toolCalls[1].name == "play_midi", "premature playback attempt parsed");
        expect (! result.toolResults[1].ok, "premature playback after generation is blocked");
        expect (result.toolResults[1].content.contains ("Call analyze_midi"), "premature playback block asks for analysis");
        expect (result.finalText.contains ("analyze before playback"), "premature playback block reaches final answer");
    }

    {
        MusicAgentCore agent;
        PrematurePlaybackRecoveryLLM llm;

        const auto result = agent.run (llm, "generate then recover from premature playback", 4096, 256);
        expect (result.toolCalls.size() >= 4, "premature playback recovery executes generate, blocked play, analyze, play");
        expect (result.toolCalls[0].name == "generate_formula_midi", "premature playback recovery starts with generation");
        expect (result.toolCalls[1].name == "play_midi" && ! result.toolResults[1].ok,
                "premature playback recovery blocks first play");
        expect (result.toolCalls[2].name == "analyze_midi" && result.toolResults[2].ok,
                "premature playback recovery analyzes generated file");
        expect (result.toolCalls[3].name == "play_midi" && result.toolResults[3].ok,
                "premature playback recovery plays after analysis");
        expect (result.finalText.contains ("analyzed"), "premature playback recovery reaches final answer");
    }

    {
        MusicAgentCore agent;
        PrematureRegenerationRecoveryLLM llm;

        const auto result = agent.run (llm, "generate then recover from premature regeneration", 4096, 256);
        expect (result.toolCalls.size() == 4, "premature regeneration recovery executes generate, blocked generate, analyze, play");
        expect (result.toolCalls[0].name == "generate_formula_midi", "premature regeneration recovery starts with generation");
        expect (result.toolCalls[1].name == "generate_formula_midi" && ! result.toolResults[1].ok,
                "premature regeneration is blocked before analysis");
        expect (result.toolResults[1].content.contains ("Call analyze_midi"),
                "premature regeneration block asks for analysis");
        expect (result.toolCalls[2].name == "analyze_midi" && result.toolResults[2].ok,
                "premature regeneration recovery analyzes generated file");
        expect (result.toolCalls[3].name == "play_midi" && result.toolResults[3].ok,
                "premature regeneration recovery plays after analysis");
    }

    {
        MusicAgentCore agent;
        ScriptedLLM llm ({ "First answer", "Second answer" });
        agent.run (llm, "first", 512, 128);
        agent.run (llm, "second", 512, 128);
        expect (llm.prompts.back().find ("First answer") != std::string::npos, "previous answer appears in later prompt");
        expect (llm.prompts.back().find ("first") != std::string::npos, "previous user message appears in later prompt");
    }

    std::cout << "MusicAgentCoreTests passed\n";
    return 0;
}
