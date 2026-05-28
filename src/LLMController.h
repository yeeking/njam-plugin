#pragma once
#include <string>
#include <memory>
#include <vector>
#include <mutex>
#include <unordered_map>
#include <deque>
#include <atomic>
#include "llama.h"
#include "ILLMController.h"

struct llama_model;

/** Concrete llama.cpp-backed language model controller used by the plugin's inference thread. */
class LLMController : public ILLMController{
    public:
        LLMController();
        ~LLMController() override;
        /** returns true if model successfully loaded , false otherwise  */
        bool readyToGenerate() override;
        /** load a gguf model from sent path. If fails, return false, else return true */
        bool loadModel(const std::string& path) override;
        void unloadModel() override;
        /** sets up a context for use in tokenizing and inference */
        void resetContext(uint32_t ctxLen) override;
        /** setup a top n sampler thingy */
        void prepareSampler() override;
        /** calls tokenize, infer and detokenize */
        std::string generate(std::string prompt, size_t maxLength) override;
        std::string generateWithStats(const std::string& prompt, size_t maxLength, InferenceStats& stats) override;
        /** convert sent string to tokens ready to feed to the model */
        std::vector<llama_token> tokenize(const std::string& prompt);
        /** feed tokens to model, generate output as tokens */
        std::vector<llama_token> infer(std::vector<llama_token>& inTokens, size_t maxLength);
        /** convert tokens (from the model/ tokenize) to a string */
        std::string detokenize(const std::vector<llama_token>& tokens);

        /** (thread safe) add a prompt and response to the p and r history */
        void addPromptResponse(const std::string& prompt, const std::string& response) override;
        /** (thread safe) retrieve a copy of the prompt and response history */
        std::vector<std::pair<std::string, std::string>> getPromptResponseHistory() override;
        /** (thread safe) pop the next prompt/response pair for processing */
        bool popPromptResponse(std::pair<std::string, std::string>& result) override;
        LLMStatus getStatus() const override;
        std::string getStatusString() const override;
        bool isReady() const override;
        void setThreadCount(int threadCount) override;
        int getModelTrainingContextLength() const override;
        void requestStop() override;
        void clearStopRequest() override;

        void registerPromptSettings(const std::string& prompt, const PromptSettings& settings) override;
        PromptSettings consumePromptSettings(const std::string& prompt) override;
    
    private:
        // no dirty old pointers mate
        // std::unique_ptr<llama_model> model;
        llama_model* model;
        const llama_vocab * vocab;
        llama_context* ctx;
        llama_sampler * smpl;
        mutable std::mutex modelMutex;


        std::vector<std::pair<std::string,std::string>> promptAndResponseHistory;
        /** mutex to control access to the promptandresponse history  */
        std::mutex historyMutex;
        std::deque<std::pair<std::string, std::string>> promptResponseQueue;
        std::mutex responseQueueMutex;
        std::atomic<LLMStatus> status = LLMStatus::WaitingForJobs;
        std::atomic<bool> stopRequested{false};
        std::unordered_map<std::string, PromptSettings> promptSettings;
        std::mutex promptSettingsMutex;
        size_t batchSizeLimit = 0;
        int threadCount = 4;

};
