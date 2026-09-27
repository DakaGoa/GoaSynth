// Drift guard between the website and the plugin it advertises.
//
// The site has already gone false once: the activation gate and the optional
// cloud AI engines both landed in Source/ *after* the pages were written,
// leaving ~20 statements on the site (two of them inside a published privacy
// policy) describing behaviour the plugin no longer had. Nothing caught it,
// because nothing compared the two.
//
// PublishCheck guards *leaks*; this guards *accuracy*. It reads the plugin's
// own source for facts the pages restate and fails when they disagree:
//
//   1. the price          - CONFIG.price vs every euro amount in every page
//   2. the preset bank    - the family counts in Presets.h vs the preset grid,
//                           plus every preset the page cites by name
//   3. the AI engines     - the default model of each cloud table vs the page
//   4. local storage      - every path the plugin writes vs the privacy policy
//   5. the trial length   - License.h's trialHours vs the copy
//
// Each check reads the number from the code that implements it, so it keeps
// working when the value changes - what it refuses to allow is the page and
// the plugin telling a buyer different things.
//
//   cmake --build build --config Release --target DocsCheck
//   ./build/Release/DocsCheck                       # or: ctest -R DocsCheck
//   ./build/Release/DocsCheck <docs> <repo root>
//
// Plain C++17 on purpose: no JUCE, no third-party regex, nothing to install.
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <regex>
#include <set>
#include <string>
#include <vector>

#ifndef GOA_DOCS_DIR
 #define GOA_DOCS_DIR "docs"
#endif
#ifndef GOA_REPO_ROOT
 #define GOA_REPO_ROOT "."
#endif

namespace fs = std::filesystem;

namespace
{
//==============================================================================
// Small file helpers. Deliberately dumb: this checker must not be clever enough
// to hide a problem, and it must never write anything.
std::string readFile (const fs::path& p)
{
    std::ifstream in (p, std::ios::binary);
    return std::string (std::istreambuf_iterator<char> (in), std::istreambuf_iterator<char>());
}

std::vector<std::string> readLines (const fs::path& p)
{
    std::vector<std::string> out;
    std::ifstream in (p, std::ios::binary);
    std::string line;

    while (std::getline (in, line))
    {
        if (! line.empty() && line.back() == '\r')
            line.pop_back();
        out.push_back (line);
    }

    return out;
}

std::string toLower (std::string s)
{
    std::transform (s.begin(), s.end(), s.begin(),
                    [] (unsigned char c) { return (char) std::tolower (c); });
    return s;
}

bool containsIgnoreCase (const std::string& haystack, const std::string& needle)
{
    return ! needle.empty() && toLower (haystack).find (toLower (needle)) != std::string::npos;
}

// The euro sign is E2 82 AC in UTF-8, spelled out so this file stays pure ASCII
// and does not depend on the compiler's source-charset switch.
const char* const euro = "\xE2\x82\xAC";

// "<euro> 15", "<euro>1,250.00" - the amount is the capture group. Grouping
// separators are only allowed *inside* the number, so "for 15, who the seller
// is" reads as 15 rather than "15,".
const std::regex& euroAmount()
{
    static const std::regex re ("\\xE2\\x82\\xAC\\s*([0-9]+(?:[.,][0-9]+)*)");
    return re;
}

std::vector<std::string> htmlFilesUnder (const fs::path& docsRoot)
{
    std::vector<std::string> out;

    for (const auto& e : fs::recursive_directory_iterator (docsRoot, fs::directory_options::skip_permission_denied))
        if (e.is_regular_file() && e.path().extension() == ".html")
            out.push_back (fs::relative (e.path(), docsRoot).generic_string());

    std::sort (out.begin(), out.end());
    return out;
}

std::string at (const std::string& file, std::size_t line)
{
    return file + ":" + std::to_string (line);
}
} // namespace

//==============================================================================
namespace
{
struct Result
{
    bool fatal = false;                  // a file this check needs is missing
    std::string fatalWhat;
    int checks = 0;
    std::vector<std::string> problems;
};

// The whole check, with no printing, so the self-test can call it directly.
Result scan (const fs::path& docsRoot, const fs::path& repoRoot)
{
    Result r;

    const fs::path indexHtml = docsRoot / "index.html";
    const fs::path appJs     = docsRoot / "app.js";
    const fs::path privacy   = docsRoot / "legal" / "privacy.html";
    const fs::path presetsH  = repoRoot / "Source" / "Presets.h";
    const fs::path aiCpp     = repoRoot / "Source" / "AiCloudGen.cpp";
    const fs::path licH      = repoRoot / "Source" / "License.h";

    for (const auto& needed : { indexHtml, appJs, privacy, presetsH, aiCpp, licH })
    {
        if (! fs::exists (needed))
        {
            r.fatal = true;
            r.fatalWhat = needed.string();
            return r;
        }
    }

    const std::string index = readFile (indexHtml);

    // ---- 1. one price, everywhere -----------------------------------------
    ++r.checks;
    {
        std::string priceDigits;
        std::smatch m;

        static const std::regex cfg (R"(price\s*:\s*'([^']*)')");
        const std::string js = readFile (appJs);
        if (std::regex_search (js, m, cfg))
        {
            std::smatch d;
            const std::string configured = m[1].str();
            if (std::regex_search (configured, d, euroAmount()))
                priceDigits = d[1].str();
        }

        if (priceDigits.empty())
        {
            r.problems.push_back ("docs/app.js: no CONFIG.price with a euro amount in it - the price "
                                  "check has nothing to compare against, so it cannot guard the pages");
        }
        else
        {
            for (const auto& rel : htmlFilesUnder (docsRoot))
            {
                const auto lines = readLines (docsRoot / rel);

                for (std::size_t i = 0; i < lines.size(); ++i)
                {
                    for (auto it = std::sregex_iterator (lines[i].begin(), lines[i].end(), euroAmount());
                         it != std::sregex_iterator(); ++it)
                    {
                        const std::string found = (*it)[1].str();

                        if (found != priceDigits)
                            r.problems.push_back (at ("docs/" + rel, i + 1) + ": says " + euro + found
                                + " but CONFIG.price in docs/app.js is " + euro + priceDigits
                                + " - the site would quote two different prices");
                    }
                }
            }
        }
    }

    // ---- 2. the preset grid matches the bank ------------------------------
    ++r.checks;
    {
        // The category headers are the plugin's own summary of the bank, and
        // some carry a trailing note ("// FX (15) - sweeps, textures, zaps").
        static const std::regex family (R"rx(^\s*//\s*([A-Z][A-Z ]*[A-Z])\s*\((\d+)\)\s*(?:[-\xE2\x80\x93\xE2\x80\x94].*)?$)rx");
        static const std::regex entry  (R"(^\s*\}, "[a-z]+" \},)");

        int headerTotal = 0, arrayEntries = 0, families = 0;
        std::vector<std::string> familyNames;

        for (const auto& line : readLines (presetsH))
        {
            std::smatch m;

            if (std::regex_match (line, m, family))
            {
                ++families;
                const std::string name = m[1].str();
                familyNames.push_back (name);
                const int count = std::stoi (m[2].str());
                headerTotal += count;

                const std::string card = "<h3>" + name + R"(\s*<span>)" + std::to_string (count) + "</span>";
                if (! std::regex_search (index, std::regex (card)))
                    r.problems.push_back ("docs/index.html: the preset grid has no \"<h3>" + name
                        + " <span>" + std::to_string (count) + "</span>\" card, but Source/Presets.h "
                          "ships " + std::to_string (count) + " " + name + " patches");
            }
            else if (std::regex_search (line, entry))
            {
                ++arrayEntries;
            }
        }

        if (families == 0)
            r.problems.push_back ("Source/Presets.h: no \"// FAMILY (n)\" headers found - the preset "
                                  "check would silently pass, so fix this checker, not the site");
        else
        {
            if (headerTotal != arrayEntries)
                r.problems.push_back ("Source/Presets.h: the category headers total "
                    + std::to_string (headerTotal) + " but the preset arrays hold "
                    + std::to_string (arrayEntries) + " entries - the headers are what the site is "
                      "checked against, so they must stay true");

            const std::regex phrase (std::to_string (headerTotal) + R"rx(\s*patches)rx");
            if (! std::regex_search (index.begin(), index.end(), phrase))
                r.problems.push_back ("docs/index.html: never says \"" + std::to_string (headerTotal)
                    + " patches\", which is how many the factory bank actually holds");

            // Every preset the page names has to exist in the bank. The counts
            // above cannot catch this: the site once shipped a "Patch of the
            // week" citing PSYTRANCE BASS 4 / UPLIFTING LEAD 2, neither of which
            // was ever in Presets.h. Presets cite themselves with
            // data-preset="NAME" so the guard has something exact to compare.
            auto isFamilyName = [&familyNames] (const std::string& n)
            {
                for (const auto& f : familyNames)
                    if (n.compare (0, f.size() + 1, f + " ") == 0)
                        return true;
                return false;
            };

            // The bank: every quoted multi-word ALL-CAPS name in Presets.h that
            // starts with a family name (so parameter ids and tags never match).
            std::set<std::string> bank;
            {
                static const std::regex quoted (R"rx("([A-Z][A-Z0-9]*(?: [A-Z0-9][A-Z0-9+\-]*)+)")rx");
                const std::string src = readFile (presetsH);

                for (std::sregex_iterator it (src.begin(), src.end(), quoted), end; it != end; ++it)
                {
                    const std::string name = (*it)[1].str();
                    if (isFamilyName (name))
                        bank.insert (name);
                }
            }

            if (bank.empty())
            {
                r.problems.push_back ("Source/Presets.h: no preset names could be read - the "
                                      "preset-name check would silently pass, so fix this checker");
            }
            else
            {
                static const std::regex cited (R"rx(data-preset="([^"]+)")rx");
                int citedCount = 0;

                for (std::sregex_iterator it (index.begin(), index.end(), cited), end; it != end; ++it)
                {
                    ++citedCount;
                    const std::string name = (*it)[1].str();
                    if (bank.count (name) == 0)
                        r.problems.push_back ("docs/index.html: cites the preset \"" + name
                            + "\", which Source/Presets.h does not contain");
                }

                if (citedCount == 0)
                    r.problems.push_back ("docs/index.html: no data-preset=\"...\" citations found - "
                                          "the preset-name check would silently pass, so fix this "
                                          "checker, not the site");
            }
        }
    }

    // ---- 3. the AI engines the page sells ---------------------------------
    ++r.checks;
    {
        const std::string ai = readFile (aiCpp);

        // The first entry of each model table is the engine's default; its
        // version token (3.6, 5.6) is what the page has to name. This is the
        // check that would have caught "2.0 Flash was shut down by Google".
        auto defaultToken = [&ai] (const char* table) -> std::string
        {
            const auto pos = ai.find (table);
            if (pos == std::string::npos)
                return {};

            static const std::regex firstEntry (R"rx(\{\s*"([a-z0-9.\-]+)",\s*"[^"]*"\s*\})rx");
            std::smatch m;

            const std::string rest = ai.substr (pos);
            if (! std::regex_search (rest, m, firstEntry))
                return {};

            const std::string id = m[1].str();
            const auto dash = id.find ('-');
            if (dash == std::string::npos)
                return {};

            const std::string tail = id.substr (dash + 1);
            const auto next = tail.find ('-');
            return next == std::string::npos ? tail : tail.substr (0, next);
        };

        const std::string geminiDefault  = defaultToken ("geminiModels");
        const std::string openAiDefault  = defaultToken ("openAiModels");

        if (geminiDefault.empty() || openAiDefault.empty())
            r.problems.push_back ("Source/AiCloudGen.cpp: could not read the default model from the "
                                  "geminiModels/openAiModels tables - the AI check would silently pass");

        for (const auto& token : { geminiDefault, openAiDefault })
            if (! token.empty() && ! containsIgnoreCase (index, token))
                r.problems.push_back ("docs/index.html: never names the current default model \""
                    + token + "\" from Source/AiCloudGen.cpp - the page is advertising a model the "
                      "plugin no longer calls");

        for (const char* engine : { "LOCAL", "GEMINI", "OPENAI", "CUSTOM" })
            if (! containsIgnoreCase (index, engine))
                r.problems.push_back (std::string ("docs/index.html: never mentions the ") + engine
                    + " engine, which Source/AiCloudGen.h exposes");
    }

    // ---- 4. what the plugin writes is what the policy documents ------------
    ++r.checks;
    {
        std::set<std::string> written;
        static const std::regex childPath (R"rx(getChildFile\s*\(\s*"([^"\n]+)"\s*\))rx");

        for (const auto& e : fs::directory_iterator (repoRoot / "Source"))
        {
            const auto ext = e.path().extension().string();
            if (ext != ".h" && ext != ".cpp")
                continue;

            const std::string text = readFile (e.path());

            for (auto it = std::sregex_iterator (text.begin(), text.end(), childPath);
                 it != std::sregex_iterator(); ++it)
            {
                const std::string name = (*it)[1].str();

                // Glob patterns are lookups, not files the plugin creates.
                if (name.find ('*') != std::string::npos || name.find ('?') != std::string::npos)
                    continue;

                written.insert (name);
            }
        }

        const std::string policy = readFile (privacy);

        for (const auto& name : written)
            if (policy.find (name) == std::string::npos)
                r.problems.push_back ("docs/legal/privacy.html: the plugin writes \"" + name
                    + "\" (named in Source/) but the privacy policy never mentions it - a published "
                      "policy that omits a local file is a false statement, not an oversight");

        if (written.empty())
            r.problems.push_back ("Source/: found no local storage paths at all - the privacy "
                                  "inventory check would silently pass, so fix this checker");
    }

    // ---- 5. the trial the page promises -----------------------------------
    ++r.checks;
    {
        static const std::regex hours (R"(trialHours\s*=\s*([0-9]+))");
        std::smatch m;
        const std::string lic = readFile (licH);

        if (! std::regex_search (lic, m, hours))
        {
            r.problems.push_back ("Source/License.h: no trialHours constant found - the trial check "
                                  "would silently pass");
        }
        else
        {
            const std::string n = m[1].str();

            if (! containsIgnoreCase (index, n + " hour") && ! containsIgnoreCase (index, n + "-hour"))
                r.problems.push_back ("docs/index.html: the plugin's trial is " + n + " hours "
                    "(Source/License.h) but the page never says so");
        }
    }

    return r;
}
} // namespace

//==============================================================================
#ifndef GOA_DOCS_CHECK_NO_MAIN
int main (int argc, char* argv[])
{
    const fs::path docsRoot = fs::absolute (fs::path (argc > 1 ? argv[1] : GOA_DOCS_DIR));
    const fs::path repoRoot = fs::absolute (fs::path (argc > 2 ? argv[2] : GOA_REPO_ROOT));

    const auto r = scan (docsRoot, repoRoot);

    if (r.fatal)
    {
        std::cout << "docs check: missing " << r.fatalWhat
                  << " - run it from the repository root, or pass <docs> <repo root>\n";
        return 2;
    }

    if (r.problems.empty())
    {
        std::cout << "docs check: " << r.checks << " claim(s) across the site still match the plugin "
                     "(price, preset bank, AI defaults, local storage, trial)\n";
        return 0;
    }

    std::cout << "docs check FAILED - " << r.problems.size() << " claim(s) the plugin contradicts:\n\n";

    for (const auto& p : r.problems)
        std::cout << "  " << p << "\n";

    std::cout << "\nThe site is written from the plugin, not from memory: when the plugin changes,\n"
                 "the pages have to change with it. See tests/DocsCheck.cpp for what is compared.\n";
    return 1;
}
#endif
