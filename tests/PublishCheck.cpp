// Publish preflight for the website.
//
// Everything under docs/ is served verbatim (GitHub Pages from the /docs folder,
// or a folder drag onto Netlify/Cloudflare), so a stray file or a pasted secret
// there is public the moment you deploy. This check fails when that would
// happen:
//
//   1. a file in docs/ that is not on the allowlist below. The allowlist IS the
//      list of things meant to be public - which is why it is explicit rather
//      than "everything except .md": a stray orders.csv, a fulfilment manifest,
//      an editor backup or a note-to-self fails the check until someone decides.
//   2. seller-side or secret material inside an allowed text file: the keypair
//      and its ledger, the generated public-key header, a real signed serial,
//      API-key shapes, an authorization token, someone's own home directory.
//   3. a served page linking to something outside docs/ - that link either
//      404s once published or points at the rest of the repository.
//
// Run it before you publish, or let it run with the suite:
//
//   cmake --build build --config Release --target PublishCheck
//   ./build/Release/PublishCheck          # or: ctest -R PublishCheck
//   ./build/Release/PublishCheck <dir>    # overrides the compiled-in docs path
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

namespace fs = std::filesystem;

//==============================================================================
// Everything here is published. Adding a page to the site means adding it here,
// which is deliberate: the moment a file is auto-accepted is the moment one
// slips out.
namespace
{
const std::set<std::string> allowlist =
{
    ".nojekyll",
    "app.js",
    "index.html",
    "styles.css",
    "assets/ui-overview.png",
    "assets/ai-patch-designer.png",
    "assets/preset-browser.png",
    "assets/mod-dots.png",
    "assets/mod-pick-flash.png",
    "favicon.ico",                // the site favicon (docs/favicon.ico)
    "assets/icon_256.png",        // apple-touch-icon referenced by index.html
    "robots.txt",                 // crawl rules, served at the site root
    "sitemap.xml",                // the page list search engines read
    "googlebe8101dec58dcb76.html", // Google Search Console's token - a token, not a page
    "downloads/SHA256SUMS.txt",   // the checksums buyers verify their download with
    "legal/eula.html",
    "legal/privacy.html",
    "legal/refunds.html",
    "legal/terms.html"
};

// Content is only skipped for things that cannot hold text; a new text file is
// scanned by default rather than excused by an extension nobody listed.
const std::set<std::string> binaryExtensions =
{
    "png", "jpg", "jpeg", "gif", "webp", "ico", "bmp", "tif", "tiff",
    "woff", "woff2", "ttf", "otf", "eot",
    "zip", "gz", "tar", "7z", "pdf", "doc", "docx", "xls", "xlsx", "ppt", "pptx",
    "mp3", "wav", "flac", "ogg", "m4a", "aif", "aiff",
    "mp4", "mov", "avi", "webm",
    "exe", "dll", "so", "dylib", "vst3", "bin"
};

struct Tripwire
{
    std::regex  pattern;    // matched case-insensitively, one line at a time
    std::string why;        // what the reader is looking at
    std::regex  except;     // a line matching this is innocent ...
    bool        hasExcept;  // ... when this is set

    Tripwire (const char* p, const char* w, const char* x)
        : pattern (p, std::regex::icase), why (w),
          except (x != nullptr ? std::regex (x, std::regex::icase) : std::regex()),
          hasExcept (x != nullptr)
    {
    }
};

// Seller-side and secret material: the things that must never appear in a served
// file, however tempting the worked example would be.
const std::vector<Tripwire>& tripwires()
{
    static const std::vector<Tripwire> list
    {
        { R"(private=)",
          "the private half of the RSA keypair", nullptr },

        { R"(masterdigest)",
          "the master-key digest that is stored with the keypair", nullptr },

        { R"(goa_master_digest|goa_license_public_key)",
          "a constant from the generated Source/LicenseKeys.h", nullptr },

        { R"(begin rsa private key)",
          "a PEM private key", nullptr },

        { R"(issued_serials|revoked_serials)",
          "a keygen ledger or revocation log", nullptr },

        { R"(keys\.txt)",
          "the keypair file itself", nullptr },

        { R"(--(unregister|revoke|genfile))",
          "a seller-only keygen command", nullptr },

        { R"(goasynthfulfil)",
          "the seller-only fulfilment tool", nullptr },

        { R"(manifest\.tsv|needs-attention|activity\.log)",
          "seller-side fulfilment output, which carries buyer names and addresses", nullptr },

        { R"(GOA1-)",
          "a real signed serial - a buyer's licence, not a documentation example",
          R"(<machine-id>|<signature>|serial:)" },

        { R"(ghp_|github_pat_)",
          "a GitHub access token", nullptr },

        { R"(sk_live|sk-[A-Za-z0-9]{20,}|AIza[A-Za-z0-9_-]{20,}|xai-[A-Za-z0-9]{20,})",
          "something shaped like an API key", nullptr },

        { R"(bearer\s+[A-Za-z0-9._-]{16,})",
          "a token in an Authorization header", nullptr },

        // The plugin's own shared bank really is C:\Users\Public\... and is
        // public documentation; any other profile path is somebody's own machine.
        { R"([A-Za-z]:\\Users\\[A-Za-z])",
          "an absolute path from someone's own user folder",
          R"(\\Public\\)" },
    };

    return list;
}

std::string lower (std::string s)
{
    std::transform (s.begin(), s.end(), s.begin(),
                    [] (unsigned char c) { return (char) std::tolower (c); });
    return s;
}

std::string extensionOf (const fs::path& p)
{
    auto e = p.extension().string();
    if (! e.empty() && e.front() == '.')
        e.erase (e.begin());
    return lower (e);
}

bool isText (const fs::path& p) { return binaryExtensions.count (extensionOf (p)) == 0; }

std::vector<std::string> readLines (const fs::path& p)
{
    std::vector<std::string> lines;
    std::ifstream in (p, std::ios::binary);

    std::string line;
    while (std::getline (in, line))
    {
        if (! line.empty() && line.back() == '\r')
            line.pop_back();

        // A UTF-8 BOM would hide a match on the very first line.
        if (lines.empty() && line.size() >= 3
             && (unsigned char) line[0] == 0xEF
             && (unsigned char) line[1] == 0xBB
             && (unsigned char) line[2] == 0xBF)
            line.erase (0, 3);

        lines.push_back (line);
    }

    return lines;
}

// A served page may only link to other served files. Returns one problem string
// per offending link; `where` is "<file>:<line>".
void checkLinks (const std::string& where, const std::string& line,
                 const fs::path& file, const fs::path& docsRoot,
                 std::vector<std::string>& problems)
{
    // A custom delimiter, because the regex itself ends with a quote and a
    // parenthesis - exactly the sequence that would close a plain R"(...)".
    static const std::regex attr (R"re((?:href|src)\s*=\s*"([^"]*)")re", std::regex::icase);

    for (auto m = std::sregex_iterator (line.begin(), line.end(), attr);
         m != std::sregex_iterator(); ++m)
    {
        std::string target = (*m)[1].str();

        if (target.empty() || target[0] == '#')
            continue;

        const std::string lowered = lower (target);

        if (lowered.rfind ("http://", 0) == 0 || lowered.rfind ("https://", 0) == 0
             || lowered.rfind ("mailto:", 0) == 0 || lowered.rfind ("tel:", 0) == 0
             || lowered.rfind ("data:", 0) == 0 || lowered.rfind ("javascript:", 0) == 0
             || lowered.rfind ("//", 0) == 0)
            continue;

        target = target.substr (0, target.find_first_of ("?#"));   // drop ?query and #fragment

        if (target.empty())
            continue;

        fs::path resolved = target.front() == '/'
                                ? docsRoot / target.substr (1)
                                : file.parent_path() / target;

        resolved = resolved.lexically_normal();

        if (fs::relative (resolved, docsRoot).generic_string().rfind ("..", 0) == 0)
        {
            problems.push_back (where + ": links to \"" + target + "\", which is outside docs/ "
                                    "and so is not published - it would 404 (link to it from the "
                                    "README instead, or move the target into docs/)");
            continue;
        }

        if (! fs::exists (resolved))
            problems.push_back (where + ": links to \"" + target + "\", which does not exist");
    }
}
} // namespace

//==============================================================================
namespace
{
struct Result
{
    bool fatal = false;                  // the folder itself was missing
    std::size_t filesSeen = 0;
    int scannedForText = 0;
    std::vector<std::string> problems;
};

// The whole check, with no printing, so tests can call it directly.
Result scan (const fs::path& docsRoot)
{
    Result r;

    if (! fs::is_directory (docsRoot))
    {
        r.fatal = true;
        return r;
    }

    std::vector<fs::path> files;
    for (auto it = fs::recursive_directory_iterator (docsRoot, fs::directory_options::skip_permission_denied);
         it != fs::recursive_directory_iterator(); ++it)
        if (it->is_regular_file())
            files.push_back (it->path());

    std::sort (files.begin(), files.end());

    r.filesSeen = files.size();

    auto& problems = r.problems;

    for (const auto& file : files)
    {
        const std::string rel = fs::relative (file, docsRoot).generic_string();

        if (allowlist.count (rel) == 0)
        {
            problems.push_back (rel + ": not on the publish allowlist - everything in docs/ is "
                                        "served verbatim, so this file would be public. Move it out "
                                        "of docs/, or add it to the allowlist in "
                                        "tests/PublishCheck.cpp if it really is a page.");
            continue;
        }

        if (! isText (file))
            continue;

        ++r.scannedForText;
        const auto lines = readLines (file);
        const bool html = extensionOf (file) == "html";

        for (std::size_t i = 0; i < lines.size(); ++i)
        {
            const std::string& line = lines[i];
            const std::string where = rel + ":" + std::to_string (i + 1);

            for (const auto& t : tripwires())
            {
                if (! std::regex_search (line, t.pattern))
                    continue;

                if (t.hasExcept && std::regex_search (line, t.except))
                    continue;

                problems.push_back (where + ": found " + t.why);
            }

            if (html)
                checkLinks (where, line, file, docsRoot, problems);
        }
    }

    return r;
}
} // namespace

//==============================================================================
#ifndef GOA_PUBLISH_CHECK_NO_MAIN
int main (int argc, char* argv[])
{
    const fs::path docsRoot = fs::absolute (fs::path (argc > 1 ? argv[1] : GOA_DOCS_DIR));
    const auto r = scan (docsRoot);

    if (r.fatal)
    {
        std::cout << "publish check: no such folder: " << docsRoot.string() << "\n";
        return 2;
    }

    if (r.problems.empty())
    {
        std::cout << "publish check: " << r.filesSeen << " file(s) under " << docsRoot.string()
                  << " (" << r.scannedForText << " scanned for text) - nothing seller-side or "
                     "secret is going to be published\n";
        return 0;
    }

    std::cout << "publish check FAILED - " << r.problems.size() << " problem(s) in "
              << docsRoot.string() << ":\n\n";

    for (const auto& p : r.problems)
        std::cout << "  " << p << "\n";

    std::cout << "\nNothing under docs/ may be seller-side or secret: that folder is the website.\n";
    return 1;
}
#endif
