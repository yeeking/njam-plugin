#include "MusicAgentCore.h"

#include <cstring>
#include <limits>

namespace
{

    // constexpr const char* managerInstructions =
    // "You are a manager agent helping the user interact with music experts. "
    // "Figure out what they want, decide whether to ask delegate music experts, generate MIDI, and explain briefly. "
    // "Retain the conversation context from the transcript. Keep status-worthy actions sparse and useful.";

constexpr const char* managerInstructions =
    "You are a manager agent helping the user make music with specialist agents and MIDI tools. "
    "Try to do what they want, consulting delegate music experts as needed. "
    "When the user asks for generated music, actually call the available MIDI generation tools; do not merely describe a tool call or show code. "
    "For mood, genre, chord-style, or form requests, use style_blueprint_agent_tool before mathematical_music_agent_tool. "
    "For complex patterned music, prefer mathematical_music_agent_tool followed by generate_formula_midi using compact lambda-style formulas, reusable motifs, and sections when useful. "
    "After generating complex MIDI, call analyze_midi to check score, density, range, voices, and duration before the final reply. "
    "If analyze_midi score is below 0.70, repair or regenerate before playback when possible. "
    "If analyze_midi reports clipped_top, too_high, or too_low but the pattern is otherwise useful, prefer fit_midi_register before playback. "
    "If analyze_midi reports narrow_range but the pattern is otherwise useful, prefer fit_midi_pitch_range before playback. "
    "If analyze_midi reports very_long or very_short but the pattern is otherwise useful, prefer fit_midi_duration before playback. "
    "If analyze_midi reports very_sparse or very_dense, adjust/regenerate before playback when possible. "
    "If analyze_midi reports flat_velocity or clockwork_rhythm, prefer humanize_midi before playback. "
    "If you generate a MIDI example, call play_midi for the generated file. "
    "Keep final replies concise and mention the tool results.";
    
constexpr const char* classicalInstructions =
    "You are a classical music expert. Anything not classical is not complex enough for your ears. "
    "You like to hear at least 50 instruments playing.";

constexpr const char* freeJazzInstructions =
    "You are a serious free jazz nut. As soon as you hear a melody, it has to change. "
    "Regular rhythms are not acceptable, but you have open ears to all kinds of music.";

constexpr const char* styleBlueprintInstructions =
    "You are a style blueprint expert. Convert mood, genre, chord style, energy arc, and instrumentation requests "
    "into concise pattern briefs for a formula composer. Name 2-4 sections when useful. For each section, describe "
    "chord palette, register, density, rhythmic feel, texture roles, and what should change from the previous section. "
    "Do not write lambdas; hand off compact musical constraints that can be converted into lambdas.";

constexpr const char* mathematicalInstructions =
    "You are a mathematical music expert. Translate musical requests into compact C++ lambda-style formulas. "
    "Prefer formulas over long note lists. Use variables i for note index, t for current beat, and voice for voice index. "
    "Use rhythm_lambda for inter-onset beat duration and gate_lambda for rests or syncopation. "
    "Use backend='expr' for executable formulas; JavaScript or native C++ JIT backends are not enabled yet. "
    "Use generate_formula_midi sections to chain lambdas across intro/body/variation/outro or other form sections. "
    "When the same figure recurs, define it once in generate_formula_midi motifs and reference it from section voices with motif plus small overrides. "
    "Prefer motif voice overrides like transpose, duration_scale, velocity_offset, start, root, channel, steps, or gate_lambda instead of rewriting similar formulas. "
    "For compact chords or stabs, use chord_offsets such as [0, 4, 7], [0, 5, 10], or [0, 7, 14] on one formula voice. "
    "For groove, use swing around 0.15-0.35 with swing_grid 0.5 instead of spelling out delayed note starts. "
    "For echoes, canons, delay lines, or layered repeats, use echoes with echo_delay, echo_transpose, and echo_velocity_decay instead of duplicating near-identical voices. "
    "For clave, techno, additive rhythms, polymeter, or sparse percussion, use euclid(pulses, steps, i[, rotation]) in gate_lambda. "
    "For organic but repeatable variation, use rand(i, seed), chance(prob, i, seed), or pick(i, seed, value) sparingly in gate, velocity, or ornament formulas. "
    "For compact repeating cells, use cycle(i, value1, value2, ...) or choose(i, value1, value2, ...) instead of long bracket lookup expressions. "
    "Section-aware formulas can use section or s plus local_t/lt for beat position within the current section. "
    "For drum or hand percussion voices, use channel 10, pitch_mode='midi', and percussion constants such as clave, cowbell, conga_low, conga_high, bongo_low, bongo_high, shaker, closed_hat, open_hat, kick, snare, and hand_clap. "
    "Use simple math functions such as sin, cos, mod, pow, min, max, cycle, choose, rand, chance, pick, and C++-style ternary expressions like "
    "(i % 7) < 3 ? 0.25 : 0.5. Bracket lookup lists like [60,64,67][i % 3] also work.";

juce::var parseJsonObject (const juce::String& text)
{
    auto parsed = juce::JSON::parse (text);
    if (parsed.isVoid())
        return juce::var (new juce::DynamicObject());
    return parsed;
}

juce::String getPropertyString (const juce::var& value, const juce::Identifier& name)
{
    if (auto* object = value.getDynamicObject())
        return object->getProperty (name).toString();
    return {};
}

void setPropertyString (juce::var& value, const juce::Identifier& name, const juce::String& text)
{
    auto* object = value.getDynamicObject();
    if (object == nullptr)
    {
        juce::DynamicObject::Ptr newObject = new juce::DynamicObject();
        value = juce::var (newObject.get());
        object = value.getDynamicObject();
    }

    if (object != nullptr)
        object->setProperty (name, text);
}

int findMatchingCloseParen (const juce::String& text, int openParen)
{
    bool inString = false;
    juce::juce_wchar stringQuote = 0;
    bool escaped = false;
    int depth = 0;

    for (int i = openParen; i < text.length(); ++i)
    {
        const auto c = text[i];
        if (inString)
        {
            if (escaped)
            {
                escaped = false;
                continue;
            }
            if (c == '\\')
            {
                escaped = true;
                continue;
            }
            if (c == stringQuote)
                inString = false;
            continue;
        }

        if (c == '"' || c == '\'')
        {
            inString = true;
            stringQuote = c;
            continue;
        }

        if (c == '(')
            ++depth;
        else if (c == ')')
        {
            --depth;
            if (depth == 0)
                return i;
        }
    }

    return -1;
}

juce::StringArray splitTopLevelCommaList (const juce::String& text)
{
    juce::StringArray parts;
    bool inString = false;
    juce::juce_wchar stringQuote = 0;
    bool escaped = false;
    int squareDepth = 0;
    int curlyDepth = 0;
    int parenDepth = 0;
    int start = 0;

    for (int i = 0; i < text.length(); ++i)
    {
        const auto c = text[i];
        if (inString)
        {
            if (escaped)
            {
                escaped = false;
                continue;
            }
            if (c == '\\')
            {
                escaped = true;
                continue;
            }
            if (c == stringQuote)
                inString = false;
            continue;
        }

        if (c == '"' || c == '\'')
        {
            inString = true;
            stringQuote = c;
            continue;
        }

        if (c == '[') ++squareDepth;
        else if (c == ']') --squareDepth;
        else if (c == '{') ++curlyDepth;
        else if (c == '}') --curlyDepth;
        else if (c == '(') ++parenDepth;
        else if (c == ')') --parenDepth;
        else if (c == ',' && squareDepth == 0 && curlyDepth == 0 && parenDepth == 0)
        {
            parts.add (text.substring (start, i).trim());
            start = i + 1;
        }
    }

    const auto tail = text.substring (start).trim();
    if (tail.isNotEmpty())
        parts.add (tail);

    return parts;
}

juce::var parseLenientJsonValue (juce::String text)
{
    text = text.trim();
    auto parsed = juce::JSON::parse (text);
    if (! parsed.isVoid())
        return parsed;

    if (text.startsWithChar ('\'') && text.endsWithChar ('\''))
        return text.substring (1, text.length() - 1);

    if (text.startsWithChar ('"') && text.endsWithChar ('"'))
        return text.substring (1, text.length() - 1);

    if (text.containsOnly ("+-0123456789."))
        return text.containsChar ('.') ? juce::var (text.getDoubleValue()) : juce::var (text.getIntValue());

    return juce::var();
}

juce::var parseKeywordArgumentObject (const juce::String& argumentText)
{
    juce::DynamicObject::Ptr object = new juce::DynamicObject();

    for (const auto& part : splitTopLevelCommaList (argumentText))
    {
        const int equals = part.indexOfChar ('=');
        if (equals <= 0)
            continue;

        const auto key = part.substring (0, equals).trim();
        const auto value = parseLenientJsonValue (part.substring (equals + 1));
        if (key.containsOnly ("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_") && ! value.isVoid())
            object->setProperty (key, value);
    }

    return juce::var (object.get());
}

std::vector<ToolInvocation> parseFunctionStyleToolCalls (const juce::String& modelText)
{
    std::vector<ToolInvocation> calls;
    const juce::StringArray functionNames { "generate_formula_midi", "play_midi" };

    for (const auto& functionName : functionNames)
    {
        int searchFrom = 0;
        while (true)
        {
            const int nameStart = modelText.indexOf (searchFrom, functionName);
            if (nameStart < 0)
                break;

            const int openParen = modelText.indexOf (nameStart + functionName.length(), "(");
            if (openParen < 0)
                break;

            const int closeParen = findMatchingCloseParen (modelText, openParen);
            if (closeParen < 0)
                break;

            const auto argumentText = modelText.substring (openParen + 1, closeParen);
            auto args = parseKeywordArgumentObject (argumentText);
            if (auto* object = args.getDynamicObject())
            {
                if (functionName == "generate_formula_midi" && object->hasProperty ("voices"))
                    calls.push_back ({ functionName, args });
                else if (functionName == "play_midi")
                {
                    if (! object->hasProperty ("midi_file"))
                    {
                        const auto positional = parseLenientJsonValue (argumentText);
                        if (positional.isString())
                            object->setProperty ("midi_file", positional);
                    }

                    if (object->hasProperty ("midi_file"))
                        calls.push_back ({ functionName, args });
                }
            }

            searchFrom = closeParen + 1;
        }
    }

    return calls;
}

bool looksLikeToolOnlyResponse (const juce::String& text)
{
    const auto trimmed = text.trim();
    return trimmed.isEmpty()
        || trimmed.startsWith ("<tool")
        || trimmed.startsWith ("<tool_call")
        || trimmed.startsWith ("<|")
        || trimmed == "</tool>";
}

bool looksLikeEmptyRemoteResponse (const juce::String& text)
{
    const auto trimmed = text.trim();
    return trimmed.startsWith ("[The model returned an empty assistant message")
        || trimmed.startsWith ("[The model returned reasoning but no visible assistant text")
        || trimmed.startsWith ("[No response from the remote model server");
}

bool isMidiCreationTool (const juce::String& name)
{
    return name == "generate_midi"
        || name == "generate_chord_midi"
        || name == "generate_polyphonic_midi"
        || name == "generate_formula_midi"
        || name == "combine_midi";
}

juce::String shortenText (juce::String text, int maxLength)
{
    text = text.replaceCharacters ("\r\n\t", "   ").trim();
    while (text.contains ("  "))
        text = text.replace ("  ", " ");
    if (text.length() <= maxLength)
        return text;
    return text.substring (0, juce::jmax (0, maxLength - 3)).trim() + "...";
}

juce::String extractParenthesizedDetails (const juce::String& text)
{
    const int open = text.indexOfChar ('(');
    const int close = text.lastIndexOfChar (')');
    if (open < 0 || close <= open)
        return {};
    return text.substring (open + 1, close);
}

juce::String extractDetailField (const juce::String& details, const juce::String& key)
{
    const auto prefix = key + "=";
    const int start = details.indexOf (prefix);
    if (start < 0)
        return {};

    const int valueStart = start + prefix.length();
    int valueEnd = details.indexOf (valueStart, ", ");
    if (valueEnd < 0)
        valueEnd = details.length();

    return details.substring (valueStart, valueEnd).trim();
}

juce::String compactToolResultForTranscript (const ToolInvocation& call, const ToolResult& result)
{
    if (! result.ok)
        return "tool " + call.name + " failed: " + shortenText (result.content, 260);

    const auto content = result.content.trim();
    const auto details = extractParenthesizedDetails (content);
    juce::StringArray fields;

    if (call.name == "analyze_midi")
    {
        for (const auto& key : { "score", "assessment", "next", "notes", "tempo", "pitch_range", "duration", "density", "velocity_range", "gap_unique" })
            if (const auto value = extractDetailField (details, key); value.isNotEmpty())
                fields.add (juce::String (key) + "=" + value);
        return "tool analyze_midi ok: " + (fields.isEmpty() ? shortenText (content, 320) : fields.joinIntoString (", "));
    }

    if (call.name == "generate_formula_midi" || call.name == "combine_midi"
        || call.name.startsWith ("fit_midi_") || call.name == "humanize_midi")
    {
        const auto midiPath = content.upToFirstOccurrenceOf (" (", false, false);
        return "tool " + call.name + " ok: " + juce::File (midiPath).getFileName()
            + (details.isNotEmpty() ? " (" + shortenText (details, 260) + ")" : juce::String());
    }

    if (call.name == "play_midi")
        return "tool play_midi ok: " + shortenText (content, 220);

    return "tool " + call.name + " ok: " + shortenText (content, 360);
}

juce::String makeDeterministicFinalSummary (const std::vector<ToolInvocation>& calls,
                                            const std::vector<ToolResult>& results)
{
    if (results.empty())
        return "I stopped after using the available tool budget, but no tool result was available to summarize.";

    for (int i = static_cast<int> (results.size()) - 1; i >= 0; --i)
    {
        const auto& result = results[static_cast<size_t> (i)];
        const auto toolName = static_cast<size_t> (i) < calls.size()
            ? calls[static_cast<size_t> (i)].name
            : juce::String ("tool");

        if (result.ok)
            return "I stopped after using the available tool budget. Latest successful tool result from "
                + toolName + ": " + result.content;
    }

    return "I stopped after using the available tool budget. Latest tool result: "
        + results.back().content;
}
}

MusicAgentCore::MusicAgentCore()
{
    messages.push_back ({ "system", managerInstructions });
}

AgentRunResult MusicAgentCore::run (ILLMController& llm,
                                    const juce::String& userPrompt,
                                    int contextLength,
                                    int maxTokens,
                                    AgentStatusCallback statusCallback)
{
    if (statusCallback)
        statusCallback ({ "thinking", "manager" });

    {
        const juce::ScopedLock scopedLock (lock);
        messages.push_back ({ "user", userPrompt });
    }

    AgentRunResult result;
    constexpr int maxToolRounds = 18;
    bool generatedMidi = false;
    bool latestMidiAnalyzed = false;
    bool latestMidiPlayed = false;
    bool needsFinalText = true;

    for (int round = 0; round < maxToolRounds; ++round)
    {
        juce::String followUpInstruction =
            "Continue the task using the tool results above. "
            "If the user requested generated music and no MIDI file has been generated yet, keep calling the appropriate expert/MIDI tools. ";
        if (generatedMidi && ! latestMidiAnalyzed)
            followUpInstruction << "The latest generated MIDI has not been analyzed yet; call analyze_midi before playback or final reply. ";
        if (generatedMidi && latestMidiAnalyzed && ! latestMidiPlayed)
            followUpInstruction << "The latest generated MIDI has been analyzed but not played yet; if the analysis is acceptable, call play_midi. If it reports too_high, clipped_top, too_low, or very_wide_range, call fit_midi_register then analyze the repaired file. If it reports narrow_range, call fit_midi_pitch_range then analyze the repaired file. If it reports very_long, call fit_midi_duration with mode='trim' then analyze the repaired file. If it reports very_short, call fit_midi_duration with mode='scale' then analyze the repaired file. If it reports flat_velocity or clockwork_rhythm, call humanize_midi then analyze the repaired file. If it reports very_sparse or very_dense, regenerate and analyze again. ";
        followUpInstruction << "Otherwise write the final concise answer. Do not include tool tags unless another tool is truly required.";

        const auto prompt = round == 0
            ? buildPromptForUserMessage ({}, contextLength, maxTokens)
            : renderTranscript (estimateTranscriptBudgetChars (contextLength, maxTokens))
                + "\n[system]\n" + followUpInstruction;

        llm.resetContext (static_cast<uint32_t> (contextLength));
        if (statusCallback)
            statusCallback ({ "calling model", round == 0 ? "manager" : "tool follow-up" });

        ILLMController::InferenceStats roundStats{};
        const auto modelText = juce::String (llm.generateWithStats (prompt.toStdString(),
                                                                    static_cast<size_t> (maxTokens),
                                                                    roundStats));
        result.stats.num_tokens_in_prompt += roundStats.num_tokens_in_prompt;
        result.stats.num_tokens_in_response += roundStats.num_tokens_in_response;
        result.stats.prompt_tokens_per_second += roundStats.prompt_tokens_per_second;
        result.stats.inference_tokens_per_second += roundStats.inference_tokens_per_second;
        result.stats.total_time_taken_for_inference += roundStats.total_time_taken_for_inference;

        auto toolCalls = parseToolCalls (modelText);
        if (toolCalls.empty())
        {
            if ((looksLikeEmptyRemoteResponse (modelText) || looksLikeToolOnlyResponse (modelText)) && round + 1 < maxToolRounds)
            {
                if (statusCallback)
                    statusCallback ({ "thinking", looksLikeToolOnlyResponse (modelText)
                                                   ? "retrying tool-only model response"
                                                   : "retrying empty model response" });
                continue;
            }

            result.finalText = modelText;
            needsFinalText = false;
            break;
        }

        juce::String toolReturnSummary;
        for (auto call : toolCalls)
        {
            ToolResult toolResult;
            const bool isSpecialistTool = call.name == "classical_agent_tool"
                || call.name == "free_jazz_agent_tool"
                || call.name == "style_blueprint_agent_tool"
                || call.name == "mathematical_music_agent_tool";
            if (isSpecialistTool && statusCallback)
                statusCallback ({ "calling tool", call.name });

            if (isSpecialistTool)
            {
                if (getPropertyString (call.args, "user_music_interest").trim().isEmpty())
                    setPropertyString (call.args, "user_music_interest", userPrompt);

                toolResult = callSpecialistTool (call);
            }
            else if (call.name == "play_midi" && generatedMidi && ! latestMidiAnalyzed)
            {
                toolResult = { false, "Refusing playback until the latest generated or repaired MIDI has been checked. Call analyze_midi on the latest MIDI file first." };
            }
            else if (isMidiCreationTool (call.name) && generatedMidi && ! latestMidiAnalyzed)
            {
                toolResult = { false, "Refusing to generate or combine another MIDI file until the latest generated or repaired MIDI has been checked. Call analyze_midi on the latest MIDI file first." };
            }
            else
            {
                toolResult = tools.call (call, statusCallback);
            }

            if (statusCallback)
                statusCallback ({ toolResult.ok ? "tool complete" : "tool failed", call.name });

            if (toolResult.ok && isMidiCreationTool (call.name))
            {
                generatedMidi = true;
                latestMidiAnalyzed = false;
                latestMidiPlayed = false;
            }

            if (toolResult.ok && (call.name == "fit_midi_register"
                                  || call.name == "fit_midi_pitch_range"
                                  || call.name == "fit_midi_duration"
                                  || call.name == "humanize_midi"))
            {
                generatedMidi = true;
                latestMidiAnalyzed = false;
                latestMidiPlayed = false;
            }

            if (toolResult.ok && call.name == "analyze_midi")
                latestMidiAnalyzed = true;

            if (toolResult.ok && call.name == "play_midi")
                latestMidiPlayed = true;

            result.toolCalls.push_back (call);
            result.toolResults.push_back (toolResult);
            toolReturnSummary << compactToolResultForTranscript (call, toolResult) << "\n";
        }

        {
            const juce::ScopedLock scopedLock (lock);
            messages.push_back ({ "assistant", modelText });
            messages.push_back ({ "tool", toolReturnSummary.trim() });
        }

        if (statusCallback)
            statusCallback ({ "thinking", generatedMidi ? "manager with MIDI result" : "manager with tool results" });
    }

    if (needsFinalText)
    {
        if (statusCallback)
            statusCallback ({ "thinking", "final summary" });

        ILLMController::InferenceStats finalStats{};
        const auto finalPrompt = renderTranscript (estimateTranscriptBudgetChars (contextLength, maxTokens))
            + "\n[system]\nThe tool budget is exhausted. Write a concise final answer using the latest tool results. "
              "Do not include tool tags or request more tools.";

        llm.resetContext (static_cast<uint32_t> (contextLength));
        if (statusCallback)
            statusCallback ({ "calling model", "final summary" });

        result.finalText = juce::String (llm.generateWithStats (finalPrompt.toStdString(),
                                                                static_cast<size_t> (maxTokens),
                                                                finalStats));
        result.stats.num_tokens_in_prompt += finalStats.num_tokens_in_prompt;
        result.stats.num_tokens_in_response += finalStats.num_tokens_in_response;
        result.stats.prompt_tokens_per_second += finalStats.prompt_tokens_per_second;
        result.stats.inference_tokens_per_second += finalStats.inference_tokens_per_second;
        result.stats.total_time_taken_for_inference += finalStats.total_time_taken_for_inference;
    }

    result.finalText = stripToolTags (result.finalText).trim();
    if (looksLikeToolOnlyResponse (result.finalText))
        result.finalText = makeDeterministicFinalSummary (result.toolCalls, result.toolResults);

    {
        const juce::ScopedLock scopedLock (lock);
        messages.push_back ({ "assistant", result.finalText });
    }

    if (statusCallback)
        statusCallback ({ "done", "ready" });

    llm.addPromptResponse (userPrompt.toStdString(), result.finalText.toStdString());
    return result;
}

void MusicAgentCore::reset()
{
    const juce::ScopedLock scopedLock (lock);
    messages.clear();
    messages.push_back ({ "system", managerInstructions });
}

std::vector<ChatMessage> MusicAgentCore::getMessages() const
{
    const juce::ScopedLock scopedLock (lock);
    return messages;
}

void MusicAgentCore::setMidiPlaybackHandler (MusicToolRegistry::MidiPlaybackHandler handler)
{
    tools.setMidiPlaybackHandler (std::move (handler));
}

juce::String MusicAgentCore::buildPromptForUserMessage (const juce::String& userPrompt) const
{
    return buildPromptForUserMessage (userPrompt, 2048, 256);
}

juce::String MusicAgentCore::buildPromptForUserMessage (const juce::String& userPrompt,
                                                        int contextLength,
                                                        int maxTokens) const
{
    juce::String prompt = renderTranscript (estimateTranscriptBudgetChars (contextLength, maxTokens));
    if (userPrompt.isNotEmpty())
        prompt << "\n[user]\n" << userPrompt << "\n";
    return prompt;
}

std::vector<ToolInvocation> MusicAgentCore::parseToolCalls (const juce::String& modelText) const
{
    std::vector<ToolInvocation> calls;
    int searchFrom = 0;
    while (true)
    {
        const int tagStart = modelText.indexOf (searchFrom, "<tool");
        if (tagStart < 0)
            break;

        const int nameStart = modelText.indexOf (tagStart, "name=\"");
        const int tagClose = modelText.indexOf (tagStart, ">");
        const int closeTag = modelText.indexOf (tagClose, "</tool>");
        if (nameStart < 0 || tagClose < 0 || closeTag < 0)
            break;

        const int nameValueStart = nameStart + 6;
        const int nameEnd = modelText.indexOf (nameValueStart, "\"");
        if (nameEnd < 0 || nameEnd > tagClose)
            break;

        calls.push_back ({
            modelText.substring (nameValueStart, nameEnd).trim(),
            parseJsonObject (modelText.substring (tagClose + 1, closeTag).trim())
        });
        searchFrom = closeTag + 7;
    }

    for (auto& call : parseFunctionStyleToolCalls (modelText))
        calls.push_back (std::move (call));

    return calls;
}

ToolResult MusicAgentCore::callSpecialistTool (const ToolInvocation& invocation)
{
    const auto interest = getPropertyString (invocation.args, "user_music_interest");
    return { true, runSpecialist (invocation.name, interest) };
}

juce::String MusicAgentCore::stripToolTags (const juce::String& modelText) const
{
    juce::String output = modelText;
    while (true)
    {
        const int tagStart = output.indexOf ("<tool");
        if (tagStart < 0)
            break;
        const int closeTag = output.indexOf (tagStart, "</tool>");
        if (closeTag < 0)
            break;
        output = output.substring (0, tagStart) + output.substring (closeTag + 7);
    }
    return output;
}

juce::String MusicAgentCore::renderTranscript() const
{
    return renderTranscript (std::numeric_limits<int>::max());
}

juce::String MusicAgentCore::renderTranscript (int maxTranscriptChars) const
{
    const juce::ScopedLock scopedLock (lock);
    juce::String prompt;
    if (messages.empty())
        return prompt;

    prompt << "[" << messages.front().role << "]\n" << messages.front().content << "\n";

    juce::String recent;
    for (auto it = messages.rbegin(); it != messages.rend(); ++it)
    {
        if (it->role == "system")
            continue;

        const juce::String rendered = "[" + it->role + "]\n" + it->content + "\n";
        if (recent.length() + rendered.length() > maxTranscriptChars && recent.isNotEmpty())
            break;

        recent = rendered + recent;
    }

    if (recent.isNotEmpty() && messages.size() > 1)
        prompt << "\n" << recent;
    return prompt;
}

int MusicAgentCore::estimateTranscriptBudgetChars (int contextLength, int maxTokens)
{
    constexpr int approximateCharsPerToken = 4;
    constexpr int fixedPromptReserveTokens = 360;
    const int responseReserveTokens = juce::jmax (0, maxTokens);
    const int availableTokens = juce::jmax (256, contextLength - responseReserveTokens - fixedPromptReserveTokens);
    return availableTokens * approximateCharsPerToken;
}

juce::String MusicAgentCore::runSpecialist (const juce::String& name, const juce::String& userMusicInterest) const
{
    const bool classical = name == "classical_agent_tool";
    const bool styleBlueprint = name == "style_blueprint_agent_tool";
    const bool mathematical = name == "mathematical_music_agent_tool";
    juce::String response;
    if (styleBlueprint)
        response << "blueprint sections for '" << userMusicInterest << "': "
                 << "intro sparse motif in a controlled register; body adds complementary ostinato, bass anchor, and wider colour tones; "
                 << "variation changes density/gate pattern while preserving palette; outro thins texture and lowers register. "
                 << "Roles: pulse/percussion, bass anchor, shimmer or counterline, transition cue.";
    else if (mathematical)
        response << "formula plan for '" << userMusicInterest << "': "
                 << "use generate_formula_midi with motifs plus section voices. For tonal voices prefer pitch_mode='degree', roots around 36-60, "
                 << "and small degree formulas like [-7,0,2,4,5][i%5] to avoid clipped high MIDI. "
                 << "For percussion use channel 10, pitch_mode='midi', pitch constants such as clave/conga_low/conga_high/shaker, "
                 << "rhythm_lambda='0.25' or '0.5', and gate_lambda='euclid(3,8,i)' or rotated variants. "
                 << "Use rand(i,seed), chance(prob,i,seed), and pick(i,seed,value) for repeatable accents, ornaments, and velocity variation. "
                 << "Use chord_offsets for stabs, swing/swing_grid for groove, echoes for canons, and analyze_midi before playback.";
    else if (classical)
        response << "classical suggestion for '" << userMusicInterest
                 << "': develop the material with orchestral layers, counterpoint, and long-range harmonic motion.";
    else
        response << "free jazz suggestion for '" << userMusicInterest
                 << "': destabilise repeated figures, vary phrase lengths, and let timbre lead the form.";
    return response;
}
