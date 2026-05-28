#include "InferenceThreadManager.h"
#include <iostream>
#include <chrono>

InferenceThreadManager::InferenceThreadManager(std::shared_ptr<LLMController> _llmControllerP)
: llmThread{InferenceThreadManager::inferenceLoop, std::ref(keepRunning), _llmControllerP, std::ref(*this)},
  prompt{""},
  llmOutput{""}
{
}

InferenceThreadManager::~InferenceThreadManager()
{
    stop();
}

/** set the prompt to be processed next. If un-processed prompt already there, it is overwritten */
void InferenceThreadManager::setPrompt(const std::string& p)
{
    std::lock_guard<std::mutex> guard(prompt_mutex);
    prompt = p; // copy it
}

std::string InferenceThreadManager::consumePrompt()
{
    std::lock_guard<std::mutex> guard(prompt_mutex);
    std::string p = prompt;
    prompt = "";
    return p;
}

void InferenceThreadManager::setLLMOutput(const std::string& o)
{
    std::lock_guard<std::mutex> guard(llmOutput_mutex);
    llmOutput = o; // copy it
    inferenceResult.output = o;
    inferenceResult.stats = ILLMController::InferenceStats{};
    hasInferenceResult = true;
}

std::string InferenceThreadManager::consumeLLMOutput()
{
    std::lock_guard<std::mutex> guard(llmOutput_mutex);
    std::string o = llmOutput;
    llmOutput = "";
    return o;
}

void InferenceThreadManager::setInferenceResult(const std::string& output, const ILLMController::InferenceStats& stats)
{
    std::lock_guard<std::mutex> guard(llmOutput_mutex);
    llmOutput = output;
    inferenceResult.output = output;
    inferenceResult.stats = stats;
    hasInferenceResult = true;
}

bool InferenceThreadManager::consumeInferenceResult(InferenceResult& result)
{
    std::lock_guard<std::mutex> guard(llmOutput_mutex);
    if (!hasInferenceResult)
        return false;

    result = inferenceResult;
    llmOutput.clear();
    inferenceResult = InferenceResult{};
    hasInferenceResult = false;
    return true;
}

void InferenceThreadManager::stop()
{
    keepRunning.store(false);
    if (llmThread.joinable())
        llmThread.join();
}


void InferenceThreadManager::inferenceLoop(std::atomic<bool>& keepRunning, 
                                           std::shared_ptr<LLMController> llmControllerP, 
                                          InferenceThreadManager& inferenceManager)
{
    while(keepRunning.load()){
        
        if (llmControllerP!= nullptr &&
            llmControllerP->getStatus() == LLMStatus::WaitingForJobs){
            std::string prompt = inferenceManager.consumePrompt();
            if (prompt != ""){
                if (! keepRunning.load())
                    break;

                const auto promptSettings = llmControllerP->consumePromptSettings(prompt);
                llmControllerP->clearStopRequest();
                if (! keepRunning.load())
                {
                    llmControllerP->requestStop();
                    break;
                }
                llmControllerP->resetContext(promptSettings.ctxLen);
                ILLMController::InferenceStats stats{};
                std::string res = llmControllerP->generateWithStats(prompt, promptSettings.maxTokens, stats);
                inferenceManager.setInferenceResult(res, stats);
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}
