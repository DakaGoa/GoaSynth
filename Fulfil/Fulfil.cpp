#include "Fulfil.h"

#include "../Keygen/KeygenCore.h"

#include <algorithm>
#include <iostream>
#include <vector>

//==============================================================================
// Order fulfilment. Seller-side only: this tool holds no secrets of its own —
// it borrows the keygen's keypair and ledger through KeygenCore.h, so a serial
// issued here is byte-identical to one issued by `GoaSynthKeygen --file`.
namespace fulfil
{
using namespace keygen::core;

namespace
{
//==============================================================================
// Field extraction
//==============================================================================
struct Order
{
    juce::String key;          // order id, else machine id, else file#row
    juce::String orderId, email, name, product, machineId, status;
    juce::StringArray problems;
    bool refunded = false;
    juce::File source;
    int row = 0;
};

struct ManifestEntry
{
    juce::String key, status, email, machineId, serial, licenseFile, emlFile, issuedAt;
};

const char* const manifestHeader = "key\tstatus\temail\tmachine_id\tserial\tlicense_file\teml_file\tissued_at";

juce::String keyFilename (const juce::String& s)
{
    // Usable as a file name on every platform. '@' is kept so a draft file still
    // reads as the buyer's address rather than "janeexample.com".
    return s.retainCharacters ("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_.@+").substring (0, 80);
}

// StringPairArray's operator[] indexes a missing key out of bounds, so every
// read goes through getValue with an empty default.
juce::String fieldValue (const juce::StringPairArray& fields, const juce::String& key)
{
    return fields.getValue (key, {});
}

// First value whose column/key name matches any pattern (case-insensitive
// substring), preferring the earlier pattern.
juce::String valueForKey (const juce::StringPairArray& fields, const juce::StringArray& patterns)
{
    for (const auto& pattern : patterns)
        for (const auto& k : fields.getAllKeys())
            if (k.containsIgnoreCase (pattern))
            {
                const juce::String v = fieldValue (fields, k);
                if (v.isNotEmpty())
                    return v;
            }

    return {};
}

bool looksLikeEmail (const juce::String& s)
{
    const int at = s.indexOfChar ('@');
    return at > 0 && s.indexOfChar ('.') > at + 1 && ! s.containsAnyOf (" \t");
}

juce::String firstEmail (const juce::StringPairArray& fields)
{
    const juce::String labelled = valueForKey (fields, { "email", "e-mail", "buyer" });
    if (looksLikeEmail (labelled))
        return labelled.trim();

    for (const auto& k : fields.getAllKeys())
        if (looksLikeEmail (fieldValue (fields, k)))
            return fieldValue (fields, k).trim();

    return {};
}

// Every maximal hex-or-dash run that is exactly a machine id. Runs longer than
// 20 hex chars (hashes, order ids glued to other data) are not split up, so a
// 40-char digest can never masquerade as one.
juce::StringArray machineIdRuns (const juce::String& value)
{
    juce::StringArray found;
    juce::String run;

    auto flush = [&]
    {
        const juce::String stripped = run.removeCharacters ("-").toUpperCase();
        if (stripped.length() == machineHexLength && looksLikeMachineId (stripped))
            found.addIfNotAlreadyThere (stripped);
        run.clear();
    };

    for (auto c : value)
    {
        if (juce::CharacterFunctions::getHexDigitValue (c) >= 0 || c == '-')
            run << c;
        else
            flush();
    }

    flush();
    return found;
}

// The machine id for one order. A column that names itself ("Machine ID",
// "HWID", "Device ID", or the store's custom-field name) wins; only when no
// labelled candidate exists do we scan every value — and then a candidate must
// contain a letter, so a 20-digit order number cannot be mistaken for one.
juce::String findMachineId (const juce::StringPairArray& fields, juce::String& problem)
{
    problem.clear();

    const juce::StringArray labelledKeys { "machine id", "machineid", "machine", "hwid", "device id", "deviceid", "fingerprint" };

    for (const auto& pattern : labelledKeys)
        for (const auto& k : fields.getAllKeys())
        {
            if (! k.containsIgnoreCase (pattern))
                continue;

            const juce::StringArray runs = machineIdRuns (fieldValue (fields, k));
            if (runs.size() == 1)
                return runs[0];
            if (runs.size() > 1)
            {
                problem = "the " + k + " column holds " + juce::String (runs.size())
                            + " different machine ids - ask the buyer to confirm which machine";
                return {};
            }
        }

    juce::StringArray candidates;
    for (const auto& k : fields.getAllKeys())
        for (const auto& id : machineIdRuns (fieldValue (fields, k)))
            if (id.containsChar ('A') || id.containsChar ('B') || id.containsChar ('C') || id.containsChar ('D')
                 || id.containsChar ('E') || id.containsChar ('F'))
                candidates.addIfNotAlreadyThere (id);

    if (candidates.size() == 1)
        return candidates[0];

    if (candidates.size() > 1)
    {
        problem = "found " + juce::String (candidates.size())
                    + " possible machine ids (" + candidates.joinIntoString (", ")
                    + ") and no column saying which is the machine id";
        return {};
    }

    problem = "no 20-character machine ID in this order - ask the buyer to install the demo "
              "and send the ID from the activation screen";
    return {};
}

bool looksRefunded (const juce::StringPairArray& fields)
{
    for (const auto& key : fields.getAllKeys())
    {
        const juce::String value = fieldValue (fields, key).trim();

        if (value.isEmpty() || value == "0" || value.equalsIgnoreCase ("false") || value.equalsIgnoreCase ("no"))
            continue;

        if (key.containsIgnoreCase ("refund") || key.containsIgnoreCase ("chargeback"))
            return true;

        if ((key.containsIgnoreCase ("status") || key.containsIgnoreCase ("state"))
            && (value.containsIgnoreCase ("refund") || value.containsIgnoreCase ("cancel")
                 || value.containsIgnoreCase ("chargeback")))
            return true;
    }

    return false;
}

juce::String productText (const juce::StringPairArray& fields)
{
    return valueForKey (fields, { "product", "item", "variant", "title", "plan" });
}

//==============================================================================
// Input parsing
//==============================================================================
juce::StringArray splitCsvLine (const juce::String& line)
{
    juce::StringArray cells;
    juce::String cell;
    bool inQuotes = false;

    for (int i = 0; i < line.length(); ++i)
    {
        const auto c = line[i];

        if (inQuotes)
        {
            if (c == '"')
            {
                if (i + 1 < line.length() && line[i + 1] == '"') { cell << '"'; ++i; }
                else                                             { inQuotes = false; }
            }
            else
            {
                cell << c;
            }
        }
        else if (c == '"')                          { inQuotes = true; }
        else if (c == ',' || c == ';' || c == '\t') { cells.add (cell.trim()); cell.clear(); }
        else                                        { cell << c; }
    }

    cells.add (cell.trim());
    return cells;
}

// Splits into records first, so a quoted field containing newlines (addresses,
// notes) cannot shift every following row by one column.
std::vector<juce::String> splitCsvRecords (const juce::String& text)
{
    std::vector<juce::String> records;
    juce::String current;
    bool inQuotes = false;

    for (const auto& line : juce::StringArray::fromLines (text))
    {
        for (auto c : line)
            if (c == '"')
                inQuotes = ! inQuotes;

        if (current.isNotEmpty())
            current << "\n";
        current << line;

        if (! inQuotes && current.trim().isNotEmpty())
        {
            records.push_back (current);
            current.clear();
        }
    }

    if (current.trim().isNotEmpty())
        records.push_back (current);

    return records;
}

void addJsonFields (const juce::var& value, const juce::String& prefix, juce::StringPairArray& fields)
{
    // Careful: a juce::var holding an array reports isObject() as well as
    // isArray(), so branch on the actual contents — asking an array var for its
    // DynamicObject hands back a null pointer.
    if (auto* object = value.getDynamicObject())
    {
        const auto& props = object->getProperties();
        for (int i = 0; i < props.size(); ++i)
        {
            const juce::String name = props.getName (i).toString();
            addJsonFields (props.getValueAt (i), prefix.isEmpty() ? name : prefix + " " + name, fields);
        }
    }
    else if (value.isArray())
    {
        for (const auto& item : *value.getArray())
            addJsonFields (item, prefix, fields);
    }
    else if (value.isVoid() || value.isUndefined() || value.isMethod())
    {
        // nothing
    }
    else if (value.toString().trim().isNotEmpty())
    {
        const juce::String existing = fieldValue (fields, prefix);
        fields.set (prefix, existing.isEmpty() ? value.toString().trim()
                                               : existing + " " + value.toString().trim());
    }
}

std::vector<Order> parseCsvFile (const juce::File& file)
{
    std::vector<Order> orders;

    const auto records = splitCsvRecords (file.loadFileAsString());
    if (records.empty())
        return orders;

    const auto header = splitCsvLine (records.front());

    for (size_t r = 1; r < records.size(); ++r)
    {
        const auto cells = splitCsvLine (records[r]);
        if (cells.size() == 1 && cells[0].isEmpty())
            continue;

        juce::StringPairArray fields;
        for (int c = 0; c < cells.size(); ++c)
        {
            const juce::String key = c < header.size() ? header[c] : "column " + juce::String (c + 1);
            if (cells[c].isNotEmpty())
                fields.set (key, cells[c]);
        }

        Order o;
        o.source = file;
        o.row = (int) r;

        o.orderId  = valueForKey (fields, { "order id", "order number", "order", "id", "reference" });
        o.email    = firstEmail (fields);
        o.name     = valueForKey (fields, { "customer name", "name" });
        o.product  = productText (fields);
        o.status   = valueForKey (fields, { "status", "state" });
        o.refunded = looksRefunded (fields);

        juce::String problem;
        o.machineId = findMachineId (fields, problem);
        if (problem.isNotEmpty())
            o.problems.add (problem);

        o.key = o.orderId.isNotEmpty() ? o.orderId
                                       : (o.machineId.isNotEmpty() ? o.machineId
                                                                   : file.getFileName() + "#" + juce::String (o.row));
        orders.push_back (o);
    }

    return orders;
}

std::vector<Order> parseJsonFile (const juce::File& file)
{
    std::vector<Order> orders;

    juce::var parsed;
    const auto result = juce::JSON::parse (file.loadFileAsString(), parsed);
    if (result.failed())
        return orders;

    std::vector<juce::var> items;
    if (parsed.isArray())
        for (const auto& item : *parsed.getArray())
            items.push_back (item);
    else
        items.push_back (parsed);

    int index = 0;
    for (const auto& item : items)
    {
        juce::StringPairArray fields;
        addJsonFields (item, {}, fields);

        if (fields.size() == 0)
            continue;

        Order o;
        o.source = file;
        o.row = ++index;

        o.orderId  = valueForKey (fields, { "order id", "order number", "order", "id", "reference" });
        o.email    = firstEmail (fields);
        o.name     = valueForKey (fields, { "customer name", "name" });
        o.product  = productText (fields);
        o.status   = valueForKey (fields, { "status", "state" });
        o.refunded = looksRefunded (fields);

        juce::String problem;
        o.machineId = findMachineId (fields, problem);
        if (problem.isNotEmpty())
            o.problems.add (problem);

        o.key = o.orderId.isNotEmpty() ? o.orderId
                                       : (o.machineId.isNotEmpty() ? o.machineId
                                                                   : file.getFileName() + "#" + juce::String (o.row));
        orders.push_back (o);
    }

    return orders;
}

std::vector<Order> parseOrderFile (const juce::File& file)
{
    return file.hasFileExtension ("json") ? parseJsonFile (file) : parseCsvFile (file);
}

//==============================================================================
// Output
//==============================================================================
std::vector<ManifestEntry> readManifest (const juce::File& f)
{
    std::vector<ManifestEntry> entries;
    if (! f.existsAsFile())
        return entries;

    const auto lines = juce::StringArray::fromLines (f.loadFileAsString());
    for (int i = 0; i < lines.size(); ++i)
    {
        const auto line = lines[i].trim();
        if (line.isEmpty() || line.startsWith ("key\t"))
            continue;

        const auto cells = juce::StringArray::fromTokens (line, "\t", "");
        if (cells.size() < 2)
            continue;

        ManifestEntry e;
        e.key         = cells[0];
        e.status      = cells[1];
        e.email       = cells.size() > 2 ? cells[2] : juce::String();
        e.machineId   = cells.size() > 3 ? cells[3] : juce::String();
        e.serial      = cells.size() > 4 ? cells[4] : juce::String();
        e.licenseFile = cells.size() > 5 ? cells[5] : juce::String();
        e.emlFile     = cells.size() > 6 ? cells[6] : juce::String();
        e.issuedAt    = cells.size() > 7 ? cells[7] : juce::String();
        entries.push_back (e);
    }

    return entries;
}

// The manifest is append-only, so an order's current state is its newest row.
ManifestEntry latestEntryFor (const std::vector<ManifestEntry>& entries, const juce::String& key, bool& found)
{
    ManifestEntry latest;
    found = false;

    for (const auto& e : entries)
        if (e.key == key)
        {
            latest = e;
            found = true;
        }

    return latest;
}

juce::String clean (const juce::String& s) { return s.replaceCharacters ("\t\r\n", "   "); }

// A 512-hex serial makes an unreadable audit log line. The full value lives in
// the keygen's ledger and in revoked_serials.txt; the log only needs enough to
// match a support email against a record.
juce::String shortSerial (const juce::String& serial)
{
    if (serial.length() < 40)
        return serial;

    return serial.substring (0, 29) + ".." + serial.substring (serial.length() - 8);
}

void appendManifest (const juce::File& f, const ManifestEntry& e)
{
    if (! f.existsAsFile())
        f.replaceWithText (juce::String (manifestHeader) + "\n");

    juce::String line;
    line << clean (e.key) << "\t" << clean (e.status) << "\t" << clean (e.email) << "\t"
         << clean (e.machineId) << "\t" << clean (e.serial) << "\t" << clean (e.licenseFile) << "\t"
         << clean (e.emlFile) << "\t" << clean (e.issuedAt) << "\n";
    f.appendText (line);
}

void writeAttentionReport (const juce::File& f, const std::vector<Order>& needsAttention)
{
    juce::String text;
    text << "order\temail\tsource\treason\n";

    for (const auto& o : needsAttention)
        text << clean (o.key) << "\t" << clean (o.email) << "\t"
             << clean (o.source.getFileName()) << "\t" << clean (o.problems.joinIntoString ("; ")) << "\n";

    f.replaceWithText (text);
}

void logLine (const juce::File& f, const juce::String& line)
{
    f.appendText (juce::Time::getCurrentTime().toISO8601 (true) + "  " + clean (line) + "\n");
}

juce::String base64Block (const void* data, int bytes)
{
    const juce::String b64 = juce::Base64::toBase64 (data, bytes);

    juce::String out;
    for (int i = 0; i < b64.length(); i += 76)
        out << b64.substring (i, i + 76) << "\r\n";
    return out;
}

juce::String defaultBody()
{
    return
        "Hi {name},\r\n"
        "\r\n"
        "thanks for buying GoaSynth. Your licence file is attached: {file}\r\n"
        "\r\n"
        "It is signed for this machine:\r\n"
        "  {machine}\r\n"
        "\r\n"
        "To activate, either double-click the attached file on that computer (Windows opens it\r\n"
        "with the GoaSynth licence helper), or open GoaSynth in your DAW, press IMPORT on the\r\n"
        "activation screen and pick the file. If you would rather type it in, here is the plain\r\n"
        "serial:\r\n"
        "\r\n"
        "  {serial}\r\n"
        "\r\n"
        "Activation is fully offline -- no account, no dongle, no server check -- and it stays\r\n"
        "unlocked once it is done. Three things worth knowing:\r\n"
        "\r\n"
        "  * GoaSynth runs a full 24-hour trial whether or not you activate, so you can try\r\n"
        "    everything right away.\r\n"
        "  * One serial covers one computer. Your licence includes three machines you use\r\n"
        "    yourself -- send me the other machine IDs and I will issue those serials free.\r\n"
        "  * Reinstalled Windows or changed hardware? The machine ID changes with it. Email me\r\n"
        "    and I will release the old binding and issue a fresh serial, free.\r\n"
        "\r\n"
        "{repo_line}"
        "Any trouble at all, just reply to this email -- there is a 14-day refund, no questions\r\n"
        "asked.\r\n"
        "\r\n"
        "Thanks,\r\n"
        "{from}\r\n";
}

juce::String fillTemplate (juce::String text, const Options& options, const Order& order,
                           const juce::String& serial, const juce::String& firstName)
{
    juce::String repoLine;
    if (options.repoUrl.isNotEmpty())
        repoLine << "The source code is published under the AGPLv3: " << options.repoUrl << "\r\n\r\n";

    text = text.replace ("{name}", order.name.isNotEmpty() ? order.name : juce::String ("there"));
    text = text.replace ("{order}", order.orderId.isNotEmpty() ? order.orderId : order.key);
    text = text.replace ("{machine}", order.machineId);
    text = text.replace ("{serial}", prettySerial (serial));
    text = text.replace ("{file}", firstName);
    text = text.replace ("{from}", options.fromAddress);
    text = text.replace ("{repo_line}", repoLine);
    text = text.replace ("{repo}", options.repoUrl);
    return text;
}

// A reply-ready RFC 5322 message: the seller's mail client opens it with the
// buyer, the subject, the body and the .goalicense already in place.
bool writeEml (const juce::File& outFile, const Options& options, const Order& order,
               const juce::String& body, const juce::File& attachment)
{
    juce::MemoryBlock attached;
    if (! attachment.existsAsFile() || ! attachment.loadFileAsData (attached))
        return false;

    const juce::String boundary = "goasynth-" + keyFilename (order.key) + "-licence";
    const juce::String subject = fillTemplate (options.subject, options, order, {}, attachment.getFileName());
    const juce::String bodyWithCrLf = body;

    juce::String eml;
    eml << "From: " << options.fromAddress << "\r\n"
        << "To: " << (order.email.isNotEmpty() ? order.email : juce::String ("(missing - check the export)")) << "\r\n"
        << "Subject: " << subject.replaceCharacters ("\r\n", " ") << "\r\n"
        << "MIME-Version: 1.0\r\n"
        << "Content-Type: multipart/mixed; boundary=\"" << boundary << "\"\r\n"
        << "X-GoaSynth-Order: " << order.orderId << "\r\n"
        << "\r\n"
        << "--" << boundary << "\r\n"
        << "Content-Type: text/plain; charset=UTF-8\r\n"
        << "Content-Transfer-Encoding: base64\r\n"
        << "\r\n"
        << base64Block (bodyWithCrLf.toRawUTF8(), (int) bodyWithCrLf.getNumBytesAsUTF8())
        << "\r\n"
        << "--" << boundary << "\r\n"
        << "Content-Type: application/octet-stream; name=\"" << attachment.getFileName() << "\"\r\n"
        << "Content-Transfer-Encoding: base64\r\n"
        << "Content-Disposition: attachment; filename=\"" << attachment.getFileName() << "\"\r\n"
        << "\r\n"
        << base64Block (attached.getData(), (int) attached.getSize())
        << "\r\n"
        << "--" << boundary << "--\r\n";

    return outFile.replaceWithText (eml);
}

//==============================================================================
void printSummary (const Options& options, const Summary& s)
{
    std::cout << "  files scanned      : " << s.filesScanned << "\n"
              << "  orders seen        : " << s.ordersSeen << "\n"
              << "  serials issued     : " << s.issued << "\n"
              << "  serials revoked    : " << s.revoked << "\n"
              << "  already fulfilled  : " << s.alreadyFulfilled << "\n"
              << "  skipped            : " << s.skipped << " (refunds and other products)\n"
              << "  need attention     : " << s.needAttention << "\n";

    if (! options.quiet)
        for (const auto& note : s.notes)
            std::cout << "    - " << note << "\n";

    if (s.needAttention > 0)
        std::cout << "  see : " << options.outbox.getChildFile ("needs-attention.tsv").getFullPathName() << "\n";
}

// The order that should hold a given machine's serial right now: the newest row
// in this pass's export that is paid, ours, and actually usable. Used to stop a
// stale refund row from revoking a serial that a later re-purchase owns.
const Order* newestPaidOrderFor (const std::vector<Order>& all, const juce::String& machineId,
                                 const Options& options)
{
    const Order* best = nullptr;

    if (machineId.isEmpty())
        return best;

    for (const auto& o : all)
    {
        if (o.refunded || o.machineId != machineId || ! o.problems.isEmpty())
            continue;

        if (options.productFilter.isNotEmpty() && o.product.isNotEmpty()
             && ! o.product.containsIgnoreCase (options.productFilter))
            continue;

        if (best == nullptr || o.key.compareNatural (best->key) > 0)
            best = &o;
    }

    return best;
}
} // namespace

//==============================================================================
juce::File defaultOutbox (const juce::File& inbox)
{
    if (inbox == juce::File())
        return juce::File::getCurrentWorkingDirectory().getChildFile ("fulfilled");

    return inbox.getParentDirectory().getChildFile ("fulfilled");
}

Summary runOnce (const Options& options)
{
    Summary s;

    const juce::File inbox  = options.inbox;
    const juce::File outbox = options.outbox != juce::File() ? options.outbox : defaultOutbox (inbox);

    if (! inbox.isDirectory())
    {
        s.error = "Inbox folder not found: " + inbox.getFullPathName()
                    + "\nPoint --inbox at the folder your store exports orders into.";
        return s;
    }

    KeyPairText keys;
    if (! options.dryRun)
    {
        keys = loadKeys();
        if (! keys.ok)
        {
            s.error = "No keypair found at " + keysFile().getFullPathName()
                        + "\nRun GoaSynthKeygen --init once (and rebuild the plugin), or point"
                          "\nGOASYNTH_KEYGEN_DIR at an existing keygen folder.";
            return s;
        }
    }

    const juce::File manifest   = outbox.getChildFile ("manifest.tsv");
    const juce::File attention  = outbox.getChildFile ("needs-attention.tsv");
    const juce::File activity   = outbox.getChildFile ("activity.log");
    const juce::File mailDir    = outbox.getChildFile ("mail");
    const juce::File licenseDir = outbox.getChildFile ("licenses");

    if (! options.dryRun)
    {
        outbox.createDirectory();
        mailDir.createDirectory();
        licenseDir.createDirectory();
    }

    auto entries = readManifest (manifest);

    std::vector<Order> needsAttention;
    std::vector<Order> allOrders;

    juce::Array<juce::File> found;
    inbox.findChildFiles (found, juce::File::findFiles, false, "*.csv;*.json");

    std::vector<juce::File> files;
    for (const auto& f : found)
        files.push_back (f);

    std::sort (files.begin(), files.end(), [] (const juce::File& a, const juce::File& b)
               { return a.getFileName().compareNatural (b.getFileName()) < 0; });

    // Never re-read our own output.
    files.erase (std::remove_if (files.begin(), files.end(), [&outbox] (const juce::File& f)
                                 { return f.isAChildOf (outbox); }),
                 files.end());

    for (const auto& file : files)
    {
        ++s.filesScanned;

        for (auto& order : parseOrderFile (file))
            allOrders.push_back (order);
    }

    std::sort (allOrders.begin(), allOrders.end(), [] (const Order& a, const Order& b)
               { return a.key.compareNatural (b.key) < 0; });

    for (auto& order : allOrders)
    {
        ++s.ordersSeen;

        // The manifest is an append-only log, so the newest row for an order is
        // its current state: issued -> (refund) -> revoked.
        bool haveEntry = false;
        const ManifestEntry latest = latestEntryFor (entries, order.key, haveEntry);
        const bool fulfilledBefore = haveEntry && latest.status == "issued";

        // ---- refunds: pull that serial out of the active ledger -------------
        if (order.refunded)
        {
            ++s.skipped;

            if (! fulfilledBefore)
            {
                if (haveEntry && (latest.status == "revoked" || latest.status == "refunded"))
                {
                    s.notes.add ("order " + order.key + " is still marked refunded");
                }
                else
                {
                    if (! options.dryRun)
                    {
                        ManifestEntry e;
                        e.key      = order.key;
                        e.status   = "refunded";
                        e.email    = order.email;
                        e.issuedAt = juce::Time::getCurrentTime().toISO8601 (true);
                        appendManifest (manifest, e);
                        entries.push_back (e);
                        logLine (activity, "REFUNDED before issue: " + order.key);
                    }
                    s.notes.add ("refunded order " + order.key + " had no serial to revoke");
                }
                continue;
            }

            // The buyer may have refunded this order and then bought again under a
            // NEWER order, whose pass already restored the serial. This export can
            // still list the old refund, and revoking here would take a paying
            // customer's licence away - the re-purchase is "already fulfilled", so
            // nothing would put it back. Leave the serial alone and keep the refund
            // on the order's record instead.
            if (const Order* newOwner = newestPaidOrderFor (allOrders, latest.machineId, options))
            {
                if (! options.dryRun)
                {
                    ManifestEntry e;
                    e.key      = order.key;
                    e.status   = "refunded";
                    e.email    = order.email;
                    e.machineId = latest.machineId;
                    e.serial   = latest.serial;
                    e.issuedAt = juce::Time::getCurrentTime().toISO8601 (true);
                    appendManifest (manifest, e);
                    entries.push_back (e);
                    logLine (activity, "REFUND " + order.key + " left the serial live (re-bought as order "
                                         + newOwner->key + ")");
                }

                s.notes.add ("refund of " + order.key + " left the serial live - it is held by order "
                              + newOwner->key);
                continue;
            }

            if (options.dryRun)
            {
                ++s.revoked;
                s.notes.add ("would revoke the serial for refunded order " + order.key
                              + " (machine " + latest.machineId + ")");
                continue;
            }

            const auto revocation = revokeSerial (latest.serial, "refund - order " + order.key);

            if (! revocation.ok)
            {
                order.problems.add ("the refund could not be recorded: " + revocation.error);
                needsAttention.push_back (order);
                ++s.needAttention;
                logLine (activity, "REVOKE FAILED " + order.key + " -> " + shortSerial (latest.serial)
                                     + " (" + revocation.error + ")");
                continue;
            }

            ++s.revoked;

            ManifestEntry e;
            e.key         = order.key;
            e.status      = "revoked";
            e.email       = order.email;
            e.machineId   = latest.machineId;
            e.serial      = latest.serial;
            e.licenseFile = latest.licenseFile;
            e.emlFile     = latest.emlFile;
            e.issuedAt    = juce::Time::getCurrentTime().toISO8601 (true);

            appendManifest (manifest, e);
            entries.push_back (e);

            logLine (activity, "REVOKED " + order.key + " -> " + shortSerial (latest.serial)
                                 + (revocation.alreadyRevoked ? " (was already revoked)"
                                                              : " (machine " + latest.machineId + ")"));
            s.notes.add ((revocation.alreadyRevoked ? "serial already revoked for " : "revoked serial for ")
                          + order.key + " (machine " + latest.machineId + ")");
            continue;
        }

        // Another product in the same export is simply not ours.
        if (options.productFilter.isNotEmpty() && order.product.isNotEmpty()
             && ! order.product.containsIgnoreCase (options.productFilter))
        {
            ++s.skipped;
            continue;
        }

        if (order.machineId.isEmpty() || ! order.problems.isEmpty())
        {
            ++s.needAttention;
            needsAttention.push_back (order);
            continue;
        }

        if (fulfilledBefore)
        {
            ++s.alreadyFulfilled;

            // Re-create a missing file rather than re-issue: the serial is
            // deterministic, so this cannot change what the buyer holds.
            const juce::File licenseFile (latest.licenseFile);
            if (! options.dryRun && ! licenseFile.existsAsFile())
            {
                writeLicenseFile (latest.serial, latest.machineId, "order " + order.key, licenseFile,
                                  order.name, order.email);
                s.notes.add ("re-wrote missing licence file for order " + order.key);
            }
            continue;
        }

        const juce::String licenseName = "GoaSynth-" + order.machineId + ".goalicense";
        const juce::File licenseFile = licenseDir.getChildFile (licenseName);
        const juce::File emlFile = mailDir.getChildFile (keyFilename (order.key)
                                                          + (order.email.isNotEmpty() ? "-" + keyFilename (order.email) : juce::String())
                                                          + ".eml");

        if (options.dryRun)
        {
            ++s.issued;
            s.notes.add ("would issue a serial for order " + order.key + " (machine " + order.machineId + ")");
            continue;
        }

        bool alreadyIssued = false;
        const juce::String serial = recordIssued (keys, order.machineId, "order " + order.key
                                                                          + (order.name.isNotEmpty() ? " - " + order.name : ""),
                                                  alreadyIssued);

        // Re-purchase, or a corrected export: the deterministic signature means
        // the buyer gets the same serial back, so this is a restoration rather
        // than a new licence. Let the ledger say so too, or --list and --verify
        // would go on calling a paying customer's serial revoked. The refund
        // event itself stays in revoked_serials.txt for the support history.
        const auto prior = revocationState (serial);
        if (prior.revoked)
        {
            const auto restore = restoreSerial (serial, "re-purchase - order " + order.key);

            s.notes.add ("order " + order.key + " re-issues a serial revoked on " + prior.at
                          + (prior.reason.isEmpty() ? juce::String() : " (" + prior.reason + ")")
                          + (restore.ok ? " - ledger line restored" : " - could not be restored: " + restore.error));

            if (restore.ok)
                logLine (activity, "RESTORED " + order.key + " -> " + shortSerial (serial)
                                     + " (refund of " + prior.at + " stays on record)");
        }

        if (! writeLicenseFile (serial, order.machineId, "order " + order.key, licenseFile,
                                order.name, order.email))
        {
            ++s.needAttention;
            order.problems.add ("could not write " + licenseFile.getFullPathName());
            needsAttention.push_back (order);
            continue;
        }

        juce::String body;
        if (options.bodyTemplate.existsAsFile())
            body = options.bodyTemplate.loadFileAsString().replace ("\r\n", "\n").replace ("\n", "\r\n");
        else
            body = defaultBody();

        body = fillTemplate (body, options, order, serial, licenseName);

        if (! writeEml (emlFile, options, order, body, licenseFile))
            s.notes.add ("licence written but the email draft failed for order " + order.key);

        ManifestEntry e;
        e.key         = order.key;
        e.status      = "issued";
        e.email       = order.email;
        e.machineId   = order.machineId;
        e.serial      = serial;
        e.licenseFile = licenseFile.getFullPathName();
        e.emlFile     = emlFile.getFullPathName();
        e.issuedAt    = juce::Time::getCurrentTime().toISO8601 (true);

        appendManifest (manifest, e);
        entries.push_back (e);

        ++s.issued;
        s.notes.add ("issued " + order.machineId + " -> " + licenseFile.getFileName());
        std::cout << "issued  order " << order.key.paddedRight (' ', 10) << " machine " << order.machineId
                  << "  (" << (alreadyIssued ? "serial was already in the ledger" : "new ledger entry") << ")\n";
    }

    if (! options.dryRun)
    {
        writeAttentionReport (attention, needsAttention);

        if (s.issued > 0 || s.revoked > 0 || s.needAttention > 0)
            logLine (activity, "pass: " + juce::String (s.issued) + " issued, "
                                 + juce::String (s.alreadyFulfilled) + " already fulfilled, "
                                 + juce::String (s.revoked) + " revoked, "
                                 + juce::String (s.skipped) + " skipped, "
                                 + juce::String (s.needAttention) + " need attention");
    }

    return s;
}

//==============================================================================
namespace
{
void printHelp()
{
    std::cout
        << "GoaSynth order fulfilment\n"
        << "=========================\n"
        << "Reads the store's order exports from an inbox folder, signs a licence\n"
        << "serial for each buyer's MACHINE ID, writes their .goalicense file and a\n"
        << "reply-ready .eml with it attached, then records the lot in\n"
        << "<outbox>/manifest.tsv so a re-run can never issue twice.\n"
        << "\n"
        << "  GoaSynthFulfil [options]\n"
        << "\n"
        << "  --inbox DIR       folder the store exports orders into (default ./orders)\n"
        << "  --out DIR         output folder (default: <inbox>/../fulfilled)\n"
        << "  --from \"NAME <email>\"   From header for the drafts\n"
        << "  --subject TEXT    Subject (same {placeholders} as the body)\n"
        << "  --body FILE       your own body template instead of the built-in one\n"
        << "  --product TEXT    only fulfil orders whose product contains TEXT\n"
        << "                    (default \"goasynth\"; pass \"\" to accept every row)\n"
        << "  --repo URL        AGPL source link to include in the email\n"
        << "  --dry-run         report what would happen; sign and write nothing\n"
        << "  --watch           keep polling the inbox instead of exiting\n"
        << "  --interval SEC    poll interval in --watch mode (default 30)\n"
        << "  --status          show the manifest and the attention report, then exit\n"
        << "  --quiet           only print issues and issued serials\n"
        << "  --help            this text\n"
        << "\n"
        << "Placeholders for --subject and --body: {name} {order} {machine} {serial}\n"
        << "{file} {from} {repo} {repo_line}\n"
        << "\n"
        << "Machine IDs: a column named \"Machine ID\"/\"HWID\"/\"Device ID\" (any\n"
        << "case) is used directly; otherwise every value is scanned for a 20-hex\n"
        << "token, which must contain a letter so a 20-digit order number cannot be\n"
        << "mistaken for one. Anything ambiguous is listed in needs-attention.tsv\n"
        << "instead of being signed -- a serial bound to the wrong machine is worse\n"
        << "than one sent a day late.\n"
        << "\n"
        << "The signing key comes from the keygen's own folder\n"
        << "(" << keygenDir().getFullPathName() << "),\n"
        << "so run GoaSynthKeygen --init first if keys.txt is missing.\n";
}

int showStatus (const Options& options)
{
    const juce::File outbox = options.outbox != juce::File() ? options.outbox : defaultOutbox (options.inbox);
    const auto entries = readManifest (outbox.getChildFile ("manifest.tsv"));

    std::vector<ManifestEntry> rows;   // newest row per order
    for (const auto& e : entries)
    {
        auto found = std::find_if (rows.begin(), rows.end(), [&e] (const ManifestEntry& r)
                                   { return r.key == e.key; });
        if (found == rows.end())
            rows.push_back (e);
        else
            *found = e;
    }

    int live = 0, revoked = 0, refunded = 0;
    for (const auto& e : rows)
    {
        if      (e.status == "issued")   ++live;
        else if (e.status == "revoked")  ++revoked;
        else if (e.status == "refunded") ++refunded;
    }

    std::cout << "outbox : " << outbox.getFullPathName() << "\n"
              << "orders : " << (int) rows.size() << " handled - " << live << " live, "
              << revoked << " revoked, " << refunded << " refunded\n"
              << "ledger : " << issuedFile().getFullPathName() << "\n"
              << "         " << revokedCount() << " revoked in " << revokedFile().getFileName() << "\n\n";

    for (const auto& e : rows)
        std::cout << "  " << e.key.paddedRight (' ', 12)
                  << e.status.paddedRight (' ', 10)
                  << (e.email.isNotEmpty() ? e.email : juce::String ("(no email)"))
                  << (e.machineId.isEmpty() ? juce::String() : "  " + e.machineId) << "\n";

    // Only worth printing when there are orders in it, not just the header.
    const juce::File attention = outbox.getChildFile ("needs-attention.tsv");
    int attentionRows = 0;

    if (attention.existsAsFile())
        for (const auto& line : juce::StringArray::fromLines (attention.loadFileAsString()))
            if (line.trim().isNotEmpty())
                ++attentionRows;

    if (attentionRows > 1)
    {
        std::cout << "\nneeding attention:\n";
        std::cout << attention.loadFileAsString();
    }

    std::cout << "\nSerials of the live licences: GoaSynthKeygen --list\n";
    return 0;
}

bool nextArg (int argc, char* argv[], int& i, juce::String& out)
{
    if (i + 1 >= argc)
        return false;

    out = juce::String (argv[++i]);
    return true;
}
} // namespace

int run (int argc, char* argv[])
{
    Options options;
    bool wantStatus = false;   // handled after the whole command line is parsed,
                               // so --status is not deaf to flags that follow it

    for (int i = 1; i < argc; ++i)
    {
        const juce::String arg (argv[i]);
        juce::String value;

        if      (arg == "--help" || arg == "-h") { printHelp(); return 0; }
        else if (arg == "--inbox")               { if (! nextArg (argc, argv, i, value)) return 1; options.inbox = juce::File (value); }
        else if (arg == "--out")                 { if (! nextArg (argc, argv, i, value)) return 1; options.outbox = juce::File (value); }
        else if (arg == "--from")                { if (! nextArg (argc, argv, i, value)) return 1; options.fromAddress = value; }
        else if (arg == "--subject")             { if (! nextArg (argc, argv, i, value)) return 1; options.subject = value; }
        else if (arg == "--body")                { if (! nextArg (argc, argv, i, value)) return 1; options.bodyTemplate = juce::File (value); }
        else if (arg == "--product")             { if (! nextArg (argc, argv, i, value)) return 1; options.productFilter = value; }
        else if (arg == "--repo")                { if (! nextArg (argc, argv, i, value)) return 1; options.repoUrl = value; }
        else if (arg == "--interval")            { if (! nextArg (argc, argv, i, value)) return 1; options.intervalSeconds = juce::jmax (5, value.getIntValue()); }
        else if (arg == "--dry-run")             { options.dryRun = true; }
        else if (arg == "--watch")               { options.watch = true; }
        else if (arg == "--once")                { options.watch = false; }   // implicit default, accepted for clarity
        else if (arg == "--quiet")               { options.quiet = true; }
        else if (arg == "--status")              { wantStatus = true; }
        else
        {
            std::cout << "Unknown option: " << arg << "\n\n";
            printHelp();
            return 1;
        }
    }

    if (options.inbox == juce::File())
        options.inbox = juce::File::getCurrentWorkingDirectory().getChildFile ("orders");

    if (options.outbox == juce::File())
        options.outbox = defaultOutbox (options.inbox);

    if (wantStatus)
        return showStatus (options);

    if (! options.quiet)
        std::cout << "GoaSynth fulfilment\n"
                  << "  inbox  : " << options.inbox.getFullPathName() << "\n"
                  << "  outbox : " << options.outbox.getFullPathName() << "\n"
                  << (options.dryRun ? "  mode   : DRY RUN (nothing is signed or written)\n"
                                     : (options.watch ? "  mode   : watching\n" : "  mode   : single pass\n"))
                  << "\n";

    for (;;)
    {
        const auto s = runOnce (options);

        if (s.error.isNotEmpty())
        {
            std::cout << s.error << "\n";
            return 1;
        }

        printSummary (options, s);

        if (! options.watch)
            return 0;

        std::cout << "\nwatching " << options.inbox.getFullPathName() << " every "
                  << options.intervalSeconds << "s (Ctrl-C to stop)\n\n";
        juce::Thread::sleep (options.intervalSeconds * 1000);
    }
}
} // namespace fulfil
