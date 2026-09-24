#pragma once

#include <juce_core/juce_core.h>

//==============================================================================
// Offline serial licensing for GoaSynth.
//
// * Every instance ships unlicensed and renders pure silence until activated.
// * Each instance must be activated with its own serial: the keygen signs the
//   buyer's MACHINE ID (a fingerprint of hostname/user/MAC addresses) with an
//   RSA private key; the plugin verifies the signature with an embedded public
//   key. A serial therefore only ever activates on the machine it was issued
//   for, so sharing keys between people is blocked by construction.
// * An activation ledger remembers which machine each serial belongs to: a
//   serial already bound to another machine refuses to activate here.
// * The serial is also written into the session state as provenance only: a
//   project saved on a licensed machine can never activate another machine,
//   because activation runs exclusively through activate() (signature +
//   machine id + the one-machine ledger).
// * The RSA private key never ships inside the plugin: it lives only with the
//   developer, in the GoaSynthKeygen tool.
// * The keygen can also export a serial as a buyer-ready .goalicense file
//   (GOA-LICENSE-1 text): the buyer double-clicks it — Windows then opens it
//   with GoaSynth — or uses the IMPORT button on the activation screen, and
//   the plugin activates from the embedded serial. Files are interchangeable
//   with the plain serial: same signature, same machine binding, same ledger.
//
namespace goa
{
class License
{
public:
    static constexpr int serialHexLength = 20;   // raw hex chars, sans dashes

    // ---- machine identity -------------------------------------------------
    // Stable 20-hex-char fingerprint of this machine (hostname + user + MAC
    // addresses). Printed in the activation overlay; the keygen signs it.
    static juce::String machineId();

    // ---- master key -------------------------------------------------------
    // The developer master key is verified against this salted SHA-256 digest
    // (the key itself is never stored). Master activates any machine.
    static bool masterMatches (const juce::String& serial);

    // ---- activation -------------------------------------------------------
    // Verifies the serial against this machine and stores the activation.
    // On failure, `error` explains why (bad serial / bound to another machine)
    // and the plugin stays locked.
    static bool activate (const juce::String& serial, juce::String& error);
    static void deactivate();
    static bool isLicensed();
    static juce::String storedSerial();          // "" when unlicensed

    // ---- formatting -------------------------------------------------------
    static juce::String formatSerial (const juce::String& hex);   // XXXX-XXXX-...

    // ---- .goalicense files -------------------------------------------------
    // Buyer-ready license files (keygen --file <machineId> [note]): a
    // GOA-LICENSE-1 text document with the RSA-signed serial embedded.
    // activateFile() parses the file, cross-checks the embedded machine id
    // against this machine (the serial signature is checked as usual) and
    // activates; on any failure `error` explains why and nothing is stored.
    static constexpr const char* fileExtension  = "goalicense";
    static constexpr const char* fileMagic      = "GOA-LICENSE-1";
    static juce::String makeLicenseFile (const juce::String& serial, const juce::String& buyerNote);
    static bool activateFile (const juce::File& f, juce::String& error);

    // ---- trial -------------------------------------------------------------
    // 24-hour full-featured trial. The first launch stamps a trial file with
    // a Unix timestamp; while unexpired the plugin runs fully licensed, after
    // expiry instances behave exactly like unlicensed ones. A serial (or the
    // master key) supersedes the trial entirely.
    static constexpr int trialHours = 24;
    static constexpr juce::int64 trialSeconds = (juce::int64) trialHours * 3600;
    static bool trialActive();                // inside the window (arms it on first call)
    static juce::String trialTimeLeft();      // "23 h 59 m", "active" or "expired"

    // Test hooks (never called in production): force the trial's first-launch
    // timestamp (0 = now) and redirect its storage via GOASYNTH_TRIAL_FILE.
    static void setTrialTestStart (juce::int64 unixSeconds);

    // Test hook only (never called in production): forces machineId() to a
    // fixed value so the suite can simulate other machines. "" restores it.
    static void setTestMachineId (const juce::String& id);
};

} // namespace goa
