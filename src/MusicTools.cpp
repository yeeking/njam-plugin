#include "MusicTools.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>

namespace
{
const std::map<juce::String, int> gmInstruments {
    { "piano", 0 }, { "acoustic_grand_piano", 0 }, { "bright_piano", 1 },
    { "electric_piano", 4 }, { "rhodes", 4 }, { "harpsichord", 6 },
    { "glockenspiel", 9 }, { "vibraphone", 11 }, { "marimba", 12 },
    { "organ", 16 }, { "church_organ", 19 }, { "guitar", 24 },
    { "jazz_guitar", 26 }, { "electric_guitar", 27 }, { "bass", 32 },
    { "violin", 40 }, { "viola", 41 }, { "cello", 42 }, { "contrabass", 43 },
    { "strings", 48 }, { "string_ensemble", 48 }, { "choir", 52 },
    { "trumpet", 56 }, { "trombone", 57 }, { "tuba", 58 },
    { "french_horn", 60 }, { "brass", 61 }, { "sax", 65 },
    { "alto_sax", 65 }, { "tenor_sax", 66 }, { "oboe", 68 },
    { "bassoon", 70 }, { "clarinet", 71 }, { "flute", 73 },
    { "synth_lead", 80 }, { "synth_pad", 88 }, { "warm_pad", 89 },
    { "banjo", 105 }, { "koto", 107 }, { "steel_drums", 114 }
};

juce::String normaliseInstrumentName (juce::String text)
{
    return text.trim().toLowerCase().replaceCharacter (' ', '_');
}

juce::Array<juce::var>* arrayOf (const juce::var& value)
{
    return value.isArray() ? value.getArray() : nullptr;
}

struct FormulaContext
{
    double i = 0.0;
    double t = 0.0;
    double voice = 0.0;
    double section = 0.0;
    double localT = 0.0;
};

class FormulaParser
{
public:
    FormulaParser (juce::String expressionIn, FormulaContext contextIn)
        : expression (std::move (expressionIn)), context (contextIn)
    {
    }

    std::optional<double> parse()
    {
        pos = 0;
        auto value = parseExpression();
        skipSpaces();
        if (! value.has_value() || pos < expression.length())
            return std::nullopt;
        return value;
    }

private:
    void skipSpaces()
    {
        while (pos < expression.length() && std::isspace (static_cast<unsigned char> (expression[pos])))
            ++pos;
    }

    bool consume (juce::juce_wchar c)
    {
        skipSpaces();
        if (pos < expression.length() && expression[pos] == c)
        {
            ++pos;
            return true;
        }
        return false;
    }

    std::optional<double> parseExpression()
    {
        return parseLogicalOr();
    }

    bool consumeText (const char* text)
    {
        skipSpaces();
        const auto token = juce::String (text);
        if (expression.substring (pos, pos + token.length()) == token)
        {
            pos += token.length();
            return true;
        }
        return false;
    }

    std::optional<double> parseLogicalOr()
    {
        auto value = parseLogicalAnd();
        while (value.has_value())
        {
            if (consumeText ("||"))
            {
                auto rhs = parseLogicalAnd();
                if (! rhs.has_value()) return std::nullopt;
                *value = (std::abs (*value) > 1.0e-9 || std::abs (*rhs) > 1.0e-9) ? 1.0 : 0.0;
            }
            else
            {
                break;
            }
        }
        return value;
    }

    std::optional<double> parseLogicalAnd()
    {
        auto value = parseTernary();
        while (value.has_value())
        {
            if (consumeText ("&&"))
            {
                auto rhs = parseTernary();
                if (! rhs.has_value()) return std::nullopt;
                *value = (std::abs (*value) > 1.0e-9 && std::abs (*rhs) > 1.0e-9) ? 1.0 : 0.0;
            }
            else
            {
                break;
            }
        }
        return value;
    }

    std::optional<double> parseTernary()
    {
        auto condition = parseComparison();
        if (! condition.has_value())
            return std::nullopt;

        if (! consume ('?'))
            return condition;

        auto trueValue = parseExpression();
        if (! trueValue.has_value() || ! consume (':'))
            return std::nullopt;

        auto falseValue = parseExpression();
        if (! falseValue.has_value())
            return std::nullopt;

        return std::abs (*condition) > 1.0e-9 ? *trueValue : *falseValue;
    }

    std::optional<double> parseComparison()
    {
        auto value = parseAddSub();
        while (value.has_value())
        {
            if (consumeText (">="))
            {
                auto rhs = parseAddSub();
                if (! rhs.has_value()) return std::nullopt;
                *value = *value >= *rhs ? 1.0 : 0.0;
            }
            else if (consumeText ("<="))
            {
                auto rhs = parseAddSub();
                if (! rhs.has_value()) return std::nullopt;
                *value = *value <= *rhs ? 1.0 : 0.0;
            }
            else if (consumeText ("=="))
            {
                auto rhs = parseAddSub();
                if (! rhs.has_value()) return std::nullopt;
                *value = std::abs (*value - *rhs) < 1.0e-9 ? 1.0 : 0.0;
            }
            else if (consumeText ("!="))
            {
                auto rhs = parseAddSub();
                if (! rhs.has_value()) return std::nullopt;
                *value = std::abs (*value - *rhs) >= 1.0e-9 ? 1.0 : 0.0;
            }
            else if (consume ('>'))
            {
                auto rhs = parseAddSub();
                if (! rhs.has_value()) return std::nullopt;
                *value = *value > *rhs ? 1.0 : 0.0;
            }
            else if (consume ('<'))
            {
                auto rhs = parseAddSub();
                if (! rhs.has_value()) return std::nullopt;
                *value = *value < *rhs ? 1.0 : 0.0;
            }
            else
            {
                break;
            }
        }
        return value;
    }

    std::optional<double> parseAddSub()
    {
        auto value = parseMulDiv();
        while (value.has_value())
        {
            if (consume ('+'))
            {
                auto rhs = parseMulDiv();
                if (! rhs.has_value()) return std::nullopt;
                *value += *rhs;
            }
            else if (consume ('-'))
            {
                auto rhs = parseMulDiv();
                if (! rhs.has_value()) return std::nullopt;
                *value -= *rhs;
            }
            else
            {
                break;
            }
        }
        return value;
    }

    std::optional<double> parseMulDiv()
    {
        auto value = parseUnary();
        while (value.has_value())
        {
            if (consume ('*'))
            {
                auto rhs = parseUnary();
                if (! rhs.has_value()) return std::nullopt;
                *value *= *rhs;
            }
            else if (consume ('/'))
            {
                auto rhs = parseUnary();
                if (! rhs.has_value() || std::abs (*rhs) < 1.0e-9) return std::nullopt;
                *value /= *rhs;
            }
            else if (consume ('%'))
            {
                auto rhs = parseUnary();
                if (! rhs.has_value() || std::abs (*rhs) < 1.0e-9) return std::nullopt;
                *value = std::fmod (*value, *rhs);
            }
            else
            {
                break;
            }
        }
        return value;
    }

    std::optional<double> parseUnary()
    {
        if (consume ('+'))
            return parseUnary();
        if (consume ('-'))
            if (auto value = parseUnary())
                return -*value;
        if (consume ('!'))
            if (auto value = parseUnary())
                return std::abs (*value) <= 1.0e-9 ? 1.0 : 0.0;
        return parsePrimary();
    }

    std::optional<double> parsePrimary()
    {
        skipSpaces();
        if (consume ('('))
        {
            auto value = parseExpression();
            if (! consume (')'))
                return std::nullopt;
            return value;
        }

        if (pos < expression.length() && (std::isdigit (static_cast<unsigned char> (expression[pos])) || expression[pos] == '.'))
            return parseNumber();

        if (consume ('['))
            return parseArrayLookup();

        if (pos < expression.length() && (std::isalpha (static_cast<unsigned char> (expression[pos])) || expression[pos] == '_'))
            return parseIdentifierOrFunction();

        return std::nullopt;
    }

    std::optional<double> parseArrayLookup()
    {
        std::vector<double> values;
        if (! consume (']'))
        {
            while (true)
            {
                auto value = parseExpression();
                if (! value.has_value())
                    return std::nullopt;
                values.push_back (*value);

                if (consume (']'))
                    break;
                if (! consume (','))
                    return std::nullopt;
            }
        }

        if (values.empty() || ! consume ('['))
            return std::nullopt;

        auto indexValue = parseExpression();
        if (! indexValue.has_value() || ! consume (']'))
            return std::nullopt;

        const auto rawIndex = static_cast<int> (std::floor (*indexValue));
        const auto wrappedIndex = ((rawIndex % static_cast<int> (values.size())) + static_cast<int> (values.size()))
            % static_cast<int> (values.size());
        return values[static_cast<size_t> (wrappedIndex)];
    }

    std::optional<double> parseNumber()
    {
        const int start = pos;
        while (pos < expression.length()
               && (std::isdigit (static_cast<unsigned char> (expression[pos])) || expression[pos] == '.'))
            ++pos;

        return expression.substring (start, pos).getDoubleValue();
    }

    std::optional<double> parseIdentifierOrFunction()
    {
        const int start = pos;
        while (pos < expression.length()
               && (std::isalnum (static_cast<unsigned char> (expression[pos]))
                   || expression[pos] == '_'
                   || expression[pos] == ':'))
            ++pos;

        auto name = expression.substring (start, pos).trim();
        if (name.startsWith ("std::"))
            name = name.substring (5);

        if (! consume ('('))
            return variableValue (name);

        std::vector<double> args;
        if (! consume (')'))
        {
            while (true)
            {
                auto arg = parseExpression();
                if (! arg.has_value())
                    return std::nullopt;
                args.push_back (*arg);

                if (consume (')'))
                    break;
                if (! consume (','))
                    return std::nullopt;
            }
        }

        return functionValue (name, args);
    }

    std::optional<double> variableValue (const juce::String& name) const
    {
        if (name == "i" || name == "n") return context.i;
        if (name == "t" || name == "beat") return context.t;
        if (name == "local_t" || name == "lt" || name == "section_t") return context.localT;
        if (name == "voice" || name == "v") return context.voice;
        if (name == "section" || name == "s") return context.section;
        if (name == "pi") return juce::MathConstants<double>::pi;
        if (name == "tau") return juce::MathConstants<double>::twoPi;
        if (name == "e") return juce::MathConstants<double>::euler;
        if (name == "kick" || name == "bass_drum") return 36.0;
        if (name == "rim" || name == "side_stick") return 37.0;
        if (name == "snare") return 38.0;
        if (name == "hand_clap" || name == "clap") return 39.0;
        if (name == "closed_hat" || name == "hat_closed") return 42.0;
        if (name == "open_hat" || name == "hat_open") return 46.0;
        if (name == "low_tom") return 45.0;
        if (name == "high_tom") return 50.0;
        if (name == "cowbell") return 56.0;
        if (name == "bongo_high") return 60.0;
        if (name == "bongo_low") return 61.0;
        if (name == "conga_high" || name == "conga_mute") return 62.0;
        if (name == "conga_low" || name == "conga_open") return 64.0;
        if (name == "timbale_high") return 65.0;
        if (name == "timbale_low") return 66.0;
        if (name == "agogo_high") return 67.0;
        if (name == "agogo_low") return 68.0;
        if (name == "shaker" || name == "maracas") return 70.0;
        if (name == "clave") return 75.0;
        return std::nullopt;
    }

    static std::optional<double> functionValue (const juce::String& name, const std::vector<double>& args)
    {
        auto unary = [&] (double (*fn) (double)) -> std::optional<double>
        {
            if (args.size() != 1) return std::nullopt;
            return fn (args[0]);
        };

        auto euclideanHit = [&]() -> std::optional<double>
        {
            if (args.size() != 3 && args.size() != 4)
                return std::nullopt;

            const int pulses = juce::jlimit (0, 1024, juce::roundToInt (args[0]));
            const int steps = juce::jlimit (1, 1024, juce::roundToInt (args[1]));
            if (pulses <= 0)
                return 0.0;
            if (pulses >= steps)
                return 1.0;

            const int index = juce::roundToInt (std::floor (args[2]));
            const int rotation = args.size() == 4 ? juce::roundToInt (std::floor (args[3])) : 0;
            const int wrapped = ((index + rotation) % steps + steps) % steps;
            return ((wrapped * pulses) % steps) < pulses ? 1.0 : 0.0;
        };

        auto deterministicUnit = [] (double index, double seed)
        {
            const double value = std::sin ((index + 1.0) * 12.9898 + (seed + 1.0) * 78.233) * 43758.5453123;
            return value - std::floor (value);
        };

        if (name == "sin") return unary (std::sin);
        if (name == "cos") return unary (std::cos);
        if (name == "tan") return unary (std::tan);
        if (name == "abs") return unary (std::abs);
        if (name == "floor") return unary (std::floor);
        if (name == "ceil") return unary (std::ceil);
        if (name == "round") return unary (std::round);
        if (name == "sqrt") return unary (std::sqrt);

        if (name == "pow" && args.size() == 2) return std::pow (args[0], args[1]);
        if (name == "min" && args.size() == 2) return std::min (args[0], args[1]);
        if (name == "max" && args.size() == 2) return std::max (args[0], args[1]);
        if (name == "clamp" && args.size() == 3) return juce::jlimit (args[1], args[2], args[0]);
        if ((name == "cycle" || name == "choose") && args.size() >= 2)
        {
            const int valueCount = static_cast<int> (args.size()) - 1;
            const int rawIndex = static_cast<int> (std::floor (args[0]));
            const int wrappedIndex = ((rawIndex % valueCount) + valueCount) % valueCount;
            return args[static_cast<size_t> (wrappedIndex + 1)];
        }
        if (name == "fract" && args.size() == 1) return args[0] - std::floor (args[0]);
        if ((name == "mod" || name == "fmod") && args.size() == 2 && std::abs (args[1]) > 1.0e-9)
            return std::fmod (args[0], args[1]);
        if (name == "euclid" || name == "euclidean") return euclideanHit();
        if ((name == "rand" || name == "noise") && (args.size() == 1 || args.size() == 2))
            return deterministicUnit (args[0], args.size() == 2 ? args[1] : 0.0);
        if (name == "chance" && (args.size() == 2 || args.size() == 3))
            return deterministicUnit (args[1], args.size() == 3 ? args[2] : 0.0) < juce::jlimit (0.0, 1.0, args[0]) ? 1.0 : 0.0;
        if (name == "pick" && args.size() == 3)
            return deterministicUnit (args[0], args[1]) < 0.5 ? 0.0 : args[2];
        if (name == "ifgt" && args.size() == 4) return args[0] > args[1] ? args[2] : args[3];
        if (name == "iflt" && args.size() == 4) return args[0] < args[1] ? args[2] : args[3];
        if (name == "ifeq" && args.size() == 4) return std::abs (args[0] - args[1]) < 1.0e-9 ? args[2] : args[3];

        return std::nullopt;
    }

    juce::String expression;
    FormulaContext context;
    int pos = 0;
};

std::optional<double> evaluateFormula (juce::String expression, FormulaContext context)
{
    return FormulaParser (std::move (expression), context).parse();
}

int degreeToMidi (int degree, int root, const std::vector<int>& scale)
{
    if (scale.empty())
        return juce::jlimit (0, 127, root + degree);

    const int scaleSize = static_cast<int> (scale.size());
    const int octave = degree >= 0 ? degree / scaleSize : ((degree + 1) / scaleSize) - 1;
    const int wrapped = ((degree % scaleSize) + scaleSize) % scaleSize;
    return juce::jlimit (0, 127, root + (12 * octave) + scale[static_cast<size_t> (wrapped)]);
}

void copyProperties (const juce::DynamicObject& source, juce::DynamicObject& destination)
{
    const auto& properties = source.getProperties();
    for (int i = 0; i < properties.size(); ++i)
        destination.setProperty (properties.getName (i), properties.getValueAt (i));
}
}

MusicToolRegistry::MusicToolRegistry()
{
    tools["generate_midi"] = [this] (const juce::var& args) { return generateMidi (args); };
    tools["generate_chord_midi"] = [this] (const juce::var& args) { return generateChordMidi (args); };
    tools["generate_polyphonic_midi"] = [this] (const juce::var& args) { return generatePolyphonicMidi (args); };
    tools["generate_formula_midi"] = [this] (const juce::var& args) { return generateFormulaMidi (args); };
    tools["combine_midi"] = [this] (const juce::var& args) { return combineMidi (args); };
    tools["analyze_midi"] = [this] (const juce::var& args) { return analyzeMidi (args); };
    tools["fit_midi_register"] = [this] (const juce::var& args) { return fitMidiRegister (args); };
    tools["fit_midi_pitch_range"] = [this] (const juce::var& args) { return fitMidiPitchRange (args); };
    tools["fit_midi_duration"] = [this] (const juce::var& args) { return fitMidiDuration (args); };
    tools["humanize_midi"] = [this] (const juce::var& args) { return humanizeMidi (args); };
    tools["play_midi"] = [this] (const juce::var& args) { return playMidi (args); };
}

ToolResult MusicToolRegistry::call (const ToolInvocation& invocation, AgentStatusCallback statusCallback)
{
    const auto toolName = invocation.name.trim();
    if (statusCallback)
        statusCallback ({ "calling tool", toolName });

    const auto it = tools.find (toolName);
    if (it == tools.end())
        return { false, "Unknown tool: " + toolName };

    return it->second (invocation.args);
}

juce::String MusicToolRegistry::describeTools() const
{
    return "Tools are called by returning one or more XML-ish tags like "
           "<tool name=\"generate_midi\">{\"note_seq\":[\"C4\",\"E4\"],\"inter_onset_intervals\":[1,1],\"instrument\":\"piano\",\"tempo\":120}</tool>.\n"
           "Available tools: classical_agent_tool, free_jazz_agent_tool, style_blueprint_agent_tool, mathematical_music_agent_tool, generate_midi, generate_chord_midi, "
           "generate_polyphonic_midi, generate_formula_midi, combine_midi, analyze_midi, fit_midi_register, fit_midi_pitch_range, fit_midi_duration, humanize_midi, play_midi. generate_formula_midi can use either top-level voices or sections, "
           "where each section has a start, steps, and its own voices/lambdas. It also supports compact reusable motifs: "
           "{\"motifs\":[{\"name\":\"bass\",\"pitch_lambda\":\"...\",\"rhythm_lambda\":\"...\"}],"
           "\"sections\":[{\"voices\":[{\"motif\":\"bass\",\"channel\":1}]}]}. "
           "Formula voices can also use chord_offsets, swing/swing_grid, echoes/echo_delay/echo_transpose/echo_velocity_decay, euclid(pulses,steps,i[,rotation]), and deterministic rand/chance/pick helpers for compact chords, grooves, canons, polymeters, and repeatable variation.";
}

juce::Array<juce::String> MusicToolRegistry::getToolNames() const
{
    juce::Array<juce::String> names;
    for (const auto& [name, _] : tools)
        names.add (name);
    return names;
}

void MusicToolRegistry::setMidiPlaybackHandler (MidiPlaybackHandler handler)
{
    midiPlaybackHandler = std::move (handler);
}

int MusicToolRegistry::noteToMidi (const juce::var& note)
{
    if (note.isInt() || note.isInt64() || note.isDouble())
        return juce::jlimit (0, 127, static_cast<int> (note));

    auto text = note.toString().trim();
    if (text.containsOnly ("0123456789"))
        return juce::jlimit (0, 127, text.getIntValue());

    const auto letter = text.substring (0, 1).toUpperCase();
    static const std::map<juce::String, int> pitchClasses {
        { "C", 0 }, { "D", 2 }, { "E", 4 }, { "F", 5 }, { "G", 7 }, { "A", 9 }, { "B", 11 }
    };

    auto it = pitchClasses.find (letter);
    if (it == pitchClasses.end())
        return 60;

    int index = 1;
    int pitch = it->second;
    if (text.length() > 1)
    {
        const auto accidental = text.substring (1, 2);
        if (accidental == "#") { ++pitch; ++index; }
        else if (accidental.equalsIgnoreCase ("b")) { --pitch; ++index; }
    }

    const int octave = text.substring (index).getIntValue();
    return juce::jlimit (0, 127, (octave + 1) * 12 + pitch);
}

std::vector<int> MusicToolRegistry::notesFromVar (const juce::var& notes)
{
    std::vector<int> result;
    if (auto* array = arrayOf (notes))
        for (const auto& note : *array)
            result.push_back (noteToMidi (note));
    else if (! notes.isVoid())
        result.push_back (noteToMidi (notes));

    return result;
}

std::vector<double> MusicToolRegistry::numbersFromVar (const juce::var& values)
{
    std::vector<double> result;
    if (auto* array = arrayOf (values))
        for (const auto& value : *array)
            result.push_back (static_cast<double> (value));
    return result;
}

int MusicToolRegistry::instrumentToProgram (const juce::var& instrument)
{
    if (instrument.isInt() || instrument.isInt64())
        return juce::jlimit (0, 127, static_cast<int> (instrument));

    const auto key = normaliseInstrumentName (instrument.toString().isEmpty() ? "piano" : instrument.toString());
    const auto it = gmInstruments.find (key);
    return it != gmInstruments.end() ? it->second : 0;
}

juce::String MusicToolRegistry::expressionFromLambdaLikeText (juce::String text)
{
    text = text.trim();
    const int returnIndex = text.indexOf ("return");
    if (returnIndex >= 0)
    {
        text = text.substring (returnIndex + 6);
        const int semicolon = text.indexOfChar (';');
        if (semicolon >= 0)
            text = text.substring (0, semicolon);
    }
    else
    {
        const int arrow = text.indexOf ("=>");
        if (arrow >= 0)
            text = text.substring (arrow + 2);
    }

    text = text.trim().trimCharactersAtStart ("{").trimCharactersAtEnd (";}").trim();

    int parenBalance = 0;
    int bracketBalance = 0;
    for (int i = 0; i < text.length(); ++i)
    {
        if (text[i] == '(') ++parenBalance;
        else if (text[i] == ')') --parenBalance;
        else if (text[i] == '[') ++bracketBalance;
        else if (text[i] == ']') --bracketBalance;
    }

    if (bracketBalance == 0 && parenBalance > 0 && parenBalance <= 2)
        text << juce::String::repeatedString (")", parenBalance);

    return text;
}

juce::File MusicToolRegistry::generatedMidiDirectory()
{
    auto canWriteTo = [] (const juce::File& dir)
    {
        if (dir.createDirectory().failed())
            return false;

        const auto probe = dir.getChildFile (".write_test");
        bool ok = false;
        {
            juce::FileOutputStream stream (probe);
            ok = stream.openedOk();
            stream.flush();
        }
        probe.deleteFile();
        return ok;
    };

    auto dir = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                   .getChildFile ("MusicAgentTeam")
                   .getChildFile ("generated_midis");

    if (! canWriteTo (dir))
    {
        dir = juce::File::getSpecialLocation (juce::File::tempDirectory)
                  .getChildFile ("MusicAgentTeam")
                  .getChildFile ("generated_midis");
        canWriteTo (dir);
    }

    return dir;
}

juce::File MusicToolRegistry::makeOutputFile (const juce::String& prefix)
{
    const auto dir = generatedMidiDirectory();
    const auto now = juce::Time::getCurrentTime();
    const auto uniqueSuffix = juce::String (juce::Time::getMillisecondCounterHiRes(), 3)
                                  .retainCharacters ("0123456789")
                              + "_" + juce::String::toHexString (juce::Random::getSystemRandom().nextInt());
    return dir.getChildFile (prefix + "_" + now.formatted ("%Y%m%d_%H%M%S") + "_" + uniqueSuffix + ".mid");
}

juce::File MusicToolRegistry::resolveMidiFile (juce::String path)
{
    path = path.trim().unquoted();
    if (path.isEmpty())
        return {};

    const bool absolutePath = path.startsWithChar (juce::File::getSeparatorChar())
        || path.startsWithChar ('~')
        || path.contains (":");
    if (absolutePath)
        return juce::File (path);

    const auto generated = generatedMidiDirectory().getChildFile (path);
    if (generated.existsAsFile())
        return generated;

    return juce::File::getCurrentWorkingDirectory().getChildFile (path);
}

void MusicToolRegistry::addNote (juce::MidiMessageSequence& sequence,
                                 int note,
                                 double startBeat,
                                 double durationBeat,
                                 int velocity,
                                 int channel)
{
    constexpr double ticksPerBeat = 960.0;
    const auto startTick = startBeat * ticksPerBeat;
    const auto endTick = std::max (startTick + 1.0, (startBeat + durationBeat) * ticksPerBeat);
    sequence.addEvent (juce::MidiMessage::noteOn (channel, note, static_cast<juce::uint8> (juce::jlimit (1, 127, velocity))), startTick);
    sequence.addEvent (juce::MidiMessage::noteOff (channel, note), endTick);
}

bool MusicToolRegistry::writeMidiFile (const juce::MidiMessageSequence& sequence,
                                       const juce::File& outputFile,
                                       int tempo)
{
    juce::MidiFile midi;
    midi.setTicksPerQuarterNote (960);
    juce::MidiMessageSequence track;
    track.addEvent (juce::MidiMessage::tempoMetaEvent (juce::roundToInt (60000000.0 / std::max (1, tempo))), 0.0);
    for (int i = 0; i < sequence.getNumEvents(); ++i)
    {
        auto message = sequence.getEventPointer (i)->message;
        message.setTimeStamp (sequence.getEventTime (i));
        track.addEvent (message, 0.0);
    }
    track.updateMatchedPairs();
    midi.addTrack (track);

    outputFile.deleteFile();
    juce::FileOutputStream stream (outputFile);
    return stream.openedOk() && midi.writeTo (stream);
}

int MusicToolRegistry::tempoFromMidiFile (const juce::MidiFile& midi, int fallback)
{
    for (int trackIndex = 0; trackIndex < midi.getNumTracks(); ++trackIndex)
    {
        if (auto* track = midi.getTrack (trackIndex))
        {
            for (int eventIndex = 0; eventIndex < track->getNumEvents(); ++eventIndex)
            {
                const auto message = track->getEventPointer (eventIndex)->message;
                if (message.isTempoMetaEvent())
                {
                    const double secondsPerQuarter = message.getTempoSecondsPerQuarterNote();
                    if (secondsPerQuarter > 0.0)
                        return juce::jlimit (1, 400, juce::roundToInt (60.0 / secondsPerQuarter));
                }
            }
        }
    }

    return fallback;
}

double MusicToolRegistry::getNumberProperty (const juce::DynamicObject& object,
                                             const juce::Identifier& key,
                                             double fallback)
{
    if (! object.hasProperty (key))
        return fallback;
    return static_cast<double> (object.getProperty (key));
}

ToolResult MusicToolRegistry::generateMidi (const juce::var& args)
{
    auto* object = args.getDynamicObject();
    if (object == nullptr)
        return { false, "generate_midi expects a JSON object." };

    const auto notes = notesFromVar (object->getProperty ("note_seq"));
    if (notes.empty())
        return { false, "No notes were supplied." };

    const auto intervals = numbersFromVar (object->getProperty ("inter_onset_intervals"));
    const int tempo = static_cast<int> (getNumberProperty (*object, "tempo", 120));
    const int program = instrumentToProgram (object->getProperty ("instrument"));

    juce::MidiMessageSequence sequence;
    sequence.addEvent (juce::MidiMessage::programChange (1, program), 0.0);
    double start = 0.0;
    for (size_t i = 0; i < notes.size(); ++i)
    {
        const double duration = intervals.empty() ? 1.0 : std::max (0.1, intervals[i % intervals.size()]);
        addNote (sequence, notes[i], start, duration, 90, 1);
        start += duration;
    }

    const auto outputFile = makeOutputFile ("generated");
    if (! writeMidiFile (sequence, outputFile, tempo))
        return { false, "Could not write MIDI file." };
    return { true, outputFile.getFullPathName() };
}

ToolResult MusicToolRegistry::generateChordMidi (const juce::var& args)
{
    auto* object = args.getDynamicObject();
    auto* chords = object != nullptr ? arrayOf (object->getProperty ("chord_seq")) : nullptr;
    if (object == nullptr || chords == nullptr || chords->isEmpty())
        return { false, "generate_chord_midi expects chord_seq as a non-empty array." };

    const auto intervals = numbersFromVar (object->getProperty ("chord_intervals"));
    const int tempo = static_cast<int> (getNumberProperty (*object, "tempo", 120));
    const int program = instrumentToProgram (object->getProperty ("instrument"));

    juce::MidiMessageSequence sequence;
    sequence.addEvent (juce::MidiMessage::programChange (1, program), 0.0);
    double start = 0.0;
    for (int i = 0; i < chords->size(); ++i)
    {
        const double duration = intervals.empty() ? 1.0 : std::max (0.1, intervals[static_cast<size_t> (i) % intervals.size()]);
        for (auto note : notesFromVar ((*chords)[i]))
            addNote (sequence, note, start, duration, 82, 1);
        start += duration;
    }

    const auto outputFile = makeOutputFile ("chords");
    if (! writeMidiFile (sequence, outputFile, tempo))
        return { false, "Could not write MIDI file." };
    return { true, outputFile.getFullPathName() };
}

ToolResult MusicToolRegistry::generatePolyphonicMidi (const juce::var& args)
{
    auto* object = args.getDynamicObject();
    auto* voices = object != nullptr ? arrayOf (object->getProperty ("voices")) : nullptr;
    if (object == nullptr || voices == nullptr || voices->isEmpty())
        return { false, "generate_polyphonic_midi expects voices as a non-empty array." };

    const int tempo = static_cast<int> (getNumberProperty (*object, "tempo", 120));
    juce::MidiMessageSequence sequence;
    for (int v = 0; v < voices->size(); ++v)
    {
        auto* voice = (*voices)[v].getDynamicObject();
        if (voice == nullptr)
            continue;

        const int channel = juce::jlimit (1, 16, static_cast<int> (getNumberProperty (*voice, "channel", v + 1)) + 1);
        const int program = instrumentToProgram (voice->getProperty ("instrument"));
        const double voiceStart = getNumberProperty (*voice, "start", 0.0);
        const auto intervals = numbersFromVar (voice->getProperty ("intervals"));
        sequence.addEvent (juce::MidiMessage::programChange (channel, program), voiceStart * 960.0);

        auto* notes = arrayOf (voice->getProperty ("notes"));
        if (notes == nullptr)
            continue;

        double start = voiceStart;
        const bool chords = voice->getProperty ("type").toString() == "chords";
        for (int i = 0; i < notes->size(); ++i)
        {
            const double duration = intervals.empty() ? 1.0 : std::max (0.1, intervals[static_cast<size_t> (i) % intervals.size()]);
            if (chords)
                for (auto note : notesFromVar ((*notes)[i]))
                    addNote (sequence, note, start, duration, 82, channel);
            else
                for (auto note : notesFromVar ((*notes)[i]))
                    addNote (sequence, note, start, duration, 90, channel);
            start += duration;
        }
    }

    const auto outputFile = makeOutputFile ("polyphonic");
    if (! writeMidiFile (sequence, outputFile, tempo))
        return { false, "Could not write MIDI file." };
    return { true, outputFile.getFullPathName() };
}

ToolResult MusicToolRegistry::generateFormulaMidi (const juce::var& args)
{
    auto* object = args.getDynamicObject();
    if (object == nullptr)
        return { false, "generate_formula_midi expects a JSON object." };

    auto* voices = arrayOf (object->getProperty ("voices"));
    auto* sections = arrayOf (object->getProperty ("sections"));
    auto* motifs = arrayOf (object->getProperty ("motifs"));
    if ((voices == nullptr || voices->isEmpty()) && (sections == nullptr || sections->isEmpty()))
        return { false, "generate_formula_midi expects voices or sections as a non-empty array. Optional motifs can be referenced by those voices." };

    const auto backend = object->getProperty ("backend").toString().trim().isEmpty()
        ? juce::String ("expr")
        : object->getProperty ("backend").toString().trim().toLowerCase();
    if (backend != "expr")
        return { false, "Unsupported formula backend '" + backend
                      + "'. Supported backend: expr. JavaScript/QuickJS and C++ JIT backends are planned but not enabled in this plugin build." };

    const int tempo = juce::jlimit (20, 300, static_cast<int> (getNumberProperty (*object, "tempo", 120)));
    const int defaultSteps = juce::jlimit (1, 4096, static_cast<int> (getNumberProperty (*object, "steps", 64)));
    const double defaultDuration = juce::jlimit (0.02, 16.0, getNumberProperty (*object, "duration", 0.85));
    const int root = juce::jlimit (0, 127, static_cast<int> (getNumberProperty (*object, "root", 60)));
    std::vector<int> defaultScale = notesFromVar (object->getProperty ("scale"));
    if (defaultScale.empty())
        defaultScale = { 0, 2, 3, 5, 7, 8, 10 };

    std::map<juce::String, const juce::DynamicObject*> motifMap;
    if (motifs != nullptr)
    {
        for (const auto& motifValue : *motifs)
        {
            if (auto* motif = motifValue.getDynamicObject())
            {
                const auto name = motif->getProperty ("name").toString().trim();
                if (name.isNotEmpty())
                    motifMap[name] = motif;
            }
        }
    }

    auto resolveVoice = [&] (const juce::DynamicObject& voice) -> juce::DynamicObject::Ptr
    {
        juce::DynamicObject::Ptr resolved = new juce::DynamicObject();
        const auto motifName = voice.getProperty ("motif").toString().trim();
        if (motifName.isNotEmpty())
        {
            const auto it = motifMap.find (motifName);
            if (it == motifMap.end())
                return {};
            copyProperties (*it->second, *resolved);
        }
        copyProperties (voice, *resolved);
        return resolved;
    };

    juce::MidiMessageSequence sequence;
    int generatedNotes = 0;
    int renderedSections = 0;
    int renderedVoices = 0;
    double lastBeat = 0.0;

    auto renderVoices = [&] (juce::Array<juce::var>* sectionVoices,
                             const juce::DynamicObject* section,
                             double sectionStart,
                             int sectionIndex) -> ToolResult
    {
        if (sectionVoices == nullptr || sectionVoices->isEmpty())
            return { false, "Each formula section needs voices as a non-empty array." };
        ++renderedSections;

        const int sectionSteps = section != nullptr
            ? juce::jlimit (1, 4096, static_cast<int> (getNumberProperty (*section, "steps", defaultSteps)))
            : defaultSteps;
        const double sectionDuration = section != nullptr
            ? juce::jlimit (0.02, 16.0, getNumberProperty (*section, "duration", defaultDuration))
            : defaultDuration;
        const int sectionRoot = section != nullptr
            ? juce::jlimit (0, 127, static_cast<int> (getNumberProperty (*section, "root", root)))
            : root;
        const auto sectionBackend = section != nullptr && section->hasProperty ("backend")
            ? section->getProperty ("backend").toString().trim().toLowerCase()
            : backend;
        if (sectionBackend != "expr")
            return { false, "Unsupported formula section backend '" + sectionBackend
                          + "'. Supported backend: expr. JavaScript/QuickJS and C++ JIT backends are planned but not enabled in this plugin build." };

        auto sectionScale = section != nullptr ? notesFromVar (section->getProperty ("scale")) : std::vector<int>();
        if (sectionScale.empty())
            sectionScale = defaultScale;

        for (int voiceIndex = 0; voiceIndex < sectionVoices->size(); ++voiceIndex)
        {
            auto* voice = (*sectionVoices)[voiceIndex].getDynamicObject();
            if (voice == nullptr)
                continue;
            auto resolvedVoice = resolveVoice (*voice);
            if (resolvedVoice == nullptr)
                return { false, "Unknown formula motif referenced by voice: " + voice->getProperty ("motif").toString() };
            voice = resolvedVoice.get();
            ++renderedVoices;

            const auto pitchExpression = expressionFromLambdaLikeText (voice->getProperty ("pitch_lambda").toString());
            const auto rhythmExpression = expressionFromLambdaLikeText (voice->getProperty ("rhythm_lambda").toString());
            const auto gateExpression = expressionFromLambdaLikeText (voice->getProperty ("gate_lambda").toString());
            const auto durationExpression = expressionFromLambdaLikeText (voice->getProperty ("duration_lambda").toString());
            const auto velocityExpression = expressionFromLambdaLikeText (voice->getProperty ("velocity_lambda").toString());

            if (pitchExpression.isEmpty() || rhythmExpression.isEmpty())
                return { false, "Each formula voice needs pitch_lambda and rhythm_lambda." };

            const int steps = juce::jlimit (1, 4096, static_cast<int> (getNumberProperty (*voice, "steps", sectionSteps)));
            const int channel = juce::jlimit (1, 16, static_cast<int> (getNumberProperty (*voice, "channel", voiceIndex + 1)));
            const int program = instrumentToProgram (voice->getProperty ("instrument"));
            const bool pitchIsMidi = voice->getProperty ("pitch_mode").toString().equalsIgnoreCase ("midi");
            const int voiceRoot = juce::jlimit (0, 127, static_cast<int> (getNumberProperty (*voice, "root", sectionRoot)));
            const double transpose = getNumberProperty (*voice, "transpose", 0.0);
            const double durationScale = juce::jlimit (0.05, 16.0, getNumberProperty (*voice, "duration_scale", 1.0));
            const double velocityOffset = getNumberProperty (*voice, "velocity_offset", 0.0);
            const double swing = juce::jlimit (0.0, 0.95, getNumberProperty (*voice, "swing", 0.0));
            const double swingGrid = juce::jlimit (0.0625, 4.0, getNumberProperty (*voice, "swing_grid", 0.5));
            const int echoes = juce::jlimit (0, 8, static_cast<int> (getNumberProperty (*voice, "echoes", 0.0)));
            const double echoDelay = juce::jlimit (0.0, 64.0, getNumberProperty (*voice, "echo_delay", 1.0));
            const int echoTranspose = juce::roundToInt (getNumberProperty (*voice, "echo_transpose", 0.0));
            const double echoVelocityDecay = juce::jlimit (0.0, 126.0, getNumberProperty (*voice, "echo_velocity_decay", 0.0));
            auto voiceScale = notesFromVar (voice->getProperty ("scale"));
            if (voiceScale.empty())
                voiceScale = sectionScale;
            auto chordOffsets = notesFromVar (voice->getProperty ("chord_offsets"));
            if (chordOffsets.empty())
                chordOffsets = { 0 };

            double beat = sectionStart + getNumberProperty (*voice, "start", 0.0);
            sequence.addEvent (juce::MidiMessage::programChange (channel, program), beat * 960.0);

            for (int i = 0; i < steps; ++i)
            {
                const FormulaContext context {
                    static_cast<double> (i),
                    beat,
                    static_cast<double> ((sectionIndex * 16) + voiceIndex),
                    static_cast<double> (sectionIndex),
                    beat - sectionStart
                };
                const auto pitchValue = evaluateFormula (pitchExpression, context);
                const auto rhythmValue = evaluateFormula (rhythmExpression, context);
                const auto durationValue = durationExpression.isEmpty()
                    ? std::optional<double> (sectionDuration)
                    : evaluateFormula (durationExpression, context);
                const auto velocityValue = velocityExpression.isEmpty()
                    ? std::optional<double> (90.0)
                    : evaluateFormula (velocityExpression, context);
                const auto gateValue = gateExpression.isEmpty()
                    ? std::optional<double> (1.0)
                    : evaluateFormula (gateExpression, context);

                auto formulaError = [&] (const juce::String& lambdaName, const juce::String& expression)
                {
                    return ToolResult { false,
                        "Could not evaluate " + lambdaName
                        + " in section " + juce::String (sectionIndex)
                        + ", voice " + juce::String (voiceIndex)
                        + ", step " + juce::String (i)
                        + ": " + expression.substring (0, 180) };
                };

                if (! pitchValue.has_value()) return formulaError ("pitch_lambda", pitchExpression);
                if (! rhythmValue.has_value()) return formulaError ("rhythm_lambda", rhythmExpression);
                if (! durationValue.has_value()) return formulaError ("duration_lambda", durationExpression);
                if (! velocityValue.has_value()) return formulaError ("velocity_lambda", velocityExpression);
                if (! gateValue.has_value()) return formulaError ("gate_lambda", gateExpression);

                if (std::abs (*gateValue) > 1.0e-9)
                {
                    const auto gridIndex = static_cast<int> (std::floor ((beat - sectionStart) / swingGrid + 1.0e-6));
                    const double swungBeat = beat + ((gridIndex % 2) != 0 ? swing * swingGrid : 0.0);
                    const int rawNote = pitchIsMidi
                        ? juce::roundToInt (*pitchValue)
                        : degreeToMidi (juce::roundToInt (*pitchValue), voiceRoot, voiceScale);
                    const double duration = juce::jlimit (0.02, 16.0, *durationValue * durationScale);
                    const int velocity = juce::jlimit (1, 127, juce::roundToInt (*velocityValue + velocityOffset));
                    for (const auto offset : chordOffsets)
                    {
                        const int baseNote = rawNote + juce::roundToInt (transpose) + offset;
                        for (int echoIndex = 0; echoIndex <= echoes; ++echoIndex)
                        {
                            const int note = juce::jlimit (0, 127, baseNote + (echoTranspose * echoIndex));
                            const double noteBeat = swungBeat + (echoDelay * echoIndex);
                            const int noteVelocity = juce::jlimit (1, 127, juce::roundToInt (velocity - (echoVelocityDecay * echoIndex)));
                            addNote (sequence, note, noteBeat, duration, noteVelocity, channel);
                            ++generatedNotes;
                            lastBeat = std::max (lastBeat, noteBeat + duration);
                        }
                    }
                }

                beat += juce::jlimit (0.02, 16.0, *rhythmValue);
            }
        }

        return { true, {} };
    };

    if (sections != nullptr && ! sections->isEmpty())
    {
        double inferredSectionStart = 0.0;
        for (int sectionIndex = 0; sectionIndex < sections->size(); ++sectionIndex)
        {
            auto* section = (*sections)[sectionIndex].getDynamicObject();
            if (section == nullptr)
                continue;

            const double sectionStart = getNumberProperty (*section, "start", inferredSectionStart);
            const auto result = renderVoices (arrayOf (section->getProperty ("voices")), section, sectionStart, sectionIndex);
            if (! result.ok)
                return result;

            const int sectionSteps = juce::jlimit (1, 4096, static_cast<int> (getNumberProperty (*section, "steps", defaultSteps)));
            inferredSectionStart = sectionStart + static_cast<double> (sectionSteps);
        }
    }
    else
    {
        const auto result = renderVoices (voices, nullptr, 0.0, 0);
        if (! result.ok)
            return result;
    }

    if (generatedNotes == 0)
        return { false, "Formula MIDI generated no notes." };

    const auto outputFile = makeOutputFile ("formula");
    if (! writeMidiFile (sequence, outputFile, tempo))
        return { false, "Could not write formula MIDI file." };

    return { true, outputFile.getFullPathName()
                   + " (" + juce::String (generatedNotes) + " formula notes, "
                   + juce::String (renderedVoices) + " voices, "
                   + juce::String (renderedSections) + " sections, backend="
                   + backend + ", " + juce::String (lastBeat, 2) + " beats)" };
}

ToolResult MusicToolRegistry::combineMidi (const juce::var& args)
{
    auto* object = args.getDynamicObject();
    auto* files = object != nullptr ? arrayOf (object->getProperty ("midi_files")) : nullptr;
    if (object == nullptr || files == nullptr || files->isEmpty())
        return { false, "No MIDI files were supplied." };

    juce::MidiMessageSequence combined;
    double offset = 0.0;
    const bool sequenceMode = object->getProperty ("mode").toString() == "sequence";
    int filesCombined = 0;
    int eventsCombined = 0;
    int noteOnsCombined = 0;
    int programChangesCombined = 0;
    std::map<int, int> notesByChannel;
    juce::StringArray sourceLengths;
    int outputTempo = 120;

    for (const auto& fileValue : *files)
    {
        const auto sourceFile = resolveMidiFile (fileValue.toString());
        if (! sourceFile.existsAsFile())
            return { false, "MIDI file not found for combine_midi: " + sourceFile.getFullPathName() };

        juce::FileInputStream stream (sourceFile);
        juce::MidiFile midi;
        if (! stream.openedOk() || ! midi.readFrom (stream))
            return { false, "Could not read MIDI file for combine_midi: " + sourceFile.getFullPathName() };
        if (filesCombined == 0)
            outputTempo = tempoFromMidiFile (midi, outputTempo);

        double lastTime = 0.0;
        int sourceNoteOns = 0;
        const double sourceTicksPerBeat = midi.getTimeFormat() > 0 ? static_cast<double> (midi.getTimeFormat()) : 960.0;
        const double tickScale = 960.0 / sourceTicksPerBeat;
        for (int t = 0; t < midi.getNumTracks(); ++t)
            if (auto* track = midi.getTrack (t))
                for (int e = 0; e < track->getNumEvents(); ++e)
                {
                    auto message = track->getEventPointer (e)->message;
                    const auto scaledTime = message.getTimeStamp() * tickScale;
                    message.setTimeStamp (0.0);
                    combined.addEvent (message, scaledTime + offset);
                    lastTime = std::max (lastTime, scaledTime);
                    if (message.isNoteOn())
                    {
                        ++noteOnsCombined;
                        ++sourceNoteOns;
                        ++notesByChannel[message.getChannel()];
                    }
                    else if (message.isProgramChange())
                    {
                        ++programChangesCombined;
                    }
                    ++eventsCombined;
                }

        sourceLengths.add (sourceFile.getFileName() + "=" + juce::String (lastTime / 960.0, 2) + "b/" + juce::String (sourceNoteOns) + "n");
        ++filesCombined;
        if (sequenceMode)
            offset += lastTime + 960.0;
    }

    if (eventsCombined == 0)
        return { false, "combine_midi found no events to combine." };

    const auto outputFile = makeOutputFile ("combined");
    if (! writeMidiFile (combined, outputFile, outputTempo))
        return { false, "Could not write combined MIDI file." };

    const auto durationBeats = combined.getEndTime() / 960.0;
    auto channelSummary = [] (const std::map<int, int>& values)
    {
        juce::String text;
        for (const auto& [channel, count] : values)
        {
            if (text.isNotEmpty())
                text << ",";
            text << channel << ":" << count;
        }
        return text;
    };

    return { true, outputFile.getFullPathName()
                   + " (" + juce::String (filesCombined) + " files, "
                   + juce::String (eventsCombined) + " events, "
                   + juce::String (noteOnsCombined) + " notes, "
                   + juce::String (programChangesCombined) + " programs, "
                   + "by_channel={" + channelSummary (notesByChannel) + "}, "
                   + (sequenceMode ? "sequence" : "overlay") + ", "
                   + "tempo=" + juce::String (outputTempo) + ", "
                   + "sources=[" + sourceLengths.joinIntoString (";") + "], "
                   + juce::String (durationBeats, 2) + " beats)" };
}

ToolResult MusicToolRegistry::analyzeMidi (const juce::var& args)
{
    const auto file = args.getDynamicObject() != nullptr
        ? resolveMidiFile (args.getDynamicObject()->getProperty ("midi_file").toString())
        : juce::File();

    if (! file.existsAsFile())
        return { false, "MIDI file not found for analyze_midi: " + file.getFullPathName() };

    juce::FileInputStream stream (file);
    juce::MidiFile midi;
    if (! stream.openedOk() || ! midi.readFrom (stream))
        return { false, "Could not read MIDI file for analyze_midi: " + file.getFullPathName() };
    const int sourceTempo = tempoFromMidiFile (midi, 120);

    int noteOns = 0;
    int noteOffs = 0;
    int programChanges = 0;
    int minNote = 128;
    int maxNote = -1;
    int pitchedMinNote = 128;
    int pitchedMaxNote = -1;
    int pitchedNoteOns = 0;
    double firstTick = std::numeric_limits<double>::max();
    double lastTick = 0.0;
    bool sawNote = false;
    std::map<int, int> notesByChannel;
    std::map<int, int> pitchClasses;
    std::vector<double> starts;
    std::set<int> velocities;

    const double ticksPerBeat = midi.getTimeFormat() > 0 ? static_cast<double> (midi.getTimeFormat()) : 960.0;

    for (int trackIndex = 0; trackIndex < midi.getNumTracks(); ++trackIndex)
    {
        if (auto* track = midi.getTrack (trackIndex))
        {
            for (int eventIndex = 0; eventIndex < track->getNumEvents(); ++eventIndex)
            {
                const auto message = track->getEventPointer (eventIndex)->message;
                const auto tick = message.getTimeStamp();
                lastTick = std::max (lastTick, tick);

                if (message.isProgramChange())
                    ++programChanges;

                if (message.isNoteOn())
                {
                    ++noteOns;
                    sawNote = true;
                    firstTick = std::min (firstTick, tick);
                    starts.push_back (tick / ticksPerBeat);
                    const int note = message.getNoteNumber();
                    minNote = std::min (minNote, note);
                    maxNote = std::max (maxNote, note);
                    if (message.getChannel() != 10)
                    {
                        ++pitchedNoteOns;
                        pitchedMinNote = std::min (pitchedMinNote, note);
                        pitchedMaxNote = std::max (pitchedMaxNote, note);
                    }
                    velocities.insert (message.getVelocity());
                    ++notesByChannel[message.getChannel()];
                    ++pitchClasses[note % 12];
                }
                else if (message.isNoteOff() && ! message.isAllNotesOff())
                {
                    ++noteOffs;
                }
            }
        }
    }

    if (noteOns == 0)
        return { false, "analyze_midi found no note-on events in: " + file.getFullPathName() };

    std::sort (starts.begin(), starts.end());
    double minGap = std::numeric_limits<double>::max();
    double maxGap = 0.0;
    double totalGap = 0.0;
    int gapCount = 0;
    std::set<int> quantizedGaps;
    for (size_t i = 1; i < starts.size(); ++i)
    {
        const double gap = starts[i] - starts[i - 1];
        if (gap <= 1.0e-9)
            continue;
        minGap = std::min (minGap, gap);
        maxGap = std::max (maxGap, gap);
        totalGap += gap;
        quantizedGaps.insert (juce::roundToInt (gap * 960.0));
        ++gapCount;
    }

    auto mapSummary = [] (const std::map<int, int>& values, int maxItems)
    {
        juce::String text;
        int emitted = 0;
        for (const auto& [key, count] : values)
        {
            if (emitted++ >= maxItems)
                break;
            if (text.isNotEmpty())
                text << ",";
            text << key << ":" << count;
        }
        return text;
    };

    const double durationBeats = lastTick / ticksPerBeat;
    const double firstBeat = sawNote ? firstTick / ticksPerBeat : 0.0;
    const double density = durationBeats > 0.0 ? static_cast<double> (noteOns) / durationBeats : 0.0;
    const double averageGap = gapCount > 0 ? totalGap / static_cast<double> (gapCount) : 0.0;
    const int registerMin = pitchedNoteOns > 0 ? pitchedMinNote : minNote;
    const int registerMax = pitchedNoteOns > 0 ? pitchedMaxNote : maxNote;
    const int registerSpan = registerMax - registerMin;
    const int minVelocity = velocities.empty() ? 0 : *velocities.begin();
    const int maxVelocity = velocities.empty() ? 0 : *velocities.rbegin();
    const bool flatVelocity = velocities.size() <= 1 && noteOns > 16;
    const bool clockworkRhythm = quantizedGaps.size() <= 1 && gapCount > 16 && notesByChannel.size() <= 2;
    juce::StringArray flags;
    if (registerMin > 84) flags.add ("too_high");
    if (registerMax >= 124) flags.add ("clipped_top");
    if (registerMax < 36) flags.add ("too_low");
    if (registerSpan < 7) flags.add ("narrow_range");
    if (registerSpan > 60) flags.add ("very_wide_range");
    if (density < 0.15) flags.add ("very_sparse");
    if (density > 8.0) flags.add ("very_dense");
    if (durationBeats < 4.0) flags.add ("very_short");
    if (durationBeats > 256.0) flags.add ("very_long");
    if (notesByChannel.size() <= 1 && noteOns > 32) flags.add ("single_channel_texture");
    if (flatVelocity) flags.add ("flat_velocity");
    if (clockworkRhythm) flags.add ("clockwork_rhythm");
    if (flags.isEmpty()) flags.add ("ok");

    double score = 1.0;
    auto penalizeIf = [&] (bool condition, double amount)
    {
        if (condition)
            score -= amount;
    };
    penalizeIf (registerMin > 84, 0.22);
    penalizeIf (registerMax >= 124, 0.30);
    penalizeIf (registerMax < 36, 0.18);
    penalizeIf (registerSpan < 7, 0.08);
    penalizeIf (registerSpan > 60, 0.10);
    penalizeIf (density < 0.15, 0.34);
    penalizeIf (density > 8.0, 0.34);
    penalizeIf (durationBeats < 4.0, 0.22);
    penalizeIf (durationBeats > 256.0, 0.22);
    penalizeIf (notesByChannel.size() <= 1 && noteOns > 32, 0.08);
    penalizeIf (flatVelocity, 0.04);
    penalizeIf (clockworkRhythm, 0.04);
    score = juce::jlimit (0.0, 1.0, score);

    juce::StringArray nextActions;
    if (flags.contains ("too_high") || flags.contains ("clipped_top") || flags.contains ("too_low"))
        nextActions.add ("fit_midi_register");
    if (flags.contains ("very_long") || flags.contains ("very_short"))
        nextActions.add ("fit_midi_duration");
    if (flags.contains ("very_sparse") || flags.contains ("very_dense"))
        nextActions.add ("regenerate_density");
    if (flags.contains ("narrow_range"))
        nextActions.add ("fit_midi_pitch_range");
    if (flags.contains ("very_wide_range"))
        nextActions.add ("fit_midi_register");
    if (flags.contains ("single_channel_texture"))
        nextActions.add ("add_voice_or_channel");
    if (flags.contains ("flat_velocity") || flags.contains ("clockwork_rhythm"))
        nextActions.add ("humanize_midi");
    if (nextActions.isEmpty())
        nextActions.add (score >= 0.70 ? "play_midi" : "regenerate");

    juce::String result;
    result << file.getFullPathName()
           << " (notes=" << noteOns
           << ", note_offs=" << noteOffs
           << ", channels=" << static_cast<int> (notesByChannel.size())
           << ", programs=" << programChanges
           << ", tempo=" << sourceTempo
           << ", pitch_range=" << minNote << "-" << maxNote
           << (pitchedNoteOns > 0 && (pitchedMinNote != minNote || pitchedMaxNote != maxNote)
                ? ", pitched_range=" + juce::String (pitchedMinNote) + "-" + juce::String (pitchedMaxNote)
                : juce::String())
           << ", duration=" << juce::String (durationBeats, 2) << " beats"
           << ", first_note=" << juce::String (firstBeat, 2)
           << ", density=" << juce::String (density, 2) << " notes/beat"
           << ", velocity_range=" << minVelocity << "-" << maxVelocity
           << ", velocity_unique=" << static_cast<int> (velocities.size())
           << ", avg_gap=" << juce::String (averageGap, 3)
           << ", min_gap=" << juce::String (gapCount > 0 ? minGap : 0.0, 3)
           << ", max_gap=" << juce::String (maxGap, 3)
           << ", gap_unique=" << static_cast<int> (quantizedGaps.size())
           << ", by_channel={" << mapSummary (notesByChannel, 12) << "}"
           << ", pitch_classes={" << mapSummary (pitchClasses, 12) << "}"
           << ", score=" << juce::String (score, 2)
           << ", assessment=" << flags.joinIntoString ("|")
           << ", next=" << nextActions.joinIntoString ("|") << ")";

    return { true, result };
}

ToolResult MusicToolRegistry::fitMidiRegister (const juce::var& args)
{
    auto* object = args.getDynamicObject();
    if (object == nullptr)
        return { false, "fit_midi_register expects a JSON object." };

    const auto sourceFile = resolveMidiFile (object->getProperty ("midi_file").toString());
    if (! sourceFile.existsAsFile())
        return { false, "MIDI file not found for fit_midi_register: " + sourceFile.getFullPathName() };

    const int low = juce::jlimit (0, 127, static_cast<int> (getNumberProperty (*object, "low", 36)));
    const int high = juce::jlimit (0, 127, static_cast<int> (getNumberProperty (*object, "high", 84)));
    if (low > high)
        return { false, "fit_midi_register expects low <= high." };

    juce::FileInputStream stream (sourceFile);
    juce::MidiFile midi;
    if (! stream.openedOk() || ! midi.readFrom (stream))
        return { false, "Could not read MIDI file for fit_midi_register: " + sourceFile.getFullPathName() };
    const int sourceTempo = tempoFromMidiFile (midi, 120);

    juce::MidiMessageSequence fitted;
    int noteMessages = 0;
    int shiftedMessages = 0;
    int clampedMessages = 0;
    int minNote = 128;
    int maxNote = -1;

    const double sourceTicksPerBeat = midi.getTimeFormat() > 0 ? static_cast<double> (midi.getTimeFormat()) : 960.0;
    const double tickScale = 960.0 / sourceTicksPerBeat;

    auto fitNote = [&] (int note)
    {
        const int original = note;
        if (low == high)
            return low;

        while (note > high && note - 12 >= low)
            note -= 12;
        while (note < low && note + 12 <= high)
            note += 12;

        const int fittedNote = juce::jlimit (low, high, note);
        if (fittedNote != original)
            ++shiftedMessages;
        if (fittedNote != note)
            ++clampedMessages;
        return fittedNote;
    };

    for (int trackIndex = 0; trackIndex < midi.getNumTracks(); ++trackIndex)
    {
        if (auto* track = midi.getTrack (trackIndex))
        {
            for (int eventIndex = 0; eventIndex < track->getNumEvents(); ++eventIndex)
            {
                auto message = track->getEventPointer (eventIndex)->message;
                const auto scaledTime = message.getTimeStamp() * tickScale;

                if (message.isNoteOnOrOff() && message.getChannel() != 10)
                {
                    const auto fittedNote = fitNote (message.getNoteNumber());
                    message.setNoteNumber (fittedNote);
                    ++noteMessages;
                    if (message.isNoteOn())
                    {
                        minNote = std::min (minNote, fittedNote);
                        maxNote = std::max (maxNote, fittedNote);
                    }
                }

                message.setTimeStamp (0.0);
                fitted.addEvent (message, scaledTime);
            }
        }
    }

    if (noteMessages == 0)
        return { false, "fit_midi_register found no note events in: " + sourceFile.getFullPathName() };

    const auto outputFile = makeOutputFile ("fitted");
    if (! writeMidiFile (fitted, outputFile, sourceTempo))
        return { false, "Could not write fitted MIDI file." };

    return { true, outputFile.getFullPathName()
                   + " (" + juce::String (noteMessages) + " note events, "
                   + juce::String (shiftedMessages) + " shifted, "
                   + juce::String (clampedMessages) + " clamped, range="
                   + juce::String (minNote) + "-" + juce::String (maxNote)
                   + ", target=" + juce::String (low) + "-" + juce::String (high)
                   + ", tempo=" + juce::String (sourceTempo) + ")" };
}

ToolResult MusicToolRegistry::fitMidiPitchRange (const juce::var& args)
{
    auto* object = args.getDynamicObject();
    if (object == nullptr)
        return { false, "fit_midi_pitch_range expects a JSON object." };

    const auto sourceFile = resolveMidiFile (object->getProperty ("midi_file").toString());
    if (! sourceFile.existsAsFile())
        return { false, "MIDI file not found for fit_midi_pitch_range: " + sourceFile.getFullPathName() };

    const int low = juce::jlimit (0, 127, static_cast<int> (getNumberProperty (*object, "low", 36)));
    const int high = juce::jlimit (0, 127, static_cast<int> (getNumberProperty (*object, "high", 96)));
    const int targetSpan = juce::jlimit (1, 96, static_cast<int> (getNumberProperty (*object, "target_span", 18)));
    if (low > high)
        return { false, "fit_midi_pitch_range expects low <= high." };

    juce::FileInputStream stream (sourceFile);
    juce::MidiFile midi;
    if (! stream.openedOk() || ! midi.readFrom (stream))
        return { false, "Could not read MIDI file for fit_midi_pitch_range: " + sourceFile.getFullPathName() };
    const int sourceTempo = tempoFromMidiFile (midi, 120);

    int sourceMin = 128;
    int sourceMax = -1;
    int noteMessages = 0;
    for (int trackIndex = 0; trackIndex < midi.getNumTracks(); ++trackIndex)
    {
        if (auto* track = midi.getTrack (trackIndex))
        {
            for (int eventIndex = 0; eventIndex < track->getNumEvents(); ++eventIndex)
            {
                const auto message = track->getEventPointer (eventIndex)->message;
                if (message.isNoteOn() && message.getChannel() != 10)
                {
                    const int note = message.getNoteNumber();
                    sourceMin = std::min (sourceMin, note);
                    sourceMax = std::max (sourceMax, note);
                }
            }
        }
    }

    if (sourceMax < sourceMin)
        return { false, "fit_midi_pitch_range found no note-on events in: " + sourceFile.getFullPathName() };

    const double sourceTicksPerBeat = midi.getTimeFormat() > 0 ? static_cast<double> (midi.getTimeFormat()) : 960.0;
    const double tickScale = 960.0 / sourceTicksPerBeat;
    juce::MidiMessageSequence fitted;
    int shiftedMessages = 0;
    int clampedMessages = 0;
    int minNote = 128;
    int maxNote = -1;
    int noteOnOrdinal = 0;
    std::map<std::pair<int, int>, std::vector<int>> activeFittedNotes;

    auto fitIntoRegister = [&] (int note)
    {
        while (note > high && note - 12 >= low)
            note -= 12;
        while (note < low && note + 12 <= high)
            note += 12;
        return juce::jlimit (low, high, note);
    };

    auto spreadNoteOn = [&] (int note)
    {
        const int original = note;
        note = fitIntoRegister (note);

        if (sourceMax - sourceMin < targetSpan)
        {
            const int shifts[] { -12, 0, 12, -24, 24 };
            constexpr int numShifts = 5;
            const int firstChoice = noteOnOrdinal % numShifts;
            for (int attempt = 0; attempt < numShifts; ++attempt)
            {
                const int candidate = note + shifts[(firstChoice + attempt) % numShifts];
                if (candidate >= low && candidate <= high)
                {
                    note = candidate;
                    break;
                }
            }
        }

        if (note != original)
            ++shiftedMessages;
        if (note == low || note == high)
            ++clampedMessages;
        ++noteOnOrdinal;
        return note;
    };

    for (int trackIndex = 0; trackIndex < midi.getNumTracks(); ++trackIndex)
    {
        if (auto* track = midi.getTrack (trackIndex))
        {
            for (int eventIndex = 0; eventIndex < track->getNumEvents(); ++eventIndex)
            {
                auto message = track->getEventPointer (eventIndex)->message;
                const auto scaledTime = message.getTimeStamp() * tickScale;

                if (message.isNoteOn() && message.getChannel() != 10)
                {
                    const auto originalNote = message.getNoteNumber();
                    const auto fittedNote = spreadNoteOn (originalNote);
                    activeFittedNotes[{ message.getChannel(), originalNote }].push_back (fittedNote);
                    message.setNoteNumber (fittedNote);
                    ++noteMessages;
                    minNote = std::min (minNote, fittedNote);
                    maxNote = std::max (maxNote, fittedNote);
                }
                else if (message.isNoteOff() && ! message.isAllNotesOff() && message.getChannel() != 10)
                {
                    const auto originalNote = message.getNoteNumber();
                    auto& active = activeFittedNotes[{ message.getChannel(), originalNote }];
                    const auto fittedNote = active.empty() ? fitIntoRegister (originalNote) : active.front();
                    if (! active.empty())
                        active.erase (active.begin());
                    if (fittedNote != originalNote)
                        ++shiftedMessages;
                    message.setNoteNumber (fittedNote);
                    ++noteMessages;
                }

                message.setTimeStamp (0.0);
                fitted.addEvent (message, scaledTime);
            }
        }
    }

    if (noteMessages == 0)
        return { false, "fit_midi_pitch_range found no note events in: " + sourceFile.getFullPathName() };

    fitted.updateMatchedPairs();

    const auto outputFile = makeOutputFile ("range_fit");
    if (! writeMidiFile (fitted, outputFile, sourceTempo))
        return { false, "Could not write pitch-range-fitted MIDI file." };

    return { true, outputFile.getFullPathName()
                   + " (" + juce::String (noteMessages) + " note events, "
                   + juce::String (shiftedMessages) + " shifted, "
                   + juce::String (clampedMessages) + " edge-clamped, source_range="
                   + juce::String (sourceMin) + "-" + juce::String (sourceMax)
                   + ", range=" + juce::String (minNote) + "-" + juce::String (maxNote)
                   + ", target_span=" + juce::String (targetSpan)
                   + ", target=" + juce::String (low) + "-" + juce::String (high)
                   + ", tempo=" + juce::String (sourceTempo) + ")" };
}

ToolResult MusicToolRegistry::fitMidiDuration (const juce::var& args)
{
    auto* object = args.getDynamicObject();
    if (object == nullptr)
        return { false, "fit_midi_duration expects a JSON object." };

    const auto sourceFile = resolveMidiFile (object->getProperty ("midi_file").toString());
    if (! sourceFile.existsAsFile())
        return { false, "MIDI file not found for fit_midi_duration: " + sourceFile.getFullPathName() };

    const double targetBeats = juce::jlimit (1.0, 256.0, getNumberProperty (*object, "target_beats", 96.0));
    const auto mode = object->getProperty ("mode").toString().trim().toLowerCase().isEmpty()
        ? juce::String ("trim")
        : object->getProperty ("mode").toString().trim().toLowerCase();
    if (mode != "trim" && mode != "scale")
        return { false, "fit_midi_duration mode must be 'trim' or 'scale'." };

    juce::FileInputStream stream (sourceFile);
    juce::MidiFile midi;
    if (! stream.openedOk() || ! midi.readFrom (stream))
        return { false, "Could not read MIDI file for fit_midi_duration: " + sourceFile.getFullPathName() };
    const int sourceTempo = tempoFromMidiFile (midi, 120);

    const double sourceTicksPerBeat = midi.getTimeFormat() > 0 ? static_cast<double> (midi.getTimeFormat()) : 960.0;
    const double tickScale = 960.0 / sourceTicksPerBeat;
    const double targetTicks = targetBeats * 960.0;
    const double sourceEndTicks = midi.getLastTimestamp() * tickScale;
    int sourceNoteOns = 0;
    for (int trackIndex = 0; trackIndex < midi.getNumTracks(); ++trackIndex)
        if (auto* track = midi.getTrack (trackIndex))
            for (int eventIndex = 0; eventIndex < track->getNumEvents(); ++eventIndex)
                if (track->getEventPointer (eventIndex)->message.isNoteOn())
                    ++sourceNoteOns;

    auto effectiveMode = mode;
    bool autoTrimmedDenseScale = false;
    const double projectedScaleDensity = targetBeats > 0.0 ? static_cast<double> (sourceNoteOns) / targetBeats : 0.0;
    if (mode == "scale" && sourceEndTicks > targetTicks && projectedScaleDensity > 8.0)
    {
        effectiveMode = "trim";
        autoTrimmedDenseScale = true;
    }

    const double timeScale = effectiveMode == "scale" && sourceEndTicks > 1.0 ? targetTicks / sourceEndTicks : 1.0;

    juce::MidiMessageSequence fitted;
    int keptEvents = 0;
    int trimmedEvents = 0;
    int keptNoteOns = 0;

    for (int trackIndex = 0; trackIndex < midi.getNumTracks(); ++trackIndex)
    {
        if (auto* track = midi.getTrack (trackIndex))
        {
            for (int eventIndex = 0; eventIndex < track->getNumEvents(); ++eventIndex)
            {
                auto message = track->getEventPointer (eventIndex)->message;
                double scaledTime = message.getTimeStamp() * tickScale * timeScale;

                if (effectiveMode == "trim" && scaledTime > targetTicks)
                {
                    ++trimmedEvents;
                    continue;
                }

                if (message.isNoteOff() && scaledTime > targetTicks)
                    scaledTime = targetTicks;
                if (message.isNoteOn() && scaledTime >= targetTicks)
                {
                    ++trimmedEvents;
                    continue;
                }

                if (message.isNoteOn())
                    ++keptNoteOns;

                message.setTimeStamp (0.0);
                fitted.addEvent (message, scaledTime);
                ++keptEvents;
            }
        }
    }

    if (keptNoteOns == 0)
        return { false, "fit_midi_duration produced no note-on events." };

    fitted.updateMatchedPairs();

    const auto outputFile = makeOutputFile ("duration_fit");
    if (! writeMidiFile (fitted, outputFile, sourceTempo))
        return { false, "Could not write duration-fitted MIDI file." };

    return { true, outputFile.getFullPathName()
                   + " (" + juce::String (keptNoteOns) + " notes, "
                   + juce::String (keptEvents) + " events kept, "
                   + juce::String (trimmedEvents) + " events trimmed, mode="
                   + effectiveMode
                   + (autoTrimmedDenseScale ? juce::String ("(auto_dense_from_scale)") : juce::String())
                   + ", target=" + juce::String (targetBeats, 2)
                   + " beats, source=" + juce::String (sourceEndTicks / 960.0, 2)
                   + " beats, tempo=" + juce::String (sourceTempo) + ")" };
}

ToolResult MusicToolRegistry::humanizeMidi (const juce::var& args)
{
    auto* object = args.getDynamicObject();
    if (object == nullptr)
        return { false, "humanize_midi expects a JSON object." };

    const auto sourceFile = resolveMidiFile (object->getProperty ("midi_file").toString());
    if (! sourceFile.existsAsFile())
        return { false, "MIDI file not found for humanize_midi: " + sourceFile.getFullPathName() };

    const double timingTicks = juce::jlimit (0.0, 120.0, getNumberProperty (*object, "timing_ticks", 18.0));
    const int velocityAmount = juce::jlimit (0, 48, static_cast<int> (getNumberProperty (*object, "velocity_amount", 8.0)));
    const double seed = getNumberProperty (*object, "seed", 1.0);

    juce::FileInputStream stream (sourceFile);
    juce::MidiFile midi;
    if (! stream.openedOk() || ! midi.readFrom (stream))
        return { false, "Could not read MIDI file for humanize_midi: " + sourceFile.getFullPathName() };
    const int sourceTempo = tempoFromMidiFile (midi, 120);

    auto unit = [] (double index, double localSeed)
    {
        const double value = std::sin ((index + 1.0) * 12.9898 + (localSeed + 1.0) * 78.233) * 43758.5453123;
        return value - std::floor (value);
    };

    const double sourceTicksPerBeat = midi.getTimeFormat() > 0 ? static_cast<double> (midi.getTimeFormat()) : 960.0;
    const double tickScale = 960.0 / sourceTicksPerBeat;
    juce::MidiMessageSequence humanized;
    std::map<std::pair<int, int>, std::vector<double>> activeOffsets;
    int noteOns = 0;
    int noteEvents = 0;
    int velocityChanged = 0;
    double maxAbsOffset = 0.0;

    for (int trackIndex = 0; trackIndex < midi.getNumTracks(); ++trackIndex)
    {
        if (auto* track = midi.getTrack (trackIndex))
        {
            for (int eventIndex = 0; eventIndex < track->getNumEvents(); ++eventIndex)
            {
                auto message = track->getEventPointer (eventIndex)->message;
                double scaledTime = message.getTimeStamp() * tickScale;

                if (message.isNoteOn())
                {
                    const double offset = timingTicks * ((unit (noteOns, seed) * 2.0) - 1.0);
                    const int velocityOffset = juce::roundToInt (velocityAmount * ((unit (noteOns, seed + 17.0) * 2.0) - 1.0));
                    const int originalVelocity = message.getVelocity();
                    const int newVelocity = juce::jlimit (1, 127, originalVelocity + velocityOffset);
                    if (newVelocity != originalVelocity)
                        ++velocityChanged;
                    message = juce::MidiMessage::noteOn (message.getChannel(),
                                                         message.getNoteNumber(),
                                                         static_cast<juce::uint8> (newVelocity));
                    activeOffsets[{ message.getChannel(), message.getNoteNumber() }].push_back (offset);
                    scaledTime = std::max (0.0, scaledTime + offset);
                    maxAbsOffset = std::max (maxAbsOffset, std::abs (offset));
                    ++noteOns;
                    ++noteEvents;
                }
                else if (message.isNoteOff() && ! message.isAllNotesOff())
                {
                    const auto key = std::make_pair (message.getChannel(), message.getNoteNumber());
                    auto& offsets = activeOffsets[key];
                    const double offset = offsets.empty() ? 0.0 : offsets.front();
                    if (! offsets.empty())
                        offsets.erase (offsets.begin());
                    scaledTime = std::max (0.0, scaledTime + offset);
                    ++noteEvents;
                }

                message.setTimeStamp (0.0);
                humanized.addEvent (message, scaledTime);
            }
        }
    }

    if (noteOns == 0)
        return { false, "humanize_midi found no note-on events in: " + sourceFile.getFullPathName() };

    humanized.updateMatchedPairs();

    const auto outputFile = makeOutputFile ("humanized");
    if (! writeMidiFile (humanized, outputFile, sourceTempo))
        return { false, "Could not write humanized MIDI file." };

    return { true, outputFile.getFullPathName()
                   + " (" + juce::String (noteOns) + " notes, "
                   + juce::String (noteEvents) + " note events, "
                   + juce::String (velocityChanged) + " velocities changed, timing_ticks="
                   + juce::String (timingTicks, 1)
                   + ", max_offset=" + juce::String (maxAbsOffset, 1)
                   + ", seed=" + juce::String (seed, 1)
                   + ", tempo=" + juce::String (sourceTempo) + ")" };
}

ToolResult MusicToolRegistry::playMidi (const juce::var& args)
{
    const auto file = args.getDynamicObject() != nullptr
        ? resolveMidiFile (args.getDynamicObject()->getProperty ("midi_file").toString())
        : resolveMidiFile (args.toString());

    if (! file.existsAsFile())
        return { false, "MIDI file not found: " + file.getFullPathName() };

    if (midiPlaybackHandler)
        return midiPlaybackHandler (file);

    return { true, "MIDI file exists but no playback handler is connected: " + file.getFullPathName() };
}
