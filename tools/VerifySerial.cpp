// Headless sanity check for the plugin's REAL activation path.
//
//   VerifySerial.exe --check                 machine id + key/keypair status
//   VerifySerial.exe --activate <serial>     run License::activate() as the VST3 would
//   VerifySerial.exe --deactivate            remove the (redirected) activation
//
// Unlike the ctest suite this target deliberately has NO GOA_TEST_BUILD, so
// Source/License.cpp compiles against the seller's real LicenseKeys.h: a
// "valid" verdict here is byte-for-byte the built VST3's verdict. Storage is
// redirected with GOASYNTH_LICENSE_FILE / GOASYNTH_LEDGER_FILE (same hooks
// the tests use) so real activations are never touched by a dev self-test.
// Not wired into ctest: the suite must stay key-independent.

#include <juce_core/juce_core.h>
#include <juce_cryptography/juce_cryptography.h>

#include "../Source/License.h"
#include "LicenseKeys.h"     // real embedded key material (gitignored)

#include <iostream>

int main (int argc, char* argv[])
{
    std::vector<juce::String> args;
    for (int i = 1; i < argc; ++i)
        args.push_back (argv[i]);

    if (args.empty())
    {
        std::cout << "usage: VerifySerial --check | --activate <serial> | --deactivate\n";
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
            std::cout << "ACTIVATED\n"
                      << "stored: " << goa::License::storedSerial() << "\n";
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
