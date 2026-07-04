#include "OpenAICompatibleLLM.h"

#include <chrono>
#include <limits>

namespace
{
struct ChatMessageForRequest
{
    juce::String role;
    juce::String content;
};

bool parseRoleHeader (const juce::String& line, juce::String& role)
{
    const auto trimmed = line.trim();
    if (! (trimmed.startsWithChar ('[') && trimmed.endsWithChar (']')))
        return false;

    const auto candidate = trimmed.substring (1, trimmed.length() - 1).trim();
    if (candidate == "system" || candidate == "user" || candidate == "assistant" || candidate == "tool")
    {
        role = candidate;
        return true;
    }

    return false;
}

std::vector<ChatMessageForRequest> promptToChatMessages (const std::string& prompt)
{
    std::vector<ChatMessageForRequest> messages;
    juce::StringArray lines;
    lines.addLines (juce::String (prompt));

    juce::String currentRole;
    juce::String currentContent;

    auto flushCurrent = [&]()
    {
        const auto content = currentContent.trim();
        if (currentRole.isEmpty() || content.isEmpty())
            return;

        auto role = currentRole;
        auto messageContent = content;

        // OpenAI tool messages require a tool_call_id; until native tool calls are wired
        // through the C++ core, present tool results as user-visible context.
        if (role == "tool")
        {
            role = "user";
            messageContent = "Tool result:\n" + messageContent;
        }

        messages.push_back ({ role, messageContent });
    };

    for (const auto& line : lines)
    {
        juce::String role;
        if (parseRoleHeader (line, role))
        {
            flushCurrent();
            currentRole = role;
            currentContent.clear();
            continue;
        }

        if (currentRole.isNotEmpty())
        {
            if (currentContent.isNotEmpty())
                currentContent << "\n";
            currentContent << line;
        }
    }

    flushCurrent();

    if (messages.empty())
        messages.push_back ({ "user", juce::String (prompt).trim() });

    return messages;
}

void addChatMessageJson (juce::Array<juce::var>& target, const ChatMessageForRequest& source)
{
    juce::DynamicObject::Ptr message = new juce::DynamicObject();
    message->setProperty ("role", source.role);
    message->setProperty ("content", source.content);
    target.add (juce::var (message.get()));
}

juce::var parseJsonOrObject (const juce::String& json)
{
    auto parsed = juce::JSON::parse (json);
    return parsed.isVoid() ? juce::var (new juce::DynamicObject()) : parsed;
}

juce::var schemaProperty (const juce::String& type)
{
    juce::DynamicObject::Ptr property = new juce::DynamicObject();
    property->setProperty ("type", type);
    return juce::var (property.get());
}

void addFormulaEchoSchemaFields (juce::DynamicObject& properties)
{
    properties.setProperty ("echoes", schemaProperty ("integer"));
    properties.setProperty ("echo_delay", schemaProperty ("number"));
    properties.setProperty ("echo_transpose", schemaProperty ("number"));
    properties.setProperty ("echo_velocity_decay", schemaProperty ("number"));
}

void addFormulaEchoSchemaFieldsToItem (const juce::var& item)
{
    auto* itemObject = item.getDynamicObject();
    if (itemObject == nullptr)
        return;

    if (auto* properties = itemObject->getProperty ("properties").getDynamicObject())
        addFormulaEchoSchemaFields (*properties);
}

void augmentGenerateFormulaMidiSchema (juce::var& parameters)
{
    auto* rootObject = parameters.getDynamicObject();
    if (rootObject == nullptr)
        return;

    auto* rootProperties = rootObject->getProperty ("properties").getDynamicObject();
    if (rootProperties == nullptr)
        return;

    auto addArrayItemFields = [] (const juce::var& arrayProperty)
    {
        if (auto* arrayObject = arrayProperty.getDynamicObject())
            addFormulaEchoSchemaFieldsToItem (arrayObject->getProperty ("items"));
    };

    addArrayItemFields (rootProperties->getProperty ("motifs"));
    addArrayItemFields (rootProperties->getProperty ("voices"));

    if (auto* sectionsObject = rootProperties->getProperty ("sections").getDynamicObject())
        if (auto* sectionItem = sectionsObject->getProperty ("items").getDynamicObject())
            if (auto* sectionProperties = sectionItem->getProperty ("properties").getDynamicObject())
                if (auto* sectionVoices = sectionProperties->getProperty ("voices").getDynamicObject())
                    addFormulaEchoSchemaFieldsToItem (sectionVoices->getProperty ("items"));
}

void addFunctionTool (juce::Array<juce::var>& tools,
                      const juce::String& name,
                      const juce::String& description,
                      const juce::String& parametersJson)
{
    auto parameters = parseJsonOrObject (parametersJson);
    if (name == "generate_formula_midi")
        augmentGenerateFormulaMidiSchema (parameters);

    juce::DynamicObject::Ptr function = new juce::DynamicObject();
    function->setProperty ("name", name);
    function->setProperty ("description", description);
    function->setProperty ("parameters", parameters);

    juce::DynamicObject::Ptr tool = new juce::DynamicObject();
    tool->setProperty ("type", "function");
    tool->setProperty ("function", juce::var (function.get()));
    tools.add (juce::var (tool.get()));
}

void addPydanticStyleTools (juce::DynamicObject& root)
{
    juce::Array<juce::var> tools;

    addFunctionTool (tools, "classical_agent_tool",
                     "Ask the classical music expert for suggestions aligned with the user's musical interest.",
                     R"json({"type":"object","properties":{"user_music_interest":{"type":"string"}},"required":["user_music_interest"],"additionalProperties":false})json");
    addFunctionTool (tools, "free_jazz_agent_tool",
                     "Ask the free jazz expert for suggestions aligned with the user's musical interest.",
                     R"json({"type":"object","properties":{"user_music_interest":{"type":"string"}},"required":["user_music_interest"],"additionalProperties":false})json");
    addFunctionTool (tools, "style_blueprint_agent_tool",
                     "Ask the style blueprint expert to convert mood, genre, chord style, form, and instrumentation into concise section/pattern briefs. This expert describes musical constraints and sections but does not write lambdas.",
                     R"json({"type":"object","properties":{"user_music_interest":{"type":"string"}},"required":["user_music_interest"],"additionalProperties":false})json");
    addFunctionTool (tools, "mathematical_music_agent_tool",
                     "Ask the mathematical music expert for compact formula/lambda ideas for a musical pattern.",
                     R"json({"type":"object","properties":{"user_music_interest":{"type":"string"}},"required":["user_music_interest"],"additionalProperties":false})json");
    addFunctionTool (tools, "generate_midi",
                     "Generate a simple melodic MIDI file from notes, inter-onset intervals, instrument, and tempo.",
                     R"json({"type":"object","properties":{"note_seq":{"type":"array","items":{"oneOf":[{"type":"string"},{"type":"integer"}]}},"inter_onset_intervals":{"type":"array","items":{"type":"number"}},"instrument":{"type":"string"},"tempo":{"type":"integer"}},"required":["note_seq"],"additionalProperties":false})json");
    addFunctionTool (tools, "generate_chord_midi",
                     "Generate a MIDI file from a sequence of chords.",
                     R"json({"type":"object","properties":{"chord_seq":{"type":"array","items":{"type":"array","items":{"oneOf":[{"type":"string"},{"type":"integer"}]}}},"chord_intervals":{"type":"array","items":{"type":"number"}},"instrument":{"type":"string"},"tempo":{"type":"integer"}},"required":["chord_seq"],"additionalProperties":false})json");
    addFunctionTool (tools, "generate_polyphonic_midi",
                     "Generate a polyphonic or multi-instrument MIDI file from voice definitions.",
                     R"json({"type":"object","properties":{"voices":{"type":"array","items":{"type":"object","properties":{"type":{"type":"string"},"notes":{"type":"array"},"intervals":{"type":"array","items":{"type":"number"}},"instrument":{"type":"string"},"channel":{"type":"integer"},"start":{"type":"number"}},"required":["notes"],"additionalProperties":true}},"tempo":{"type":"integer"}},"required":["voices"],"additionalProperties":false})json");
    addFunctionTool (tools, "generate_formula_midi",
                     "Generate complex MIDI patterns from compact C++ lambda-style formulas for pitch, rhythm, optional gate, duration, and velocity. Current execution backend is backend=\"expr\": a safe deterministic expression VM, not a native C++ JIT. Use motifs to define repeated lambda figures once, then reference them from section voices with motif plus small numeric overrides such as transpose, duration_scale, velocity_offset, start, root, channel, gate_lambda, or steps. Use chord_offsets like [0,4,7], [0,5,10], or [0,7,14] to make compact chord/stab voices without extra note lists. Use swing with swing_grid=0.5 for shuffled eighth-note groove without spelling out delayed starts. Use echoes with echo_delay, echo_transpose, and echo_velocity_decay for canons, delay lines, or layered repeats without duplicate voices. Use euclid(pulses,steps,i[,rotation]) in gate_lambda for clave, techno, additive percussion, or polymeter. Use cycle(i,...)/choose(i,...) for compact repeating pitch, rhythm, velocity, or percussion cells. Use rand(i,seed), chance(prob,i,seed), and pick(i,seed,value) sparingly for repeatable humanized accents, velocities, and ornaments. Use sections to chain different lambda sets for intro/body/variation/outro while keeping each section concise. When MIDI generation is requested, use this as a real function/tool call rather than writing a prose or code-block example. rhythm_lambda returns inter-onset beats. gate_lambda: non-zero writes a note, zero rests while time advances. Formula variables: i, t/global beat, local_t/lt section beat, voice, section/s. For percussion, use channel 10, pitch_mode=\"midi\", and constants like clave, cowbell, conga_low, conga_high, bongo_low, bongo_high, shaker, closed_hat, open_hat, kick, snare, hand_clap. Supported expr syntax includes arithmetic, %, &&, ||, !, comparisons, ternary ?:, bracket lists like [60,64,67][i % 3], std::sin/sin, cos, euclid, cycle, choose, rand, chance, pick, mod, pow, min, max, clamp.",
                     R"json({"type":"object","properties":{"backend":{"type":"string","enum":["expr"]},"tempo":{"type":"integer"},"steps":{"type":"integer"},"root":{"type":"integer"},"scale":{"type":"array","items":{"type":"integer"}},"duration":{"type":"number"},"motifs":{"type":"array","items":{"type":"object","properties":{"name":{"type":"string"},"pitch_lambda":{"type":"string"},"rhythm_lambda":{"type":"string"},"gate_lambda":{"type":"string"},"duration_lambda":{"type":"string"},"velocity_lambda":{"type":"string"},"pitch_mode":{"type":"string","enum":["degree","midi"]},"instrument":{"type":"string"},"channel":{"type":"integer"},"steps":{"type":"integer"},"root":{"type":"integer"},"scale":{"type":"array","items":{"type":"integer"}},"start":{"type":"number"},"transpose":{"type":"number"},"duration_scale":{"type":"number"},"velocity_offset":{"type":"number"},"chord_offsets":{"type":"array","items":{"type":"integer"}},"swing":{"type":"number"},"swing_grid":{"type":"number"}},"required":["name","pitch_lambda","rhythm_lambda"],"additionalProperties":false}},"voices":{"type":"array","items":{"type":"object","properties":{"motif":{"type":"string"},"pitch_lambda":{"type":"string"},"rhythm_lambda":{"type":"string"},"gate_lambda":{"type":"string"},"duration_lambda":{"type":"string"},"velocity_lambda":{"type":"string"},"pitch_mode":{"type":"string","enum":["degree","midi"]},"instrument":{"type":"string"},"channel":{"type":"integer"},"steps":{"type":"integer"},"root":{"type":"integer"},"scale":{"type":"array","items":{"type":"integer"}},"start":{"type":"number"},"transpose":{"type":"number"},"duration_scale":{"type":"number"},"velocity_offset":{"type":"number"},"chord_offsets":{"type":"array","items":{"type":"integer"}},"swing":{"type":"number"},"swing_grid":{"type":"number"}},"additionalProperties":false}},"sections":{"type":"array","items":{"type":"object","properties":{"backend":{"type":"string","enum":["expr"]},"name":{"type":"string"},"start":{"type":"number"},"steps":{"type":"integer"},"root":{"type":"integer"},"scale":{"type":"array","items":{"type":"integer"}},"duration":{"type":"number"},"voices":{"type":"array","items":{"type":"object","properties":{"motif":{"type":"string"},"pitch_lambda":{"type":"string"},"rhythm_lambda":{"type":"string"},"gate_lambda":{"type":"string"},"duration_lambda":{"type":"string"},"velocity_lambda":{"type":"string"},"pitch_mode":{"type":"string","enum":["degree","midi"]},"instrument":{"type":"string"},"channel":{"type":"integer"},"steps":{"type":"integer"},"root":{"type":"integer"},"scale":{"type":"array","items":{"type":"integer"}},"start":{"type":"number"},"transpose":{"type":"number"},"duration_scale":{"type":"number"},"velocity_offset":{"type":"number"},"chord_offsets":{"type":"array","items":{"type":"integer"}},"swing":{"type":"number"},"swing_grid":{"type":"number"}},"additionalProperties":false}}},"required":["voices"],"additionalProperties":false}}},"additionalProperties":false})json");
    addFunctionTool (tools, "combine_midi",
                     "Combine generated MIDI files into a single MIDI file.",
                     R"json({"type":"object","properties":{"midi_files":{"type":"array","items":{"type":"string"}},"mode":{"type":"string","enum":["sequence","overlay"]}},"required":["midi_files"],"additionalProperties":false})json");
    addFunctionTool (tools, "analyze_midi",
                     "Analyze a generated MIDI file and return compact musical/timing statistics: note count, channels, pitch range, duration, density, gap range, pitch classes, score from 0.00 to 1.00, and assessment flags. Use this after generation when checking whether a complex mathematical pattern looks plausible before final response or playback. If score is below 0.70 or assessment includes clipped_top, too_high, too_low, very_sparse, very_dense, very_short, or very_long, repair or regenerate before playback when possible.",
                     R"json({"type":"object","properties":{"midi_file":{"type":"string"}},"required":["midi_file"],"additionalProperties":false})json");
    addFunctionTool (tools, "fit_midi_register",
                     "Repair a generated MIDI file by octave-transposing and clamping note events into a target register while preserving timing, channels, velocities, and program changes. Use this when analyze_midi reports too_high, clipped_top, or too_low and the rhythmic/formal shape is otherwise usable. Typical piano/bass target: low=36, high=84.",
                     R"json({"type":"object","properties":{"midi_file":{"type":"string"},"low":{"type":"integer"},"high":{"type":"integer"}},"required":["midi_file"],"additionalProperties":false})json");
    addFunctionTool (tools, "fit_midi_pitch_range",
                     "Repair a generated MIDI file whose pitch range is too narrow by deterministic octave spreading inside a target register while preserving timing, channels, velocities, and program changes. Use this when analyze_midi reports narrow_range or next=fit_midi_pitch_range and the rhythmic/formal shape is otherwise usable. Typical target_span: 18 or 24 semitones.",
                     R"json({"type":"object","properties":{"midi_file":{"type":"string"},"low":{"type":"integer"},"high":{"type":"integer"},"target_span":{"type":"integer"}},"required":["midi_file"],"additionalProperties":false})json");
    addFunctionTool (tools, "fit_midi_duration",
                     "Repair a generated MIDI file that is too long or too short. mode='trim' keeps timing and removes/shortens material after target_beats; mode='scale' compresses or expands all event times to target_beats. Prefer mode='trim' for very_long files, especially chordal/dense material, because scaling can make density worse. Prefer mode='scale' for very_short files. Typical target_beats: 32, 64, 96, or 128.",
                     R"json({"type":"object","properties":{"midi_file":{"type":"string"},"target_beats":{"type":"number"},"mode":{"type":"string","enum":["trim","scale"]}},"required":["midi_file"],"additionalProperties":false})json");
    addFunctionTool (tools, "humanize_midi",
                     "Repair a generated MIDI file that sounds too mechanical by applying deterministic timing and velocity variation while preserving note pairing, channels, pitches, program changes, and reproducibility. Use this when analyze_midi reports flat_velocity, clockwork_rhythm, or next=humanize_midi. Typical timing_ticks: 8-24, velocity_amount: 4-12.",
                     R"json({"type":"object","properties":{"midi_file":{"type":"string"},"timing_ticks":{"type":"number"},"velocity_amount":{"type":"integer"},"seed":{"type":"number"}},"required":["midi_file"],"additionalProperties":false})json");
    addFunctionTool (tools, "play_midi",
                     "Prepare a MIDI file for playback.",
                     R"json({"type":"object","properties":{"midi_file":{"type":"string"}},"required":["midi_file"],"additionalProperties":false})json");

    root.setProperty ("tools", tools);
}

bool containsAny (const juce::String& text, std::initializer_list<const char*> needles)
{
    for (const auto* needle : needles)
        if (text.contains (needle))
            return true;
    return false;
}

juce::String latestRealUserMessage (const std::vector<ChatMessageForRequest>& messages)
{
    for (auto it = messages.rbegin(); it != messages.rend(); ++it)
        if (it->role == "user" && ! it->content.trimStart().startsWithIgnoreCase ("Tool result:"))
            return it->content;

    return {};
}

bool hasToolResultContext (const std::vector<ChatMessageForRequest>& messages)
{
    for (const auto& message : messages)
        if (message.content.trimStart().startsWithIgnoreCase ("Tool result:"))
            return true;

    return false;
}

bool shouldRequireMusicTool (const std::vector<ChatMessageForRequest>& messages)
{
    if (hasToolResultContext (messages))
        return false;

    const auto userText = latestRealUserMessage (messages).toLowerCase();
    if (userText.isEmpty())
        return false;

    const bool asksForCreation = containsAny (userText, {
        "generate", "make", "write", "create", "compose", "build", "produce"
    });
    const bool mentionsMusic = containsAny (userText, {
        "midi", "music", "musical", "melody", "pattern", "rhythm", "canon", "marimba",
        "piano", "bass", "chord", "chords", "counterpoint", "polyrhythm", "phase", "phased", "shifting",
        "genre", "style", "mood", "nocturnal", "dub", "techno", "jazz", "classical", "ambient",
        "quartal", "modal", "minor", "major", "harmony", "groove", "ostinato", "section", "sections"
    });
    const bool wantsFormulaFriendlyPattern = containsAny (userText, {
        "formula", "lambda", "mathematical", "math", "algorithm", "complex", "pattern",
        "polyrhythm", "phase", "phased", "shifting", "canon", "reich", "minimal"
    });
    const bool wantsStyleBlueprint = containsAny (userText, {
        "mood", "genre", "style", "chord style", "chord", "chords", "quartal", "modal",
        "nocturnal", "dub", "techno", "ambient", "form", "section", "sections", "energy arc"
    });

    return asksForCreation && mentionsMusic && (wantsFormulaFriendlyPattern || wantsStyleBlueprint);
}

void requireToolChoice (juce::DynamicObject& root)
{
    root.setProperty ("tool_choice", "required");
}

juce::String toolCallsToXml (const juce::var& toolCalls)
{
    const auto* calls = toolCalls.getArray();
    if (calls == nullptr || calls->isEmpty())
        return {};

    juce::String xml;
    for (const auto& callVar : *calls)
    {
        auto* call = callVar.getDynamicObject();
        if (call == nullptr)
            continue;

        auto* function = call->getProperty ("function").getDynamicObject();
        if (function == nullptr)
            continue;

        const auto name = function->getProperty ("name").toString();
        const auto arguments = function->getProperty ("arguments").toString().trim();
        if (name.isEmpty())
            continue;

        xml << "<tool name=\"" << name << "\">"
            << (arguments.isEmpty() ? "{}" : arguments)
            << "</tool>\n";
    }

    return xml.trim();
}
}

OpenAICompatibleLLM::OpenAICompatibleLLM() = default;

bool OpenAICompatibleLLM::promptShouldRequireMusicToolForTesting (const juce::String& userPrompt)
{
    return shouldRequireMusicTool ({ { "user", userPrompt } });
}

void OpenAICompatibleLLM::setEndpoint (const juce::String& endpointIn)
{
    const juce::ScopedLock scopedLock (lock);
    endpoint = endpointIn.trim();
    if (! endpoint.endsWithChar ('/'))
        endpoint << "/";
}

juce::String OpenAICompatibleLLM::getEndpoint() const
{
    const juce::ScopedLock scopedLock (lock);
    return endpoint;
}

void OpenAICompatibleLLM::setModelName (const juce::String& modelNameIn)
{
    const juce::ScopedLock scopedLock (lock);
    modelName = modelNameIn.trim().isEmpty() ? "local-model" : modelNameIn.trim();
}

bool OpenAICompatibleLLM::readyToGenerate() { return true; }
bool OpenAICompatibleLLM::loadModel (const std::string& path)
{
    if (! path.empty())
        setModelName (path);
    status.store (LLMStatus::WaitingForJobs);
    return true;
}
void OpenAICompatibleLLM::unloadModel() {}
void OpenAICompatibleLLM::resetContext (uint32_t ctxLen) { contextLength = ctxLen; }
void OpenAICompatibleLLM::prepareSampler() {}

std::string OpenAICompatibleLLM::generate (std::string prompt, size_t maxLength)
{
    InferenceStats stats;
    return generateWithStats (prompt, maxLength, stats);
}

std::string OpenAICompatibleLLM::generateWithStats (const std::string& prompt, size_t maxLength, InferenceStats& stats)
{
    stats = {};
    status.store (LLMStatus::Generating);
    const auto start = std::chrono::steady_clock::now();

    juce::DynamicObject::Ptr root = new juce::DynamicObject();
    root->setProperty ("model", modelName);
    if (maxLength != std::numeric_limits<size_t>::max())
    {
        const auto visibleTokenBudget = juce::jmax (static_cast<int> (maxLength), 768);
        root->setProperty ("max_tokens", visibleTokenBudget);
    }
    root->setProperty ("temperature", 0.2);
    addPydanticStyleTools (*root);

    const auto chatMessages = promptToChatMessages (prompt);
    if (shouldRequireMusicTool (chatMessages))
        requireToolChoice (*root);

    juce::Array<juce::var> messages;
    for (const auto& message : chatMessages)
        addChatMessageJson (messages, message);
    root->setProperty ("messages", messages);

    const auto response = postJson (completionUrl(), juce::JSON::toString (juce::var (root.get()), false));
    if (response.trim().isEmpty())
    {
        status.store (LLMStatus::WaitingForJobs);
        return "[No response from the remote model server. Check that the endpoint is reachable and supports /chat/completions.]";
    }

    auto parsed = juce::JSON::parse (response);
    if (parsed.isVoid())
    {
        status.store (LLMStatus::WaitingForJobs);
        return ("[Remote model returned non-JSON data: " + response.substring (0, 300) + "]").toStdString();
    }

    juce::String content;
    if (auto* object = parsed.getDynamicObject())
    {
        if (object->hasProperty ("error"))
        {
            status.store (LLMStatus::WaitingForJobs);
            return ("[Remote model error: " + juce::JSON::toString (object->getProperty ("error"), true) + "]").toStdString();
        }

        if (auto* choices = object->getProperty ("choices").getArray())
                if (! choices->isEmpty())
                if (auto* choice = (*choices)[0].getDynamicObject())
                    if (auto* msg = choice->getProperty ("message").getDynamicObject())
                    {
                        content = msg->getProperty ("content").toString();
                        if (const auto toolCallText = toolCallsToXml (msg->getProperty ("tool_calls")); toolCallText.isNotEmpty())
                            content = toolCallText;
                        if (content.trim().isEmpty())
                        {
                            const auto finishReason = choice->getProperty ("finish_reason").toString();
                            const auto reasoning = msg->getProperty ("reasoning_content").toString()
                                + msg->getProperty ("reasoning").toString();
                            if (reasoning.trim().isNotEmpty())
                                content = "[The model returned reasoning but no visible assistant text. finish_reason="
                                    + finishReason + "]\n" + reasoning.trim();
                            else
                                content = "[The model returned an empty assistant message. finish_reason="
                                    + finishReason + "]";
                        }
                    }
    }

    if (content.trim().isEmpty())
        content = "[Remote model response did not contain choices[0].message.content.]";

    const auto end = std::chrono::steady_clock::now();
    stats.num_tokens_in_prompt = static_cast<size_t> (prompt.size() / 4);
    stats.num_tokens_in_response = static_cast<size_t> (content.length() / 4);
    stats.total_time_taken_for_inference = std::chrono::duration<double> (end - start).count();
    if (stats.total_time_taken_for_inference > 0.0)
    {
        stats.prompt_tokens_per_second = stats.num_tokens_in_prompt / stats.total_time_taken_for_inference;
        stats.inference_tokens_per_second = stats.num_tokens_in_response / stats.total_time_taken_for_inference;
    }

    status.store (LLMStatus::WaitingForJobs);
    return content.toStdString();
}

void OpenAICompatibleLLM::addPromptResponse (const std::string& prompt, const std::string& response)
{
    const juce::ScopedLock scopedLock (lock);
    history.emplace_back (prompt, response);
}

std::vector<std::pair<std::string, std::string>> OpenAICompatibleLLM::getPromptResponseHistory()
{
    const juce::ScopedLock scopedLock (lock);
    return history;
}

bool OpenAICompatibleLLM::popPromptResponse (std::pair<std::string, std::string>&)
{
    return false;
}

LLMStatus OpenAICompatibleLLM::getStatus() const { return status.load(); }

std::string OpenAICompatibleLLM::getStatusString() const
{
    switch (status.load())
    {
        case LLMStatus::WaitingForJobs: return "Waiting for jobs";
        case LLMStatus::Generating: return "Generating";
        case LLMStatus::LoadedButNeedContextReset: return "Model loaded but need context setup";
        case LLMStatus::ModelNotReady: return "Model not ready";
    }
    return "Unknown";
}

bool OpenAICompatibleLLM::isReady() const { return true; }
void OpenAICompatibleLLM::setThreadCount (int) {}
int OpenAICompatibleLLM::getModelTrainingContextLength() const { return static_cast<int> (contextLength); }
void OpenAICompatibleLLM::requestStop() { stopRequested.store (true); }
void OpenAICompatibleLLM::clearStopRequest() { stopRequested.store (false); }
void OpenAICompatibleLLM::registerPromptSettings (const std::string&, const PromptSettings&) {}
OpenAICompatibleLLM::PromptSettings OpenAICompatibleLLM::consumePromptSettings (const std::string&) { return {}; }

juce::String OpenAICompatibleLLM::completionUrl() const
{
    const juce::ScopedLock scopedLock (lock);
    return endpoint + "chat/completions";
}

juce::String OpenAICompatibleLLM::postJson (const juce::String& url, const juce::String& body) const
{
    juce::URL endpointUrl = juce::URL (url).withPOSTData (body);

    auto options = juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
                       .withExtraHeaders ("Content-Type: application/json\r\nAuthorization: Bearer not-needed\r\n")
                       .withConnectionTimeoutMs (30000)
                       .withNumRedirectsToFollow (2)
                       .withHttpRequestCmd ("POST");

    std::unique_ptr<juce::InputStream> stream (endpointUrl.createInputStream (options));
    if (stream == nullptr)
        return {};
    return stream->readEntireStreamAsString();
}
