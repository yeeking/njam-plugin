#pragma once

#include <JuceHeader.h>
#include "ILLMController.h"

class OpenAICompatibleLLM final : public ILLMController
{
public:
    OpenAICompatibleLLM();

    void setEndpoint (const juce::String& endpointIn);
    juce::String getEndpoint() const;
    void setModelName (const juce::String& modelNameIn);

    bool readyToGenerate() override;
    bool loadModel (const std::string& path) override;
    void unloadModel() override;
    void resetContext (uint32_t ctxLen) override;
    void prepareSampler() override;
    std::string generate (std::string prompt, size_t maxLength) override;
    std::string generateWithStats (const std::string& prompt, size_t maxLength, InferenceStats& stats) override;
    void addPromptResponse (const std::string& prompt, const std::string& response) override;
    std::vector<std::pair<std::string, std::string>> getPromptResponseHistory() override;
    bool popPromptResponse (std::pair<std::string, std::string>& result) override;
    LLMStatus getStatus() const override;
    std::string getStatusString() const override;
    bool isReady() const override;
    void setThreadCount (int threadCount) override;
    int getModelTrainingContextLength() const override;
    void requestStop() override;
    void clearStopRequest() override;
    void registerPromptSettings (const std::string& prompt, const PromptSettings& settings) override;
    PromptSettings consumePromptSettings (const std::string& prompt) override;
    static bool promptShouldRequireMusicToolForTesting (const juce::String& userPrompt);

private:
    juce::String completionUrl() const;
    juce::String postJson (const juce::String& url, const juce::String& body) const;

    mutable juce::CriticalSection lock;
    juce::String endpoint = "http://localhost:2224/v1/";
    juce::String modelName = "google/gemma-4-26b-a4b-qat";
    uint32_t contextLength = 2048;
    std::atomic<LLMStatus> status { LLMStatus::WaitingForJobs };
    std::atomic<bool> stopRequested { false };
    std::vector<std::pair<std::string, std::string>> history;
};
