// Tests for the publish preflight.
//
// A leak guard that has never been shown to fail is a guard you cannot trust,
// so this copies the real docs/ tree to a temp folder, breaks it in each way the
// checker claims to catch, and asserts the checker notices - then asserts the
// untouched tree passes.
//
// The checker's scan() is reused directly (its main() is compiled out with
// GOA_PUBLISH_CHECK_NO_MAIN), so there is no subprocess or path guessing.
#define GOA_PUBLISH_CHECK_NO_MAIN
#include "PublishCheck.cpp"

#include <cstdio>
#include <ctime>
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
        std::printf ("PUBLISH CHECK: %s\n", what.c_str());
        ++fails;
    }
}

std::vector<std::string> problemsMentioning (const std::vector<std::string>& problems,
                                             const std::string& needle)
{
    std::vector<std::string> hits;

    for (const auto& p : problems)
        if (p.find (needle) != std::string::npos)
            hits.push_back (p);

    return hits;
}

void append (const fs::path& file, const std::string& text)
{
    std::ofstream out (file, std::ios::binary | std::ios::app);
    out << text;
}

std::string firstOf (const std::vector<std::string>& v)
{
    return v.empty() ? std::string ("(none)") : v.front();
}
} // namespace

int main()
{
    const fs::path realDocs = GOA_DOCS_DIR;

    if (! fs::is_directory (realDocs))
    {
        std::printf ("PUBLISH CHECK: docs folder not found: %s\n", realDocs.string().c_str());
        return 1;
    }

    // ---- the real site must pass ---------------------------------------------
    const auto clean = scan (realDocs);
    expect (! clean.fatal, "the real docs/ folder reported as missing");
    expect (clean.filesSeen > 0, "the real docs/ folder reported no files");
    expect (clean.problems.empty(),
            "the real docs/ tree does not pass the publish check: " + firstOf (clean.problems));

    // ---- a missing folder is a distinct, fatal outcome ------------------------
    const auto missing = scan (realDocs.parent_path() / "no-such-docs-folder");
    expect (missing.fatal, "a missing docs folder was not reported as fatal");

    // ---- a sandbox copy to break on purpose -----------------------------------
    const fs::path root = fs::temp_directory_path()
                              / ("goasynth_publish_" + std::to_string ((long long) std::time (nullptr)));
    const fs::path copy = root / "docs";

    fs::remove_all (root);
    fs::create_directories (root);

    std::error_code ec;
    fs::copy (realDocs, copy, fs::copy_options::recursive | fs::copy_options::overwrite_existing, ec);

    if (ec)
    {
        std::printf ("PUBLISH CHECK: could not copy docs/ (%s)\n", ec.message().c_str());
        return 1;
    }

    const auto copied = scan (copy);
    expect (copied.problems.empty(),
            "a fresh copy of docs/ does not pass: " + firstOf (copied.problems));
    expect (copied.filesSeen == clean.filesSeen,
            "the copy holds a different number of files than the original");

    // ---- 1. a stray seller-side file in docs/ ---------------------------------
    append (copy / "CHECKOUT.md", "# seller notes\n");
    append (copy / "orders.csv", "order,email\n1,buyer@example.com\n");

    {
        const auto r = scan (copy);
        expect (! r.problems.empty(), "a seller playbook and an order export in docs/ passed the check");
        expect (problemsMentioning (r.problems, "CHECKOUT.md").size() == 1,
                "the stray playbook was not named: " + firstOf (r.problems));
        expect (problemsMentioning (r.problems, "orders.csv").size() == 1,
                "the stray order export was not named: " + firstOf (r.problems));
        expect (! problemsMentioning (r.problems, "publish allowlist").empty(),
                "the stray files were not reported as off-allowlist");

        fs::remove (copy / "CHECKOUT.md");
        fs::remove (copy / "orders.csv");
    }

    // ---- 2. secrets and seller paths pasted into a served page ----------------
    append (copy / "index.html",
            "<p>key file C:\\Users\\someone\\AppData\\Roaming\\GoaSynth\\Keygen\\keys.txt</p>\n"
            "<p>masterDigest=ABC123 private=XYZ</p>\n"
            "<p>GOA1-1A2B3C4D5E6F70819A2B-DEADBEEF</p>\n"
            "<p>revoked_serials.txt and manifest.tsv</p>\n");

    append (copy / "legal" / "privacy.html",
            "<p>Authorization: Bearer sk-abcdefghijklmnopqrstuvwx1234</p>\n");

    {
        const auto r = scan (copy);

        expect (! problemsMentioning (r.problems, "keypair file").empty(),
                "a keys.txt path in a served page passed the check");
        expect (! problemsMentioning (r.problems, "own user folder").empty(),
                "a real user profile path in a served page passed the check");
        expect (! problemsMentioning (r.problems, "private half").empty(),
                "a pasted private key passed the check");
        expect (! problemsMentioning (r.problems, "master-key digest").empty(),
                "a pasted master digest passed the check");
        expect (! problemsMentioning (r.problems, "signed serial").empty(),
                "a real signed serial passed the check");
        expect (! problemsMentioning (r.problems, "API key").empty(),
                "an API-key-shaped string passed the check");
        expect (! problemsMentioning (r.problems, "Authorization header").empty(),
                "a bearer token passed the check");
        expect (! problemsMentioning (r.problems, "revocation log").empty(),
                "keygen ledger/log filenames passed the check");
        expect (! problemsMentioning (r.problems, "fulfilment output").empty(),
                "fulfilment output filenames passed the check");
    }

    // ---- 3. the plugin's own shared path is documentation, not a leak ---------
    // The real index.html/privacy.html document C:\Users\Public\... several
    // times; only the injected profile path above may be reported.
    {
        const auto r = scan (copy);
        const auto hits = problemsMentioning (r.problems, "own user folder");

        expect (hits.size() == 1,
                "expected exactly one user-folder hit (the injected one), got "
                  + std::to_string (hits.size()) + " - the C:\\Users\\Public exception is not working");
    }

    // ---- 4. links that leave docs/ or point at nothing ------------------------
    append (copy / "index.html", "<a href=\"../Fulfil/CHECKOUT.md\">playbook</a>\n");
    append (copy / "index.html", "<img src=\"legal/does-not-exist.html\" alt=\"\">\n");

    {
        const auto r = scan (copy);

        expect (! problemsMentioning (r.problems, "outside docs/").empty(),
                "a link out of docs/ passed the check");
        expect (! problemsMentioning (r.problems, "does not exist").empty(),
                "a link to a file that is not published passed the check");
    }

    // ---- 5. the checker does not flag ordinary markup -------------------------
    {
        const fs::path plain = root / "plain";
        fs::create_directories (plain);
        fs::create_directories (plain / "assets");
        fs::create_directories (plain / "legal");

        // Not a real PNG, but the check only cares that the linked file exists
        // and is on the allowlist.
        append (plain / "assets" / "ui-overview.png", "\x89PNG stand-in");

        append (plain / "index.html",
                "<a href=\"legal/eula.html\">EULA</a>\n"
                "<a href=\"#buy\">Buy</a>\n"
                "<a href=\"https://github.com/you/goasynth\">source</a>\n"
                "<img src=\"assets/ui-overview.png\" alt=\"\">\n"
                "<p>mask-image: radial-gradient(closest-side, #000 8%, transparent 72%);</p>\n"
                "<p>Your licence lives in C:\\Users\\Public\\Documents\\GoaSynth</p>\n");
        append (plain / "legal" / "eula.html", "<p>fine</p>\n");

        const auto r = scan (plain);

        expect (r.problems.empty(),
                "ordinary markup was flagged: " + firstOf (r.problems));
    }

    fs::remove_all (root);

    std::printf (fails == 0 ? "PUBLISH CHECK OK\n" : "FAILURES: %d\n", fails);
    return fails == 0 ? 0 : 1;
}
