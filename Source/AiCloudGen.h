#pragma once

#include <juce_core/juce_core.h>
#include <map>

// Cloud-backed AI patch designer: sends the user's brief to an LLM (Google
// Gemini, any OpenAI-compatible chat endpoint, or a custom base URL such as a
// local Ollama/LM Studio server) and turns the model's JSON reply into a
// GoaSynth patch. The offline AiPatchGen stays the fallback when no key or
// network is available.
//
// The model never sees the plugin's internals — only a compact parameter
// table — and its reply is parsed defensively: unknown ids, out-of-range
// numbers and bogus choice names are dropped or clamped before anything
// touches the synth.
class AiCloudGen
{
public:
    using Patch = std::map<juce::String, float>;

    enum class Engine { Local, Gemini, OpenAI, Custom };

    struct Response
    {
        bool ok = false;
        juce::String title;         // ALL-CAPS patch name from the model
        juce::String description;   // one-line model comment about the patch
        juce::String error;         // human-readable failure reason when !ok
        Patch patch;
    };

    // Blocking network call — must NOT run on the message thread. `credential`
    // is the API key (Gemini/OpenAI) or the base URL (Custom, e.g.
    // "http://localhost:11434/v1"). Variation follows AiPatchGen: 0 subtle,
    // 1 normal, 2 wild (mapped to model temperature). `modelOverride` is a
    // model label/id; empty means the engine's newest default model.
    // A reply that fails validation automatically triggers ONE correction
    // follow-up (the model sees its rejected reply and what was wrong) before
    // generate() gives up.
    static Response generate (const juce::String& brief, Engine engine,
                              const juce::String& credential, int variation,
                              const juce::String& modelOverride = {});

    // Tiny live round-trip behind the overlay's TEST KEY button: sends a
    // minimal one-shot request to the selected engine and model and reports
    // whether the credential and model name are accepted. Blocking — must NOT
    // run on the message thread.
    static Response testConnection (Engine engine, const juce::String& credential,
                                    const juce::String& modelOverride = {});

    // The exact system prompt handed to the model (public for tests).
    static juce::String buildPrompt();

    // Defensively parse a model reply: strips markdown fences, extracts the
    // JSON object, whitelists parameter ids, clamps numbers and validates
    // choice strings. Returns ok=false with a reason when nothing usable
    // remains. `rejectLog` (optional) collects what was dropped or clamped —
    // used to brief the model during the self-healing correction round.
    static Response parseReply (const juce::String& modelText,
                                juce::String* rejectLog = nullptr);

    // The model's plain-text reply, extracted from a provider response body
    // (OpenAI choices / Gemini candidates shapes). Public for tests.
    static juce::String extractReplyText (Engine engine, const juce::String& replyJson);

    // The follow-up instruction sent with the one-round correction retry.
    static juce::String buildCorrectionMessage (const juce::String& rejectReason,
                                                const juce::String& details);

    // Models available per cloud engine, newest first; index 0 is the default.
    // `customModel` handles the Custom engine, whose model lives on the server.
    static juce::StringArray modelNames (Engine engine);

    // True when the credential looks usable for the engine (cheap sanity
    // check before spending a network round-trip).
    static bool credentialLooksValid (Engine engine, const juce::String& credential);
};
