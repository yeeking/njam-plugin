#pragma once 
#include <string>
#include <mutex>
#include <atomic>
#include <thread>
#include <memory>

#include "LLMController.h"
#include "ILLMController.h"

/** thread safe class allowing plugin processor to 
 *  to make prompt requests and llm controller to receive them, 
 * then vice versa for llm outputs */
class InferenceThreadManager{
    public:
        struct InferenceResult
        {
            std::string output;
            ILLMController::InferenceStats stats{};
        };

        InferenceThreadManager(std::shared_ptr<LLMController> _llmControllerP);
        ~InferenceThreadManager();
        
        /** set the prompt to be processed next. If un-processed prompt already there, it is overwritten */
        void setPrompt(const std::string&);
        /** retrieve and reset the prompt  */
        std::string consumePrompt();
        /** when llm result comes back, use this to store it */
        void setLLMOutput(const std::string&);
        /** retrieve and reset the llm output  */
        std::string consumeLLMOutput();
        /** store output and stats together for an atomic result handoff */
        void setInferenceResult(const std::string& output, const ILLMController::InferenceStats& stats);
        /** retrieve output and stats together; returns false if no new result */
        bool consumeInferenceResult(InferenceResult& result);
        void stop();
        std::atomic<bool> promptReady{false};
                
    private:
        std::atomic<bool> keepRunning{true};
        std::thread llmThread;

        std::mutex prompt_mutex;
        std::mutex llmOutput_mutex;
        std::string prompt;
        std::string llmOutput;
        bool hasInferenceResult{false};
        InferenceResult inferenceResult;
       /** its.a static function so i can pass it to thread
        * as a function reference, but we then pass it
        * an inferenceManager object anyways 
        */
        static void inferenceLoop(std::atomic<bool>& keepRunning,  std::shared_ptr<LLMController> llmControllerP, InferenceThreadManager& inferenceManager);
};
