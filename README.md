# MIDI jammer plugin using neural networks

The aim of this plugin is to be a MIDI improviser. You play MIDI in and it generates MIDI in response. 

## How to install 

* Download a release from the release section
* unzip it 


## For developers

To build the plugin from src:

```
git clone git@github.com:yeeking/njam-plugin.git
cd njam-plugin/libs
git clone https://github.com/ggml-org/llama.cpp.git
git clone https://github.com/juce-framework/JUCE.git
cd ..
cmake -B build .
cmake --build build --config Release -j 8
```

