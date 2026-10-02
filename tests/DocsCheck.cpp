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
//   6. the address        - the canonical link in every page, sitemap.xml and
//                           robots.txt all naming one origin, with the sitemap
//                           listing every published page (and nothing else),
//                           and og:url / the social images / the JSON-LD URLs
//                           derived from the canonicals rather than written
//                           down again.
//                           Search-engine verification files are tokens, not
//                           pages, so they are exempt and stay out of the list
//                           - but the token itself is pinned byte for byte,
//                           because a search console is the only authority on
//                           what it issued, and a token that drifts fails
//                           silently: the file still serves 200 and the page
//                           is unchanged, so nothing else here would notice
//   7. the reviews        - the visible reviews, their average, the count and
//                           the aggregateRating in the head all agreeing, and
//                           every incentivized review disclosing that it was
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
#include <map>
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

    // ---- 6. one address, and a page list that is actually complete ---------
    // Search engines read the address out of each page's canonical link, out of
    // sitemap.xml and out of robots.txt, and they treat a page that names one
    // host in one place and another host in another as two weak pages instead of
    // one strong one. The same pass catches the quieter failure: a published
    // page that the sitemap never lists, which is a page nobody crawls. Both are
    // what a hand-edited set of static files grows the moment the site moves to
    // a custom domain.
    ++r.checks;
    {
        static const std::regex canonicalRe (R"re(<link\s+rel="canonical"\s+href="([^"]+)")re");
        static const std::regex locRe       (R"re(<loc>([^<]+)</loc>)re");
        static const std::regex sitemapRe   (R"re(Sitemap:\s*(\S+))re");

        // "https://host/path" -> "https://host"; anything without a scheme is
        // returned whole so the comparison below still catches a relative link.
        auto originOf = [] (const std::string& url)
        {
            const auto scheme = url.find ("://");
            if (scheme == std::string::npos)
                return url;

            const auto slash = url.find ('/', scheme + 3);
            return slash == std::string::npos ? url : url.substr (0, slash);
        };

        // A search-engine verification file is a token, not a page: no navigation,
        // no canonical, no place in the sitemap - and it has to sit at the site
        // root, because that is where the console fetches it from. It is the one
        // .html file under docs/ allowed to skip the rules below.
        // Case-insensitive: the tokens are handed out in lowercase today, but a
        // guard that only recognises today's spelling is a guard that breaks on
        // the day a console changes it.
        static const std::regex verificationFile (R"(^google[a-z0-9]+\.html$)", std::regex::icase);

        // The token is the one fact under docs/ that is written down here rather
        // than derived from the code that decides it: every other number on the
        // site is read out of Source/ or app.js, but what this file says was
        // decided by Search Console, and there is nothing in the repository to
        // read it from. So it is pinned, bytes included.
        //
        // It is worth a pin because its failure is invisible. Google's HTML-file
        // method fetches the file and looks for its own name inside it, so a
        // token re-saved with a trailing newline, saved as UTF-8 with a byte
        // order mark, passed through an editor that curls the quotes, or simply
        // left over from a property that was deleted reads as *not verified* -
        // while still answering 200 and leaving every page on the site looking
        // exactly as it did before. Nothing visual changes, so nothing visual
        // catches it, which is the argument for a test rather than a convention.
        //
        // Moving to a new property is a deliberate act: replace both the file
        // and these two lines together, and the build keeps working.
        const std::string tokenFile = "googlebe8101dec58dcb76.html";
        const std::string tokenBody = "google-site-verification: googlebe8101dec58dcb76.html";

        // Bytes are what a console compares, and the usual drift is a byte a
        // person cannot see in an editor, so failures show the bytes rather
        // than the text: "\n", "\xEF" and so on, plus the length.
        auto showBytes = [] (const std::string& s)
        {
            static const char* const hex = "0123456789ABCDEF";
            std::string out;

            for (const unsigned char c : s)
            {
                if (c == '\n')      out += "\\n";
                else if (c == '\r') out += "\\r";
                else if (c == '\t') out += "\\t";
                else if (c < 0x20 || c > 0x7e)
                {
                    out += "\\x";
                    out += hex[(c >> 4) & 0xF];
                    out += hex[c & 0xF];
                }
                else
                    out += (char) c;
            }

            return out;
        };

        const fs::path tokenPath = docsRoot / tokenFile;

        if (! fs::exists (tokenPath))
        {
            r.problems.push_back ("docs/" + tokenFile + ": missing - this is the file the search "
                "console fetches to prove the site is yours, and it has to stay published for as "
                "long as the property exists, because verification is re-checked rather than "
                "granted once");
        }
        else
        {
            const std::string found = readFile (tokenPath);

            if (found != tokenBody)
                r.problems.push_back ("docs/" + tokenFile + ": holds \"" + showBytes (found)
                    + "\" (" + std::to_string (found.size()) + " bytes) but the verification token "
                      "is \"" + showBytes (tokenBody) + "\" (" + std::to_string (tokenBody.size())
                    + " bytes) - a console reads this file byte for byte, so a trailing newline, a "
                      "byte-order mark or a re-saved encoding silently un-verifies the property");
        }

        std::set<std::string> pages;    // the canonical URL of every published page
        std::map<std::string, std::string> canonByRel;
        std::string origin;

        for (const auto& rel : htmlFilesUnder (docsRoot))
        {
            if (std::regex_match (rel, verificationFile))
            {
                // The pin above covers today's token. This covers the next one,
                // and it is the rule that has to hold for any of them: the file
                // contains the marker, a space, its own file name as spelled on
                // disk, and nothing else. Renaming the token without rewriting
                // what is inside it is the same silent failure as editing the
                // bytes, and this is the half of the check that does not need to
                // know which token the console issued.
                const std::string body = readFile (docsRoot / rel);
                const std::string want = "google-site-verification: " + rel;

                if (body != want)
                    r.problems.push_back ("docs/" + rel + ": holds \"" + showBytes (body) + "\" ("
                        + std::to_string (body.size()) + " bytes) but a search-engine verification "
                          "file has to be exactly \"" + showBytes (want) + "\" - the console looks "
                          "for its own file name in here, and anything else (a trailing newline, a "
                          "byte-order mark, a stale token) reads as not verified");

                continue;
            }

            std::smatch m;
            const std::string page = readFile (docsRoot / rel);

            if (! std::regex_search (page, m, canonicalRe))
            {
                r.problems.push_back ("docs/" + rel + ": no <link rel=\"canonical\"> - a page "
                    "without one is indexed under every address it is reachable at, sitemap "
                    "and all");
                continue;
            }

            const std::string url  = m[1].str();
            const std::string here = originOf (url);

            if (origin.empty())
                origin = here;
            else if (here != origin)
                r.problems.push_back ("docs/" + rel + ": declares its canonical address as " + url
                    + ", but another page declares " + origin + " - the site is split across "
                      "two hosts, and a crawler will index whichever half it trusts");

            pages.insert (url);
            canonByRel[rel] = url;
        }

        // The head repeats the address in absolute URLs, and a stale one is
        // invisible on the page: it surfaces as a preview card showing a broken
        // image, or as a link to a host that has moved, weeks later. og:url is
        // this page's own canonical, and the social images are served from
        // beside the site root, so both are derived rather than written down.
        static const std::regex metaTag (R"re(<meta\s[^>]*>)re");
        static const std::regex contentAttr (R"re(content="([^"]*)")re", std::regex::icase);

        auto metaValues = [&] (const std::string& page, const std::string& keyIs,
                               const std::string& key)
        {
            std::vector<std::string> values;
            const std::regex hasKey (keyIs + "=\"" + key + "\"", std::regex::icase);

            for (auto it = std::sregex_iterator (page.begin(), page.end(), metaTag);
                 it != std::sregex_iterator(); ++it)
            {
                const std::string tag = (*it)[0].str();

                if (! std::regex_search (tag, hasKey))
                    continue;

                std::smatch m;
                values.push_back (std::regex_search (tag, m, contentAttr) ? m[1].str()
                                                                         : std::string());
            }

            return values;
        };

        const auto baseAt = canonByRel.find ("index.html");
        const std::string base = baseAt == canonByRel.end() ? std::string() : baseAt->second;

        if (base.empty())
            r.problems.push_back ("docs/index.html: its canonical link is missing or unreadable, "
                                  "so there is no site root to check the head's absolute URLs "
                                  "against");

        for (const auto& entry : canonByRel)
        {
            const std::string page = readFile (docsRoot / entry.first);
            const auto ogUrls = metaValues (page, "property", "og:url");

            if (ogUrls.empty())
                r.problems.push_back ("docs/" + entry.first + ": no <meta property=\"og:url\"> - "
                    "a shared link then resolves to whatever address it was shared at, which is "
                    "not necessarily the canonical one");

            for (const auto& value : ogUrls)
                if (value != entry.second)
                    r.problems.push_back ("docs/" + entry.first + ": og:url is " + value
                        + " but its canonical link says " + entry.second + " - the head and the "
                          "canonical link disagree about this page's own address");

            auto checkImage = [&] (const char* keyIs, const char* key)
            {
                for (const auto& value : metaValues (page, keyIs, key))
                {
                    if (value.rfind ("https://", 0) != 0)
                        r.problems.push_back ("docs/" + entry.first + ": " + key + " is \"" + value
                            + "\", which is not an absolute https:// URL - a preview card cannot "
                              "resolve it");
                    else if (! base.empty() && value.compare (0, base.size(), base) != 0)
                        r.problems.push_back ("docs/" + entry.first + ": " + key + " points at "
                            + value + ", which is not under the site root " + base + " - the page "
                              "still names an address the site no longer lives at");
                }
            };

            checkImage ("property", "og:image");
            checkImage ("name", "twitter:image");

            // The JSON-LD repeats the address as well, and it is the copy nobody
            // reads: the block is invisible on the page, so a stale host there
            // survives every visual review and then turns up in search results.
            // Every absolute URL in it is either the site's own - and so must be
            // under the site root - or part of the schema.org vocabulary it is
            // written in, which is not an address at all.
            static const std::regex ldBlock (R"re(<script\s+type="application/ld\+json">([\s\S]*?)</script>)re");
            static const std::regex absoluteUrl (R"re(https://[^"\s]*)re");
            std::smatch ld;

            if (std::regex_search (page, ld, ldBlock))
            {
                const std::string block = ld[1].str();
                int ownUrls = 0;

                for (auto it = std::sregex_iterator (block.begin(), block.end(), absoluteUrl);
                     it != std::sregex_iterator(); ++it)
                {
                    const std::string url = (*it)[0].str();

                    if (originOf (url) == "https://schema.org")
                        continue;

                    ++ownUrls;

                    if (! base.empty() && url.compare (0, base.size(), base) != 0)
                        r.problems.push_back ("docs/" + entry.first + ": the JSON-LD names " + url
                            + ", which is not under the site root " + base + " - structured data "
                              "that outlives a move is invisible on the page and only shows up "
                              "in search results");
                }

                if (ownUrls == 0)
                    r.problems.push_back ("docs/" + entry.first + ": its JSON-LD block names no "
                        "URL of its own, only vocabulary terms - the check above would pass "
                        "without looking at anything");
            }
        }

        const fs::path sitemap = docsRoot / "sitemap.xml";
        const fs::path robots  = docsRoot / "robots.txt";

        if (! fs::exists (sitemap))
        {
            r.problems.push_back ("docs/sitemap.xml: missing - without it a crawler has to "
                                  "discover the legal pages by following links");
        }
        else
        {
            const std::string xml = readFile (sitemap);
            std::set<std::string> listed;

            for (auto it = std::sregex_iterator (xml.begin(), xml.end(), locRe);
                 it != std::sregex_iterator(); ++it)
            {
                const std::string url = (*it)[1].str();
                listed.insert (url);

                if (pages.count (url) == 0)
                    r.problems.push_back ("docs/sitemap.xml: lists " + url + ", which is not the "
                        "canonical address of any page under docs/ - a sitemap that advertises "
                        "a URL nothing serves sends a crawler to a 404");
            }

            if (listed.empty())
                r.problems.push_back ("docs/sitemap.xml: no <loc> entries at all - the page "
                                      "list check would silently pass");

            for (const auto& url : pages)
                if (listed.count (url) == 0)
                    r.problems.push_back ("docs/sitemap.xml: never lists " + url + " - that page "
                        "is published but nothing points a crawler at it");
        }

        if (! fs::exists (robots))
        {
            r.problems.push_back ("docs/robots.txt: missing - a crawler has no Sitemap: line "
                                  "and no explicit permission to index anything");
        }
        else
        {
            std::smatch m;
            const std::string txt = readFile (robots);

            if (! std::regex_search (txt, m, sitemapRe))
                r.problems.push_back ("docs/robots.txt: no \"Sitemap:\" line, so nothing connects "
                                      "the crawl rules to the page list");
            else if (originOf (m[1].str()) != origin)
                r.problems.push_back ("docs/robots.txt: points a crawler at the sitemap on "
                    + originOf (m[1].str()) + ", but the pages declare " + origin);
        }
    }

    // ---- 7. the reviews the page shows back the rating it claims -----------
    // Stars in a search result are a claim about the product, and the claim is
    // made in two places that a hand edit can pull apart: the aggregateRating in
    // the head, and the reviews section on the page. Google's review snippet
    // guidelines require the marked-up reviews to be visible on the marked-up
    // page, and "don't include fake reviews" covers an aggregate nobody can see
    // the evidence for. This is a manual action, not a lost snippet, so it is
    // worth a check rather than a convention.
    ++r.checks;
    {
        const std::string page = readFile (docsRoot / "index.html");

        // data-rating is what the generator writes on each review card; the
        // section itself is generated, so this reads the generated contract.
        // Digit runs are bounded so the std::stoi calls below cannot throw: an
        // unhandled exception in a checker prints nothing at all and exits on a
        // fast-fail code, which is the one failure that reads like a pass.
        static const std::regex ratingAttr  (R"re(data-rating="([1-5])")re");
        static const std::regex visibleAvg  (R"re(data-review-average>([0-9]{1,3}(\.[0-9])?)<)re");
        static const std::regex visibleCount(R"re(data-review-count>([0-9]{1,6})<)re");
        static const std::regex aggregate   (R"re("aggregateRating")re");
        static const std::regex aggValue    (R"re("ratingValue"\s*:\s*([0-9]{1,3}(\.[0-9])?))re");
        static const std::regex aggCount    (R"re("(?:ratingCount|reviewCount)"\s*:\s*([0-9]{1,6}))re");
        static const std::regex incentivizedRe (R"re(data-incentive="true")re");
        static const std::regex disclosure  (R"re(class="review-disclosure")re");

        auto occurrences = [&page] (const std::regex& re)
        {
            int n = 0;
            for (auto it = std::sregex_iterator (page.begin(), page.end(), re);
                 it != std::sregex_iterator(); ++it)
                ++n;
            return n;
        };

        // A rating as a whole number of tenths, so 4.6 is 46 and 4 is 40 - the
        // same arithmetic tests/DocsCheck.cpp's counterpart in
        // tools/make-reviews.py uses, and for the same reason: two roundings of
        // one mean is how a guard starts failing on 4.25.
        auto tenthsOf = [] (const std::string& value)
        {
            const auto dot = value.find ('.');
            const std::string whole = dot == std::string::npos ? value : value.substr (0, dot);
            const int frac = dot == std::string::npos ? 0 : value[dot + 1] - '0';
            return std::stoi (whole) * 10 + frac;
        };

        int total = 0, reviews = 0;
        for (auto it = std::sregex_iterator (page.begin(), page.end(), ratingAttr);
             it != std::sregex_iterator(); ++it)
        {
            total += (*it)[1].str()[0] - '0';
            ++reviews;
        }

        std::smatch m;
        const bool rated = std::regex_search (page, m, aggregate);
        const std::string afterRating = rated ? page.substr ((std::size_t) m.position (0))
                                             : std::string();

        std::smatch valueMatch, countMatch;
        const bool hasValue = std::regex_search (afterRating, valueMatch, aggValue);
        const bool hasCount = std::regex_search (afterRating, countMatch, aggCount);

        std::smatch visible;
        const int shownAverage = std::regex_search (page, visible, visibleAvg)
                                     ? tenthsOf (visible[1].str()) : -1;
        const int shownCount = std::regex_search (page, visible, visibleCount)
                                   ? std::stoi (visible[1].str()) : -1;

        if (reviews == 0 && rated)
            r.problems.push_back ("docs/index.html: it carries an aggregateRating but no review "
                "is visible on the page - a rating the reader cannot check the evidence for is "
                "the fake-review case Google takes manual action over, and the guidelines "
                "require the marked-up reviews to be on the marked-up page");

        if (reviews > 0 && ! rated)
            r.problems.push_back ("docs/index.html: it shows " + std::to_string (reviews)
                + " review(s) but carries no aggregateRating - where individual reviews are "
                  "marked up, the aggregate of those reviews has to be there too");

        if (rated && reviews > 0)
        {
            if (! hasValue || ! hasCount)
            {
                r.problems.push_back ("docs/index.html: the aggregateRating has no "
                    "ratingValue or no ratingCount/reviewCount - both are required before Google "
                    "will show the stars at all");
            }
            else
            {
                const int claimed = tenthsOf (valueMatch[1].str());
                const int counted = std::stoi (countMatch[1].str());
                const int mean = (total * 20 + reviews) / (2 * reviews);

                if (counted != reviews)
                    r.problems.push_back ("docs/index.html: the aggregateRating says "
                        + std::to_string (counted) + " ratings but " + std::to_string (reviews)
                        + " review(s) are on the page - the count has to be the reviews a reader "
                          "can see, not a bigger number");

                if (claimed != mean)
                    r.problems.push_back ("docs/index.html: the aggregateRating says "
                        + valueMatch[1].str() + " but the visible reviews average "
                        + std::to_string (mean / 10) + "." + std::to_string (mean % 10)
                        + " - the stars and the reviews come from the same set or the stars are "
                          "not backed by anything");

                if (shownAverage != claimed)
                    r.problems.push_back ("docs/index.html: the reviews section says the average "
                        "is " + std::to_string (shownAverage / 10) + "."
                        + std::to_string (shownAverage % 10) + " while the markup says "
                        + valueMatch[1].str() + " - a reader has to be able to see the rating "
                          "the markup claims");

                if (shownCount != counted)
                    r.problems.push_back ("docs/index.html: the reviews section says there are "
                        + std::to_string (shownCount) + " buyers while the markup says "
                        + std::to_string (counted));
            }
        }

        // Google, 24 July 2026: a review written in exchange for a free copy, a
        // discount or anything else has to say so, clearly and prominently.
        const int incentivized = occurrences (incentivizedRe);
        const int disclosed = occurrences (disclosure);

        if (incentivized != disclosed)
            r.problems.push_back ("docs/index.html: " + std::to_string (incentivized)
                + " review(s) are marked as incentivized but " + std::to_string (disclosed)
                + " carry a disclosure - a review written for a free copy or a discount has to "
                  "say so, clearly and prominently, or the markup is against the guidelines");
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
                     "(price, preset bank, AI defaults, local storage, trial, address +"
                     " verification token, reviews)\n";
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
