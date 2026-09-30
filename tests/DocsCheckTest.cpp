// Tests for the website/plugin drift guard.
//
// Same reasoning as the publish preflight's self-test: a checker that has never
// been shown to fail is a checker you cannot trust, and this one's failure mode
// is a silent pass - the expensive direction. So this builds a throwaway copy of
// the real docs/ tree and Source/ folder, injects each kind of drift the guard
// claims to catch, and asserts it notices; then asserts the untouched copy is
// clean.
#define GOA_DOCS_CHECK_NO_MAIN
#include "DocsCheck.cpp"

#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

namespace
{
int fails = 0;

void expect (bool ok, const std::string& what)
{
    if (! ok)
    {
        std::printf ("DOCS CHECK: %s\n", what.c_str());
        ++fails;
    }
}

std::string readWhole (const fs::path& p)
{
    std::ifstream in (p, std::ios::binary);
    return std::string (std::istreambuf_iterator<char> (in), std::istreambuf_iterator<char>());
}

void writeWhole (const fs::path& p, const std::string& text)
{
    std::ofstream out (p, std::ios::binary | std::ios::trunc);
    out << text;
}

// Replace every occurrence; returns how many were swapped so the test can fail
// loudly if the text it injects into has moved.
int replaceAll (const fs::path& p, const std::string& from, const std::string& to)
{
    std::string text = readWhole (p);
    int n = 0;

    for (std::size_t at = text.find (from); at != std::string::npos; at = text.find (from, at + to.size()))
    {
        text.replace (at, from.size(), to);
        ++n;
    }

    writeWhole (p, text);
    return n;
}

// Shift the "// FAMILY (n)" header in Presets.h by `delta`, whatever n happens
// to be. The fixture must not hard-code the current count: the factory bank
// grows over time, and a literal "// ACID (15)" silently stopped injecting any
// drift the moment a preset was added - the self-test then passed while
// checking nothing. Returns 0 if the header was not found.
int bumpFamilyCount (const fs::path& p, const std::string& family, int delta)
{
    std::string text = readWhole (p);
    const std::string needle = "// " + family + " (";
    const std::size_t at = text.find (needle);

    if (at == std::string::npos)
        return 0;

    const std::size_t numAt  = at + needle.size();
    const std::size_t numEnd = text.find (')', numAt);

    if (numEnd == std::string::npos || numEnd == numAt)
        return 0;

    const int n = std::stoi (text.substr (numAt, numEnd - numAt));
    text.replace (numAt, numEnd - numAt, std::to_string (n + delta));
    writeWhole (p, text);
    return 1;
}

bool anyProblemContains (const std::vector<std::string>& problems, const std::string& needle)
{
    for (const auto& p : problems)
        if (p.find (needle) != std::string::npos)
            return true;

    return false;
}

void report (const char* what, const std::vector<std::string>& problems, bool expectFinding)
{
    if (expectFinding)
    {
        expect (! problems.empty(), std::string (what) + ": the guard found nothing to complain about");

        if (! problems.empty())
            std::printf ("  ok - %s: %s\n", what, problems.front().c_str());
    }
    else
    {
        for (const auto& p : problems)
            std::printf ("  unexpected: %s\n", p.c_str());

        expect (problems.empty(), std::string (what) + ": the guard failed a clean tree");
    }
}
} // namespace

//==============================================================================
int main()
{
    const fs::path realDocs = fs::absolute (fs::path (GOA_DOCS_DIR));
    const fs::path realRepo = fs::absolute (fs::path (GOA_REPO_ROOT));

    const fs::path root = fs::temp_directory_path() / "goasynth_docscheck";
    const fs::path docs = root / "docs";
    const fs::path src  = root / "Source";

    // Returns false when the scratch tree could not be built, so every caller
    // has to stop rather than scan a half-copied tree and blame the site for it.
    auto buildTree = [&]() -> bool
    {
        std::error_code ec;
        fs::remove_all (root, ec);
        fs::create_directories (root, ec);
        fs::copy (realDocs, docs, fs::copy_options::recursive, ec);
        fs::copy (realRepo / "Source", src, fs::copy_options::recursive, ec);

        if (ec || ! fs::exists (docs / "index.html") || ! fs::exists (src / "Presets.h"))
        {
            std::printf ("DOCS CHECK: could not build the scratch tree (%s)\n", ec.message().c_str());
            return false;
        }

        return true;
    };

    // ---- the untouched tree must pass -------------------------------------
    if (! buildTree())
        return 2;

    {
        const auto r = scan (docs, root);

        expect (! r.fatal, "clean tree: the guard could not run at all");
        expect (r.checks == 6, "clean tree: expected 6 checks to run, got " + std::to_string (r.checks));
        report ("clean docs/ + Source/", r.problems, false);
    }

    // ---- 1. the price drifts out of step with the config -------------------
    if (! buildTree())
        return 2;

    {
        const int n = replaceAll (docs / "app.js", std::string (euro) + "15", std::string (euro) + "25");
        expect (n == 1, "price drift: expected exactly one CONFIG.price to rewrite, found "
                        + std::to_string (n));

        const auto r = scan (docs, root);
        expect (anyProblemContains (r.problems, "would quote two different prices"),
                "price drift: a page quoting a different price was not reported");
        expect (anyProblemContains (r.problems, "eula.html") || anyProblemContains (r.problems, "terms.html"),
                "price drift: the legal pages were not scanned for the price");
        report ("price drift", r.problems, true);
    }

    // ---- 2. a preset family changes count in the plugin --------------------
    if (! buildTree())
        return 2;

    {
        const int n = bumpFamilyCount (src / "Presets.h", "ACID", -1);
        expect (n == 1, "preset drift: could not rewrite the ACID family header");

        const auto r = scan (docs, root);
        expect (anyProblemContains (r.problems, "ACID"),
                "preset drift: a family count the site no longer matches was not reported");
        expect (anyProblemContains (r.problems, "headers total"),
                "preset drift: the header/array disagreement was not reported");
        report ("preset drift", r.problems, true);
    }

    // ---- 2b. the page cites a preset the bank does not contain -------------
    // The counts above cannot catch this: the real site once shipped a "Patch
    // of the week" naming PSYTRANCE BASS 4 and UPLIFTING LEAD 2, neither of
    // which was ever in Presets.h.
    if (! buildTree())
        return 2;

    {
        const int n = replaceAll (docs / "index.html", "BASS PSY ROLLER", "PSYTRANCE BASS 4");
        expect (n > 0, "preset-name drift: expected the page to cite a factory preset by name");

        const auto r = scan (docs, root);
        expect (anyProblemContains (r.problems, "PSYTRANCE BASS 4"),
                "preset-name drift: a page citing a preset that does not ship was not reported");
        report ("preset-name drift", r.problems, true);
    }

    // ---- 3. the default cloud model moves on (the 2.0 Flash story) ---------
    if (! buildTree())
        return 2;

    {
        const int n = replaceAll (docs / "index.html", "3.6", "2.0");
        expect (n > 0, "model drift: expected the page to name the current Gemini default");

        const auto r = scan (docs, root);
        expect (anyProblemContains (r.problems, "3.6"),
                "model drift: a page naming a retired default model was not reported");
        report ("model drift", r.problems, true);
    }

    // ---- 4. the plugin starts writing a file the policy never mentions -----
    if (! buildTree())
        return 2;

    {
        std::ofstream out (src / "License.cpp", std::ios::binary | std::ios::app);
        out << "\n// injected by DocsCheckTest: a new local file\n"
               "static juce::File scratchCache() { return licenseDir().getChildFile (\"secret_cache.bin\"); }\n";
        out.close();

        const auto r = scan (docs, root);
        expect (anyProblemContains (r.problems, "secret_cache.bin"),
                "storage drift: a local file missing from the privacy policy was not reported");
        report ("storage drift", r.problems, true);
    }

    // ---- 5. the trial length changes --------------------------------------
    if (! buildTree())
        return 2;

    {
        const int n = replaceAll (src / "License.h", "trialHours = 24", "trialHours = 48");
        expect (n == 1, "trial drift: expected one trialHours constant, found " + std::to_string (n));

        const auto r = scan (docs, root);
        expect (anyProblemContains (r.problems, "48 hours"),
                "trial drift: a page still promising the old trial length was not reported");
        report ("trial drift", r.problems, true);
    }

    // ---- 6. the canonical address splits across two hosts ------------------
    if (! buildTree())
        return 2;

    {
        // Only the canonical link, not the matching og:url beside it: the point
        // is a page that *declares* a different origin, and hitting both would
        // test the same branch twice.
        const int n = replaceAll (docs / "legal" / "terms.html",
                                  "<link rel=\"canonical\" href=\"https://y4m4.github.io/GoaSynth/legal/terms.html\">",
                                  "<link rel=\"canonical\" href=\"https://goasynth.example.com/legal/terms.html\">");
        expect (n == 1, "address drift: expected exactly one canonical link to rewrite, found "
                        + std::to_string (n));

        const auto r = scan (docs, root);
        expect (anyProblemContains (r.problems, "split across two hosts"),
                "address drift: a page declaring a different origin was not reported");
        expect (anyProblemContains (r.problems, "sitemap.xml"),
                "address drift: the sitemap was not compared against the pages");
        report ("address drift", r.problems, true);
    }

    // ---- 6b. a published page the sitemap forgets --------------------------
    if (! buildTree())
        return 2;

    {
        const int n = replaceAll (docs / "sitemap.xml",
                                  "<loc>https://y4m4.github.io/GoaSynth/legal/refunds.html</loc>",
                                  "<loc>https://y4m4.github.io/GoaSynth/legal/refunds-2.html</loc>");
        expect (n == 1, "sitemap drift: expected exactly one <loc> entry to rewrite, found "
                        + std::to_string (n));

        const auto r = scan (docs, root);
        expect (anyProblemContains (r.problems, "nothing points a crawler at it"),
                "sitemap drift: a page missing from the sitemap was not reported");
        expect (anyProblemContains (r.problems, "not the canonical address of any page"),
                "sitemap drift: a sitemap URL that serves nothing was not reported");
        report ("sitemap drift", r.problems, true);
    }

    // ---- and clean again, so a sticky failure cannot pass for a fresh one --
    if (! buildTree())
        return 2;

    {
        const auto r = scan (docs, root);
        report ("clean tree after the injections", r.problems, false);
    }

    std::error_code ec;
    fs::remove_all (root, ec);

    std::printf (fails == 0 ? "DOCS CHECK OK\n" : "DOCS CHECK FAILURES: %d\n", fails);
    return fails == 0 ? 0 : 1;
}
