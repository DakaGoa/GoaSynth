// Test for the seller-side fulfilment tool: point it at a throwaway keypair
// (GOASYNTH_KEYGEN_DIR), feed it order exports in the shapes a store produces,
// and check what it signs, writes and refuses to sign.
//
// The real keypair under %APPDATA%/GoaSynth/Keygen is never read or written:
// the first two checks below fail loudly if the override does not take effect.
#include <juce_core/juce_core.h>

#include <cstdio>
#include <cstdlib>
#include <deque>
#include <vector>

#include "../Fulfil/Fulfil.h"
#include "../Keygen/KeygenCore.h"

static int fails = 0;

// juce_core has no portable setter for the process environment. On Windows
// _putenv stores the pointer it is given rather than copying the string, so the
// text has to outlive the call: a std::deque keeps every element's address
// stable as more are added.
static void putEnv (const char* name, const juce::String& value)
{
    static std::deque<juce::String> storage;
    storage.push_back (juce::String (name) + "=" + value);

   #if JUCE_WINDOWS
    _putenv (const_cast<char*> (storage.back().toRawUTF8()));
   #else
    setenv (name, value.toRawUTF8(), 1);
   #endif
}

static void expect (bool ok, const juce::String& what)
{
    if (! ok)
    {
        std::printf ("FULFIL: %s\n", what.toRawUTF8());
        ++fails;
    }
}

static int countLines (const juce::File& f, const juce::String& mustContain = {})
{
    int n = 0;
    if (! f.existsAsFile())
        return 0;

    for (const auto& line : juce::StringArray::fromLines (f.loadFileAsString()))
        if (line.trim().isNotEmpty() && (mustContain.isEmpty() || line.containsIgnoreCase (mustContain)))
            ++n;

    return n;
}

// The first base64 block in an .eml is the body; decode it so the test can look
// for the serial the way the buyer would read it.
static juce::String decodeFirstBase64Block (const juce::String& eml)
{
    const juce::String marker ("Content-Transfer-Encoding: base64\r\n\r\n");
    const int start = eml.indexOf (marker);
    if (start < 0)
        return {};

    const int bodyStart = start + marker.length();
    const int end = eml.indexOf (bodyStart, "\r\n--");
    if (end < 0)
        return {};

    juce::MemoryOutputStream decoded;
    if (! juce::Base64::convertFromBase64 (decoded, eml.substring (bodyStart, end).removeCharacters ("\r\n")))
        return {};

    return juce::String::fromUTF8 ((const char*) decoded.getData(), (int) decoded.getDataSize());
}

// The seller tools are sibling artefacts of this test in the build tree, but the
// depth differs by generator: a multi-config build puts the test in
// build/<Config>/ while the tools live in build/<Target>_artefacts/<Config>/, and
// a single-config build is one level shallower. So walk up from this executable
// until the artefact is actually found - a hardcoded parent count silently found
// nothing and skipped every CLI check.
static juce::File findSiblingArtefact (const juce::String& relativePath)
{
    juce::File dir = juce::File::getSpecialLocation (juce::File::currentExecutableFile).getParentDirectory();

    for (int i = 0; i < 6 && dir.isDirectory(); ++i)
    {
        const juce::File candidate = dir.getChildFile (relativePath);
        if (candidate.existsAsFile())
            return candidate;

        const juce::File parent = dir.getParentDirectory();
        if (parent == dir)
            break;
        dir = parent;
    }

    return {};
}

// Drive the real keygen with no arguments: feed a scripted keystroke sequence
// through stdin and return the console output. A plain ChildProcess argument
// vector handles paths with spaces, unlike a hand-built shell command, and the
// child inherits GOASYNTH_KEYGEN_DIR. It runs in its own folder so a guided
// licence and --init / --rotate-master's Source/LicenseKeys.h cannot land in
// the build tree.
static juce::String runScriptedMenu (const juce::File& tool, const juce::StringArray& script,
                                     const juce::File& runDir, const juce::File& scriptFile,
                                     const juce::File& outFile, int& exitCode)
{
    exitCode = -1;

    if (! runDir.isDirectory())
        runDir.createDirectory();

    scriptFile.replaceWithText (script.joinIntoString ("\n") + "\n");

    juce::ChildProcess p;

   #if JUCE_WINDOWS
    const juce::File wrapper = runDir.getChildFile ("run-menu.bat");
    wrapper.replaceWithText (juce::String ("@echo off\r\n")
        + "cd /d \"" + runDir.getFullPathName() + "\"\r\n"
        + "\"" + tool.getFullPathName() + "\" < \"" + scriptFile.getFullPathName()
              + "\" > \"" + outFile.getFullPathName() + "\" 2>&1\r\n"
        + "exit /b %errorlevel%\r\n");

    if (p.start ({ "cmd.exe", "/c", wrapper.getFullPathName() }))
    {
        p.waitForProcessToFinish (-1);
        exitCode = p.getExitCode();
    }
   #else
    const juce::File wrapper = runDir.getChildFile ("run-menu.sh");
    wrapper.replaceWithText ("#!/bin/sh\ncd \"" + runDir.getFullPathName() + "\"\n\""
                             + tool.getFullPathName() + "\" < \"" + scriptFile.getFullPathName()
                             + "\" > \"" + outFile.getFullPathName() + "\" 2>&1\n");

    if (p.start ({ "/bin/sh", wrapper.getFullPathName() }))
    {
        p.waitForProcessToFinish (-1);
        exitCode = p.getExitCode();
    }
   #endif

    return outFile.existsAsFile() ? outFile.loadFileAsString() : juce::String();
}

int main()
{
    using namespace keygen::core;

    // ---- sandbox ----------------------------------------------------------
    const juce::File root = juce::File::getSpecialLocation (juce::File::tempDirectory)
                                .getChildFile ("goasynth_fulfil_" + juce::String (juce::Time::currentTimeMillis()));
    const juce::File keyDir  = root.getChildFile ("keygen");
    const juce::File inbox   = root.getChildFile ("inbox");
    const juce::File outbox  = root.getChildFile ("fulfilled");

    keyDir.createDirectory();
    inbox.createDirectory();

    putEnv ("GOASYNTH_KEYGEN_DIR", keyDir.getFullPathName());

    const juce::File realKeyDir = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                                      .getChildFile ("GoaSynth").getChildFile ("Keygen");
    expect (keygenDir() == keyDir, "GOASYNTH_KEYGEN_DIR is not honoured - refusing to touch the real key store");
    expect (keygenDir() != realKeyDir, "keygen dir resolved to the real seller key folder");
    expect (keyDir.isAChildOf (juce::File::getSpecialLocation (juce::File::tempDirectory)),
            "sandbox keypair is not in the temp folder");

    const auto init = createKeypair ("TestMaster!23", {});
    expect (init.ok, "createKeypair: " + init.error);

    const auto keys = loadKeys();
    expect (keys.ok, "loadKeys found no keypair in the sandbox");

    // ---- fixtures ---------------------------------------------------------
    // 1001 issued, 1002 issued (dashed id normalised), 1003 no machine id,
    // 1004 refunded, 1005 another product.
    inbox.getChildFile ("orders.csv").replaceWithText (
        "Order ID,Email,Customer Name,Product,Machine ID,Status\n"
        "1001,jane@example.com,Jane Doe,\"GoaSynth - Goa Trance Synthesizer (VST3)\",1A2B3C4D5E6F70819A2B,paid\n"
        "1002,bob@example.com,Bob,\"GoaSynth (VST3), download\",9F8E-7D6C-5B4A-3928-1706,paid\n"
        "1003,sam@example.com,Sam,GoaSynth,,paid\n"
        "1004,eva@example.com,Eva,GoaSynth,11111111111111111111,refunded\n"
        "1005,kim@example.com,Kim,\"Some Other Plugin\",22222222222222222222,paid\n");

    // No machine-id column at all: the id is inside free text and must still be
    // found, but only because it contains letters (a 20-digit order number does
    // not qualify - see orders3.csv).
    inbox.getChildFile ("orders2.csv").replaceWithText (
        "Order ID,Email,Product,Notes\n"
        "1006,leo@example.com,GoaSynth,\"buyer sent id A1B2C3D4E5F60718293A on the phone\"\n");

    inbox.getChildFile ("orders3.csv").replaceWithText (
        "Order ID,Email,Product,Notes\n"
        "1007,mia@example.com,GoaSynth,\"reference 12345678901234567890 has no machine id\"\n");

    inbox.getChildFile ("orders4.json").replaceWithText (
        "{ \"orders\": [ { \"order_id\": \"2001\", \"email\": \"zoe@example.com\", \"name\": \"Zoe\",\n"
        "  \"product_name\": \"GoaSynth (VST3)\", \"custom_data\": { \"Machine ID\": \"AAAABBBBCCCCDDDDEEEE\" },\n"
        "  \"status\": \"paid\" } ] }\n");

    fulfil::Options o;
    o.inbox = inbox;
    o.outbox = outbox;
    o.fromAddress = "GoaSynth orders <orders@example.com>";
    o.repoUrl = "https://example.com/goasynth";

    // ---- first pass -------------------------------------------------------
    const auto s1 = fulfil::runOnce (o);

    expect (s1.error.isEmpty(), "first pass error: " + s1.error);
    expect (s1.filesScanned == 4, "files scanned = " + juce::String (s1.filesScanned) + ", expected 4");
    expect (s1.ordersSeen == 8, "orders seen = " + juce::String (s1.ordersSeen) + ", expected 8 (5 + 1 + 1 + 1)");
    expect (s1.issued == 4, "issued = " + juce::String (s1.issued) + ", expected 4 (1001, 1002, 1006, 2001)");
    expect (s1.skipped == 2, "skipped = " + juce::String (s1.skipped) + ", expected 2 (refund + other product)");
    expect (s1.needAttention == 2, "attention = " + juce::String (s1.needAttention) + ", expected 2 (1003, 1007)");

    const juce::File licenseDir = outbox.getChildFile ("licenses");
    const juce::File mailDir    = outbox.getChildFile ("mail");
    const juce::File manifest   = outbox.getChildFile ("manifest.tsv");
    const juce::File attention  = outbox.getChildFile ("needs-attention.tsv");
    const juce::File ledger     = keyDir.getChildFile ("issued_serials.txt");
    const juce::File revokedLog  = keyDir.getChildFile ("revoked_serials.txt");
    const juce::File activityLog = outbox.getChildFile ("activity.log");

    expect (countLines (ledger) == 4, "ledger lines = " + juce::String (countLines (ledger)) + ", expected 4");
    expect (countLines (manifest, "\tissued\t") == 4,
            "manifest issued rows = " + juce::String (countLines (manifest, "\tissued\t")) + ", expected 4");
    expect (countLines (attention) == 3, "attention report should hold a header + 2 orders, got "
                                          + juce::String (countLines (attention)));

    const juce::String attentionText = attention.loadFileAsString();
    expect (attentionText.contains ("1003") && attentionText.contains ("sam@example.com"),
            "attention report does not list the order with no machine id");
    expect (attentionText.contains ("1007"), "attention report does not list the 20-digit false candidate");

    // Every issued order: a .goalicense that verifies against the test keypair,
    // a draft email addressed to the buyer, and the serial inside both.
    const juce::StringArray expectedIds { "1A2B3C4D5E6F70819A2B", "9F8E7D6C5B4A39281706",
                                          "A1B2C3D4E5F60718293A", "AAAABBBBCCCCDDDDEEEE" };

    for (const auto& id : expectedIds)
    {
        const juce::File lic = licenseDir.getChildFile ("GoaSynth-" + id + ".goalicense");
        expect (lic.existsAsFile(), "missing licence file for " + id);

        const juce::String licText = lic.loadFileAsString();
        expect (licText.startsWith ("GOA-LICENSE-1"), "licence file for " + id + " lost its magic line");
        expect (licText.contains ("machine: " + id), "licence file for " + id + " names the wrong machine");

        // The serial from the file must verify against the public half, and be
        // bound to exactly this machine id.
        const int serialLine = licText.indexOf ("serial:  ");
        const juce::String serial = licText.substring (serialLine + 9).upToFirstOccurrenceOf ("\n", false, false).trim();

        juce::String verifiedId, why;
        expect (verifySerial (keys, serial, verifiedId, why), "serial for " + id + " does not verify: " + why);
        expect (verifiedId == id, "serial for " + id + " verifies against " + verifiedId);

        // ...and the deterministic signature must match the keygen's ledger.
        expect (ledger.loadFileAsString().contains (serial), "ledger has no entry for the serial of " + id);

        // Regression: the pretty-printed form Fulfil pastes into buyer emails
        // used to carry 127 stray dashes inside the signature (grouped 4-char
        // chunks), which the plugin's activate() choked on — a buyer pasting
        // the email's serial verbatim was refused. The pretty form must be
        // canonical now, and copy/paste artifacts (stray dashes) must verify.
        const juce::String pretty = prettySerial (serial);
        expect (pretty == serial, "prettySerial no longer canonical for " + id);
        juce::String wrapId, wrapWhy;
        expect (verifySerial (keys, pretty.substring (0, 100) + "-" + pretty.substring (100),
                              wrapId, wrapWhy),
                "dash-mangled pretty serial does not verify: " + wrapWhy);
        expect (wrapId == id, "dash-mangled serial verifies against the wrong machine");
    }

    expect (! licenseDir.getChildFile ("GoaSynth-11111111111111111111.goalicense").existsAsFile(),
            "a refunded order must not get a licence file");
    expect (! licenseDir.getChildFile ("GoaSynth-22222222222222222222.goalicense").existsAsFile(),
            "another product must not get a licence file");

    juce::Array<juce::File> drafts;
    mailDir.findChildFiles (drafts, juce::File::findFiles, false, "*.eml");
    expect (drafts.size() == 4, "email drafts = " + juce::String (drafts.size()) + ", expected 4");
    expect (mailDir.getChildFile ("1001-jane@example.com.eml").existsAsFile(),
            "draft file names should keep the buyer's address readable");

    for (const auto& draft : drafts)
    {
        const juce::String eml = draft.loadFileAsString();
        expect (eml.startsWith ("From: GoaSynth orders <orders@example.com>"), "draft " + draft.getFileName() + " has no From header");
        expect (eml.contains ("MIME-Version: 1.0") && eml.contains ("multipart/mixed"), "draft " + draft.getFileName() + " is not multipart");
        expect (eml.contains ("Content-Disposition: attachment; filename=\"GoaSynth-"), "draft " + draft.getFileName() + " has no attachment");
        expect (eml.contains ("Subject: Your GoaSynth licence"), "draft " + draft.getFileName() + " has no subject");

        const juce::String body = decodeFirstBase64Block (eml);
        expect (body.contains ("GOA1-"), "draft " + draft.getFileName() + " body has no serial for the buyer");
        expect (body.contains ("IMPORT"), "draft " + draft.getFileName() + " does not explain activation");
        expect (body.contains ("example.com/goasynth"), "draft " + draft.getFileName() + " is missing the AGPL source link");
        expect (! body.contains ("{"), "draft " + draft.getFileName() + " has an unfilled placeholder");
    }

    // ---- second pass: nothing is issued twice ------------------------------
    const auto s2 = fulfil::runOnce (o);

    expect (s2.issued == 0, "second pass issued " + juce::String (s2.issued) + " more serials");
    expect (s2.alreadyFulfilled == 4, "second pass recognised " + juce::String (s2.alreadyFulfilled) + " of 4 fulfilled orders");
    expect (countLines (ledger) == 4, "second pass changed the ledger (" + juce::String (countLines (ledger)) + " lines)");
    expect (countLines (manifest, "\tissued\t") == 4, "second pass duplicated manifest rows");

    // A deleted licence file is re-created from the manifest, not re-signed.
    const juce::File reissued = licenseDir.getChildFile ("GoaSynth-1A2B3C4D5E6F70819A2B.goalicense");
    expect (reissued.deleteFile(), "could not delete the licence file for the re-write check");
    const auto s3 = fulfil::runOnce (o);
    expect (reissued.existsAsFile(), "a missing licence file was not re-written");
    expect (s3.issued == 0, "re-writing a lost file issued a new serial");
    expect (countLines (ledger) == 4, "re-writing a lost file changed the ledger");

    // ---- refund after issue: the serial is revoked, not just reported -------
    const juce::String refundedMachine = "1A2B3C4D5E6F70819A2B";
    const juce::String refundedSerial = makeSerial (keys, refundedMachine);

    expect (ledger.loadFileAsString().contains (refundedSerial), "the refund test needs the serial in the ledger first");

    inbox.getChildFile ("orders.csv").replaceWithText (
        inbox.getChildFile ("orders.csv").loadFileAsString()
            .replace ("1A2B3C4D5E6F70819A2B,paid", "1A2B3C4D5E6F70819A2B,refunded"));

    const auto s4 = fulfil::runOnce (o);

    expect (s4.issued == 0, "a refunded order was issued again");
    expect (s4.revoked == 1, "revoked = " + juce::String (s4.revoked) + ", expected 1");
    expect (! ledger.loadFileAsString().contains (refundedSerial),
            "the revoked serial is still in the active ledger");
    expect (revokedLog.existsAsFile() && revokedLog.loadFileAsString().contains (refundedSerial),
            "the revocation was not recorded in " + revokedLog.getFileName());
    expect (revokedLog.loadFileAsString().contains ("1001"), "the revocation record does not name the refunded order");
    expect (countLines (ledger) == 3, "ledger lines = " + juce::String (countLines (ledger)) + " after the refund, expected 3");

    // The manifest's newest row for the order is now its state, and the pass
    // says so out loud.
    expect (manifest.loadFileAsString().contains ("1001\trevoked"), "manifest has no revoked row for order 1001");
    bool revokedNote = false;
    for (const auto& note : s4.notes)
        if (note.contains ("revoked serial for 1001"))
            revokedNote = true;
    expect (revokedNote, "the pass did not report the revocation");
    expect (activityLog.loadFileAsString().contains ("REVOKED 1001"), "the revocation is not in the activity log");

    // Revocation is bookkeeping, not enforcement: the signature is still valid
    // and an activated copy keeps working. The test asserts the honest part.
    {
        juce::String id, why;
        expect (verifySerial (keys, refundedSerial, id, why) && id == refundedMachine,
                "a revoked serial should still carry a valid signature: " + why);
        expect (isRevoked (refundedSerial), "isRevoked() does not report the revoked serial");
    }

    // ---- refunded and never issued ------------------------------------------
    expect (manifest.loadFileAsString().contains ("1004\trefunded"),
            "a refund with no serial to revoke should still be recorded");
    expect (! revokedLog.loadFileAsString().contains ("11111111111111111111"),
            "revoked log mentions a machine that was never issued");

    // ---- running again changes nothing --------------------------------------
    const auto s4b = fulfil::runOnce (o);
    expect (s4b.issued == 0 && s4b.revoked == 0, "a second pass over the same refund changed state");
    expect (countLines (revokedLog, refundedSerial) == 1,
            "the revocation was recorded " + juce::String (countLines (revokedLog, refundedSerial)) + " times");
    expect (countLines (ledger) == 3, "a second pass changed the ledger");

    // ---- buying again after the refund --------------------------------------
    // The signature is deterministic, so the same machine gets the identical
    // serial back - a restoration, not a new licence.
    inbox.getChildFile ("reorders.csv").replaceWithText (
        "Order ID,Email,Product,Machine ID,Status\n"
        "1008,jane@example.com,GoaSynth," + refundedMachine + ",paid\n");

    const auto s7 = fulfil::runOnce (o);
    expect (s7.issued == 1, "the re-purchase issued " + juce::String (s7.issued) + " serials, expected 1");
    expect (ledger.loadFileAsString().contains (refundedSerial),
            "re-buying did not put the serial back in the active ledger");
    expect (manifest.loadFileAsString().contains ("1008\tissued"), "manifest has no issued row for the re-purchase");

    bool restoredNote = false;
    for (const auto& note : s7.notes)
        if (note.contains ("re-issues a serial revoked"))
            restoredNote = true;
    expect (restoredNote, "the pass did not mention restoring a revoked serial");

    // ...and the keygen's own view agrees: the serial is a live licence again,
    // while the refund that pulled it stays on the record for the support trail.
    {
        juce::String revReason, revWhen;
        expect (! isRevoked (refundedSerial),
                "a re-purchased serial is still reported as revoked");
        expect (revokedCount() == 0, "revoked count = " + juce::String (revokedCount()) + ", expected 0 after a restore");
        expect (wasRevoked (refundedSerial, revReason, revWhen),
                "the refund disappeared from the history when the serial was restored");
        expect (revReason.contains ("1001"), "the refund reason was lost: " + revReason);
        expect (revokedLog.loadFileAsString().contains ("restored"),
                "the restore was not recorded in " + revokedLog.getFileName());
        expect (countLines (revokedLog, refundedSerial) == 2,
                "the revocation log should hold the refund and the restore, not "
                  + juce::String (countLines (revokedLog, refundedSerial)) + " line(s)");

        // Idempotent: restoring twice must not append a second event.
        expect (restoreSerial (refundedSerial, "again").alreadyRestored,
                "restoring an already-live serial was not reported as a no-op");
        expect (countLines (revokedLog, refundedSerial) == 2, "a redundant restore appended an event");
    }

    // ---- dry run -----------------------------------------------------------
    // A fresh outbox, so every paid GoaSynth order is issuable again (including
    // the re-purchase); the refunded order is not.
    fulfil::Options dry = o;
    dry.dryRun = true;
    dry.outbox = root.getChildFile ("dry");
    const auto s5 = fulfil::runOnce (dry);
    expect (s5.issued == 4, "dry run counted " + juce::String (s5.issued) + " issuable orders, expected 4");
    expect (s5.revoked == 0, "dry run revoked something it had never issued");
    expect (! dry.outbox.getChildFile ("licenses").isDirectory(), "dry run created output folders");
    expect (countLines (ledger) == 4, "dry run changed the ledger");

    // ---- missing keypair is a clear refusal ---------------------------------
    putEnv ("GOASYNTH_KEYGEN_DIR", root.getChildFile ("empty").getFullPathName());
    const auto s6 = fulfil::runOnce (o);
    expect (s6.error.contains ("keys.txt"), "missing keypair did not produce a pointing error: " + s6.error);
    putEnv ("GOASYNTH_KEYGEN_DIR", keyDir.getFullPathName());

    // ---- a refund and a re-purchase in the same export ----------------------
    // The buyer refunded order 3001 but bought again as 3002, and the store
    // export lists both. Revoking 3001's serial here would strip the licence
    // 3002 is about to hold - and since 3002 is "already fulfilled", nothing
    // would put it back. The serial must stay live, and the refund must not
    // become a revoke/re-issue event pair on every pass.
    {
        const juce::File lateInbox  = root.getChildFile ("late-inbox");
        const juce::File lateOutbox = root.getChildFile ("late-fulfilled");
        lateInbox.createDirectory();

        fulfil::Options late = o;
        late.inbox  = lateInbox;
        late.outbox = lateOutbox;

        const juce::String lateMachine = "AABBCCDDEEFF00112233";

        lateInbox.getChildFile ("first.csv").replaceWithText (
            "Order ID,Email,Product,Machine ID,Status\n"
            "3001,ivy@example.com,GoaSynth," + lateMachine + ",paid\n");

        const auto beforeRefund = fulfil::runOnce (late);
        expect (beforeRefund.issued == 1, "the late-refund scenario did not issue its first order");

        lateInbox.getChildFile ("first.csv").deleteFile();
        lateInbox.getChildFile ("late.csv").replaceWithText (
            "Order ID,Email,Product,Machine ID,Status\n"
            "3001,ivy@example.com,GoaSynth," + lateMachine + ",refunded\n"
            "3002,ivy@example.com,GoaSynth," + lateMachine + ",paid\n");

        const auto combined = fulfil::runOnce (late);

        expect (combined.revoked == 0, "a refund revoked a serial the re-purchase holds");
        expect (combined.issued == 1, "the re-purchase did not receive the serial");
        expect (ledger.loadFileAsString().contains (makeSerial (keys, lateMachine)),
                "the serial left the active ledger when the refund was recorded");
        expect (countLines (revokedLog, lateMachine) == 0,
                "a revocation was recorded for a serial that stayed live");
        expect (lateOutbox.getChildFile ("manifest.tsv").loadFileAsString().contains ("3001\trefunded"),
                "the refund is not on order 3001's record");

        // ...and it stays put: no revoke/restore churn on later passes.
        const auto stable = fulfil::runOnce (late);
        expect (stable.issued == 0 && stable.revoked == 0, "the combined export was not idempotent");
        expect (countLines (revokedLog, lateMachine) == 0, "events appeared on a later pass");
        expect (ledger.loadFileAsString().contains (makeSerial (keys, lateMachine)),
                "a later pass dropped the serial from the ledger");
    }

    // ---- machine move: one command retires the old serial, signs the new -----
    // A buyer changes machine, so the serial signed for their old id cannot
    // activate the new one. This used to be --unregister then --file - two
    // commands that could leave two live ledger lines for one buyer, or drop the
    // old line and forget to issue the new one. reissueSerial does both halves
    // as one operation and records the move, so support can tell a move apart
    // from a refund later.
    {
        const juce::String movedMachine = "A1B2C3D4E5F60718293A";   // order 1006, no revocation history
        const juce::String newMachine   = "BBBBCCCCDDDDEEEEFFFF";
        const juce::String oldSerial    = makeSerial (keys, movedMachine);

        expect (ledger.loadFileAsString().contains (oldSerial),
                "the move test needs the old serial live in the ledger first");

        const auto move = reissueSerial (keys, oldSerial, newMachine, "order 1006 machine move");

        expect (move.ok, "reissueSerial: " + move.error);
        expect (! move.sameMachine, "a different machine was reported as the same machine");
        expect (! move.oldAlreadyGone, "a live serial was reported as already retired");
        expect (move.ledgerLinesRemoved == 1,
                "reissue removed " + juce::String (move.ledgerLinesRemoved) + " ledger lines, expected 1");
        expect (move.oldMachineId == movedMachine, "the move lost the old machine id");
        expect (move.newMachineId == newMachine, "the move normalised the new machine id wrong");

        // The old serial is out of the active list and on the record, with the
        // reason it left - that is what makes a move auditable. It is recorded
        // as a MOVE, not a refund: the log tells the two apart.
        expect (! ledger.loadFileAsString().contains (oldSerial),
                "the old serial is still in the active ledger after the move");
        expect (isMoved (oldSerial), "the move was not recorded as a move");
        expect (! isRevoked (oldSerial), "a machine move is being reported as a refund-style revocation");
        expect (revocationState (oldSerial).event == "moved",
                "the log event for a move is \"" + revocationState (oldSerial).event + "\", expected \"moved\"");
        expect (revocationState (oldSerial).reason.contains ("machine move"),
                "the move record lost its reason: " + revocationState (oldSerial).reason);
        expect (revokedLog.loadFileAsString().contains (movedMachine),
                "the move record does not name the machine that moved");

        // ...and the counts separate the two states: moved, not revoked.
        expect (movedCount() == 1, "movedCount = " + juce::String (movedCount()) + ", expected 1");
        expect (revokedCount() == 0, "a machine move was counted as a revocation");

        // ...and the new machine got a serial that verifies for exactly it.
        expect (move.newSerial == makeSerial (keys, newMachine),
                "a machine move must issue the deterministic serial for the new machine");
        expect (ledger.loadFileAsString().contains (move.newSerial),
                "the new serial is not in the active ledger");
        {
            juce::String seenId, why;
            expect (verifySerial (keys, move.newSerial, seenId, why), "the new serial does not verify: " + why);
            expect (seenId == newMachine, "the new serial verifies against the wrong machine");
        }

        // Repeating the move is a no-op: nothing is retired twice, no event is
        // appended, and the same serial comes back (the signature is deterministic).
        const auto again = reissueSerial (keys, oldSerial, newMachine, "order 1006 machine move");
        expect (again.ok, "repeating the move failed: " + again.error);
        expect (again.oldAlreadyGone, "repeating the move re-retired the serial");
        expect (again.newAlreadyIssued, "repeating the move did not recognise the issued serial");
        expect (again.newSerial == move.newSerial, "repeating the move issued a different serial");
        expect (countLines (revokedLog, oldSerial) == 1,
                "repeating the move appended another event ("
                  + juce::String (countLines (revokedLog, oldSerial)) + " lines)");

        // Moving to the machine the serial is already for is not a move: the
        // live serial must survive.
        const auto sameness = reissueSerial (keys, move.newSerial, newMachine, "same again");
        expect (sameness.ok && sameness.sameMachine, "re-issuing for the same machine was not detected");
        expect (! isRevoked (move.newSerial), "the same-machine path retired the live serial");
        expect (ledger.loadFileAsString().contains (move.newSerial),
                "the same-machine path dropped the live serial");

        // Refusals: an unknown old serial is a typo, not a reason to hand out a
        // second licence, and a malformed new id cannot be signed for.
        expect (! reissueSerial (keys, "GOA1-DEADBEEFDEADBEEFDEADBEEFDEADBEEFDEADBEEFDEADBEEFDEADBEEFDEADBEEFDEADBEEFDEADBEEFDEADBEEFDEADBEEFDEADBEEFDEADBEEFDEADBEEFDEADBEEFDEADBEEFDEADBEEFDEAD",
                                 "1234567890ABCDEF1234").ok,
                "re-issuing from a serial that was never issued was accepted");
        expect (! reissueSerial (keys, oldSerial, "not-a-machine-id").ok,
                "re-issuing for a malformed machine id was accepted");

        // The CLI is the same code path; drive it when it has been built. It
        // writes the licence to the path we give it, so the test tree stays clean.
        const juce::File tool = findSiblingArtefact ("GoaSynthKeygen_artefacts/Release/GoaSynthKeygen.exe");

        if (tool.existsAsFile())
        {
            juce::ChildProcess help;
            if (help.start ({ tool.getFullPathName(), "--help" }, juce::ChildProcess::wantStdOut))
            {
                const juce::String out = help.readAllProcessOutput();
                expect (out.contains ("--reissue"), "keygen --help does not list --reissue");
            }

            // --verify must call a moved serial MOVED, never REVOKED...
            juce::ChildProcess verify;
            if (verify.start ({ tool.getFullPathName(), "--verify", oldSerial }, juce::ChildProcess::wantStdOut))
            {
                const juce::String out = verify.readAllProcessOutput();
                expect (out.contains ("MOVED"), "keygen --verify does not report a moved serial as MOVED: "
                                                  + out.substring (0, 140));
                expect (! out.contains ("REVOKED"), "keygen --verify reports a moved serial as REVOKED");
            }

            // ...and --list must mention the moved count.
            juce::ChildProcess list;
            if (list.start ({ tool.getFullPathName(), "--list" }, juce::ChildProcess::wantStdOut))
            {
                const juce::String out = list.readAllProcessOutput();
                expect (out.contains ("moved"), "keygen --list does not report moved serials: "
                                                  + out.substring (out.length() - 120));
            }

            const juce::String cliMachine = "CCCCDDDDEEEEFFFF0000";
            const juce::File cliFile = root.getChildFile ("cli-move.goalicense");

            juce::ChildProcess mv;
            if (mv.start ({ tool.getFullPathName(), "--reissue", move.newSerial, cliMachine,
                            cliFile.getFullPathName() },
                          juce::ChildProcess::wantStdOut))
            {
                const juce::String out = mv.readAllProcessOutput();
                expect (mv.getExitCode() == 0, "keygen --reissue exit code " + juce::String (mv.getExitCode()));
                expect (out.contains ("Machine move complete"), "--reissue said nothing useful: " + out.substring (0, 160));
                expect (cliFile.existsAsFile() && cliFile.loadFileAsString().contains ("machine: " + cliMachine),
                        "--reissue did not write a licence for the new machine");
                expect (! ledger.loadFileAsString().contains (move.newSerial),
                        "--reissue left the previous serial in the active ledger");
                expect (ledger.loadFileAsString().contains (makeSerial (keys, cliMachine)),
                        "--reissue did not log the new serial");
            }
        }
    }

    // ---- master-key rotation ------------------------------------------------
    // The command exists for one moment: the master key may have leaked, and the
    // answer cannot be "delete keys.txt and run --init" — that invalidates every
    // serial in the wild. What has to survive a rotation is exactly that: the
    // keypair, and every serial signed with it.
    {
        const juce::String idBefore     = "0123456789ABCDEF0123";
        const juce::String serialBefore = makeSerial (keys, idBefore);
        const juce::String ledgerBefore = issuedFile().loadFileAsString();

        juce::String seenId, why;
        expect (verifySerial (serialBefore, keys.pub, seenId, why),
                "the pre-rotation fixture serial did not verify: " + why);

        const juce::File header = root.getChildFile ("Source/LicenseKeys.h");
        const auto rot = rotateMasterKey ("RotatedMaster!23", header);

        expect (rot.ok, "rotateMasterKey: " + rot.error);
        expect (rot.masterKey == "RotatedMaster!23", "rotation did not report the key it was given");
        expect (rot.digest != rot.previousDigest, "the digest did not change");
        expect (rot.digest == stretchedMasterDigest ("RotatedMaster!23"),
                "the new digest is not the stretched new key");
        expect (rot.headerWritten, "the plugin header was not written");
        expect (rot.logged, "the rotation was not recorded in the rotation log");

        const auto after = loadKeys();
        expect (after.ok && after.pub == keys.pub && after.priv == keys.priv,
                "the keypair changed - every serial ever issued would be dead");
        expect (after.masterDigest.toUpperCase() == rot.digest, "keys.txt kept the old digest");
        expect (keysFile().loadFileAsString().contains ("created="),
                "the rotation dropped the created= stamp");

        // The whole point: a serial signed before the rotation still verifies.
        expect (verifySerial (serialBefore, after.pub, seenId, why),
                "a serial issued before the rotation stopped verifying: " + why);
        expect (stretchedMasterDigest ("TestMaster!23") != after.masterDigest,
                "the retired master key still matches the embedded digest");
        expect (issuedFile().loadFileAsString() == ledgerBefore,
                "the rotation disturbed the issued-serial ledger");

        const juce::String headerText = header.loadFileAsString();
        expect (headerText.contains (rot.digest), "the header does not carry the new digest");
        expect (headerText.contains (keys.pub), "the header lost the public key");

        const juce::String logText = masterLogFile().loadFileAsString();
        expect (logText.contains (rot.digest) && logText.contains (rot.previousDigest),
                "master_rotations.txt does not record the before and after digests");
        expect (! logText.contains ("RotatedMaster!23"),
                "the rotation log wrote the master key itself into master_rotations.txt");

        // Refusals: the key already embedded, a key too short to be worth
        // stretching, and a store that does not exist at all.
        expect (! rotateMasterKey ("RotatedMaster!23", header).ok,
                "rotating to the key already embedded was accepted");
        expect (! rotateMasterKey ("short", header).ok, "a 5-character master key was accepted");
        expect (loadKeys().masterDigest.toUpperCase() == rot.digest,
                "a refused rotation still changed the digest");

        putEnv ("GOASYNTH_KEYGEN_DIR", root.getChildFile ("no-keypair").getFullPathName());
        const auto absent = rotateMasterKey ("RotatedMaster!23", {});
        expect (! absent.ok && absent.error.contains ("--init"),
                "rotating with no keypair did not point at --init: " + absent.error);
        expect (! keysFile().existsAsFile(), "the refused rotation created a keypair");
        putEnv ("GOASYNTH_KEYGEN_DIR", keyDir.getFullPathName());

        // An empty key means "generate one", and the key it reports has to be the
        // one the next build will actually accept.
        const auto generated = rotateMasterKey ("", {});
        expect (generated.ok, "rotating with a generated key: " + generated.error);
        expect (generated.masterKey.length() == 12,
                "the generated master key is " + juce::String (generated.masterKey.length()) + " characters, expected 12");
        expect (generated.digest == stretchedMasterDigest (generated.masterKey),
                "the reported key does not match the digest that was written");
        expect (! generated.headerWritten, "an empty header path was reported as written");
    }

    // ---- CLI -----------------------------------------------------------------
    // The tool is a sibling artefact of this test in the build tree; smoke-test
    // its help output when it has been built, and stay quiet if it has not.
    {
        const juce::File tool = findSiblingArtefact ("GoaSynthFulfil_artefacts/Release/GoaSynthFulfil.exe");

        if (tool.existsAsFile())
        {
            juce::ChildProcess p;
            if (p.start ({ tool.getFullPathName(), "--help" }, juce::ChildProcess::wantStdOut))
            {
                const juce::String out = p.readAllProcessOutput();
                expect (out.contains ("--inbox") && out.contains ("--dry-run"), "CLI --help output looks wrong");
                expect (p.getExitCode() == 0, "CLI --help exit code was " + juce::String (p.getExitCode()));
            }

            // --status must see flags that come after it.
            juce::ChildProcess s;
            if (s.start ({ tool.getFullPathName(), "--status", "--out", outbox.getFullPathName() },
                         juce::ChildProcess::wantStdOut))
            {
                const juce::String out = s.readAllProcessOutput();
                expect (out.contains ("4 live, 0 moved, 1 revoked, 1 refunded"),
                        "CLI --status did not read the manifest: " + out.substring (0, 140));
                expect (out.contains ("jane@example.com"), "CLI --status output omits the buyers");
                expect (out.contains ("revoked_serials.txt"), "CLI --status does not point at the revocation log");
            }
        }
    }

    // ---- real store export shapes ------------------------------------------
    // A Lemon Squeezy export carries both "Product ID" and "Product Name", and
    // its JSON wraps orders in an array. Both used to break: the product id
    // shadowed the product name so every row was skipped as "another product",
    // and the wrapper object was read as one order, gluing two orders' fields
    // into a single key ("6001 6002") with a bogus refund flag attached.
    {
        const juce::File storeInbox  = root.getChildFile ("store-inbox");
        const juce::File storeOutbox = root.getChildFile ("store-fulfilled");
        storeInbox.createDirectory();

        fulfil::Options store = o;
        store.inbox  = storeInbox;
        store.outbox = storeOutbox;

        storeInbox.getChildFile ("orders.csv").replaceWithText (
            "Order ID,Order Number,Order Status,Refunded,Customer Name,Customer Email,"
            "Product ID,Product Name,Variant Name,Machine ID\n"
            "5001,5001,paid,No,Jane Doe,jane@example.com,prod_551,"
            "\"GoaSynth - Goa Trance Synthesizer (VST3)\",\"Personal licence - 3 machines\",1A2B3C4D5E6F70819A2B\n"
            "5002,5002,paid,No,Bob,bob@example.com,prod_551,"
            "\"GoaSynth - Goa Trance Synthesizer (VST3)\",\"Personal licence - 3 machines\",AABBCCDDEEFF00112233\n");

        storeInbox.getChildFile ("orders.json").replaceWithText (
            "{ \"data\": [ "
            "{ \"order_id\": \"6001\", \"customer_email\": \"zoe@example.com\", \"customer_name\": \"Zoe\", "
            "\"product_name\": \"GoaSynth - Goa Trance Synthesizer (VST3)\", \"variant_name\": \"Personal licence - 3 machines\", "
            "\"status\": \"paid\", \"refunded\": false, \"machine_id\": \"112233445566778899AA\" }, "
            "{ \"order_id\": \"6002\", \"customer_email\": \"mia@example.com\", \"customer_name\": \"Mia\", "
            "\"product_name\": \"GoaSynth - Goa Trance Synthesizer (VST3)\", "
            "\"status\": \"paid\", \"refunded\": false, \"machine_id\": \"998877665544332211BB\" } ] }");

        const auto storePass = fulfil::runOnce (store);

        expect (storePass.ordersSeen == 4, "store export: orders seen = " + juce::String (storePass.ordersSeen) + ", expected 4");
        expect (storePass.issued == 4, "store export: issued = " + juce::String (storePass.issued) + ", expected 4");
        expect (storePass.skipped == 0, "store export: skipped = " + juce::String (storePass.skipped) + ", expected 0");

        // Each JSON order keeps its own key - never "6001 6002".
        const juce::String storeManifest = storeOutbox.getChildFile ("manifest.tsv").loadFileAsString();
        expect (storeManifest.contains ("6001\tissued") && storeManifest.contains ("6002\tissued"),
                "the JSON wrapper glued the two orders into one key: " + storeManifest);

        const juce::File storeLicenses = storeOutbox.getChildFile ("licenses");
        expect (storeLicenses.getChildFile ("GoaSynth-1A2B3C4D5E6F70819A2B.goalicense").existsAsFile(),
                "the CSV order's licence was not written");
        expect (storeLicenses.getChildFile ("GoaSynth-112233445566778899AA.goalicense").existsAsFile(),
                "the first JSON order's licence was not written");
        expect (storeLicenses.getChildFile ("GoaSynth-998877665544332211BB.goalicense").existsAsFile(),
                "the second JSON order's licence was not written");
    }

    // ---- machine move in an export: same order, new machine ----------------
    // The buyer changes computer and their order reappears (or support drops the
    // new machine id into manual.csv under the SAME order id) naming a different
    // machine. The tool must retire the old serial and issue for the new one in
    // one pass - silently doing nothing would strand the buyer, and a plain
    // second issue would leave two live seats against one order. This is the
    // same operation as `GoaSynthKeygen --reissue`, through the shared core.
    {
        const juce::File moveInbox  = root.getChildFile ("move-inbox");
        const juce::File moveOutbox = root.getChildFile ("move-fulfilled");
        moveInbox.createDirectory();

        fulfil::Options mv = o;
        mv.inbox  = moveInbox;
        mv.outbox = moveOutbox;

        const juce::String oldMachine   = "AA11BB22CC33DD44EE55";
        const juce::String newMachine   = "FF66EE77DD88CC99BBAA";
        const juce::String otherMachine = "0A1B2C3D4E5F60718293";
        const juce::String moveOrder    = "7001";

        moveInbox.getChildFile ("orders.csv").replaceWithText (
            "Order ID,Email,Product,Machine ID,Status\n"
            + moveOrder + ",ivy@example.com,GoaSynth," + oldMachine + ",paid\n");

        const auto first = fulfil::runOnce (mv);
        expect (first.issued == 1 && first.moved == 0, "the pre-move order did not issue cleanly");

        const juce::String oldSerial = makeSerial (keys, oldMachine);
        expect (ledger.loadFileAsString().contains (oldSerial), "the pre-move serial is not in the ledger");

        // Same order id, new machine id: a machine move.
        moveInbox.getChildFile ("orders.csv").replaceWithText (
            "Order ID,Email,Product,Machine ID,Status\n"
            + moveOrder + ",ivy@example.com,GoaSynth," + newMachine + ",paid\n");

        const auto moved = fulfil::runOnce (mv);

        expect (moved.moved == 1, "moved = " + juce::String (moved.moved) + ", expected 1");
        expect (moved.issued == 0, "a machine move was also counted as a new issue");
        expect (moved.alreadyFulfilled == 0, "a machine move was counted as already fulfilled");

        // Old serial retired, and on the record as a move rather than a refund.
        expect (! ledger.loadFileAsString().contains (oldSerial),
                "the move left the old serial in the active ledger");
        expect (isMoved (oldSerial), "the move did not record a move event");
        expect (! isRevoked (oldSerial), "a machine move is being reported as a refund-style revocation");
        expect (revocationState (oldSerial).event == "moved",
                "the log event for a move is \"" + revocationState (oldSerial).event + "\", expected \"moved\"");
        expect (revocationState (oldSerial).reason.contains ("machine move"),
                "the move record lost its reason: " + revocationState (oldSerial).reason);

        // New serial issued for exactly the new machine.
        const juce::String newSerial = makeSerial (keys, newMachine);
        expect (ledger.loadFileAsString().contains (newSerial), "the move did not issue for the new machine");
        {
            juce::String seenId, why;
            expect (verifySerial (keys, newSerial, seenId, why) && seenId == newMachine,
                    "the new serial does not verify for the new machine: " + why);
        }

        const juce::File licenseDir2 = moveOutbox.getChildFile ("licenses");
        expect (licenseDir2.getChildFile ("GoaSynth-" + newMachine + ".goalicense").existsAsFile(),
                "the move did not write a licence for the new machine");

        juce::Array<juce::File> moveDrafts;
        moveOutbox.getChildFile ("mail").findChildFiles (moveDrafts, juce::File::findFiles, false, "*.eml");
        expect (moveDrafts.size() == 1, "the move should leave one reply-ready draft, got "
                                         + juce::String (moveDrafts.size()));

        const juce::String moveManifest = moveOutbox.getChildFile ("manifest.tsv").loadFileAsString();
        expect (moveManifest.contains (moveOrder + "\tmoved\t") && moveManifest.contains (newMachine),
                "the manifest's newest row is not a moved row naming the new machine:\n" + moveManifest);
        expect (moveOutbox.getChildFile ("activity.log").loadFileAsString().contains ("MOVED " + moveOrder),
                "the move is not in the activity log");

        // The order report must count that seat as moved, not live, so --status
        // matches the revocation log's three states.
        if (const juce::File tool = findSiblingArtefact ("GoaSynthFulfil_artefacts/Release/GoaSynthFulfil.exe");
              tool.existsAsFile())
        {
            juce::ChildProcess st;
            if (st.start ({ tool.getFullPathName(), "--status", "--out", moveOutbox.getFullPathName() },
                          juce::ChildProcess::wantStdOut))
            {
                const juce::String out = st.readAllProcessOutput();
                expect (out.contains ("0 live, 1 moved, 0 revoked"),
                        "CLI --status did not count the moved order as moved: " + out.substring (0, 160));
                expect (out.contains (moveOrder) && out.contains ("moved"),
                        "CLI --status's per-order row does not read moved: " + out.substring (0, 200));
            }
        }

        // Idempotent: the machine now matches, so a later pass leaves it alone.
        const auto stable = fulfil::runOnce (mv);
        expect (stable.moved == 0 && stable.issued == 0 && stable.alreadyFulfilled == 1,
                "a second pass over the moved order did more work");
        expect (countLines (revokedLog, oldSerial) == 1, "a second pass appended another move event");
        expect (ledger.loadFileAsString().contains (newSerial), "a second pass dropped the new serial");

        // A genuinely NEW order from the same email for another machine is a
        // second seat, not a move: the first machine's live serial must stay.
        moveInbox.getChildFile ("orders.csv").replaceWithText (
            "Order ID,Email,Product,Machine ID,Status\n"
            + moveOrder + ",ivy@example.com,GoaSynth," + newMachine + ",paid\n"
            "7002,ivy@example.com,GoaSynth," + otherMachine + ",paid\n");

        const auto secondSeat = fulfil::runOnce (mv);
        expect (secondSeat.issued == 1 && secondSeat.moved == 0,
                "a second order from the same email was treated as a move");
        expect (ledger.loadFileAsString().contains (newSerial),
                "a second purchase by the same email retired the first machine's serial");
        expect (! isRevoked (newSerial), "a second purchase marked the first serial revoked");

        // Dry run reports the move without touching anything. It runs against the
        // real outbox so the manifest still knows the order's current machine.
        moveInbox.getChildFile ("orders.csv").replaceWithText (
            "Order ID,Email,Product,Machine ID,Status\n"
            + moveOrder + ",ivy@example.com,GoaSynth," + oldMachine + ",paid\n");

        fulfil::Options dryMove = mv;
        dryMove.dryRun = true;
        const auto wouldMove = fulfil::runOnce (dryMove);
        expect (wouldMove.moved == 1, "dry run did not report the move");
        expect (! isRevoked (newSerial), "dry run retired the live serial it only meant to move");
        expect (countLines (revokedLog, newSerial) == 0, "dry run appended a revocation event");
        expect (! moveOutbox.getChildFile ("activity.log").loadFileAsString()
                     .contains ("MOVED " + moveOrder + " machine " + newMachine + " -> " + oldMachine),
                "dry run logged a move it did not perform");
        expect (ledger.loadFileAsString().contains (newSerial), "dry run disturbed the ledger");
    }

    // ---- the interactive menu, driven end to end ---------------------------
    // The menu is the path a seller actually uses (double-click, no flags), and
    // its options are numbered: inserting the move option renumbered every one
    // below it. A number that no longer maps to its command - or a missing
    // handler that falls through to "Unknown option" - is exactly the kind of
    // bug a unit test of each command would miss. So run the real binary with no
    // arguments, feed it a scripted keystroke sequence through stdin, and check
    // that each numbered option did its own job.
    {
        const juce::File tool = findSiblingArtefact ("GoaSynthKeygen_artefacts/Release/GoaSynthKeygen.exe");

        if (tool.existsAsFile())
        {
            const juce::String machineA = "AA00BB11CC22DD33EE44";
            const juce::String machineB = "BB00CC11DD22EE33FF44";
            const juce::String machineC = "CC00DD11EE22FF330011";
            const juce::String serialA  = makeSerial (keys, machineA);
            const juce::String serialB  = makeSerial (keys, machineB);
            const juce::String serialC  = makeSerial (keys, machineC);

            // Each option number, then its prompts, then a blank line for the
            // "Press Enter for the menu..." pause. "0" exits.
            juce::StringArray script;
            script.add ("1");  script.add ("");                                          // master info
            script.add ("2");  script.add (machineA); script.add ("Menu Buyer");
                               script.add ("menu@example.com"); script.add ("order menu");
                               script.add ("menu-a.goalicense"); script.add ("");          // guided .goalicense
            script.add ("3");  script.add (machineB); script.add ("Menu B");
                               script.add ("menub@example.com"); script.add ("order menub"); script.add ("");
            script.add ("4");  script.add ("");                                          // list
            script.add ("5");  script.add (serialB); script.add ("");                    // verify
            script.add ("6");  script.add (serialB); script.add ("");                    // unregister
            script.add ("7");  script.add (serialA); script.add (machineC);
                               script.add ("moved menu"); script.add ("");                // move (the new option)
            script.add ("8");  script.add (serialC); script.add ("menu test"); script.add ("");  // revoke
            script.add ("9");  script.add ("");                                          // machine id
            script.add ("10"); script.add ("MenuRotate!23"); script.add ("ROTATE"); script.add (""); // rotate
            script.add ("0");

            const juce::File menuCwd = root.getChildFile ("menu-cwd");
            int status = -1;
            const juce::String out = runScriptedMenu (tool, script, menuCwd,
                                                      root.getChildFile ("menu-in.txt"),
                                                      root.getChildFile ("menu-out.txt"), status);

            expect (status == 0, "menu run exited " + juce::String (status));

            expect (out.contains ("Master digest embedded in the plugin:"), "menu 1 did not show master info");
            expect (out.contains ("License file written:"), "menu 2 did not write a guided .goalicense");
            expect (out.contains ("Serial (send this to the buyer):"), "menu 3 did not issue a bare serial");
            expect (out.contains ("active serial(s)"), "menu 4 did not list the serials");
            expect (out.contains ("VALID") && out.contains ("signed for machine id " + machineB),
                    "menu 5 did not verify the serial");
            expect (out.contains ("Unregistered "), "menu 6 did not unregister the serial");
            expect (out.contains ("Machine move complete"), "menu 7 did not perform the move");
            expect (out.contains ("Revoked "), "menu 8 did not revoke the serial");
            expect (out.contains ("Rotating the master key only"), "menu 10 did not rotate the master key");
            expect (! out.contains ("Unknown option"),
                    "a menu number has no handler (renumbering left a gap):"
                      + out.substring (juce::jmax (0, out.indexOf ("Unknown option") - 40), 120));

            // Option 9 prints the machine id on a line of its own.
            bool sawMachineId = false;
            for (const auto& line : juce::StringArray::fromLines (out))
                if (line.trim() == thisMachineId())
                    sawMachineId = true;
            expect (sawMachineId, "menu 9 did not print this machine's id");

            // Each option did the real work, not just printed a marker.
            expect (menuCwd.getChildFile ("menu-a.goalicense").existsAsFile(),
                    "menu 2 wrote no licence file");
            expect (! ledger.loadFileAsString().contains (serialB),
                    "menu 6 left the unregistered serial in the active ledger");
            expect (isMoved (serialA), "menu 7 did not retire the old serial as a move");
            expect (isRevoked (serialA) == false, "menu 7 called a move a refund");
            expect (isRevoked (serialC), "menu 8 did not revoke the serial the move issued");
            expect (loadKeys().masterDigest == stretchedMasterDigest ("MenuRotate!23"),
                    "menu 10 did not actually rotate the master key");
        }
    }

    // ---- the interactive menu's failure branches, driven end to end --------
    // The happy path above proves each number maps to its command. This second
    // run proves the menu survives the answers a seller actually gets wrong: an
    // option 1 pressed on a store with no keypair yet, a malformed machine id,
    // and a rotate the user backs out of. Each must print its guidance rather
    // than crash or fall through, and the menu must stay usable afterwards.
    {
        const juce::File tool = findSiblingArtefact ("GoaSynthKeygen_artefacts/Release/GoaSynthKeygen.exe");

        if (tool.existsAsFile())
        {
            // A keypair-less store of its own: option 1 is the create path here,
            // and the options below then run against the keypair it makes.
            const juce::File emptyKeyDir = root.getChildFile ("menu2-keys");
            emptyKeyDir.createDirectory();
            putEnv ("GOASYNTH_KEYGEN_DIR", emptyKeyDir.getFullPathName());

            const juce::String badId     = "not-a-machine-id";
            const juce::String goodId    = "DD00EE11FF2200331144";
            const juce::String newMaster = "SecondMenu!23";

            juce::StringArray script;
            script.add ("1");  script.add (newMaster); script.add ("");   // create keypair (none yet)
            script.add ("3");  script.add (badId);                        // invalid id -> retry hint
                               script.add (goodId); script.add ("Fail Buyer");
                               script.add ("fail@example.com"); script.add ("order bad-id"); script.add ("");
            script.add ("10"); script.add ("CancelRotate!23"); script.add ("nope"); script.add (""); // cancel
            script.add ("1");  script.add ("");                           // master info now works
            script.add ("0");

            int status = -1;
            const juce::String out = runScriptedMenu (tool, script,
                                                      root.getChildFile ("menu2-cwd"),
                                                      root.getChildFile ("menu2-in.txt"),
                                                      root.getChildFile ("menu2-out.txt"), status);

            expect (status == 0, "failure-branch menu run exited " + juce::String (status));

            // Option 1 on a keypair-less store explains itself rather than guessing,
            // and really does create the keypair the options below rely on.
            expect (out.contains ("Keypair: NOT CREATED YET"),
                    "the menu did not notice the missing keypair");
            expect (out.contains ("The master key must MATCH what was embedded"),
                    "option 1 on a keypair-less store did not explain the master key");
            expect (emptyKeyDir.getChildFile ("keys.txt").existsAsFile(),
                    "option 1 did not create a keypair");

            // A malformed machine id prints the retry hint and does not abort the
            // menu: the good id on the next line goes on to issue a serial.
            expect (out.contains ("That is not a valid 20-char machine id"),
                    "an invalid machine id did not print the retry hint");
            expect (out.contains ("Serial (send this to the buyer):"),
                    "the menu did not issue after the bad machine id");

            // Backing out of the rotate changes nothing.
            expect (out.contains ("Cancelled"), "a cancelled rotate did not say so");
            expect (loadKeys().masterDigest == stretchedMasterDigest (newMaster),
                    "a cancelled rotate still changed the master key");

            // The menu stayed usable: option 1's info path ran, nothing fell
            // through to "Unknown option", and it exited cleanly.
            expect (out.contains ("Master digest embedded in the plugin:"),
                    "the menu was not usable after the failure branches");
            expect (! out.contains ("Unknown option"),
                    "a menu number lost its handler: " + out.substring (0, 160));

            putEnv ("GOASYNTH_KEYGEN_DIR", keyDir.getFullPathName());
        }
    }

    // ---- cleanup ------------------------------------------------------------
    root.deleteRecursively();

    std::printf (fails == 0 ? "FULFIL OK\n" : "FAILURES: %d\n", fails);
    return fails == 0 ? 0 : 1;
}
