#include "LLMController.h"
#include <algorithm>
#include <filesystem>
#include <iostream>
#include <thread>
#include <chrono>

/** call this to switch off llama cpp logging 
 * e.g.     llama_log_set(llama_log_callback_null, NULL);
 * https://github.com/ggml-org/llama.cpp/discussions/1758
*/
static void llama_log_callback_null(ggml_log_level level, const char * text, void * user_data) { (void) level; (void) text; (void) user_data; }

namespace
{
void truncatePromptToFitContext(std::vector<llama_token>& tokens, size_t contextSize, size_t batchLimit, size_t maxLength)
{
    if (tokens.empty() || contextSize == 0)
        return;

    const size_t requestedOutputTokens = maxLength > 0 ? maxLength : 1;
    const size_t reservedOutputTokens = std::min(contextSize > 1 ? contextSize - 1 : size_t{0}, requestedOutputTokens);
    const size_t maxPromptTokens = std::max<size_t>(1, contextSize - reservedOutputTokens);
    const size_t keep = std::min(batchLimit, maxPromptTokens);

    if (tokens.size() > keep)
        tokens.erase(tokens.begin(), tokens.end() - static_cast<ptrdiff_t>(keep));
}
}


LLMController::LLMController() 
            : model{nullptr}, 
              vocab{nullptr}, 
              ctx{nullptr}, 
              smpl{nullptr}

{
    // switch off llama logging
    llama_log_set(llama_log_callback_null, NULL);
    status.store(LLMStatus::ModelNotReady);
}

LLMController::~LLMController()
{
    stopRequested.store(true);
    if (smpl != nullptr){
        llama_sampler_free(smpl);
    }
    if (ctx != nullptr){
        llama_free(ctx);
    } 
   
    if (model != nullptr){
        llama_model_free(model);
    }
}

bool LLMController::loadModel(const std::string& path)
{
    std::lock_guard<std::mutex> lock(modelMutex);

    std::filesystem::path fp = path;
    if (! std::filesystem::exists(fp)) {
        std::cout << " LLMController::loadModel file not found" <<  path << std::endl;
        status.store(LLMStatus::ModelNotReady);
        return false; 
    }
    
    
    if (model != nullptr){
        llama_model_free(model);
    }
    
    try {
        llama_model_params model_params = llama_model_default_params();
        
        this->model = llama_model_load_from_file(path.c_str(), model_params);
        this->vocab = llama_model_get_vocab(model);

        if (!model) {
            status.store(LLMStatus::ModelNotReady);
            return false; 
        }

        std::cout << "LLMController::loadModel model loaded! " << std::endl;
        status.store(LLMStatus::WaitingForJobs);
        return true; 
    }
    catch (...){
        std::cout << " LLMController::loadModel exception thrown loading model " << std::endl;
        status.store(LLMStatus::ModelNotReady);
        return false; 
    }
    return false; 

}

void LLMController::unloadModel()
{
    std::lock_guard<std::mutex> lock(modelMutex);
    if (smpl != nullptr){
        llama_sampler_free(smpl);
        smpl = nullptr;
    }
    if (ctx != nullptr){
        llama_free(ctx);
        ctx = nullptr;
    }
    if (model != nullptr){
        llama_model_free(model);
        model = nullptr;
    }
    vocab = nullptr;
    status.store(LLMStatus::ModelNotReady);
}

void LLMController::resetContext(uint32_t ctxLen)
{
    std::lock_guard<std::mutex> lock(modelMutex);
    if (ctx != nullptr){
        llama_free(ctx);
        ctx = nullptr;
    }

    // code to prepare a context 
    // uint32_t n_ctx = 64; // TODO 64 is way too short - should be enough to store prompt and n predicted tokens
    // int prev_len = 0;
    llama_context_params ctx_params = llama_context_default_params();
    ctx_params.n_ctx = ctxLen;
    ctx_params.n_batch = ctxLen;
    ctx_params.n_ubatch = ctxLen;
    
    const int workerThreads = std::max(1, threadCount);
    ctx_params.n_threads = workerThreads;
    ctx_params.n_threads_batch = workerThreads;
    ctx_params.no_perf = false;

    this->ctx = llama_init_from_model(model, ctx_params);
    batchSizeLimit = ctxLen;
    std::cout << "LLMController::resetContext threads n_threads=" << llama_n_threads(ctx)
              << " n_threads_batch=" << llama_n_threads_batch(ctx) << std::endl;

}

void LLMController::prepareSampler()
{
    std::lock_guard<std::mutex> lock(modelMutex);

    uint32_t rand_seed = 42;
    float temperature = 0.6;
    float min_p = 5;

    this->smpl = llama_sampler_chain_init(llama_sampler_chain_default_params());
    llama_sampler_chain_add(smpl, llama_sampler_init_min_p(min_p, 1));
    llama_sampler_chain_add(smpl, llama_sampler_init_temp(temperature));// low is deterministic
    llama_sampler_chain_add(smpl, llama_sampler_init_dist(rand_seed));
}


std::vector<llama_token> LLMController::tokenize(const std::string& prompt)
{
    std::lock_guard<std::mutex> lock(modelMutex);

    if (vocab == nullptr){// vocab not loaded yet 
        std::cout << "LLMController::tokenize vocab not loaded, returning empty result" << std::endl;
        return std::vector<llama_token>();
    }

    const int n_prompt = -llama_tokenize(vocab, prompt.c_str(), prompt.size(), NULL, 0, 
                                            true, // add_special
                                            true);// parse_special

    // allocate space for the tokens and tokenize the prompt
    std::vector<llama_token> prompt_tokens(static_cast<size_t>(n_prompt));
    if (llama_tokenize(vocab, prompt.c_str(), prompt.size(), prompt_tokens.data(), prompt_tokens.size(), true, true) < 0) {
        return std::vector<llama_token>();
    }
    else{
        // return 
        return prompt_tokens; 
    }
}


std::string LLMController::detokenize(const std::vector<llama_token>& tokens)
{
    std::lock_guard<std::mutex> lock(modelMutex);

    if (vocab == nullptr){// vocab not loaded yet 
        return "";
    }
    std::string response{};
    for (auto id : tokens) {
        char buf[256];
        int32_t n = llama_token_to_piece(vocab, id, buf, sizeof(buf), 0, true);
        // if (n < 0) {
        //     fprintf(stderr, "%s: error: failed to convert token to piece\n", __func__);
        //     return 1;
        // }
        std::string piece(buf, static_cast<std::string::size_type>(n));
        response += piece;
    }
    return response; 
    // static_cast<std::string::size_type>
}



std::vector<llama_token> LLMController::infer(std::vector<llama_token>& inTokens, size_t maxLength)
{
    std::lock_guard<std::mutex> lock(modelMutex);

    if (model == nullptr || ctx == nullptr || smpl == nullptr){// model/ ctx not loaded yet 
        if (model == nullptr){
            std::cout << "LLMController::infer model not loaded. You need to call loadModel first.  returning empty result" << std::endl;
        }
        else if (ctx == nullptr){
            std::cout << "LLMController::infer ctx not loaded. You need to call resetContext first.  returning empty result" << std::endl;
        }
        else if (smpl == nullptr){
            std::cout << "LLMController::infer sampler not loaded. You need to call prepareSampler first.  returning empty result" << std::endl;
        }
        
        return std::vector<llama_token>();
    }

    std::vector<llama_token> outTokens{};
    
    uint32_t n_ctx = llama_n_ctx(ctx);
    if (n_ctx == 0)
        return outTokens;

    const size_t batchLimit = batchSizeLimit > 0 ? batchSizeLimit : static_cast<size_t>(n_ctx);
    truncatePromptToFitContext(inTokens, static_cast<size_t>(n_ctx), batchLimit, maxLength);
    const size_t promptSize = inTokens.size();

    // Prepare a batch for the prompt

    llama_batch batch = llama_batch_get_one(inTokens.data(), promptSize);
    llama_token new_token_id;

    // int n_decode = 0;

    // from 0 up to the length of the context basically
    for (int n_pos = 0; n_pos + batch.n_tokens < static_cast<int32_t>(n_ctx); ) {
        if (llama_decode(ctx, batch)) {
            // problem 
            return outTokens;
        }
        n_pos += batch.n_tokens;

        // Sample the next token
        new_token_id = llama_sampler_sample(smpl, ctx, -1);

        // is it an end of generation?
        if (llama_vocab_is_eog(vocab, new_token_id)) {
            break;
        }
        outTokens.push_back(new_token_id);
        if (maxLength > 0 && outTokens.size() >= maxLength)
            break;
        if ((outTokens.size() & 0x7) == 0)
            std::this_thread::yield();

        // Prepare the next batch with the sampled token
        batch = llama_batch_get_one(&new_token_id, 1);
    }
    return outTokens;
}
        
std::string LLMController::generate(std::string prompt, size_t maxLength)
{
    status.store(LLMStatus::Generating);
    std::vector<llama_token> inTokes = tokenize(prompt);
    std::vector<llama_token> outTokes = infer(inTokes, maxLength);
    status.store(LLMStatus::WaitingForJobs);
    return detokenize(outTokes);
}

std::string LLMController::generateWithStats(const std::string& prompt, size_t maxLength, InferenceStats& stats)
{
    stats = InferenceStats{};
    status.store(LLMStatus::Generating);

    std::vector<llama_token> inTokens = tokenize(prompt);
    stats.num_tokens_in_prompt = inTokens.size();

    std::vector<llama_token> outTokens;
    {
        std::lock_guard<std::mutex> lock(modelMutex);
        if (model == nullptr || ctx == nullptr || smpl == nullptr){
            status.store(LLMStatus::WaitingForJobs);
            return {};
        }

        uint32_t n_ctx = llama_n_ctx(ctx);
        if (n_ctx == 0) {
            status.store(LLMStatus::WaitingForJobs);
            return {};
        }

        const size_t batchLimit = batchSizeLimit > 0 ? batchSizeLimit : static_cast<size_t>(n_ctx);
        truncatePromptToFitContext(inTokens, static_cast<size_t>(n_ctx), batchLimit, maxLength);
        const size_t promptSize = inTokens.size();
        stats.num_tokens_in_prompt = promptSize;

        llama_batch batch = llama_batch_get_one(inTokens.data(), promptSize);
        llama_token new_token_id;

        const auto t0 = std::chrono::steady_clock::now();
        if (llama_decode(ctx, batch)) {
            status.store(LLMStatus::WaitingForJobs);
            return {};
        }
        const auto t1 = std::chrono::steady_clock::now();
        const double prefillSeconds = std::chrono::duration<double>(t1 - t0).count();

        outTokens.reserve(maxLength > 0 ? maxLength : 128);

        const auto genStart = std::chrono::steady_clock::now();
        for (int n_pos = static_cast<int>(batch.n_tokens);
             n_pos + 1 < static_cast<int32_t>(n_ctx); )
        {
            if (stopRequested.load())
                break;

            new_token_id = llama_sampler_sample(smpl, ctx, -1);
            if (llama_vocab_is_eog(vocab, new_token_id))
                break;

            outTokens.push_back(new_token_id);
            if (maxLength > 0 && outTokens.size() >= maxLength)
                break;

            if ((outTokens.size() & 0x7) == 0)
                std::this_thread::yield();

            batch = llama_batch_get_one(&new_token_id, 1);
            if (llama_decode(ctx, batch)) {
                break;
            }
            n_pos += batch.n_tokens;
        }
        const auto genEnd = std::chrono::steady_clock::now();
        const double generationSeconds = std::chrono::duration<double>(genEnd - genStart).count();
        stats.num_tokens_in_response = outTokens.size();
        stats.prompt_tokens_per_second = prefillSeconds > 0.0
            ? static_cast<double>(stats.num_tokens_in_prompt) / prefillSeconds
            : 0.0;
        stats.inference_tokens_per_second = generationSeconds > 0.0
            ? static_cast<double>(stats.num_tokens_in_response) / generationSeconds
            : 0.0;
        stats.total_time_taken_for_inference = prefillSeconds + generationSeconds;
    }

    status.store(LLMStatus::WaitingForJobs);
    return detokenize(outTokens);
}


bool LLMController::readyToGenerate()
{
    std::lock_guard<std::mutex> lock(modelMutex);

    if (model == nullptr || ctx == nullptr || smpl == nullptr){// model/ ctx not loaded yet 
        std::cout << "LLMController::readyToGenerate not ready" << std::endl;
        if (model == nullptr) std::cout << "Model not loaded yet " << std::endl;
        if (ctx == nullptr) std::cout << "Context not setup yet " << std::endl;
        if (smpl == nullptr) std::cout << "Sampler not loaded yet " << std::endl;
        
        return false;
    }
    return true; 
}

void LLMController::setThreadCount(int threadCountIn)
{
    std::lock_guard<std::mutex> lock(modelMutex);
    threadCount = std::max(1, threadCountIn);
    if (ctx != nullptr)
    {
        llama_set_n_threads(ctx, threadCount, threadCount);
        std::cout << "LLMController::setThreadCount threads n_threads=" << llama_n_threads(ctx)
                  << " n_threads_batch=" << llama_n_threads_batch(ctx) << std::endl;
    }
}



// Adds a new prompt-response pair to the history
void LLMController::addPromptResponse(const std::string& prompt, const std::string& response) 
{
    {
        std::lock_guard<std::mutex> lock(historyMutex);
        promptAndResponseHistory.emplace_back(prompt, response);
    }
    {
        std::lock_guard<std::mutex> lock(responseQueueMutex);
        promptResponseQueue.emplace_back(prompt, response);
    }
}

// Retrieves a copy of the entire prompt-response history
std::vector<std::pair<std::string, std::string>> LLMController::getPromptResponseHistory() 
{
    std::lock_guard<std::mutex> lock(historyMutex);
    return promptAndResponseHistory;
}

bool LLMController::popPromptResponse(std::pair<std::string, std::string>& result)
{
    std::lock_guard<std::mutex> lock(responseQueueMutex);
    if (promptResponseQueue.empty())
        return false;

    result = std::move(promptResponseQueue.front());
    promptResponseQueue.pop_front();
    return true;
}

LLMStatus LLMController::getStatus() const {
    return status.load();  // atomic, thread-safe read
}

std::string LLMController::getStatusString() const {
    switch (status.load()) {
        case LLMStatus::WaitingForJobs: return "Waiting for jobs";
        case LLMStatus::Generating:     return "Generating";
        case LLMStatus::LoadedButNeedContextReset: return "Model loaded but need context setup";
        case LLMStatus::ModelNotReady:  return "Model not ready";
        default: return "Unknown";
    }
}

void LLMController::registerPromptSettings(const std::string& prompt, const PromptSettings& settings)
{
    std::lock_guard<std::mutex> lock(promptSettingsMutex);
    promptSettings[prompt] = settings;
}

LLMController::PromptSettings LLMController::consumePromptSettings(const std::string& prompt)
{
    std::lock_guard<std::mutex> lock(promptSettingsMutex);
    auto it = promptSettings.find(prompt);
    if (it == promptSettings.end())
        return PromptSettings{};

    PromptSettings settings = it->second;
    promptSettings.erase(it);
    return settings;
}

bool LLMController::isReady() const
{
    std::lock_guard<std::mutex> lock(modelMutex);
    return model != nullptr && ctx != nullptr && smpl != nullptr;
}

int LLMController::getModelTrainingContextLength() const
{
    std::lock_guard<std::mutex> lock(modelMutex);
    if (model == nullptr)
        return 0;

    return static_cast<int> (llama_model_n_ctx_train (model));
}

void LLMController::requestStop()
{
    stopRequested.store(true);
}

void LLMController::clearStopRequest()
{
    stopRequested.store(false);
}
