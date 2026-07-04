# NJAM plugin

Transformer-based MIDI improviser plugin

## How to install 

It is a bit fiddly to install on macos because of notarisation. I've not sorted out notarisation so you have to 'de-quarantine' the VST before using it. Also I am a bit too lazy to 

* Download a release from the release section
* unzip it 
* open the Terminal.app
* run this command:
```
open ~/Library/Audio/Plug-Ins/VST3/
```
* your finder will show you a folder 
* drop the .vst3 file from the zip into that folder
* now back in the terminal, remove quarantine from the plugin like this:
```
xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/VST3/
```
* now launch your DAW and it should scan for new plugs and detect it
* you can now add it to a channel

## Example model

Here is a model that has some interesting behaviour:

```
https://drive.google.com/drive/folders/1RrxZlP9TqoozFhkjiMtVgk-ciNN8dtLQ?usp=sharing
```

The model is based on this architecture:

https://huggingface.co/EleutherAI/pythia-160m

The weights were randomised, then it was trained on the Queen Mary  Pijama dataset, after conversion from MIDI to njam format:

```
@article{edwards2023pijama,
  title={Pijama: Piano jazz with automatic midi annotations},
  author={Edwards, Drew and Dixon, Simon and Benetos, Emmanouil},
  journal={Transactions of the International Society for Music Information Retrieval},
  volume={6},
  number={1},
  year={2023}
}
```
## Building the plugin from source

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

## Training another model

I trained that model using my small-lm toolkit, which is a generic toolkit for training small llms from known architectures:

https://github.com/yeeking/small-lm-toolkit

Small-lm toolkit does not provide music-specific dataset preparation. 

To make a dataset which you can then train on with small-lm, you need this repo:

https://github.com/yeeking/neural-jammer

So it is a bit tricky for now but I've got a new repo called super-njam which will contain the data prep and training scripts.



