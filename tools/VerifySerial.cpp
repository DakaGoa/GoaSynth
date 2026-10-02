// Headless sanity check for the plugin's REAL activation path.
//
//   VerifySerial.exe --check                 machine id + key/keypair status
//   VerifySerial.exe [--machine <id>] --activate <serial-or-master-key>
//   VerifySerial.exe --deactivate            remove the (redirected) activation
//
// Unlike the ctest suite this target deliberately has NO GOA_TEST_BUILD, so
// Source/License.cpp compiles against the seller's real LicenseKeys.h: a
// "valid" verdict here is byte-for-byte the built VST3's verdict. Storage is
// redirected with GOASYNTH_LICENSE_FILE / GOASYNTH_LEDGER_FILE (same hooks
// the tests use) so real activations are never touched by a dev self-test.
// Not wired into ctest: the suite must stay key-independent.
//
// --machine <id> uses the same test hook the ctest suite uses
// (License::setTestMachineId, this process only) so the seller can prove a
// master key or a serial's behaviour on an arbitrary machine id without
// touching this machine's real identity.

#include <juce_core/juce_core.h>
#include <juce_cryptography/juce_cryptography.h>

#include "../Source/License.h"
#include "LicenseKeys.h"     // real embedded key material (gitignored)

#include <iostream>

int main (int argc, char* argv[])
{
    juce::String machineOverride;
    std::vector<juce::String> args;
    for (int i = 1; i < argc; ++i)
    {
        const juce::String a (argv[i]);
        if (a == "--machine" && i + 1 < argc)
            machineOverride = juce::String (argv[++i]).trim();
        else
            args.push_back (a);
    }

    if (machineOverride.isNotEmpty())
    {
        goa::License::setTestMachineId (machineOverride);
        std::cout << "machine override  : " << machineOverride
                  << "  (test hook, this process only)\n";
    }

    if (args.empty())
    {
        std::cout << "usage: VerifySerial [--machine <id>] --check | --activate <serial-or-master-key> | --deactivate\n";
        return 2;
    }

    const juce::String cmd = args[0];

    if (cmd == "--check")
    {
        const bool havePub    = juce::String (GOA_LICENSE_PUBLIC_KEY).isNotEmpty();
        const bool haveMaster = juce::String (GOA_MASTER_DIGEST).isNotEmpty();

        std::cout << "keypair embedded : " << (havePub ? "yes (real RSA public key)" : "NO - run GoaSynthKeygen --init, then rebuild") << "\n"
                  << "master digest    : " << (haveMaster ? "present" : "missing") << "\n"
                  << "machine id       : " << goa::License::machineId() << "\n"
                  << "licensed now     : " << (goa::License::isLicensed() ? "yes" : "no") << "\n"
                  << "trial            : " << goa::License::trialTimeLeft() << "\n";
        return 0;
    }

    if (cmd == "--activate")
    {
        if (args.size() < 2)
        {
            std::cout << "usage: VerifySerial --activate <serial>\n";
            return 2;
        }

        juce::String err;
        if (goa::License::activate (args[1], err))
        {
            // The stored value is echoed masked: master keys never persist in
            // the clear (stored as GOA1-MASTER), and a real serial is the
            // buyer's credential — the seller's issued_serials.txt has it.
            const juce::String stored = goa::License::storedSerial();
            std::cout << "ACTIVATED\n"
                      << "stored: " << (stored.length() > 24
                                          ? stored.substring (0, 12) + "..."
                                                + stored.getLastCharacters (4) + " (masked)"
                                          : stored) << "\n";
            return 0;
        }

        std::cout << "REFUSED: " << err << "\n";
        return 1;
    }

    if (cmd == "--deactivate")
    {
        goa::License::deactivate();
        std::cout << "deactivated (isLicensed = " << (goa::License::isLicensed() ? "yes" : "no") << ")\n";
        return 0;
    }

    std::cout << "unknown option: " << cmd << "\n";
    return 2;
}
