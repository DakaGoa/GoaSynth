#include "GoaSynthKeygen.h"
#include "KeygenCore.h"

// Everything that signs, verifies or logs a serial lives in KeygenCore.h, shared
// with the order-fulfilment tool (Fulfil/) so both use one code path.
using namespace keygen::core;

//==============================================================================
// GoaSynth key generator (seller-side console tool).
//
//   GoaSynthKeygen --init                    create keypair + master key (once!)
//   GoaSynthKeygen --rotate-master [newKey]  replace ONLY the master key, keeping the
//                                            keypair so every issued serial stays valid
//   GoaSynthKeygen --machine-id              print this machine's id
//   GoaSynthKeygen --gen <machineId> [note]  issue a serial for a buyer's machine
//   GoaSynthKeygen --reissue <oldSerial> <newMachineId> [note]
//                                            move a licence: retire the old serial
//                                            and issue one for the new machine at once
//   GoaSynthKeygen --list                    list every issued serial
//   GoaSynthKeygen --verify <serial>         cryptographically check a serial
//   GoaSynthKeygen --master-info             show stored master digest
//
// Serial format (identical to the plugin's verifier):
//   GOA1-<machineId, 20 hex>-<RSA/2048 signature of sha256(machineId), 512 hex>
//
// The private key and master key never leave this tool; the plugin embeds only
// the public key and a salted SHA-256 of the master key.
namespace
{
// One interactive console prompt. Accepts a fallback when the user just
// presses Enter, and loops on an optional validator (with a retry hint).
juce::String promptLine (const juce::String& question,
                         const juce::String& fallback = {},
                         bool (*validator) (const juce::String&) = nullptr,
                         const char* retryHint = nullptr)
{
    for (;;)
    {
        std::cout << question;
        if (fallback.isNotEmpty())
            std::cout << " [Enter = " << fallback << "]";
        std::cout << ": \n> ";

        std::string line;
        if (! std::getline (std::cin, line))
        {
            // stdin closed (piped input exhausted, Ctrl-Z/Ctrl-D): return the
            // fallback instead of spinning the validator loop forever.
            return fallback;
        }

        juce::String in { juce::CharPointer_UTF8 (line.c_str()) };
        in = in.trim();

        if (in.isEmpty() && fallback.isNotEmpty())
            return fallback;

        if (validator == nullptr || validator (in))
            return in;

        std::cout << retryHint << "\n";
    }
}

// A master key pressing Enter at the rotate prompt is allowed through as empty:
// that is the "generate one for me" answer. Anything typed has to be long enough
// to be worth stretching.
bool masterKeyAcceptable (const juce::String& key)
{
    return key.isEmpty() || key.trim().length() >= 8;
}

//==============================================================================
int cmdInit (const juce::String& presetMaster)
{
    if (keysFile().existsAsFile())
    {
        std::cout << "A keypair already exists at:\n  " << keysFile().getFullPathName()
                  << "\nDelete it first if you really want to invalidate all existing serials.\n"
                  << "\nTo change the master key WITHOUT invalidating anything, keeping every\n"
                  << "serial already issued working, run:\n"
                  << "\n  GoaSynthKeygen --rotate-master [newKey]\n";
        return 1;
    }

    std::cout << "Generating RSA-2048 keypair (this can take a few seconds)...\n";

    // The plugin's public-key header lives in the build tree; the core writes it.
    const auto init = createKeypair (
        presetMaster,
        juce::File::getCurrentWorkingDirectory().getChildFile ("Source/LicenseKeys.h"));

    if (! init.ok)
    {
        std::cout << init.error << "\n";
        return 1;
    }

    const juce::File keysHeader (juce::File::getCurrentWorkingDirectory().getChildFile ("Source/LicenseKeys.h"));

    std::cout
        << "\nKeypair written to:  " << keysFile().getFullPathName() << "\n"
        << "Public key embedded: " << keysHeader.getFullPathName() << " (gitignored)\n"
        << "\n====================================================================\n"
        << "  YOUR MASTER KEY (activate any machine, shows only once):\n\n"
        << "      " << init.masterKey << "\n"
        << "\n  Store it in your password manager NOW, then delete this window.\n"
        << "====================================================================\n"
        << "\nNext: rebuild the plugin so it embeds the public key.\n"
        << "Keep keys.txt + issued_serials.txt private!\n";
    return 0;
}

//==============================================================================
// Change the master key without touching the keypair. The distinction matters at
// the moment it is used: --init refuses to run while keys.txt exists precisely
// because its answer would be to invalidate every serial ever issued, and that is
// not the answer to "the master key may have leaked".
int cmdRotateMaster (const juce::String& newMaster)
{
    if (! keysFile().existsAsFile())
    {
        std::cout << "No keypair found. Run:  GoaSynthKeygen --init\n";
        return 1;
    }

    const juce::String trimmed = newMaster.trim();

    std::cout << "Rotating the master key only — the RSA keypair is kept, so every\n"
                 "serial already issued keeps verifying.\n";

    if (! trimmed.isEmpty() && trimmed.length() < 12)
        std::cout << "Note: that is shorter than the 12 characters --init generates. The\n"
                     "      digest ships inside the build, so a short key is worth guessing\n"
                     "      offline — a long random one costs nothing to copy/paste.\n";

    // Same header path as --init: the plugin's public-key header.
    const juce::File header (juce::File::getCurrentWorkingDirectory().getChildFile ("Source/LicenseKeys.h"));
    const auto r = rotateMasterKey (trimmed, header);

    if (! r.ok)
    {
        std::cout << r.error << "\n";
        return 1;
    }

    std::cout
        << "\nDigest " << r.previousDigest.substring (0, 8) << "... -> "
                        << r.digest.substring (0, 8) << "...\n"
        << header.getFullPathName() << " updated (gitignored)\n"
        << "\n====================================================================\n"
        << "  YOUR NEW MASTER KEY (activate any machine, shows only once):\n\n"
        << "      " << r.masterKey << "\n"
        << "\n  Store it in your password manager NOW, then delete this window.\n"
        << "  The old key no longer works in a build made from here on.\n"
        << "====================================================================\n";

    if (r.logged)
        std::cout << "\nLogged in " << masterLogFile().getFileName() << " (digests only, never the key).\n";

    std::cout << "\nNext: REBUILD and republish. A build that has already shipped still\n"
                 "accepts the old key — only a new build carries the new digest.\n"
                 "Every serial already issued stays valid; there is nothing to re-issue.\n";
    return 0;
}

//==============================================================================
int cmdGen (const juce::String& idIn, const juce::String& note)
{
    const auto keys = loadKeys();
    if (! keys.ok)
    {
        std::cout << "No keypair found. Run:  GoaSynthKeygen --init\n";
        return 1;
    }

    const juce::String id = normaliseMachineId (idIn);
    if (! looksLikeMachineId (id))
    {
        std::cout << "That is not a valid 20-char machine id.\n"
                  << "Ask the buyer to read the MACHINE ID shown on the plugin's\n"
                  << "activation screen and send it to you.\n"
                  << "Example: GoaSynthKeygen --gen 1A2B3C4D5E6F70819A2B \"John's studio PC\"\n";
        return 1;
    }

    bool alreadyIssued = false;
    const juce::String serial = recordIssued (keys, id, note, alreadyIssued);

    std::cout << (alreadyIssued ? "Serial was already issued for this machine (ledger entry kept)."
                                : "Serial issued and logged in " + issuedFile().getFullPathName()) << "\n\n"
              << "Serial (send this to the buyer):\n\n  " << prettySerial (serial) << "\n"
              << "\nIt activates ONLY on machine id " << id << ".\n"
              << "If the buyer re-installs Windows or changes hardware, issue a fresh key.\n";
    return 0;
}

//==============================================================================
// --file <machineId> [note] [out.goalicense]: export a buyer-ready .goalicense
// file. The serial is issued exactly as with --gen (same signature, same
// ledger entry) but wrapped as a GOA-LICENSE-1 document the buyer can
// double-click or IMPORT on the plugin's activation screen.
// (The file template is written here, not shared with Source/License.cpp,
// because the keygen must build even before LicenseKeys.h exists.)
int cmdFile (const std::vector<juce::String>& args)
{
    const auto keys = loadKeys();
    if (! keys.ok)
    {
        std::cout << "No keypair found. Run:  GoaSynthKeygen --init\n";
        return 1;
    }

    const juce::String id = normaliseMachineId (args[1]);
    if (! looksLikeMachineId (id))
    {
        std::cout << "That is not a valid 20-char machine id.\n"
                  << "Example: GoaSynthKeygen --file 1A2B3C4D5E6F70819A2B \"John's studio PC\"\n";
        return 1;
    }

    // args[2..] is the note; a final token ending in .goalicense names the
    // output file instead.
    juce::String note;
    for (int i = 2; i < (int) args.size(); ++i)
    {
        if (i > 2) note << " ";
        note << args[(size_t) i];
    }

    juce::File outFile (juce::File::getCurrentWorkingDirectory()
                            .getChildFile ("GoaSynth-" + id + ".goalicense"));

    if (note.trim().endsWithIgnoreCase (".goalicense"))
    {
        const int sp = note.lastIndexOfChar (' ');
        outFile = juce::File::getCurrentWorkingDirectory()
                      .getChildFile (note.substring (sp + 1).trim());
        note = (sp > 0 ? note.substring (0, sp) : juce::String()).trim();
    }

    // Same ledger handling as --gen (one line per distinct serial).
    bool alreadyIssued = false;
    const juce::String serial = recordIssued (keys, id, note, alreadyIssued);

    // GOA-LICENSE-1 layout comes from the shared core (keep in sync with
    // Source/License.cpp). The parser only needs the serial line; everything
    // else is documentation for humans.
    if (! writeLicenseFile (serial, id, note, outFile))
    {
        std::cout << "Could not write " << outFile.getFullPathName() << "\n";
        return 1;
    }

    std::cout << "License file written:\n  " << outFile.getFullPathName() << "\n\n"
              << "Serial inside (also logged in " << issuedFile().getFullPathName() << "):\n  "
              << prettySerial (serial) << "\n\n"
              << "Send this one file to the buyer - they double-click it (or press\n"
              << "IMPORT on the activation screen) and GoaSynth activates.\n"
              << "It activates ONLY on machine id " << id << ".\n";
    return 0;
}

//==============================================================================
// --genfile: same output as --file, but everything is asked interactively -
// no command-line arguments to remember. Reuses cmdFile() for the actual
// issuing so both paths stay identical.
int cmdGenFile()
{
    const auto keys = loadKeys();
    if (! keys.ok)
    {
        std::cout << "No keypair found. Run:  GoaSynthKeygen --init\n";
        return 1;
    }

    std::cout << "\nIssue a buyer-ready .goalicense file\n"
              << "----------------------------------\n"
              << "The buyer reads their 20-char MACHINE ID off the plugin's\n"
              << "activation screen (COPY button) and sends it to you.\n\n";

    const juce::String id = promptLine ("Buyer's MACHINE ID (20 hex chars)", {},
                                        looksLikeMachineId,
                                        "That is not a valid 20-char machine id (hex, no dashes).");

    const juce::String name  = promptLine ("Buyer's name (optional)");
    const juce::String email = promptLine ("Buyer's email (optional)");
    const juce::String note  = promptLine ("Note, e.g. order reference (optional)");

    const juce::String defName = "GoaSynth-" + id + ".goalicense";
    const juce::String outName = promptLine ("Output file", defName);

    std::cout << "\n";

    // Build the .goalicense directly so name/email are written into the file.
    bool alreadyIssued = false;
    const juce::String serial = recordIssued (keys, id, note, alreadyIssued);
    juce::File outFile (juce::File::getCurrentWorkingDirectory().getChildFile (outName));
    if (! writeLicenseFile (serial, id, note, outFile, name, email))
    {
        std::cout << "Could not write " << outFile.getFullPathName() << "\n";
        return 1;
    }

    std::cout << "License file written:\n  " << outFile.getFullPathName() << "\n\n"
              << "Serial inside (also logged in " << issuedFile().getFileName() << "):\n  "
              << prettySerial (serial) << "\n\n"
              << "Send this one file to the buyer - they double-click it (or press\n"
              << "IMPORT on the activation screen) and GoaSynth activates.\n"
              << "It activates ONLY on machine id " << id << ".\n";
    return 0;
}

//==============================================================================
int cmdList()
{
    if (! issuedFile().existsAsFile())
    {
        std::cout << "No serials issued yet.\n";
        return 0;
    }

    int n = 0;
    for (const auto& line : juce::StringArray::fromLines (issuedFile().loadFileAsString()))
    {
        auto t = line.trim();
        if (t.isEmpty()) continue;

        // Line layout: serial<TAB>machineId<TAB>note
        const auto c = juce::StringArray::fromTokens (t, "\t", "");
        const juce::String serial  = c[0];
        const juce::String machine = c.size() > 1 ? c[1] : juce::String ("?");
        juce::String note;
        for (int i = 2; i < c.size(); ++i)
        {
            if (i > 2) note << " ";
            note << c[i];
        }

        std::cout << juce::String (++n).paddedLeft ('0', 3) << "  "
                  << machine << "  "
                  << (note.isEmpty() ? juce::String ("-") : note) << "\n      "
                  << prettySerial (serial) << "\n";
    }

    const int revoked = revokedCount();
    const int moved   = movedCount();

    std::cout << "\n" << n << " active serial(s)";
    if (revoked > 0)
        std::cout << ", " << revoked << " revoked";
    if (moved > 0)
        std::cout << ", " << moved << " moved";
    if (revoked > 0 || moved > 0)
        std::cout << " (" << revokedFile().getFileName() << ")";
    std::cout << ".\n";
    return 0;
}

//==============================================================================
//==============================================================================
// --unregister <serial>: forget a binding so a buyer can move the key to a
// new computer (after a re-install, hardware change or resale). The serial
// itself stays cryptographically valid for its ORIGINAL machine id only — a
// transferred buyer needs a freshly issued serial, but the old one can no
// longer be resurrected to dodge a "used elsewhere" block.
int cmdUnregister (const juce::String& serialIn)
{
    const juce::String serial = serialIn.trim().toUpperCase();

    juce::File f = issuedFile();
    if (! f.existsAsFile())
    {
        std::cout << "No serials issued yet.\n";
        return 1;
    }

    juce::StringArray kept;
    int removedCount = 0;
    int n = 0;

    for (const auto& line : juce::StringArray::fromLines (f.loadFileAsString()))
    {
        const juce::String t = line.trim();
        if (t.isEmpty())
            continue;

        ++n;
        if (juce::StringArray::fromTokens (t, "\t", "")[0] == serial)
        {
            ++removedCount;   // duplicate ledger lines share one serial
            continue;
        }
        kept.add (t);
    }

    if (removedCount == 0)
    {
        std::cout << "Serial not found in the issued list.\n";
        return 1;
    }

    f.replaceWithText (kept.joinIntoString ("\n") + (kept.isEmpty() ? juce::String() : juce::String ("\n")));
    std::cout << "Unregistered " << removedCount << " ledger line(s); " << kept.size()
              << " serial(s) remain issued.\n"
              << "Its machine binding is lifted; issue a fresh serial for the buyer's\n"
              << "new MACHINE ID if they are moving the license.\n";
    return 0;
}

//==============================================================================
// --revoke <serial> [reason]: pull a serial out of the active ledger because the
// order was refunded, and keep the reason on the record. Unlike --unregister
// (a transfer, where erasing the line is the point) this leaves history behind.
int cmdRevoke (const juce::String& serialIn, const juce::String& reason)
{
    const auto r = revokeSerial (serialIn, reason);

    if (! r.ok)
    {
        std::cout << r.error << "\n";
        return 1;
    }

    if (r.alreadyRevoked)
    {
        std::cout << "That serial is already revoked";
        if (r.reason.isNotEmpty())
        {
            std::cout << " (" << r.reason;
            if (r.revokedAt.isNotEmpty())
                std::cout << ", " << r.revokedAt;
            std::cout << ")";
        }
        std::cout << " - nothing changed.\n";
        return 0;
    }

    std::cout << "Revoked " << prettySerial (r.serial) << "\n\n"
              << "  machine id : " << (r.machineId.isEmpty() ? juce::String ("?") : r.machineId) << "\n"
              << "  reason     : " << r.reason << "\n"
              << "  ledger     : " << r.ledgerLinesRemoved << " line(s) removed from "
              << issuedFile().getFileName() << "\n"
              << "  recorded   : " << revokedFile().getFullPathName() << "\n\n"
              << "The buyer's installed copy keeps working: activation is offline, so nothing\n"
              << "can switch a serial off remotely. What this changes is that the serial no\n"
              << "longer counts as a live licence, and that the refund is on the record.\n"
              << "If the same machine buys again - or a corrected export shows the order as\n"
              << "paid - the serial is issued again; the signature is deterministic, so the\n"
              << "buyer gets the identical one back, and GoaSynthFulfil records a restore so\n"
              << "--list stops counting it as revoked. The refund itself stays in this log.\n";
    return 0;
}

//==============================================================================
// --reissue <oldSerial> <newMachineId> [note] [out.goalicense]: the one-command
// machine move. It retires the serial bound to the buyer's old machine (removing
// it from the active ledger and recording the move) and issues a fresh serial
// for the new machine, writing a buyer-ready .goalicense in the same pass. This
// is the two steps of --unregister + --file done together, so the ledger never
// holds two live licences for one buyer and the old line cannot be forgotten.
int cmdReissue (const std::vector<juce::String>& args)
{
    const auto keys = loadKeys();
    if (! keys.ok)
    {
        std::cout << "No keypair found. Run:  GoaSynthKeygen --init\n";
        return 1;
    }

    const juce::String oldSerial = args[1].trim();
    const juce::String newId     = normaliseMachineId (args[2]);

    if (! looksLikeMachineId (newId))
    {
        std::cout << "That is not a valid 20-char machine id.\n"
                  << "Example: GoaSynthKeygen --reissue <oldSerial> 1A2B3C4D5E6F70819A2B \"moved to laptop\"\n";
        return 1;
    }

    // args[3..] is the note; a final token ending in .goalicense names the
    // output file instead (same convention as --file).
    juce::String note;
    for (int i = 3; i < (int) args.size(); ++i)
    {
        if (i > 3) note << " ";
        note << args[(size_t) i];
    }

    juce::File outFile (juce::File::getCurrentWorkingDirectory()
                            .getChildFile ("GoaSynth-" + newId + ".goalicense"));

    if (note.trim().endsWithIgnoreCase (".goalicense"))
    {
        const int sp = note.lastIndexOfChar (' ');
        outFile = juce::File::getCurrentWorkingDirectory()
                      .getChildFile (note.substring (sp + 1).trim());
        note = (sp > 0 ? note.substring (0, sp) : juce::String()).trim();
    }

    const auto r = reissueSerial (keys, oldSerial, newId, note);
    if (! r.ok)
    {
        std::cout << r.error << "\n";
        return 1;
    }

    if (r.sameMachine)
    {
        std::cout << "That serial is already issued for machine id " << r.newMachineId << " -\n"
                  << "nothing to move. The serial is unchanged:\n  " << prettySerial (r.newSerial) << "\n";
        return 0;
    }

    if (! writeLicenseFile (r.newSerial, r.newMachineId, note, outFile))
    {
        std::cout << "Could not write " << outFile.getFullPathName() << "\n";
        return 1;
    }

    std::cout << "Machine move complete - one command, both halves done.\n\n"
              << "  retired   : " << prettySerial (r.oldSerial) << "\n"
              << "              machine " << (r.oldMachineId.isEmpty() ? juce::String ("?") : r.oldMachineId)
              << (r.oldAlreadyGone ? "  (was already retired; the ledger was left alone)\n" : "\n")
              << "  new file  : " << outFile.getFullPathName() << "\n"
              << "  new serial (also logged in " << issuedFile().getFileName() << "):\n              "
              << prettySerial (r.newSerial) << "\n"
              << "              activates ONLY on machine id " << r.newMachineId << "\n\n"
              << "The old serial is out of the active list and the move is on the record in "
              << revokedFile().getFileName() << ",\n"
              << "so a later question - moved or refunded? - has an answer.\n"
              << "Send the new file to the buyer: their old machine keeps working until they\n"
              << "import it, because activation is offline and nothing can switch a copy off\n"
              << "remotely.\n";
    return 0;
}

//==============================================================================
int cmdVerify (const juce::String& serial)
{
    const auto keys = loadKeys();
    if (! keys.ok)
    {
        std::cout << "No keypair found. Run:  GoaSynthKeygen --init\n";
        return 1;
    }

    juce::String id, why;
    if (verifySerial (serial, keys.pub, id, why))
    {
        std::cout << "VALID — signed for machine id " << id << "\n";

        juce::String revId, revReason, revWhen;

        if (movedEntry (serial, revId, revReason, revWhen))
            std::cout << "  MOVED on " << revWhen
                      << (revReason.isEmpty() ? juce::String() : " - " + revReason) << "\n"
                      << "  (retired when the licence moved to a new machine - not a live\n"
                         "   licence, and NOT a refund; the move and the machine it went to\n"
                         "   are in " << revokedFile().getFileName() << ")\n";
        else if (revokedEntry (serial, revId, revReason, revWhen))
            std::cout << "  REVOKED on " << revWhen
                      << (revReason.isEmpty() ? juce::String() : " - " + revReason) << "\n"
                      << "  (the signature is still valid and an activated copy keeps working;\n"
                         "   this serial is simply no longer a live licence)\n";
        else if (wasRevoked (serial, revReason, revWhen))
            std::cout << "  was revoked on " << revWhen
                      << (revReason.isEmpty() ? juce::String() : " - " + revReason)
                      << ", later restored (" << revocationState (serial).at << ")\n"
                      << "  (a live licence again: the refund stays on the record, the sale does not)\n";

        return 0;
    }

    std::cout << "INVALID: " << why << "\n";
    return 1;
}

//==============================================================================
int cmdMasterInfo()
{
    const auto keys = loadKeys();
    if (! keys.ok)
    {
        std::cout << "No keypair found. Run:  GoaSynthKeygen --init\n";
        return 1;
    }

    std::cout << "Master digest embedded in the plugin:\n  " << keys.masterDigest.toUpperCase() << "\n"
              << "(The plaintext master key is never stored — only you keep it.)\n";
    return 0;
}

//==============================================================================
void printUsage()
{
    std::cout
        << "GoaSynth key generator\n"
        << "======================\n"
        << "Double-click (no arguments) opens an interactive menu.\n"
        << "Command line:\n"
        << "  GoaSynthKeygen --init [masterKey]         create keypair + master key (once)\n"
        << "  GoaSynthKeygen --rotate-master [newKey]   replace ONLY the master key: the\n"
        << "                                            keypair is kept, so every serial\n"
        << "                                            already issued stays valid. Use this\n"
        << "                                            when the master key may have leaked\n"
        << "  GoaSynthKeygen --machine-id               print this machine's id\n"
        << "  GoaSynthKeygen --gen <machineId> [note]   issue a serial for a buyer's machine\n"
        << "  GoaSynthKeygen --file <machineId> [note] [out.goalicense]\n"
        << "                                            issue a serial AND write a buyer-ready\n"
        << "                                            .goalicense file the buyer double-clicks\n"
        << "  GoaSynthKeygen --genfile                  issue a .goalicense file, asked\n"
        << "                                            step by step (name, email, no args)\n"
        << "  GoaSynthKeygen --list                     list every issued serial\n"
        << "  GoaSynthKeygen --verify <serial>          check a serial's signature\n"
        << "  GoaSynthKeygen --master-info              show stored master digest\n"
        << "  GoaSynthKeygen --reissue <oldSerial> <newMachineId> [note] [out.goalicense]\n"
        << "                                            move a licence in one step: retire\n"
        << "                                            the old serial and issue + write a\n"
        << "                                            fresh .goalicense for the new machine\n"
        << "  GoaSynthKeygen --unregister <serial>      lift a machine binding (transfers)\n"
        << "  GoaSynthKeygen --revoke <serial> [reason] pull a serial out of the active\n"
        << "                                            ledger (refunds) and record why\n\n"
        << "Master key check: type the master key into the plugin's serial box on\n"
        << "any machine to activate it (use sparingly!). If it ever leaks, rotate it\n"
        << "(--rotate-master) and rebuild: already-shipped builds keep accepting the\n"
        << "old key, so the rebuild is the part that closes it.\n\n"
        << "Flow: buyer sends you their MACHINE ID (shown on the plugin's activation\n"
        << "screen)  ->  you run --gen  ->  you send the serial back.\n"
        << "One serial = one machine. Second machine = blocked by the plugin.\n";
}

//==============================================================================
// Double-click launch (no arguments): an interactive menu instead of a usage
// dump that vanishes the instant the console window closes.
int cmdMenu()
{
    for (;;)
    {
        const bool haveKeys = loadKeys().ok;

        std::cout << "\nGoaSynth key generator\n"
                  << "======================\n"
                  << (haveKeys ? "Keypair: ready\n"
                               : "Keypair: NOT CREATED YET — choose option 1 first\n")
                  << "\n"
                  << "  1) " << (haveKeys ? "Show master key info"
                                         : "Create keypair + master key (once)") << "\n"
                  << "  2) Issue a buyer-ready .goalicense file (guided)\n"
                  << "  3) Issue a bare serial (no file)\n"
                  << "  4) List every serial issued so far\n"
                  << "  5) Verify a serial\n"
                  << "  6) Unregister a serial (free it for a new machine)\n"
                  << "  7) Move a licence to a new machine (re-issue, one step)\n"
                  << "  8) Revoke a serial (refund - keeps a revocation record)\n"
                  << "  9) Show this machine's id\n"
                  << " 10) Rotate the master key (keeps every issued serial valid)\n"
                  << "  0) Exit\n\n";

        const juce::String choice = promptLine ("Choose an option");
        std::cout << "\n";

        if (choice.isEmpty() || choice == "0" || choice.equalsIgnoreCase ("q")
            || choice.equalsIgnoreCase ("exit"))
            return 0;

        if (choice == "1")
        {
            if (haveKeys)
            {
                cmdMasterInfo();
            }
            else
            {
                // The master key is baked into the plugin build — a silently
                // generated random one would NOT match it. Ask, don't guess.
                std::cout << "The master key must MATCH what was embedded in the\n"
                          << "plugin build (the one you ran --init with originally).\n";
                const juce::String mk = promptLine (
                    "Master key (empty = generate a random one instead)");
                cmdInit (mk);
            }
        }
        else if (choice == "2")
        {
            cmdGenFile();
        }
        else if (choice == "3")
        {
            const juce::String id = promptLine ("Buyer's MACHINE ID (20 hex chars)", {},
                                                looksLikeMachineId,
                                                "That is not a valid 20-char machine id (hex, no dashes).");
            const juce::String name  = promptLine ("Buyer's name (optional)");
            const juce::String email = promptLine ("Buyer's email (optional)");
            const juce::String note  = promptLine ("Note, e.g. order reference (optional)");
            cmdGen (id, note + " | " + name + " <" + email + ">");
        }
        else if (choice == "4")
        {
            cmdList();
        }
        else if (choice == "5")
        {
            const juce::String serial = promptLine ("Serial to verify");
            cmdVerify (serial);
        }
        else if (choice == "6")
        {
            const juce::String serial = promptLine ("Serial to unregister");
            cmdUnregister (serial);
        }
        else if (choice == "7")
        {
            const juce::String serial = promptLine ("Serial to retire (the old machine's)");
            const juce::String id = promptLine ("New MACHINE ID (20 hex chars)", {},
                                                looksLikeMachineId,
                                                "That is not a valid 20-char machine id (hex, no dashes).");
            const juce::String note = promptLine ("Note, e.g. order reference (optional)");
            cmdReissue (std::vector<juce::String> { "--reissue", serial, id, note });
        }
        else if (choice == "8")
        {
            const juce::String serial = promptLine ("Serial to revoke");
            const juce::String reason = promptLine ("Reason", "refund");
            cmdRevoke (serial, reason);
        }
        else if (choice == "9")
        {
            std::cout << thisMachineId() << "\n";
        }
        else if (choice == "10")
        {
            if (! haveKeys)
            {
                std::cout << "No keypair yet — create one first (option 1).\n";
            }
            else
            {
                std::cout << "Rotating keeps the RSA keypair, so every serial already issued\n"
                             "stays valid. The OLD key stops working in the next build you make;\n"
                             "builds already shipped keep accepting it until they are rebuilt.\n\n";

                const juce::String mk = promptLine (
                    "New master key (empty = generate a random one)", {},
                    masterKeyAcceptable,
                    "Needs at least 8 characters, or press Enter for a generated one.");

                if (! promptLine ("Type ROTATE to confirm").equalsIgnoreCase ("rotate"))
                {
                    std::cout << "Cancelled — nothing changed.\n";
                }
                else
                {
                    cmdRotateMaster (mk);
                }
            }
        }
        else
        {
            std::cout << "Unknown option: " << choice << "\n";
        }

        // Keep the result on screen until the user has read it.
        std::cout << "\nPress Enter for the menu...";
        std::string pauseLine;
        std::getline (std::cin, pauseLine);
    }
}
} // namespace

namespace keygen
{
int run (int argc, char* argv[])
{
    std::vector<juce::String> args;
    for (int i = 1; i < argc; ++i)
        args.push_back (argv[i]);

    if (args.empty())
        return cmdMenu();   // double-click launch: interactive menu

    const juce::String cmd = args[0];

    if (cmd == "--init")       return cmdInit (args.size() > 1 ? args[1] : juce::String());
    if (cmd == "--rotate-master") return cmdRotateMaster (args.size() > 1 ? args[1] : juce::String());
    if (cmd == "--machine-id") { std::cout << thisMachineId() << "\n"; return 0; }
    if (cmd == "--gen")
    {
        if (args.size() < 2) { printUsage(); return 1; }
        return cmdGen (args[1], args.size() > 2 ? args[2] : juce::String());
    }
    if (cmd == "--file")
    {
        if (args.size() < 2) { printUsage(); return 1; }
        return cmdFile (args);
    }
    if (cmd == "--reissue")
    {
        if (args.size() < 3) { printUsage(); return 1; }
        return cmdReissue (args);
    }
    if (cmd == "--genfile")    return cmdGenFile();
    if (cmd == "--list")       return cmdList();
    if (cmd == "--verify")     return args.size() > 1 ? cmdVerify (args[1]) : (printUsage(), 1);
    if (cmd == "--master-info") return cmdMasterInfo();
    if (cmd == "--unregister")  return args.size() > 1 ? cmdUnregister (args[1]) : (printUsage(), 1);
    if (cmd == "--revoke")
    {
        if (args.size() < 2) { printUsage(); return 1; }

        juce::String reason;
        for (int i = 2; i < (int) args.size(); ++i)
        {
            if (i > 2) reason << " ";
            reason << args[(size_t) i];
        }
        return cmdRevoke (args[1], reason);
    }

    printUsage();
    return cmd == "--help" ? 0 : 1;
}
}

//==============================================================================
int main (int argc, char* argv[])
{
    return keygen::run (argc, argv);
}
