#pragma once

#include <juce_core/juce_core.h>
#include <juce_cryptography/juce_cryptography.h>

// LicenseKeys.h is (re)created by --init and may not exist on a fresh
// checkout; it defines GOA_MASTER_STRETCH_ROUNDS when present.
#if __has_include ("../Source/LicenseKeys.h")
 #include "../Source/LicenseKeys.h"
#endif

#ifndef GOA_MASTER_STRETCH_ROUNDS
 #define GOA_MASTER_STRETCH_ROUNDS 64
#endif

//==============================================================================
// Seller-side licence core.
//
// Shared by the console keygen (Keygen/GoaSynthKeygen.cpp) and the order
// fulfilment tool (Fulfil/) so that both sign serials through exactly the code
// path the plugin verifies — one place to change, no chance of drift.
//
//   serial = GOA1-<machineId, 20 hex>-<RSA/2048 signature of sha256(machineId)>
//
// Storage: %APPDATA%/GoaSynth/Keygen/ holds keys.txt (the RSA keypair plus the
// stretched master digest) and issued_serials.txt (serial<TAB>machineId<TAB>note,
// one line per distinct serial). The private key and master key never leave
// these files; the plugin embeds only the public key and the digest.
//
// GOASYNTH_KEYGEN_DIR redirects that folder, so tests and CI can run against a
// throwaway keypair instead of the seller's real one (the same trick the plugin
// uses with GOASYNTH_TRIAL_FILE).
namespace keygen::core
{
constexpr int machineHexLength = 20;   // 10 bytes of SHA-256, hex-encoded

//==============================================================================
inline juce::File keygenDir()
{
    const juce::String override = juce::SystemStats::getEnvironmentVariable ("GOASYNTH_KEYGEN_DIR", {});
    if (override.isNotEmpty())
        return juce::File (override);

    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
               .getChildFile ("GoaSynth").getChildFile ("Keygen");
}

inline juce::File keysFile()   { return keygenDir().getChildFile ("keys.txt"); }
inline juce::File issuedFile() { return keygenDir().getChildFile ("issued_serials.txt"); }

//==============================================================================
inline juce::String sha256Hex (const void* data, size_t n) { return juce::SHA256 (data, n).toHexString(); }
inline juce::String sha256Hex (const juce::String& s)      { return sha256Hex (s.toRawUTF8(), (size_t) s.getNumBytesAsUTF8()); }

// Master key verification: the plugin embeds only this stretched digest, so
// neither the key nor a replayable hash of it lives in the binary.
inline juce::String stretchedMasterDigest (const juce::String& rawKey)
{
    juce::String h = sha256Hex ("goa-master::" + rawKey);
    for (int i = 0; i < GOA_MASTER_STRETCH_ROUNDS; ++i)
        h = sha256Hex (h);
    return h.toUpperCase();
}

inline juce::BigInteger valueFromHex (const juce::String& hex)
{
    juce::BigInteger v;
    v.parseString (hex, 16);
    return v;
}

inline juce::String hexOfValue (const juce::BigInteger& v) { return v.toString (16, 512).toLowerCase(); }

//==============================================================================
// Machine ids: identical derivation to Source/License.cpp — keep in sync!
inline juce::String machineSeed()
{
    juce::String s;
    s << juce::SystemStats::getComputerName() << "|"
      << juce::SystemStats::getFullUserName() << "|"
      << juce::SystemStats::getLogonName() << "|"
      << juce::SystemStats::getUniqueDeviceID() << "|";

    for (const auto& mac : juce::MACAddress::getAllAddresses())
        s << mac.toString() << ";";

    return s;
}

inline juce::String idFromSeed (const juce::String& seed)
{
    const juce::SHA256 h (seed.toRawUTF8(), (size_t) seed.getNumBytesAsUTF8());
    return juce::String::toHexString (h.getRawData().getData(), 10, 0).toUpperCase();
}

inline juce::String thisMachineId() { return idFromSeed (machineSeed()); }

inline juce::String normaliseMachineId (juce::String in)
{
    in = in.removeCharacters ("-").removeCharacters (" ").toUpperCase();
    return in;
}

inline bool looksLikeMachineId (const juce::String& idIn)
{
    const juce::String id = normaliseMachineId (idIn);

    if (id.length() != machineHexLength)
        return false;

    for (auto c : id)
        if (juce::CharacterFunctions::getHexDigitValue (c) < 0)
            return false;

    return true;
}

//==============================================================================
struct KeyPairText { juce::String pub, priv, masterDigest; bool ok = false; };

inline KeyPairText loadKeys()
{
    KeyPairText k;

    const juce::File f = keysFile();
    if (! f.existsAsFile())
        return k;

    const auto lines = juce::StringArray::fromLines (f.loadFileAsString());
    for (auto& line : lines)
    {
        auto t = line.trim();
        if      (t.startsWith ("public="))       { k.pub    = t.substring (7); }
        else if (t.startsWith ("private="))      { k.priv   = t.substring (8); }
        else if (t.startsWith ("masterDigest=")) { k.masterDigest = t.substring (13); }
    }

    k.ok = k.pub.isNotEmpty() && k.priv.isNotEmpty();
    return k;
}

//==============================================================================
struct InitResult
{
    bool ok = false;
    juce::String masterKey;   // shown to the seller once, never stored in clear
    juce::String error;
};

// Creates the keypair + master key. `headerFile` is where the plugin's
// public-key header (Source/LicenseKeys.h) is written; pass {} to skip it
// (tests do, so they never touch the build tree).
inline InitResult createKeypair (const juce::String& presetMaster, const juce::File& headerFile = {})
{
    InitResult r;

    if (keysFile().existsAsFile())
    {
        r.error = "A keypair already exists at:\n  " + keysFile().getFullPathName()
                    + "\nDelete it first if you really want to invalidate all existing serials.";
        return r;
    }

    juce::RSAKey pub, priv;
    juce::RSAKey::createKeyPair (pub, priv, 2048);

    if (! pub.isValid() || ! priv.isValid())
    {
        r.error = "Key generation failed.";
        return r;
    }

    // Master key: either the one given (e.g. --init YOUR-MASTER-KEY) or a random
    // printable 12-char secret.
    juce::String master = presetMaster.trim();

    if (master.isEmpty())
    {
        const juce::String charset = "ABCDEFGHJKLMNPQRSTUVWXYZabcdefghjkmnpqrstuvwxyz23456789!@#$%&";
        juce::Random rng;
        for (int i = 0; i < 12; ++i)
            master << charset[rng.nextInt ((int) charset.length())];
    }
    else if (master.length() < 8)
    {
        r.error = "Master key too short (use at least 8 characters).";
        return r;
    }

    const juce::String digest = stretchedMasterDigest (master);

    keygenDir().createDirectory();
    juce::String text;
    text << "# GoaSynth keygen master file — BACK THIS UP, losing it invalidates all future serials\n"
         << "created=" << juce::Time::getCurrentTime().toISO8601 (true) << "\n"
         << "public=" << pub.toString() << "\n"
         << "private=" << priv.toString() << "\n"
         << "masterDigest=" << digest << "\n";

    if (! keysFile().replaceWithText (text))
    {
        r.error = "Could not write " + keysFile().getFullPathName();
        return r;
    }

    // Write the public half + master digest for the plugin build.
    if (headerFile != juce::File())
    {
        juce::String header;
        header << "// AUTO-GENERATED by GoaSynthKeygen --init — do not commit.\n"
               << "// Rebuild the plugin after running --init.\n"
               << "#pragma once\n\n"
               << "#define GOA_LICENSE_PUBLIC_KEY \"" << pub.toString() << "\"\n"
               << "#define GOA_MASTER_DIGEST \"" << digest << "\"\n"
               << "#define GOA_MASTER_STRETCH_ROUNDS 64\n";   // keep in sync with the keygen

        headerFile.getParentDirectory().createDirectory();
        headerFile.replaceWithText (header);
    }

    r.ok = true;
    r.masterKey = master;
    return r;
}

//==============================================================================
inline juce::String makeSerial (const juce::String& machineId, const juce::RSAKey& priv)
{
    juce::BigInteger v = valueFromHex (sha256Hex (machineId));
    priv.applyToValue (v);
    return "GOA1-" + machineId + "-" + hexOfValue (v).toUpperCase();
}

inline juce::String makeSerial (const KeyPairText& keys, const juce::String& machineId)
{
    return makeSerial (normaliseMachineId (machineId), juce::RSAKey (keys.priv));
}

inline juce::String groupHex (const juce::String& hex)
{
    juce::String out;
    for (int i = 0; i < hex.length(); i += 4)
    {
        if (i > 0) out << "-";
        out << hex.substring (i, i + 4);
    }
    return out;
}

// Pretty display form: GOA1-<grouped machine id>-<grouped signature>.
// (Dashes are cosmetic; the plugin strips them when parsing.)
inline juce::String prettySerial (const juce::String& raw)
{
    const juce::String body = raw.startsWith ("GOA1-") ? raw.substring (5) : raw;
    const int dash = body.indexOfChar ('-');
    if (dash < 0)
        return groupHex (raw);

    return "GOA1-" + groupHex (body.substring (0, dash)) + "-" + groupHex (body.substring (dash + 1));
}

// True when pubKey(privSig) == sha256(machineId) — same check the plugin runs.
inline bool verifySerial (const juce::String& serialIn, const juce::String& pubText,
                          juce::String& machineIdOut, juce::String& whyNot)
{
    const juce::String serial = serialIn.trim().toUpperCase();

    machineIdOut.clear();
    whyNot.clear();

    if (! serial.startsWith ("GOA1-"))
    {
        whyNot = "serial must start with GOA1-";
        return false;
    }

    const juce::String body = serial.substring (5);
    const int dash = body.indexOfChar ('-');

    if (dash != machineHexLength)
    {
        whyNot = "malformed serial";
        return false;
    }

    const juce::String id  = body.substring (0, dash);
    const juce::String sig = body.substring (dash + 1);

    if (! looksLikeMachineId (id))
    {
        whyNot = "bad machine id";
        return false;
    }

    juce::BigInteger v = valueFromHex (sig);
    const juce::RSAKey pub (pubText);

    if (v.isZero() || ! pub.isValid() || ! pub.applyToValue (v))
    {
        whyNot = "signature undecodable";
        return false;
    }

    if (v != valueFromHex (sha256Hex (id)))
    {
        whyNot = "signature does not match machine id (forged or tampered)";
        return false;
    }

    machineIdOut = id;
    return true;
}

inline bool verifySerial (const KeyPairText& keys, const juce::String& serial,
                          juce::String& machineIdOut, juce::String& whyNot)
{
    return verifySerial (serial, keys.pub, machineIdOut, whyNot);
}

//==============================================================================
// Issue (or re-issue) a serial for a machine id and record it in the ledger.
// The signature is deterministic: the same machine id always yields the SAME
// serial, so the ledger keeps one line per distinct serial — a re-issue finds
// the existing entry and keeps it.
inline juce::String recordIssued (const KeyPairText& keys, const juce::String& idIn,
                                  const juce::String& note, bool& alreadyIssued)
{
    const juce::String id = normaliseMachineId (idIn);
    const juce::String serial = makeSerial (id, juce::RSAKey (keys.priv));

    alreadyIssued = false;
    for (const auto& line : juce::StringArray::fromLines (issuedFile().loadFileAsString()))
    {
        const auto toks = juce::StringArray::fromTokens (line.trim(), "\t", "");
        if (toks.size() > 0 && toks[0] == serial)
            alreadyIssued = true;
    }

    if (! alreadyIssued)
    {
        juce::String log;
        log << serial << "\t" << id << "\t" << note.replaceCharacters ("\t\r\n", "   ") << "\n";
        keygenDir().createDirectory();
        issuedFile().appendText (log);
    }

    return serial;
}

//==============================================================================
// Refunds.
//
// --unregister erases a ledger line, which is right for a transfer (the buyer
// moves to a new machine and the old serial stops counting) but wrong for a
// refund: it leaves no trace that the serial ever existed or why it went away.
// revokeSerial() instead removes the line from the active ledger AND appends it
// to revoked_serials.txt, so --list counts live licences while the history of
// what was pulled, when and why survives.
//
// This is bookkeeping, not enforcement: activation is offline, so a buyer who
// already activated keeps working. What revocation changes is what you re-issue,
// what you support, and what you can prove later.
//
// The log is an append-only event stream, so a serial's state is its NEWEST
// line: revoke, restore, revoke again all just append. The last column names
// the event ("revoked" or "restored"); lines written before restores existed
// have four columns and read as revocations, so the format stayed compatible.
// A refund therefore stays on the record even after the buyer buys again - that
// is the point of keeping history instead of erasing a line.
//==============================================================================
inline juce::File revokedFile() { return keygenDir().getChildFile ("revoked_serials.txt"); }

inline const char* revokedFileHeader()
{
    return "# GoaSynth serial revocation log - serial\tmachine id\treason\tat\tevent (revoked|restored)\n";
}

struct RevocationEvent
{
    bool found = false;     // the serial appears in the log at all
    bool revoked = false;   // ...and its newest event is a revocation
    juce::String serial, machineId, reason, at, event;
};

// A serial's current state, read from the newest line that mentions it.
inline RevocationEvent revocationState (const juce::String& serialIn)
{
    RevocationEvent state;

    const juce::String serial = serialIn.trim().toUpperCase();

    if (serial.isEmpty() || ! revokedFile().existsAsFile())
        return state;

    for (const auto& line : juce::StringArray::fromLines (revokedFile().loadFileAsString()))
    {
        const juce::String t = line.trim();
        if (t.isEmpty() || t.startsWithChar ('#'))
            continue;

        const auto cells = juce::StringArray::fromTokens (t, "\t", "");
        if (cells.isEmpty() || cells[0].toUpperCase() != serial)
            continue;

        state.found     = true;
        state.serial    = cells[0];
        state.machineId = cells.size() > 1 ? cells[1] : juce::String();
        state.reason    = cells.size() > 2 ? cells[2] : juce::String();
        state.at        = cells.size() > 3 ? cells[3] : juce::String();
        state.event     = cells.size() > 4 ? cells[4].toLowerCase() : juce::String ("revoked");
        state.revoked   = state.event != "restored";
    }

    return state;
}

// Looks a serial up in the revocation log. Returns the machine id, reason and
// timestamp so callers can report it rather than just say "revoked". True only
// while the serial's newest event is a revocation: a restored serial is a live
// licence again, even though the refund that pulled it stays on the record.
inline bool revokedEntry (const juce::String& serialIn, juce::String& machineIdOut,
                          juce::String& reasonOut, juce::String& whenOut)
{
    const auto state = revocationState (serialIn);

    machineIdOut = state.machineId;
    reasonOut    = state.reason;
    whenOut      = state.at;

    return state.found && state.revoked;
}

inline bool isRevoked (const juce::String& serial)
{
    juce::String id, reason, when;
    return revokedEntry (serial, id, reason, when);
}

// True when the serial was pulled at any point, restored or not, with the first
// revocation's reason and date - what a support email needs ("yes, order 7001
// was refunded in June").
inline bool wasRevoked (const juce::String& serialIn, juce::String& reasonOut, juce::String& whenOut)
{
    reasonOut.clear();
    whenOut.clear();

    const juce::String serial = serialIn.trim().toUpperCase();

    if (serial.isEmpty() || ! revokedFile().existsAsFile())
        return false;

    for (const auto& line : juce::StringArray::fromLines (revokedFile().loadFileAsString()))
    {
        const juce::String t = line.trim();
        if (t.isEmpty() || t.startsWithChar ('#'))
            continue;

        const auto cells = juce::StringArray::fromTokens (t, "\t", "");
        if (cells.isEmpty() || cells[0].toUpperCase() != serial)
            continue;

        const bool restored = cells.size() > 4 && cells[4].equalsIgnoreCase ("restored");
        if (! restored)
        {
            reasonOut = cells.size() > 2 ? cells[2] : juce::String();
            whenOut   = cells.size() > 3 ? cells[3] : juce::String();
            return true;
        }
    }

    return false;
}

// Serials whose newest event is a revocation - the count --list reports, so a
// restored re-purchase is not counted as revoked for ever.
inline int revokedCount()
{
    if (! revokedFile().existsAsFile())
        return 0;

    juce::StringArray revoked, restored;

    for (const auto& line : juce::StringArray::fromLines (revokedFile().loadFileAsString()))
    {
        const juce::String t = line.trim();
        if (t.isEmpty() || t.startsWithChar ('#'))
            continue;

        const auto cells = juce::StringArray::fromTokens (t, "\t", "");
        if (cells.isEmpty())
            continue;

        const juce::String s = cells[0].toUpperCase();

        if (cells.size() > 4 && cells[4].equalsIgnoreCase ("restored"))
        {
            restored.addIfNotAlreadyThere (s);
            revoked.removeString (s);
        }
        else
        {
            revoked.addIfNotAlreadyThere (s);
            restored.removeString (s);
        }
    }

    return revoked.size();
}

// The machine id a live serial was issued for, or {} when it is not in the
// active ledger any more.
inline juce::String machineIdForSerial (const juce::String& serialIn)
{
    const juce::String serial = serialIn.trim().toUpperCase();

    for (const auto& line : juce::StringArray::fromLines (issuedFile().loadFileAsString()))
    {
        const auto cells = juce::StringArray::fromTokens (line.trim(), "\t", "");
        if (cells.size() > 1 && cells[0].toUpperCase() == serial)
            return cells[1];
    }

    return {};
}

struct RevokeResult
{
    bool ok = false;
    bool alreadyRevoked = false;
    int ledgerLinesRemoved = 0;
    juce::String serial, machineId, reason, revokedAt, error;
};

// Removes a serial from the active ledger and records the revocation. Safe to
// call twice: a serial already in the log is reported, not duplicated.
inline RevokeResult revokeSerial (const juce::String& serialIn, const juce::String& reasonIn)
{
    RevokeResult r;
    r.serial = serialIn.trim().toUpperCase();

    if (r.serial.isEmpty())
    {
        r.error = "no serial given";
        return r;
    }

    if (revokedEntry (r.serial, r.machineId, r.reason, r.revokedAt))
    {
        r.ok = true;
        r.alreadyRevoked = true;
        if (r.machineId.isEmpty())
            r.machineId = machineIdForSerial (r.serial);
        return r;
    }

    const juce::File ledger = issuedFile();
    if (! ledger.existsAsFile())
    {
        r.error = "No serials issued yet.";
        return r;
    }

    juce::StringArray kept;
    for (const auto& line : juce::StringArray::fromLines (ledger.loadFileAsString()))
    {
        const juce::String t = line.trim();
        if (t.isEmpty())
            continue;

        const auto cells = juce::StringArray::fromTokens (t, "\t", "");
        if (cells.size() > 0 && cells[0].toUpperCase() == r.serial)
        {
            ++r.ledgerLinesRemoved;
            if (cells.size() > 1)
                r.machineId = cells[1];
            continue;
        }

        kept.add (t);
    }

    if (r.ledgerLinesRemoved == 0)
    {
        r.error = "Serial not found in the issued list (never issued, or already unregistered).";
        return r;
    }

    ledger.replaceWithText (kept.joinIntoString ("\n") + (kept.isEmpty() ? juce::String() : juce::String ("\n")));

    if (! revokedFile().existsAsFile())
        revokedFile().replaceWithText (juce::String (revokedFileHeader()));

    r.reason = reasonIn.trim().isEmpty() ? juce::String ("refund") : reasonIn.trim();
    r.revokedAt = juce::Time::getCurrentTime().toISO8601 (true);

    revokedFile().appendText (r.serial + "\t" + r.machineId + "\t"
                                + r.reason.replaceCharacters ("\t\r\n", "   ") + "\t" + r.revokedAt + "\n");

    r.ok = true;
    return r;
}

struct RestoreResult
{
    bool ok = false;
    bool hadRevocation = false;       // it was pulled at some point
    bool alreadyRestored = false;     // ...and this call changed nothing
    juce::String serial, machineId, reason, restoredAt, error;
};

// The other half of a refund: the buyer changes their mind and buys again (or a
// corrected export shows the order was paid after all). The ledger line is put
// back by recordIssued() - the signature is deterministic, so they get the
// identical serial - and this appends a `restored` event so --list stops
// counting the serial as revoked while the refund stays in the history.
inline RestoreResult restoreSerial (const juce::String& serialIn, const juce::String& reasonIn)
{
    RestoreResult r;
    r.serial = serialIn.trim().toUpperCase();

    if (r.serial.isEmpty())
    {
        r.error = "no serial given";
        return r;
    }

    const auto state = revocationState (r.serial);

    if (! state.found)
    {
        r.error = "Serial is not in the revocation log (nothing to restore).";
        return r;
    }

    r.hadRevocation = true;
    r.machineId     = state.machineId;

    if (! state.revoked)
    {
        r.ok = true;
        r.alreadyRestored = true;
        return r;
    }

    r.reason     = reasonIn.trim().isEmpty() ? juce::String ("restored") : reasonIn.trim();
    r.restoredAt = juce::Time::getCurrentTime().toISO8601 (true);

    revokedFile().appendText (r.serial + "\t" + r.machineId + "\t"
                                + r.reason.replaceCharacters ("\t\r\n", "   ") + "\t"
                                + r.restoredAt + "\trestored\n");

    r.ok = true;
    return r;
}

//==============================================================================
// Buyer-ready .goalicense document (GOA-LICENSE-1). The plugin's parser only
// needs the serial line; everything else is documentation for humans.
// Keep in sync with Source/License.cpp (makeLicenseFile).
inline juce::String licenseFileContents (const juce::String& serial, const juce::String& machineId,
                                         const juce::String& note,
                                         const juce::String& buyerName = {},
                                         const juce::String& buyerEmail = {})
{
    const juce::String name  = buyerName.trim().replaceCharacters ("\r\n", "  ");
    const juce::String email = buyerEmail.trim().replaceCharacters ("\r\n", "  ");
    juce::String contents;
    contents << "GOA-LICENSE-1\n"
             << "serial:  " << serial << "\n"
             << "machine: " << machineId << "\n"
             << "name:    " << (name.isEmpty()  ? juce::String ("-") : name) << "\n"
             << "email:   " << (email.isEmpty() ? juce::String ("-") : email) << "\n"
             << "note:    " << (note.isEmpty()  ? juce::String ("-") : note) << "\n"
             << "issued:  " << juce::Time::getCurrentTime().toISO8601 (true) << "\n"
             << "plugin:  GoaSynth VST3\n"
             << "usage:   double-click this file (opens GoaSynth) or IMPORT it on the activation screen\n";
    return contents;
}

inline bool writeLicenseFile (const juce::String& serial, const juce::String& machineId,
                              const juce::String& note, const juce::File& outFile,
                              const juce::String& buyerName = {},
                              const juce::String& buyerEmail = {})
{
    return outFile.getParentDirectory().createDirectory().wasOk()
             && outFile.replaceWithText (licenseFileContents (serial, machineId, note, buyerName, buyerEmail));
}

} // namespace keygen::core
