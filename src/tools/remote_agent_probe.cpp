#include "../MusicAgentCore.h"
#include "../OpenAICompatibleLLM.h"

#include <iostream>

namespace
{
juce::String joinPromptArgs (int argc, char* argv[], int firstPromptArg)
{
    juce::String prompt;
    for (int i = firstPromptArg; i < argc; ++i)
    {
        if (prompt.isNotEmpty())
            prompt << " ";
        prompt << argv[i];
    }
    return prompt;
}
}

int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI juceInitialiser;

    juce::String endpoint = "http://localhost:2224/v1/";
    juce::String model = "google/gemma-4-26b-a4b-qat";
    int promptStart = 1;

    if (argc > 2 && juce::String (argv[1]) == "--endpoint")
    {
        endpoint = argv[2];
        promptStart = 3;
    }

    if (argc > promptStart + 1 && juce::String (argv[promptStart]) == "--model")
    {
        model = argv[promptStart + 1];
        promptStart += 2;
    }

    auto prompt = joinPromptArgs (argc, argv, promptStart).trim();
    if (prompt.isEmpty())
        prompt = "make nocturnal dub techno with quartal chords and two sections";

    OpenAICompatibleLLM llm;
    llm.setEndpoint (endpoint);
    llm.setModelName (model);

    MusicAgentCore agent;
    agent.setMidiPlaybackHandler ([] (const juce::File& file)
    {
        return ToolResult { file.existsAsFile(), file.existsAsFile()
            ? "playback scheduled: " + file.getFullPathName()
            : "MIDI file not found: " + file.getFullPathName() };
    });

    std::cout << "endpoint: " << endpoint << "\n";
    std::cout << "model: " << model << "\n";
    std::cout << "prompt: " << prompt << "\n";

    auto result = agent.run (llm, prompt, 8192, -1, [] (const AgentStatusEvent& event)
    {
        std::cout << "status: " << event.state;
        if (event.detail.isNotEmpty())
            std::cout << " / " << event.detail;
        std::cout << "\n";
    });

    for (size_t i = 0; i < result.toolCalls.size(); ++i)
    {
        const auto& call = result.toolCalls[i];
        const auto* toolResult = i < result.toolResults.size() ? &result.toolResults[i] : nullptr;
        std::cout << "tool: " << call.name << " -> "
                  << (toolResult != nullptr && toolResult->ok ? "ok" : "failed");
        if (toolResult != nullptr)
            std::cout << " / " << toolResult->content;
        std::cout << "\n";
    }

    std::cout << "final: " << result.finalText << "\n";
    std::cout << "stats: prompt_tokens=" << result.stats.num_tokens_in_prompt
              << " response_tokens=" << result.stats.num_tokens_in_response
              << " total_seconds=" << result.stats.total_time_taken_for_inference << "\n";

    return result.finalText.startsWith ("Agent error") ? 1 : 0;
}
