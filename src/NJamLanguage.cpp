#include "NJamLanguage.h"
#include <cmath>
#include <sstream>
#include <cctype>

// Constructor using initialiser list
NJamMessage::NJamMessage(int pitch, int channel, uint8 velocity,
                         int duration, int wait, int offset, bool complete)
    : pitch(pitch), channel(channel), velocity(velocity),
      duration(duration), wait(wait), offset(offset), complete(complete)
{}

NJamMessage::NJamMessage(const std::string& njamStr)
{
    this->fromString(njamStr);
}

void NJamMessage::fromString(const std::string& njamStr)
{
    // Expected token format: "p_64 c_1 v_50 d_2 w_0" (whitespace-separated)
    std::istringstream iss(njamStr);
    std::string token;

    while (iss >> token)
    {
        auto pos = token.find('_');
        if (pos == std::string::npos || pos == token.size() - 1)
            continue;

        const std::string key = token.substr(0, pos);
        const int value = std::stoi(token.substr(pos + 1));

        if (key == "p")       setPitch(value);
        else if (key == "c")  setChannel(value);
        else if (key == "v")  setVelocity(value);
        else if (key == "d")  setDuration(value);
        else if (key == "w")  setWait(value);
    }

    // Any message parsed from string is considered complete; offset not encoded.
    setOffset(0);
    setComplete(true);
}
    

std::string NJamMessage::toString()
{
    return  "p_" + std::to_string (pitch)
          + " c_" + std::to_string (channel)
          + " v_" + std::to_string (velocity)
          + " d_" + std::to_string (duration)
          + " w_" + std::to_string (wait);
}

// Getters
int NJamMessage::getPitch() const     { return pitch; }
int NJamMessage::getChannel() const   { return channel; }
int NJamMessage::getVelocity() const  { return velocity; }
int NJamMessage::getDuration() const  { return duration; }
int NJamMessage::getWait() const      { return wait; }
int NJamMessage::getOffset() const    { return offset; }
bool NJamMessage::isComplete() const  { return complete; }

// Setters
void NJamMessage::setPitch(int val)     { pitch = val; }
void NJamMessage::setChannel(int val)   { channel = val; }
void NJamMessage::setVelocity(int val)  { velocity = val; }
void NJamMessage::setDuration(int val)  { duration = val; }
void NJamMessage::setWait(int val)      { wait = val; }
void NJamMessage::setOffset(int val)    { offset = val; }
void NJamMessage::setComplete(bool val) { complete = val; }

NJamLanguage::NJamLanguage()
{
}

std::string NJamLanguage::MIDIToNJam(const juce::MidiBuffer& midiBuffer, double sampleRate, double bpm, int targetTicksPerQuarter)
{
    auto njams = MIDIToNJamVec(midiBuffer, sampleRate, bpm, targetTicksPerQuarter);
    // TODO: aggregate to full string if needed
    if (njams.empty())
        return std::string{};
    std::string out;
    for (size_t i = 0; i < njams.size(); ++i)
    {
        if (i > 0)
            out += " ";
        out += njams[i].toString() + "\n";
    }
    return out;
}

juce::MidiBuffer NJamLanguage::NJamToMIDI(const std::string& njam, double sampleRate, double bpm, int targetTicksPerQuarter)
{
    auto messages = NJamStrToMessages(njam);
    return NJamVecToMIDI(messages, sampleRate, bpm, targetTicksPerQuarter);
}

std::vector<NJamMessage> NJamLanguage::MIDIToNJamVec(const juce::MidiBuffer& buffer, double sampleRate, double bpm, int targetTicksPerQuarter)
{
    if (sampleRate <= 0.0 || bpm <= 0.0 || targetTicksPerQuarter <= 0)
        return {};

    const int kDefaultDuration = targetTicksPerQuarter / 2;
    const double samplesPerTick = (60.0 / bpm) * sampleRate / static_cast<double>(targetTicksPerQuarter);

    std::vector<NJamMessage> njams;

    int lastEventIndex = -1; // wait times are always vs. the last event's index where event is either note on or cc
    
    for (const auto& msgWrapper : buffer)  
    {
        const juce::MidiMessage& msg = msgWrapper.getMessage();
        const int offset = msgWrapper.samplePosition;

        // ─────────── NOTE-ON ───────────
        if (msg.isNoteOn())
        {
            // Close the previous "open" note by filling its wait time
            // if (lastEventIndex >= 0) 
            // {
            //     NJamMessage& prev = njams[static_cast<size_t>(lastEventIndex)];   
            //     prev.setWait(offset - prev.getOffset()); // wait in samples
            //     std::cout << "Set wait of prev to " << prev.getWait() << std::endl;
            // }

            // Store the new one (duration & wait will be filled later) with sample-based timing
            NJamMessage m{msg.getNoteNumber(), msg.getChannel() - 1, msg.getVelocity(), 0, 0, offset, false};
            // std::cout << "njam sent offset in samples " << offset << " made wait of " << m.getWait() << " midi offset " << msg.getTimeStamp() << std::endl;
            njams.push_back (m);
            // i am now the last event 
            lastEventIndex = njams.size() - 1;
        }
        // ─────────── NOTE-OFF ───────────
        else if (msg.isNoteOff())
        {
            const int pitch   = msg.getNoteNumber();
            const int channel = msg.getChannel() - 1;

            // use a 'reverse' iterator to go from most recently added note backwards
            // looking for our matching njam note
            for (auto it = njams.rbegin(); it != njams.rend(); ++it)
            {
                if (!it->isComplete() && it->getPitch() == pitch && it->getChannel() == channel)
                {
                    it->setDuration(offset - it->getOffset());
                    it->setComplete(true);
                    break;// stop iterating 
                }
            }
        }
        // Other MIDI event types are ignored for this pass
    }

    // convert waits to relative values between offsets
    if (njams.empty())
        return njams;

    int offset = njams[0].getOffset();
    njams[0].setWait(offset); // first one wait == offset
    for (size_t i = 1; i < njams.size(); ++i)
    {
        njams[i].setWait(njams[i].getOffset() - njams[i - 1].getOffset());
        // std::cout << "Set wait to " << njams[i].getWait() << std::endl;
    }
    

    // ────────────────────────────────────────
    // Final pass – convert samples to ticks and fill defaults
    // ────────────────────────────────────────
    for (auto& m : njams)
    {
        const int durationTicks = static_cast<int>(std::llround(m.getDuration() / samplesPerTick));
        const int waitTicks = static_cast<int>(std::llround(m.getWait() / samplesPerTick));
        // std::cout << "njam offset (wait) in ticks " << waitTicks << " from wait of " << m.getWait() << std::endl;

        m.setDuration(durationTicks);
        m.setWait(waitTicks);

        if (!m.isComplete())
        {
            if (m.getDuration() == 0)
                m.setDuration(kDefaultDuration);

            // Mark it complete
            m.setComplete(true);
        }
    }

    return njams;
}

juce::MidiBuffer NJamLanguage::NJamVecToMIDI(std::vector<NJamMessage>& njamVec, double sampleRate, double bpm, int targetTicksPerQuarter)
{
    if (sampleRate <= 0.0 || bpm <= 0.0 || targetTicksPerQuarter <= 0)
        return {};

    const double samplesPerTick = (60.0 / bpm) * sampleRate / static_cast<double>(targetTicksPerQuarter);
    juce::MidiBuffer buffer;

    int currentSamplePosition = 0;
    // note that njam waits need to be accumulated in ordert to 
    for (const auto& m : njamVec)
    {
        // Wait is stored in ticks relative to the previous note-on; convert to samples and accumulate.
        currentSamplePosition += static_cast<int>(std::llround(m.getWait() * samplesPerTick));

        const int noteOnSample = currentSamplePosition;
        int durationSamples = static_cast<int>(std::llround(m.getDuration() * samplesPerTick));
        // cut short errouneously long notes... 
        if (durationSamples / sampleRate > 4) {durationSamples = sampleRate;}
        const int noteOffSample = noteOnSample + durationSamples;
        buffer.addEvent(juce::MidiMessage::noteOn(m.getChannel() + 1,
                                                  m.getPitch(),
                                                  static_cast<juce::uint8>(m.getVelocity())),
                        noteOnSample);
        buffer.addEvent(juce::MidiMessage::noteOff(m.getChannel() + 1,
                                                   m.getPitch()),
                        noteOffSample);
    }

    return buffer;
}

std::vector<NJamMessage> NJamLanguage::NJamStrToMessages(const std::string& njamStr)
{
    std::vector<NJamMessage> messages;
    std::istringstream iss(njamStr);
    std::string line;

    while (std::getline(iss, line))
    {
        // Skip empty or whitespace-only lines
        bool nonWhitespace = false;
        for (char c : line)
        {
            if (!std::isspace(static_cast<unsigned char>(c)))
            {
                nonWhitespace = true;
                break;
            }
        }
        if (!nonWhitespace)
            continue;

        // Parse tokens of the form k_v and require all keys.
        int pitch = 0, channel = 0, velocity = 0, duration = 0, wait = 0;
        bool hasP = false, hasC = false, hasV = false, hasD = false, hasW = false;

        std::istringstream ls(line);
        std::string token;
        while (ls >> token)
        {
            auto pos = token.find('_');
            if (pos == std::string::npos || pos == token.size() - 1)
                continue;

            const std::string key = token.substr(0, pos);
            const std::string valStr = token.substr(pos + 1);
            int val = 0;
            try { val = std::stoi(valStr); }
            catch (...) { continue; }

            if (key == "p")      { pitch = val; hasP = true; }
            else if (key == "c") { channel = val; hasC = true; }
            else if (key == "v") { velocity = val; hasV = true; }
            else if (key == "d") { duration = val; hasD = true; }
            else if (key == "w") { wait = val; hasW = true; }
        }

        if (hasP && hasC && hasV && hasD && hasW)
        {
            if (channel < 0 || channel > 15)
                channel = 0;
            if (velocity < 0 || velocity > 127)
                velocity = 64;
            if (pitch < 0 || pitch > 127)
                continue;

            messages.emplace_back(pitch, channel, static_cast<uint8>(velocity), duration, wait, 0, true);
        }
    }

    return messages;
}
