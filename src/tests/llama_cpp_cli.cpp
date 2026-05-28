// This program instantiates a llama cpp model 
// from a gguf then runs inference on it in a background thread
// using an LLMController class
#include "../LLMController.h"
#include <string>
#include <iostream>
#include <thread> 





void testSameThreadInfer(){
    LLMController llm{};
    bool loaded = llm.loadModel("/Users/matthewyk/src/ai-music/neural-jammer/models/EleutherAI__pythia-70m-last-q8_0.gguf");
    if (loaded){
        std::cout << "Model loaded. Status " << llm.getStatusString() << std::endl;
    }
    else{
        return; 
    }

    llm.resetContext(512);
    llm.prepareSampler();

    // now do something
    std::string prompt{"p_52 c_0 v_35 d_4120 w_1520"};
    std::string res = llm.generate(prompt, 128);

    std::cout << "Generated " << res << std::endl;

}

void runInference(LLMController& llm){
    std::cout << "runInference starting " << std::endl;
    std::string prompt{"p_52 c_0 v_35 d_4120 w_1520"};
    std::cout << "runInference calling llm generate " << std::endl;

    std::string res = llm.generate(prompt, 128);

    std::cout << "runInference:: " << res << std::endl;
    std::cout << "runInference returning " << std::endl;

}

void testBackgroundThreadInfer(){
    LLMController llm{};
    bool loaded = llm.loadModel("/Users/matthewyk/src/ai-music/neural-jammer/models/EleutherAI__pythia-70m-last-q8_0.gguf");
    if (loaded){
        std::cout << "Model loaded. Status " << llm.getStatusString() << std::endl;
    }
    else{
        return; 
    }

    llm.resetContext(512);
    llm.prepareSampler();

    // now do something

    std::thread thread1(runInference, std::ref(llm)); // pass by reference

    std::cout << "after thread 1, joining and waiting... " << std::endl;

    // now as a test, can i 

    thread1.join();// prevent freeing of llm object 
}

void testManyInference()
{
    
}

int main(){
    // testSameThreadInfer();
    testBackgroundThreadInfer();

    return 0; 
}
