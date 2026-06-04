#pragma once

#include <string>
#include <vector>
#include <utility>
#include <cstddef>
#include <cstdint>

enum class LLMStatus
{
    WaitingForJobs,
    LoadedButNeedContextReset,
    Generating,
    ModelNotReady
};

/** Interface for model backends that can load a language model and run prompt/response generation. */
class ILLMController
{
public:
    virtual ~ILLMController() = default;

    struct PromptSettings
    {
        uint32_t ctxLen = 32;
        size_t maxTokens = 100;
        bool echoOnNotReady = false;
    };

    struct InferenceStats
    {
        size_t num_tokens_in_prompt = 0;
        size_t num_tokens_in_response = 0;
        double prompt_tokens_per_second = 0.0;
        double inference_tokens_per_second = 0.0;
        double total_time_taken_for_inference = 0.0; // seconds
    };

    virtual bool readyToGenerate() = 0;
    virtual bool loadModel(const std::string& path) = 0;
    virtual void unloadModel() = 0;
    virtual void resetContext(uint32_t ctxLen) = 0;
    virtual void prepareSampler() = 0;
    virtual std::string generate(std::string prompt, size_t maxLength) = 0;
    virtual std::string generateWithStats(const std::string& prompt, size_t maxLength, InferenceStats& stats) = 0;

    virtual void addPromptResponse(const std::string& prompt, const std::string& response) = 0;
    virtual std::vector<std::pair<std::string, std::string>> getPromptResponseHistory() = 0;
    virtual bool popPromptResponse(std::pair<std::string, std::string>& result) = 0;

    virtual LLMStatus getStatus() const = 0;
    virtual std::string getStatusString() const = 0;
    virtual bool isReady() const = 0;
    virtual void setThreadCount(int threadCount) = 0;
    virtual int getModelTrainingContextLength() const = 0;
    virtual void requestStop() = 0;
    virtual void clearStopRequest() = 0;

    virtual void registerPromptSettings(const std::string& prompt, const PromptSettings& settings) = 0;
    virtual PromptSettings consumePromptSettings(const std::string& prompt) = 0;
};
