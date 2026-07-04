#include "../LLMController.h"
#include "../NJamLanguage.h"

#include "ggml-backend.h"
#include <JuceHeader.h>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <string>

namespace
{
constexpr int kTargetTicksPerQuarter = 960;
constexpr double kDefaultBpm = 120.0;
constexpr int kDefaultPromptTokens = 1024;
constexpr int kDefaultGenerateTokens = 512;

void printUsage(const char* executable)
{
    std::cerr << "Usage: " << executable
              << " <input.mid> [output.mid] [--model path] [--model-path path]"
              << " [--prompt-tokens n] [--generate-tokens n] [--threads n] [--bpm bpm] [--include-prompt]\n";
}

struct Options
{
    juce::File inputFile;
    juce::File outputFile;
    juce::File modelFile { juce::File::getCurrentWorkingDirectory().getChildFile("test_model_continuator.gguf") };
    int promptTokens = kDefaultPromptTokens;
    int generateTokens = kDefaultGenerateTokens;
    int threads = 4;
    double bpm = kDefaultBpm;
    bool includePrompt = false;
};

juce::File resolveFileArgument(const char* path)
{
    return juce::File::isAbsolutePath(path)
        ? juce::File(path)
        : juce::File::getCurrentWorkingDirectory().getChildFile(path);
}

int parsePositiveInt(const std::string& text, int fallback)
{
    try
    {
        const int value = std::stoi(text);
        return value > 0 ? value : fallback;
    }
    catch (...)
    {
        return fallback;
    }
}

double parsePositiveDouble(const std::string& text, double fallback)
{
    try
    {
        const double value = std::stod(text);
        return value > 0.0 ? value : fallback;
    }
    catch (...)
    {
        return fallback;
    }
}

Options parseOptions(int argc, char* argv[])
{
    Options options;

    if (argc < 2)
        return options;

    options.inputFile = resolveFileArgument(argv[1]);
    options.outputFile = options.inputFile.getSiblingFile(options.inputFile.getFileNameWithoutExtension() + "_continued.mid");

    int index = 2;
    if (index < argc && std::string(argv[index]).rfind("--", 0) != 0)
        options.outputFile = resolveFileArgument(argv[index++]);

    while (index < argc)
    {
        const std::string arg = argv[index++];

        if ((arg == "--model" || arg == "--model-path") && index < argc)
            options.modelFile = resolveFileArgument(argv[index++]);
        else if (arg == "--prompt-tokens" && index < argc)
            options.promptTokens = parsePositiveInt(argv[index++], options.promptTokens);
        else if (arg == "--generate-tokens" && index < argc)
            options.generateTokens = parsePositiveInt(argv[index++], options.generateTokens);
        else if (arg == "--threads" && index < argc)
            options.threads = parsePositiveInt(argv[index++], options.threads);
        else if (arg == "--bpm" && index < argc)
            options.bpm = parsePositiveDouble(argv[index++], options.bpm);
        else if (arg == "--include-prompt")
            options.includePrompt = true;
        else
            std::cerr << "Ignoring unknown or incomplete argument: " << arg << "\n";
    }

    return options;
}

bool loadMidiFile(const juce::File& file, juce::MidiFile& midiFile)
{
    juce::FileInputStream input(file);
    if (! input.openedOk())
        return false;

    return midiFile.readFrom(input);
}

juce::MidiBuffer midiFileToBuffer(juce::MidiFile& midiFile, double bpm)
{
    juce::MidiBuffer buffer;
    const short timeFormat = midiFile.getTimeFormat();
    const bool isPpq = timeFormat > 0;

    if (! isPpq)
        midiFile.convertTimestampTicksToSeconds();

    for (int trackIndex = 0; trackIndex < midiFile.getNumTracks(); ++trackIndex)
    {
        const auto* track = midiFile.getTrack(trackIndex);
        if (track == nullptr)
            continue;

        for (int eventIndex = 0; eventIndex < track->getNumEvents(); ++eventIndex)
        {
            const auto* event = track->getEventPointer(eventIndex);
            if (event == nullptr)
                continue;

            const auto& message = event->message;
            if (! message.isNoteOnOrOff())
                continue;

            const double timestamp = message.getTimeStamp();
            const int targetTick = isPpq
                ? static_cast<int>(std::llround(timestamp * static_cast<double>(kTargetTicksPerQuarter) / static_cast<double>(timeFormat)))
                : static_cast<int>(std::llround(timestamp * (bpm / 60.0) * static_cast<double>(kTargetTicksPerQuarter)));

            buffer.addEvent(message, std::max(0, targetTick));
        }
    }

    return buffer;
}

juce::MidiMessageSequence bufferToSequence(const juce::MidiBuffer& buffer, double bpm)
{
    juce::MidiMessageSequence sequence;
    sequence.addEvent(juce::MidiMessage::tempoMetaEvent(static_cast<int>(std::llround(60000000.0 / bpm))), 0.0);

    for (const auto metadata : buffer)
    {
        auto message = metadata.getMessage();
        message.setTimeStamp(static_cast<double>(metadata.samplePosition));
        sequence.addEvent(message);
    }

    sequence.updateMatchedPairs();
    return sequence;
}

std::string detokenizeTailPrompt(LLMController& llm, const std::string& prompt, int promptTokens)
{
    auto tokens = llm.tokenize(prompt);
    if (tokens.size() > static_cast<size_t>(promptTokens))
        tokens.erase(tokens.begin(), tokens.end() - promptTokens);

    return llm.detokenize(tokens);
}

bool writeMidiFile(const juce::File& file, const juce::MidiBuffer& buffer, double bpm)
{
    juce::MidiFile midiFile;
    midiFile.setTicksPerQuarterNote(kTargetTicksPerQuarter);
    midiFile.addTrack(bufferToSequence(buffer, bpm));

    if (file.existsAsFile() && ! file.deleteFile())
        return false;

    juce::FileOutputStream output(file);
    if (! output.openedOk())
        return false;

    return midiFile.writeTo(output, 1);
}

const char* backendDeviceTypeName(enum ggml_backend_dev_type type)
{
    switch (type)
    {
        case GGML_BACKEND_DEVICE_TYPE_CPU:  return "CPU";
        case GGML_BACKEND_DEVICE_TYPE_GPU:  return "GPU";
        case GGML_BACKEND_DEVICE_TYPE_IGPU: return "IGPU";
        default:                            return "unknown";
    }
}

void printBackendDevices()
{
    ggml_backend_load_all();

    const size_t count = ggml_backend_dev_count();
    std::cout << "ggml backend devices: " << count << "\n";

    for (size_t i = 0; i < count; ++i)
    {
        auto* device = ggml_backend_dev_get(i);
        std::cout << "  [" << i << "] "
                  << ggml_backend_dev_name(device)
                  << " (" << backendDeviceTypeName(ggml_backend_dev_type(device)) << ") - "
                  << ggml_backend_dev_description(device)
                  << "\n";
    }
}
}

int main(int argc, char* argv[])
{
    std::cout << "myk-midi-continuator build: " << NJAM_BUILD_TYPE << "\n";

    const auto options = parseOptions(argc, argv);
    if (! options.inputFile.existsAsFile())
    {
        printUsage(argv[0]);
        std::cerr << "Input MIDI file not found: " << options.inputFile.getFullPathName() << "\n";
        return 1;
    }

    if (! options.modelFile.existsAsFile())
    {
        std::cerr << "Model file not found: " << options.modelFile.getFullPathName() << "\n";
        return 1;
    }

    juce::MidiFile sourceMidi;
    if (! loadMidiFile(options.inputFile, sourceMidi))
    {
        std::cerr << "Could not read MIDI file: " << options.inputFile.getFullPathName() << "\n";
        return 1;
    }

    const auto inputBuffer = midiFileToBuffer(sourceMidi, options.bpm);
    const std::string sourceNJam = NJamLanguage::MIDIToNJam(inputBuffer,
                                                            kTargetTicksPerQuarter * options.bpm / 60.0,
                                                            options.bpm,
                                                            kTargetTicksPerQuarter);
    if (sourceNJam.empty())
    {
        std::cerr << "No note events found in MIDI file.\n";
        return 1;
    }

    LLMController llm;
    printBackendDevices();
    std::cout << "llama.cpp GPU offload support: "
              << (llama_supports_gpu_offload() ? "yes" : "no") << "\n";

    llm.setThreadCount(options.threads);
    if (! llm.loadModel(options.modelFile.getFullPathName().toStdString()))
        return 1;

    const uint32_t contextTokens = static_cast<uint32_t>(std::max(options.promptTokens + options.generateTokens + 64, 512));
    llm.resetContext(contextTokens);
    llm.prepareSampler();

    const std::string tailPrompt = detokenizeTailPrompt(llm, sourceNJam, options.promptTokens);
    std::cout << "Generating " << options.generateTokens << " tokens from "
              << options.promptTokens << " prompt tokens with " << options.threads << " threads...\n";

    LLMController::InferenceStats stats;
    const std::string continuation = llm.generateWithStats(tailPrompt,
                                                           static_cast<size_t>(options.generateTokens),
                                                           stats);
    std::cout << "Prompt tokens used: " << stats.num_tokens_in_prompt << "\n"
              << "Generated tokens: " << stats.num_tokens_in_response << "\n"
              << "Prompt prefill speed: " << stats.prompt_tokens_per_second << " tok/s\n"
              << "Generation speed: " << stats.inference_tokens_per_second << " tok/s\n"
              << "Inference time: " << stats.total_time_taken_for_inference << " s\n";

    const std::string outputNJam = options.includePrompt ? sourceNJam + "\n" + continuation : continuation;
    auto messages = NJamLanguage::NJamStrToMessages(outputNJam);
    if (messages.empty())
    {
        std::cerr << "Model output did not produce parseable NJam note messages.\n";
        return 1;
    }

    auto outputBuffer = NJamLanguage::NJamVecToMIDI(messages,
                                                    kTargetTicksPerQuarter * options.bpm / 60.0,
                                                    options.bpm,
                                                    kTargetTicksPerQuarter);

    if (! writeMidiFile(options.outputFile, outputBuffer, options.bpm))
    {
        std::cerr << "Could not write output MIDI file: " << options.outputFile.getFullPathName() << "\n";
        return 1;
    }

    std::cout << "Wrote continuation MIDI: " << options.outputFile.getFullPathName() << "\n";
    return 0;
}
