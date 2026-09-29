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
        const juce::File tool = juce::File::getSpecialLocation (juce::File::currentExecutableFile)
                                    .getParentDirectory()                    // Release
                                    .getParentDirectory()                    // FulfilTest_artefacts
                                    .getParentDirectory()                    // build
                                    .getChildFile ("GoaSynthFulfil_artefacts/Release/GoaSynthFulfil.exe");

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
                expect (out.contains ("4 live, 1 revoked, 1 refunded"),
                        "CLI --status did not read the manifest: " + out.substring (0, 140));
                expect (out.contains ("jane@example.com"), "CLI --status output omits the buyers");
                expect (out.contains ("revoked_serials.txt"), "CLI --status does not point at the revocation log");
            }
        }
    }

    // ---- cleanup ------------------------------------------------------------
    root.deleteRecursively();

    std::printf (fails == 0 ? "FULFIL OK\n" : "FAILURES: %d\n", fails);
    return fails == 0 ? 0 : 1;
}
