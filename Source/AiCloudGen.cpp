#include "AiCloudGen.h"
#include "Parameters.h"

#include <random>

namespace
{

//==============================================================================
// The descriptor table is the single source of truth: it drives both the model
// prompt lines (id, human name, choices/range) and the response sanitizer
// (id whitelist, clamping, choice-name validation).
struct Desc
{
    const char* id;
    bool isChoice;
    bool isBool;
    int  numChoices;       // valid values 0..numChoices-1 when isChoice
    const char* choices;   // '|' separated, when isChoice
    float lo, hi;          // inclusive numeric bounds otherwise
    const char* name;      // human-readable name handed to the model
};

constexpr Desc descs[] =
{
    // ---- OSC A ------------------------------------------------------------
    { "osc1Wave", true, false, 6, "Saw|Square|PWM|Triangle|Sine|User", 0, 5, "OSC A wave" },
    { "osc1Oct",  false, true, 0, "", -2, 2, "OSC A octave" },
    { "osc1Fine", false, false, 0, "", -50, 50, "OSC A fine detune (cents)" },
    { "osc1Level", false, false, 0, "", 0, 1, "OSC A level" },
    { "osc1Pan",  false, false, 0, "", 0, 1, "OSC A pan (0.5 centre)" },
    { "osc1Phase", false, false, 0, "", 0, 360, "OSC A start phase (degrees)" },
    { "osc1PRand", true, true, 2, "Off|On", 0, 1, "OSC A random phase" },
    { "osc1WtPos", false, false, 0, "", 0, 1, "OSC A wavetable position" },
    // ---- OSC B ------------------------------------------------------------
    { "osc2Wave", true, false, 6, "Saw|Square|PWM|Triangle|Sine|User", 0, 5, "OSC B wave" },
    { "osc2Oct",  false, true, 0, "", -2, 2, "OSC B octave" },
    { "osc2Fine", false, false, 0, "", -50, 50, "OSC B fine detune (cents)" },
    { "osc2Level", false, false, 0, "", 0, 1, "OSC B level" },
    { "osc2Pan",  false, false, 0, "", 0, 1, "OSC B pan (0.5 centre)" },
    { "osc2Phase", false, false, 0, "", 0, 360, "OSC B start phase (degrees)" },
    { "osc2PRand", true, true, 2, "Off|On", 0, 1, "OSC B random phase" },
    { "osc2WtPos", false, false, 0, "", 0, 1, "OSC B wavetable position" },
    { "fmAmount", false, false, 0, "", 0, 1, "FM amount (OSC B modulates A)" },
    // ---- SUB / NOISE ------------------------------------------------------
    { "subWave", true, false, 3, "Square|Sine|Triangle", 0, 2, "Sub wave" },
    { "subOct",  false, true, 0, "", -2, 0, "Sub octave" },
    { "subLevel", false, false, 0, "", 0, 1, "Sub level" },
    { "noiseLevel", false, false, 0, "", 0, 1, "Noise level" },
    // ---- UNISON -----------------------------------------------------------
    { "uniVoices", false, true, 0, "", 1, 7, "Unison voice count" },
    { "uniDetune", false, false, 0, "", 0, 50, "Unison detune (cents)" },
    { "uniSpread", false, false, 0, "", 0, 1, "Unison stereo width" },
    { "drift", false, false, 0, "", 0, 1, "Analog drift amount" },
    // ---- FILTER A ---------------------------------------------------------
    { "filterType", true, false, 5, "LP 12dB|LP 24dB|HP 12dB|BP 12dB|NOTCH", 0, 4, "Filter A type" },
    { "cutoff", false, false, 0, "", 20, 20000, "Filter A cutoff (Hz)" },
    { "reso", false, false, 0, "", 0, 1, "Filter A resonance" },
    { "envAmt", false, false, 0, "", 0, 5, "Filter env amount (octaves)" },
    { "keytrack", false, false, 0, "", 0, 1, "Filter key track" },
    { "drive", false, false, 0, "", 0, 1, "Drive saturation" },
    { "modDepth", false, false, 0, "", 0, 3, "Modwheel to cutoff depth (octaves)" },
    // ---- FILTER B ---------------------------------------------------------
    { "filter2Type", true, false, 5, "LP 12dB|LP 24dB|HP 12dB|BP 12dB|NOTCH", 0, 4, "Filter B type" },
    { "cutoff2", false, false, 0, "", 20, 20000, "Filter B cutoff (Hz)" },
    { "reso2", false, false, 0, "", 0, 1, "Filter B resonance" },
    { "filterRoute", true, false, 3, "SERIAL|PARALLEL|SPLIT", 0, 2, "Filter routing" },
    // ---- FILTER ENVELOPE --------------------------------------------------
    { "filtA", false, false, 0, "", 0.001f, 5, "Filter attack (s)" },
    { "filtD", false, false, 0, "", 0.001f, 5, "Filter decay (s)" },
    { "filtS", false, false, 0, "", 0, 1, "Filter sustain" },
    { "filtR", false, false, 0, "", 0.01f, 12, "Filter release (s)" },
    // ---- AMP ENVELOPE -----------------------------------------------------
    { "ampA", false, false, 0, "", 0.001f, 5, "Amp attack (s)" },
    { "ampD", false, false, 0, "", 0.001f, 5, "Amp decay (s)" },
    { "ampS", false, false, 0, "", 0, 1, "Amp sustain" },
    { "ampR", false, false, 0, "", 0.01f, 12, "Amp release (s)" },
    // ---- LFOs -------------------------------------------------------------
    { "lfo1Rate", false, false, 0, "", 0.02f, 20, "LFO 1 rate (Hz)" },
    { "lfo1Wave", true, false, 4, "Sine|Triangle|Square|Random", 0, 3, "LFO 1 wave" },
    { "lfo1Target", true, false, 7, "Pitch|Cutoff|PWM|Volume|WT POS A|WT POS B|VOWEL", 0, 6, "LFO 1 target" },
    { "lfo1Depth", false, false, 0, "", 0, 1, "LFO 1 depth" },
    { "lfo1Unit", true, false, 2, "HZ|BPM", 0, 1, "LFO 1 rate unit" },
    { "lfo1Div", true, false, 6, "1/1|1/2|1/4|1/8|1/16|3/16", 0, 5, "LFO 1 beat division" },
    { "lfo2Rate", false, false, 0, "", 0.02f, 20, "LFO 2 rate (Hz)" },
    { "lfo2Wave", true, false, 4, "Sine|Triangle|Square|Random", 0, 3, "LFO 2 wave" },
    { "lfo2Target", true, false, 7, "Pitch|Cutoff|PWM|Volume|WT POS A|WT POS B|VOWEL", 0, 6, "LFO 2 target" },
    { "lfo2Depth", false, false, 0, "", 0, 1, "LFO 2 depth" },
    { "lfo2Unit", true, false, 2, "HZ|BPM", 0, 1, "LFO 2 rate unit" },
    { "lfo2Div", true, false, 6, "1/1|1/2|1/4|1/8|1/16|3/16", 0, 5, "LFO 2 beat division" },
    // ---- FX ---------------------------------------------------------------
    { "chorusRate", false, false, 0, "", 0.05f, 8, "Chorus rate (Hz)" },
    { "chorusDepth", false, false, 0, "", 0, 1, "Chorus depth" },
    { "chorusMix", false, false, 0, "", 0, 1, "Chorus mix" },
    { "phRate", false, false, 0, "", 0.02f, 8, "Phaser rate (Hz)" },
    { "phDepth", false, false, 0, "", 0, 1, "Phaser depth" },
    { "phMix", false, false, 0, "", 0, 1, "Phaser mix" },
    { "delaySync", true, false, 5, "Off|1/16|1/8 Dotted|1/8|1/4", 0, 4, "Delay sync" },
    { "delayTime", false, false, 0, "", 10, 2000, "Delay time (ms)" },
    { "delayFb", false, false, 0, "", 0, 0.92f, "Delay feedback" },
    { "delayMix", false, false, 0, "", 0, 1, "Delay mix" },
    { "revSize", false, false, 0, "", 0, 1, "Reverb size" },
    { "revDamp", false, false, 0, "", 0, 1, "Reverb damping" },
    { "revMix", false, false, 0, "", 0, 1, "Reverb mix" },
    { "ottDepth", false, false, 0, "", 0, 1, "OTT multiband depth" },
    { "ottLow", false, false, 0, "", 0, 1, "OTT low band amount" },
    { "ottMid", false, false, 0, "", 0, 1, "OTT mid band amount" },
    { "ottHigh", false, false, 0, "", 0, 1, "OTT high band amount" },
    { "ottOut", false, false, 0, "", -12, 6, "OTT output trim (dB)" },
    // ---- MASTER / VOICING -------------------------------------------------
    { "masterGain", false, false, 0, "", -60, 6, "Master gain (dB)" },
    { "glide", false, false, 0, "", 0, 1000, "Glide (ms)" },
    { "voicing", true, false, 3, "POLY|MONO|LEGATO", 0, 2, "Voicing mode" },
    { "polyMax", false, true, 0, "", 1, 16, "Max voices" },
    { "bendRange", false, true, 0, "", 0, 12, "Pitch bend range (semitones)" },
    // ---- SCALE QUANTIZER / VOWEL / TUNING ---------------------------------
    { "arpScale", true, false, 9, "CHROMATIC|MINOR|PHRYGIAN|HARM MIN|HUNG MIN|DBL HARM|DORIAN|MAJOR|PENTA MIN", 0, 8, "Arp scale quantizer" },
    { "arpRoot", true, false, 12, "C|C#|D|D#|E|F|F#|G|G#|A|A#|B", 0, 11, "Arp scale root" },
    { "scaleLock", true, true, 2, "Off|On", 0, 1, "Scale lock for live playing" },
    { "vowelOn", true, true, 2, "Off|On", 0, 1, "Vowel/formant filter" },
    { "masterHQ", true, true, 2, "Off|On", 0, 1, "2x oversampled master quality" },
    { "vowelMorph", false, false, 0, "", 0, 1, "Vowel morph (0=AH .. 1=OO)" },
    { "vowelRes", false, false, 0, "", 0, 1, "Vowel resonance" },
    { "vowelMix", false, false, 0, "", 0, 1, "Vowel dry/wet mix" },
    { "tuningFine", false, false, 0, "", -100, 100, "Fine tune (cents)" },
    // ---- PUMP / ANALOG / FILTER CHARACTER ----------------------------------
    { "pumpSync", true, false, 6, "Off|1/4|1/8|1/8 T|1/16|1/16 T", 0, 5, "Sidechain pump sync" },
    { "pumpDepth", false, false, 0, "", 0, 1, "Sidechain pump depth" },
    { "analogAmt", false, false, 0, "", 0, 1, "Analog wow/flutter character" },
    { "fDrive", false, false, 0, "", 0, 1, "Filter drive (pre-filter saturation)" },
    { "fFeedback", false, false, 0, "", 0, 1, "Filter feedback (self-oscillation screech)" },
};

constexpr size_t numDescs = sizeof (descs) / sizeof (descs[0]);

const Desc* findDesc (const juce::String& id)
{
    for (size_t i = 0; i < numDescs; ++i)
        if (id == descs[i].id)
            return &descs[i];
    return nullptr;
}

//==============================================================================
juce::String jsonEscape (const juce::String& s)
{
    juce::String out;
    for (const auto c : s)
    {
        switch (c)
        {
            case '"':  out << "\\\""; break;
            case '\\': out << "\\\\"; break;
            case '\n': out << "\\n";  break;
            case '\r': break;
            case '\t': out << "\\t";  break;
            default:   out << c; break;
        }
    }
    return out;
}

// Models love wrapping JSON in markdown fences or prose; find the outermost
// balanced {...} block. Returns an empty string when none exists.
juce::String extractJsonObject (const juce::String& text)
{
    const auto start = text.indexOfChar ('{');
    if (start < 0)
        return {};

    int depth = 0;
    bool inString = false;
    for (int i = start; i < text.length(); ++i)
    {
        const juce::juce_wchar c = text[i];
        if (inString)
        {
            if (c == '\\') { ++i; continue; }
            if (c == '"') inString = false;
            continue;
        }
        if (c == '"') { inString = true; continue; }
        if (c == '{') ++depth;
        else if (c == '}' && --depth == 0)
            return text.substring (start, i + 1);
    }
    return {};
}

// Coerce a JSON var into a patch value using the descriptor. Accepts numbers
// for numerics, "On"/"Off" for bools, and choice names (case-insensitive,
// tolerant of "LP24", "lowpass" style variants) for choice params.
bool coerceValue (const Desc& d, const juce::var& v, float& out)
{
    if (v.isVoid() || v.isUndefined())
        return false;

    if (d.isChoice)
    {
        juce::String s;
        if (v.isString())
            s = v.toString().trim().toUpperCase();
        else if (v.isInt() || v.isInt64() || v.isDouble())
        {
            const double n = (double) v;
            if (n < -0.5 || n > (double) d.numChoices - 0.5)
                return false;
            out = (float) n;
            return true;
        }
        else
            return false;

        if (s.isEmpty())
            return false;

        // Accept either the exact choice name or common synonyms.
        juce::String norm = s.replaceCharacter ('-', ' ');
        norm = norm.replace ("LOWPASS", "LP").replace ("HIGHPASS", "HP")
                   .replace ("BANDPASS", "BP").replace (" ", "");
        for (int i = 0; i < d.numChoices; ++i)
        {
            // Split the '|' list manually to keep indices aligned.
            const auto list = juce::StringArray::fromTokens (juce::String (d.choices), "|", "");
            if (i >= list.size()) break;
            juce::String cn = list[i].toUpperCase().replaceCharacter ('-', ' ').replace (" ", "");
            // Also compare the choice's first word: "lowpass" should match
            // "LP 12dB" (normalized "LP12DB" vs "LP" would not).
            const auto ct = list[i].toUpperCase().upToFirstOccurrenceOf (" ", false, true)
                                      .replaceCharacter ('-', ' ').replace (" ", "");
            if (cn == norm || ct == norm || list[i].equalsIgnoreCase (s))
            {
                out = (float) i;
                return true;
            }
        }
        // Generic on/off fallback: only reached when the choice list itself
        // did not contain Off/On, so "Off" never shifts an indexed choice.
        if (s == "ON" || s == "TRUE")  { out = 1.0f; return true; }
        if (s == "OFF" || s == "FALSE") { out = 0.0f; return true; }
        return false;
    }

    double n;
    if (v.isBool())            n = (bool) v ? 1.0 : 0.0;
    else if (v.isString())
    {
        const auto s = v.toString().trim();
        if (d.isBool)
        {
            const auto u = s.toUpperCase();
            if (u == "ON" || u == "TRUE" || u == "YES") { out = 1.0f; return true; }
            if (u == "OFF" || u == "FALSE" || u == "NO") { out = 0.0f; return true; }
        }
        n = s.getDoubleValue();
        if (n == 0.0 && ! s.containsOnly ("0123456789.-+eE"))
            return false;
    }
    else                       n = (double) v;

    if (n < (double) d.lo) n = (double) d.lo;
    if (n > (double) d.hi) n = (double) d.hi;
    out = (float) n;
    return true;
}

//==============================================================================
// Cloud models, newest first (index 0 = default). Refreshed Sep 2026: Google
// shut down gemini-2.0-flash and OpenAI moved the cheap fast tier to GPT-5.6
// Luna, so the old defaults would 404. The last entry of each list ("-") means
// "no override": the engine uses its server default (Custom/Ollama).
struct ModelEntry { const char* id; const char* label; };

const ModelEntry geminiModels[] =
{
    { "gemini-3.6-flash",      "GEMINI 3.6 FLASH" },
    { "gemini-3.5-flash",      "GEMINI 3.5 FLASH" },
    { "gemini-3.5-flash-lite", "GEMINI 3.5 LITE" },
};

const ModelEntry openAiModels[] =
{
    { "gpt-5.6-luna",  "GPT-5.6 LUNA" },
    { "gpt-5-mini",    "GPT-5 MINI" },
    { "gpt-4o-mini",   "GPT-4O MINI (LEGACY)" },
};

// Maps a model label (or raw id) back to the API model string. Unknown labels
// are passed through as-is so a hand-typed id in ai.txt still works.
static juce::String modelIdForLabel (AiCloudGen::Engine engine, const juce::String& label)
{
    const ModelEntry* table = nullptr;
    size_t n = 0;
    if (engine == AiCloudGen::Engine::Gemini)      { table = geminiModels; n = sizeof (geminiModels) / sizeof (ModelEntry); }
    else if (engine == AiCloudGen::Engine::OpenAI) { table = openAiModels;  n = sizeof (openAiModels)  / sizeof (ModelEntry); }

    const auto l = label.trim().toUpperCase();
    if (l.isEmpty() || l == "-" || l == "DEFAULT" || l == "SERVER DEFAULT")
        return table != nullptr ? juce::String (table[0].id) : juce::String();

    if (table == nullptr)
        return label;                    // Custom: the override is the model id
    for (size_t i = 0; i < n; ++i)
        if (l == juce::String (table[i].label).toUpperCase() || l == juce::String (table[i].id).toUpperCase())
            return table[i].id;
    return label;                        // custom model id typed by the user
}

// Appends "model": "..." to a chat-completions body. jsonEscape handles
// hand-typed overrides; a null model string keeps the server default.
static void appendModel (juce::String& body, const juce::String& model)
{
    if (model.trim().isEmpty() || model.trim() == "-")
        return;
    body.replace ("\"messages\":",
                  "\"model\":\"" + jsonEscape (model.trim()) + "\",\"messages\":", false);
}

//==============================================================================
// Shared HTTP plumbing for generate() and testConnection().
juce::var buildGeminiBody (const juce::String& brief, double temperature);
juce::var buildOpenAiBody (const juce::String& brief, double temperature);
juce::String describeHttpError (const juce::String& reply, int code);
AiCloudGen::Response finishGenerate (const juce::String& reply);

juce::String endpointFor (AiCloudGen::Engine engine, const juce::String& key,
                          const juce::String& modelOverride, juce::String& auth,
                          juce::String& body, juce::String& errorOut)
{
    const auto cleanKey = key.trim();
    juce::String url;

    if (engine == AiCloudGen::Engine::Gemini)
    {
        url = "https://generativelanguage.googleapis.com/v1beta/models/"
              + modelIdForLabel (engine, modelOverride) + ":generateContent";
        auth = "x-goog-api-key: *** " + cleanKey;
        body = juce::JSON::toString (buildGeminiBody ("ping", 0.0));
    }
    else
    {
        const juce::String base = engine == AiCloudGen::Engine::Custom
            ? cleanKey.trimCharactersAtEnd ("/")
            : "https://api.openai.com/v1";
        if (engine == AiCloudGen::Engine::Custom && ! base.contains ("://"))
        {
            errorOut = "custom engine needs a base URL, e.g. http://localhost:11434/v1";
            return {};
        }
        url = base + "/chat/completions";
        if (engine == AiCloudGen::Engine::OpenAI)
            auth = "Authorization: Bearer " + cleanKey;
        body = juce::JSON::toString (buildOpenAiBody ("ping", 0.0));
        appendModel (body, modelIdForLabel (engine, modelOverride));
    }
    return url;
}

AiCloudGen::Response postJson (const juce::String& url, const juce::String& auth,
                               const juce::String& body, int timeoutMs,
                               juce::String* rawOut = nullptr, int* codeOut = nullptr)
{
    AiCloudGen::Response r;
    juce::URL u (url);
    u = u.withPOSTData (body);

    juce::WebInputStream stream (u, false);
    stream.withCustomRequestCommand ("POST")
          .withExtraHeaders (auth.isEmpty()
              ? juce::String ("Content-Type: application/json\r\nAccept: application/json")
              : auth + "\r\nContent-Type: application/json\r\nAccept: application/json")
          .withConnectionTimeout (timeoutMs);

    if (! stream.connect (nullptr))
    {
        r.error = "could not reach the AI service (check the key and your network)";
        return r;
    }

    const int code = stream.getStatusCode();
    const auto reply = stream.readEntireStreamAsString();
    if (codeOut != nullptr) *codeOut = code;
    if (rawOut  != nullptr) *rawOut  = reply;
    if (code < 200 || code >= 300)
    {
        r.error = describeHttpError (reply, code);
        return r;
    }
    r.ok = true;                 // HTTP success; payload parsing is the caller's job
    return r;
}

//==============================================================================
// Correction-round request bodies: the original brief + the model's rejected
// reply + a follow-up instruction, as a proper multi-turn conversation.
juce::var buildOpenAiCorrectionBody (const juce::String& brief, const juce::String& firstReply,
                                     const juce::String& correction, double temperature)
{
    auto* sys = new juce::DynamicObject();
    sys->setProperty ("role", "system");
    sys->setProperty ("content", AiCloudGen::buildPrompt());
    auto* usr = new juce::DynamicObject();
    usr->setProperty ("role", "user");
    usr->setProperty ("content", brief);
    auto* asst = new juce::DynamicObject();
    asst->setProperty ("role", "assistant");
    asst->setProperty ("content", firstReply);
    auto* fix = new juce::DynamicObject();
    fix->setProperty ("role", "user");
    fix->setProperty ("content", correction);
    juce::Array<juce::var> msgs { juce::var (sys), juce::var (usr), juce::var (asst), juce::var (fix) };

    auto* body = new juce::DynamicObject();
    body->setProperty ("messages", msgs);
    body->setProperty ("temperature", temperature);
    return juce::var (body);
}

juce::var buildGeminiCorrectionBody (const juce::String& brief, const juce::String& firstReply,
                                     const juce::String& correction, double temperature)
{
    auto mkPart = [] (const juce::String& text) -> juce::var
    {
        auto* t = new juce::DynamicObject();
        t->setProperty ("text", text);
        return juce::var (t);
    };
    auto mkContent = [&] (const juce::String& role, const juce::String& text) -> juce::var
    {
        auto* c = new juce::DynamicObject();
        juce::Array<juce::var> parts { mkPart (text) };
        c->setProperty ("parts", parts);
        c->setProperty ("role", role);
        return juce::var (c);
    };
    juce::Array<juce::var> contents { mkContent ("user", brief),
                                      mkContent ("model", firstReply),
                                      mkContent ("user", correction) };

    auto* gen = new juce::DynamicObject();
    gen->setProperty ("temperature", temperature);
    auto* body = new juce::DynamicObject();
    body->setProperty ("contents", contents);
    body->setProperty ("generationConfig", juce::var (gen));
    return juce::var (body);
}

//==============================================================================
// Request bodies. Both shapes embed the system prompt + the user brief and a
// temperature mapped from the variation strength.
juce::var buildOpenAiBody (const juce::String& brief, double temperature)
{
    auto* sys = new juce::DynamicObject();
    sys->setProperty ("role", "system");
    sys->setProperty ("content", AiCloudGen::buildPrompt());
    auto* usr = new juce::DynamicObject();
    usr->setProperty ("role", "user");
    usr->setProperty ("content", brief);
    juce::Array<juce::var> msgs { juce::var (sys), juce::var (usr) };

    auto* body = new juce::DynamicObject();
    body->setProperty ("messages", msgs);
    body->setProperty ("temperature", temperature);
    return juce::var (body);
}

juce::var buildGeminiBody (const juce::String& brief, double temperature)
{
    auto* txt = new juce::DynamicObject();
    txt->setProperty ("text", AiCloudGen::buildPrompt() + brief);
    juce::Array<juce::var> parts { juce::var (txt) };
    auto* content = new juce::DynamicObject();
    content->setProperty ("parts", parts);
    content->setProperty ("role", "user");
    juce::Array<juce::var> contents { juce::var (content) };

    auto* gen = new juce::DynamicObject();
    gen->setProperty ("temperature", temperature);
    auto* body = new juce::DynamicObject();
    body->setProperty ("contents", contents);
    body->setProperty ("generationConfig", juce::var (gen));
    return juce::var (body);
}

// Human-readable failure including the service's own error message when the
// response carries one (both providers put a message inside an error object).
juce::String describeHttpError (const juce::String& reply, int code)
{
    juce::String detail;
    const auto parsed = juce::JSON::parse (reply);
    if (parsed.isObject())
    {
        const auto err = parsed.getProperty ("error", {});
        if (err.isObject())
            detail = err.getProperty ("message", {}).toString();
        else if (err.isString())
            detail = err.toString();
    }
    juce::String s = "AI service error " + juce::String (code);
    if (code == 401 || code == 403)
        s << " (key rejected)";
    else if (code == 429)
        s << " (rate limit / quota)";
    if (detail.isNotEmpty())
        s << ": " + detail.trim().substring (0, 160);
    return s;
}

AiCloudGen::Response finishGenerate (const juce::String& reply)
{
    auto r = AiCloudGen::parseReply (reply);
    if (! r.ok)
        r.error = r.error.isNotEmpty() ? r.error
                                       : "the AI service returned an unusable reply";
    return r;
}

} // namespace

juce::StringArray AiCloudGen::modelNames (Engine engine)
{
    juce::StringArray names;
    if (engine == Engine::Gemini)
        for (const auto& m : geminiModels) names.add (m.label);
    else if (engine == Engine::OpenAI)
        for (const auto& m : openAiModels) names.add (m.label);
    else if (engine == Engine::Custom)
        names.add ("SERVER DEFAULT");
    return names;
}

//==============================================================================
juce::String AiCloudGen::buildPrompt()
{
    juce::String params;
    for (size_t i = 0; i < numDescs; ++i)
    {
        const auto& d = descs[i];
        params << "  " << d.id << " = " << d.name;
        if (d.isChoice)
            params << "; one of: " << d.choices << " (give the name or index)";
        else if (d.isBool)
            params << "; integer " << (int) d.lo << ".." << (int) d.hi;
        else
            params << "; range " << d.lo << " to " << d.hi;
        params << "\n";
    }

    return
        "You are a sound designer for GoaSynth, a Goa trance virtual analog "
        "synthesizer. Turn the user's brief into one patch.\n\n"
        "Rules:\n"
        "- Reply with ONLY a JSON object, no markdown, no prose.\n"
        "- Shape: {\"title\": \"SHORT PATCH NAME\", \"description\": \"one "
        "sentence about the sound\", \"patch\": { <param>: <value>, ... }}\n"
        "- The title is at most 22 characters, upper case, no quotes.\n"
        "- You do not need every parameter; give only the ones that matter "
        "for the brief. Omitted parameters keep sensible defaults.\n"
        "- Respect the ranges exactly; stay inside them.\n"
        "- Goa knowledge matters: acid means high resonance and fast filter "
        "decays; screech means HP filters and detuned saws; rolling bass means "
        "saturated low saw + sub with 16th gating; pads mean slow attacks, "
        "wide unison and reverb; plucks mean short decay with delay; risers "
        "mean long filter attacks and pitch LFOs.\n\n"
        "Parameters (id = meaning; constraint):\n" + params +
        "\nUser brief: ";
}

bool AiCloudGen::credentialLooksValid (Engine engine, const juce::String& credential)
{
    if (engine == Engine::Local)
        return true;
    return credential.trim().length() >= 8;
}

AiCloudGen::Response AiCloudGen::parseReply (const juce::String& modelText,
                                             juce::String* rejectLog)
{
    Response r;

    const auto json = extractJsonObject (modelText);
    if (json.isEmpty())
    {
        r.error = "the model reply contained no JSON object";
        return r;
    }

    const auto parsed = juce::JSON::parse (json);
    if (! parsed.isObject())
    {
        r.error = "the model reply was not valid JSON";
        return r;
    }

    r.title = parsed.getProperty ("title", {}).toString().trim().toUpperCase();
    if (r.title.isEmpty())
        r.title = "AI CLOUD PATCH";
    if (r.title.length() > 26)
        r.title = r.title.substring (0, 26);

    r.description = parsed.getProperty ("description", {}).toString().trim();

    const auto patchVar = parsed.getProperty ("patch", {});
    if (! patchVar.isObject())
    {
        r.error = "the model reply had no patch object";
        return r;
    }

    // juce::DynamicObject exposes properties through getProperties() (a
    // NamedValueSet) — walk it and whitelist every id.
    if (auto* obj = patchVar.getDynamicObject())
    {
        for (const auto& prop : obj->getProperties())
        {
            const auto id = prop.name.toString().trim();
            const Desc* d = findDesc (id);
            if (d == nullptr)
            {
                if (rejectLog != nullptr)
                    *rejectLog << "\n- unknown parameter id \"" << id
                               << "\" — not a GoaSynth parameter, remove it";
                continue;                       // unknown id: dropped
            }

            float value = 0.0f;
            if (coerceValue (*d, prop.value, value))
            {
                r.patch[id] = value;
                continue;
            }

            // Values that fail validation are dropped, not guessed — but the
            // reason lands in the reject log so a correction round can fix it.
            if (rejectLog != nullptr)
            {
                *rejectLog << "\n- \"" << id << "\" (" << d->name
                           << "): the value you gave is unusable";
                if (d->isChoice)
                    *rejectLog << "; use one of: " << d->choices;
                else
                    *rejectLog << "; it must be a number between " << d->lo
                               << " and " << d->hi;
            }
        }
    }

    if (r.patch.empty())
    {
        r.error = "no known parameters survived validation";
        return r;
    }

    r.ok = true;
    return r;
}

//==============================================================================
// Pull the model's plain text out of the provider response envelope:
// OpenAI chat shape (choices[0].message.content) or Gemini shape
// (candidates[0].content.parts[*].text). Anything that is not a recognised
// envelope is returned as-is, so raw model text still parses.
juce::String AiCloudGen::extractReplyText (Engine engine, const juce::String& replyJson)
{
    const auto root = juce::JSON::parse (replyJson);
    if (! root.isObject())
        return replyJson;                    // not an envelope: treat as raw text

    if (engine == Engine::Gemini)
    {
        const auto cands = root.getProperty ("candidates", {});
        if (cands.isArray() && cands.size() > 0)
        {
            const auto parts = cands[0].getProperty ("content", {}).getProperty ("parts", {});
            if (auto* arr = parts.getArray())
            {
                juce::String text;
                for (const auto& p : *arr)
                    text += p.getProperty ("text", {}).toString();
                if (text.isNotEmpty())
                    return text;
            }
        }
        return {};
    }

    // OpenAI-compatible chat shape (also what most CUSTOM servers speak).
    const auto choices = root.getProperty ("choices", {});
    if (choices.isArray() && choices.size() > 0)
        return choices[0].getProperty ("message", {}).getProperty ("content", {}).toString();
    return {};
}

juce::String AiCloudGen::buildCorrectionMessage (const juce::String& rejectReason,
                                                 const juce::String& details)
{
    juce::String s = "Your JSON reply could not be used: " + rejectReason.trim();
    if (details.trim().isNotEmpty())
        s << "\n" << details.trim();
    s << "\n\nResend the complete corrected patch as ONLY a JSON object, same shape"
         " as before ({\"title\": ..., \"description\": ..., \"patch\": {...}})."
         " Keep every parameter that was already valid, fix or remove the ones"
         " listed above, respect the exact ranges and choice names from the"
         " parameter table, and reply with no markdown and no prose.";
    return s;
}

//==============================================================================
AiCloudGen::Response AiCloudGen::generate (const juce::String& brief, Engine engine,
                                           const juce::String& credential, int variation,
                                           const juce::String& modelOverride)
{
    Response r;
    const auto clean = brief.trim();
    if (clean.isEmpty())
    {
        r.error = "empty brief";
        return r;
    }

    const juce::String key = credential.trim();

    // ---- request body ------------------------------------------------------
    const double temperature = variation <= 0 ? 0.35 : variation >= 2 ? 1.15 : 0.75;

    juce::String body;
    juce::String url;
    juce::String auth;

    const juce::String model = modelIdForLabel (engine, modelOverride);

    if (engine == Engine::Gemini)
    {
        url = "https://generativelanguage.googleapis.com/v1beta/models/"
              + model + ":generateContent";
        auth = "x-goog-api-key: *** " + key;
        body = juce::JSON::toString (buildGeminiBody (clean, temperature));
    }
    else
    {
        const juce::String base = engine == Engine::Custom
            ? key.trimCharactersAtEnd ("/")
            : "https://api.openai.com/v1";
        if (engine == Engine::Custom && ! base.contains ("://"))
        {
            r.error = "custom engine needs a base URL, e.g. http://localhost:11434/v1";
            return r;
        }
        url = base + "/chat/completions";
        if (engine == Engine::OpenAI)
            auth = "Authorization: Bearer " + key;
        body = juce::JSON::toString (buildOpenAiBody (clean, temperature));
        appendModel (body, model);
    }

    // ---- HTTP (blocking; caller must not be the message thread) ------------
    juce::String raw;
    auto http = postJson (url, auth, body, 20000, &raw);
    if (! http.ok)
    {
        r.error = http.error;
        return r;
    }

    // First parse attempt; a failure arms ONE self-healing correction round
    // in which the model sees its own rejected reply and exactly what the
    // sanitizer dropped or clamped.
    const auto modelText = extractReplyText (engine, raw);
    juce::String rejectLog;
    r = parseReply (modelText, &rejectLog);
    if (r.ok)
        return r;

    const juce::String correction = buildCorrectionMessage (r.error, rejectLog);
    juce::String retryBody = engine == Engine::Gemini
        ? juce::JSON::toString (buildGeminiCorrectionBody (clean, modelText, correction, temperature))
        : juce::JSON::toString (buildOpenAiCorrectionBody (clean, modelText, correction, temperature));
    if (engine != Engine::Gemini)
        appendModel (retryBody, model);

    juce::String raw2;
    auto http2 = postJson (url, auth, retryBody, 20000, &raw2);
    if (! http2.ok)
    {
        r.error += " (correction round failed: " + http2.error + ")";
        return r;
    }

    auto corrected = parseReply (extractReplyText (engine, raw2));
    if (corrected.ok)
        return corrected;

    r.error += " (correction round also failed: " + corrected.error + ")";
    return r;
}

//==============================================================================
// TEST KEY: minimal round-trip against the selected engine + model.
AiCloudGen::Response AiCloudGen::testConnection (Engine engine, const juce::String& credential,
                                                 const juce::String& modelOverride)
{
    Response r;
    const auto clean = credential.trim();
    if (clean.isEmpty())
    {
        r.error = "paste an API key (or endpoint URL) first";
        return r;
    }
    juce::String url, auth, body, preErr;
    url = endpointFor (engine, clean, modelOverride, auth, body, preErr);
    if (url.isEmpty())
    {
        r.error = preErr;                    // precise engine error (bad URL, etc.)
        return r;
    }
    if (! credentialLooksValid (engine, clean))
    {
        r.error = "that credential looks too short to be valid";
        return r;
    }

    auto res = postJson (url, auth, body, 10000);
    if (! res.ok)
        return res;                          // carries describeHttpError detail

    const juce::String model = modelIdForLabel (engine, modelOverride);
    r.ok = true;
    r.title = "CONNECTION OK";
    r.description = model.isNotEmpty()
        ? model + " accepted the key and answered"
        : "server default model accepted the key and answered";
    return r;
}
