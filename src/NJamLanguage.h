#pragma once

#include <string> 
#include <vector> 
#include <JuceHeader.h>


#pragma once

/** Value object representing one NJam note event with pitch, timing, and completion state. */
class NJamMessage
{
public:
    // Constructor
    NJamMessage(int pitch, int channel, uint8 velocity,
                int duration, int wait, int offset, bool complete);

    NJamMessage(const std::string& njamStr);

    std::string toString();
    void fromString(const std::string& njamStr);
    
    // Getters
    int getPitch() const;
    int getChannel() const;
    int getVelocity() const;
    int getDuration() const;
    int getWait() const;
    int getOffset() const;
    bool isComplete() const;

    // Setters
    void setPitch(int pitch);
    void setChannel(int channel);
    void setVelocity(int velocity);
    void setDuration(int duration);
    void setWait(int wait);
    void setOffset(int offset);
    void setComplete(bool complete);


private:
    int pitch;
    int channel;
    int velocity;
    int duration;
    int wait;
    int offset;
    bool complete;
};



/** Converts between MIDI buffers and the project's NJam text representation. */
class NJamLanguage {
public:
    NJamLanguage();
    // std::string MIDIToNJam(std::vector<short> rawMIDI);
    // std::vector<short> NJamToMIDI(std::string njam);
    
    /** convert a midi buffer via NJamMessages to a string which can contain more than one njam sentence*/
    static std::string MIDIToNJam(const juce::MidiBuffer& buffer, double sampleRate, double bpm, int targetTicksPerQuarter = 960);
    /** convert an NJam string which might contain more than one message to a MIDIBuffer containing MIDI messages representing the sent njam events*/
    static juce::MidiBuffer NJamToMIDI(const std::string& njamStr, double sampleRate, double bpm, int targetTicksPerQuarter = 960);

    /** convert MIDIBuffer to a vector of NJamMessage objects - halfway to njam strings */
    static std::vector<NJamMessage> MIDIToNJamVec(const juce::MidiBuffer& buffer, double sampleRate, double bpm, int targetTicksPerQuarter = 960);
    /** convert NJamMessage vector into MIDIBuffer containing midi messages */
    static juce::MidiBuffer NJamVecToMIDI(std::vector<NJamMessage>& njamVec, double sampleRate, double bpm, int targetTicksPerQuarter = 960);

    /** top level - convert arbitrary njam string to vector of NJamMessages. Split the string, use NJamMessage.fromString */
    static std::vector<NJamMessage> NJamStrToMessages(const std::string& njamStr);
    
    
};
