#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_cryptography/juce_cryptography.h>

#include "../Source/License.h"

#if JUCE_WINDOWS
 #include <windows.h>
 #include <shlobj.h>   // SHChangeNotify (SHCNE_ASSOCCHANGED)
#endif

//==============================================================================
// GoaSynthLicense — the .goalicense double-click helper.
//
// A VST3 plugin is a DLL: Windows cannot open a license file "with" it, so
// this tiny executable owns the file association instead. The seller sends a
// .goalicense file, the buyer double-clicks it, Explorer launches this helper,
// and the file is activated through exactly the same code path the plugin uses
// (License::activateFile -> RSA signature + machine id + one-machine ledger).
//
//   GoaSynthLicense.exe                  first run: registers the association
//   GoaSynthLicense.exe <file.goalicense>  double-click: activate + report
//   GoaSynthLicense.exe --unregister     remove the association again
//   GoaSynthLicense.exe --selftest       register -> verify -> unregister, log
//                                        result to %TEMP%, exit (CI/dev only)
//
// The association is registered for the current user only (HKCU): no admin
// rights, no installer.
namespace
{

constexpr const char* fileExt  = ".goalicense";
constexpr const char* progId   = "GoaSynth.License";
constexpr const char* fileDesc = "GoaSynth license file";

juce::File selftestFile()
{
    return juce::File::getSpecialLocation (juce::File::tempDirectory)
               .getChildFile ("goasynth_license_selftest.txt");
}

// ---- file association --------------------------------------------------------

bool associationRegistered()
{
   #if JUCE_WINDOWS
    // Trailing backslash = the key's (nameless) default value, matching how
    // WindowsRegistry::registerFileAssociation writes it.
    return juce::WindowsRegistry::valueExists (
        juce::String ("HKEY_CURRENT_USER\\Software\\Classes\\") + progId + "\\shell\\open\\command\\");
   #else
    return false;
   #endif
}

bool registerAssociation()
{
   #if JUCE_WINDOWS
    const auto exe = juce::File::getSpecialLocation (juce::File::currentExecutableFile);

    bool ok = juce::WindowsRegistry::registerFileAssociation (
        fileExt, progId, fileDesc, exe, 0, /*currentUserOnly*/ true);

    // Appear in Explorer's "Open with" list too.
    juce::WindowsRegistry::setValue (
        juce::String ("HKEY_CURRENT_USER\\Software\\Classes\\") + fileExt + "\\OpenWithProgids\\" + progId, juce::String());

    // Best-effort default override. UserChoice is hash-protected on Win10+ and
    // may be ignored — then "GoaSynth license file" still shows in the Open
    // With picker, so the user selects it once and Windows remembers.
    juce::WindowsRegistry::setValue (
        juce::String ("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\FileExts\\")
            + fileExt + "\\UserChoice\\Progid", juce::String (progId));

    ::SHChangeNotify (SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
    return ok;
   #else
    return true;
   #endif
}

void unregisterAssociation()
{
   #if JUCE_WINDOWS
    const juce::String classes = "HKEY_CURRENT_USER\\Software\\Classes";

    // Leaf-to-root, since RegDeleteKey refuses non-empty keys.
    juce::WindowsRegistry::deleteValue (classes + "\\" + fileExt + "\\OpenWithProgids\\" + progId);
    juce::WindowsRegistry::deleteValue (classes + "\\" + fileExt);
    juce::WindowsRegistry::deleteKey   (classes + "\\" + fileExt + "\\OpenWithProgids");
    juce::WindowsRegistry::deleteKey   (classes + "\\" + fileExt);

    juce::WindowsRegistry::deleteValue (classes + "\\" + progId + "\\shell\\open\\command");
    juce::WindowsRegistry::deleteKey   (classes + "\\" + progId + "\\shell\\open\\command");
    juce::WindowsRegistry::deleteKey   (classes + "\\" + progId + "\\shell\\open");
    juce::WindowsRegistry::deleteKey   (classes + "\\" + progId + "\\shell");
    juce::WindowsRegistry::deleteKey   (classes + "\\" + progId);

    juce::WindowsRegistry::deleteValue (
        juce::String ("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\FileExts\\")
            + fileExt + "\\UserChoice\\Progid");

    ::SHChangeNotify (SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
   #endif
}

// ---- result window -----------------------------------------------------------

constexpr uint32_t bgDark   = 0xff0d0b16;
constexpr uint32_t bgPanel  = 0xff16121f;
constexpr uint32_t accentA  = 0xff40e0c0;
constexpr uint32_t okGreen  = 0xff7de8a3;
constexpr uint32_t badRed   = 0xffe05a6a;
constexpr uint32_t textDim  = 0xff8a86a0;
constexpr uint32_t textBright = 0xfff0eef8;

struct ResultPanel : juce::Component
{
    ResultPanel (const juce::String& head, const juce::String& status,
                 const juce::String& detail, const juce::String& footnote, bool success)
    {
        headLabel.setText (head, juce::dontSendNotification);
        headLabel.setFont (juce::Font (juce::FontOptions (11.0f, juce::Font::bold)));
        headLabel.setColour (juce::Label::textColourId, juce::Colour (accentA));
        addAndMakeVisible (headLabel);

        statusLabel.setText (status, juce::dontSendNotification);
        statusLabel.setFont (juce::Font (juce::FontOptions (18.0f, juce::Font::bold)));
        statusLabel.setColour (juce::Label::textColourId, juce::Colour (success ? okGreen : badRed));
        addAndMakeVisible (statusLabel);

        detailLabel.setText (detail, juce::dontSendNotification);
        detailLabel.setFont (juce::Font (juce::FontOptions (12.5f)));
        detailLabel.setColour (juce::Label::textColourId, juce::Colour (textBright));
        detailLabel.setJustificationType (juce::Justification::centredLeft);
        addAndMakeVisible (detailLabel);

        footLabel.setText (footnote, juce::dontSendNotification);
        footLabel.setFont (juce::Font (juce::FontOptions (10.5f)));
        footLabel.setColour (juce::Label::textColourId, juce::Colour (textDim));
        footLabel.setVisible (footnote.isNotEmpty());
        addAndMakeVisible (footLabel);

        closeBtn.setButtonText ("CLOSE");
        closeBtn.setColour (juce::TextButton::buttonColourId, juce::Colour (bgPanel));
        closeBtn.setColour (juce::TextButton::textColourOffId, juce::Colour (accentA));
        closeBtn.onClick = [] { juce::JUCEApplication::quit(); };
        addAndMakeVisible (closeBtn);
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colour (bgDark));

        auto uv = juce::Colour (0xff9b5cff);
        g.setGradientFill (juce::ColourGradient (uv, 0.0f, 0.0f,
                                                 juce::Colour (accentA), (float) getWidth(), 0.0f, false));
        g.fillRect (0, 0, getWidth(), 2);
    }

    void resized() override
    {
        auto b = getLocalBounds().reduced (18, 12);
        headLabel.setBounds (b.removeFromTop (18));
        statusLabel.setBounds (b.removeFromTop (30));
        b.removeFromTop (4);
        footLabel.setBounds (b.removeFromBottom (20));
        b.removeFromBottom (6);
        closeBtn.setBounds (b.removeFromBottom (30).withSizeKeepingCentre (110, 24));
        b.removeFromBottom (8);
        detailLabel.setBounds (b);
    }

    juce::Label headLabel, statusLabel, detailLabel, footLabel;
    juce::TextButton closeBtn;
};

struct ResultWindow : juce::DocumentWindow
{
    ResultWindow (ResultPanel* panel)
        : juce::DocumentWindow ("GoaSynth", juce::Colour (bgDark), juce::DocumentWindow::closeButton)
    {
        setUsingNativeTitleBar (true);
        setContentOwned (panel, true);
        centreWithSize (470, 262);
        setVisible (true);
    }

    void closeButtonPressed() override { juce::JUCEApplication::quit(); }
};

// ---- helpers -----------------------------------------------------------------

// Explorer passes the double-clicked file (usually quoted) on the command line.
juce::File extractLicenseFile (const juce::String& args)
{
    if (! args.containsIgnoreCase (fileExt))
        return {};

    juce::String s = args.trim();
    if (s.startsWithChar ('"') && s.indexOfChar (1, '"') > 0)
        s = s.substring (1, s.indexOfChar (1, '"'));

    s = s.trim().unquoted();
    const juce::File f (s);
    return (f.hasFileExtension (fileExt) && f.existsAsFile()) ? f : juce::File();
}

juce::String maskSerial (const juce::String& s)
{
    if (! s.startsWith ("GOA1-"))
        return s;                       // e.g. GOA1-MASTER

    const juce::String body = s.substring (5);
    const int dash = body.indexOfChar ('-');
    if (dash < 0)
        return s;

    const juce::String sig = body.substring (dash + 1);
    return "GOA1-" + body.substring (0, dash) + "-" + sig.substring (0, 4)
         + juce::String::charToString (0x2026) + sig.getLastCharacters (4);
}

} // namespace

//==============================================================================
class GoaSynthLicenseApp : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName()    override { return "GoaSynthLicense"; }
    const juce::String getApplicationVersion() override { return "1.1.0"; }
    bool moreThanOneInstanceAllowed()          override { return true; }

    void initialise (const juce::String& commandLine) override
    {
        const juce::String args = commandLine.trim();

        if (args.containsIgnoreCase ("--selftest"))
        {
            runSelfTest();
            quit();
            return;
        }

        if (args.containsIgnoreCase ("--unregister"))
        {
            unregisterAssociation();
            showWindow ("GOASYNTH LICENSE HELPER", "FILE ASSOCIATION REMOVED",
                        "Windows no longer offers GoaSynth for .goalicense files.\n"
                        "You can still import license files with the IMPORT button\n"
                        "on the plugin's activation screen.", {}, true);
            return;
        }

        // Double-click path: Explorer launches us with the license file.
        const juce::File licenseFile = extractLicenseFile (args);

        if (licenseFile == juce::File())
        {
            // Manual run = first-time setup.
            const bool ok = registerAssociation();
            showWindow ("GOASYNTH LICENSE HELPER",
                        ok ? "SET UP COMPLETE" : "SETUP FAILED",
                        ok ? "Windows now opens .goalicense files with GoaSynth.\n\n"
                             "Double-click the license file your seller sent you and\n"
                             "GoaSynth activates on this computer.\n\n"
                             "If Windows still asks how to open the file, pick\n"
                             "\"GoaSynth license file\" once - it remembers."
                           : "Could not write the Windows file association.\n"
                             "Use the IMPORT button on the plugin's activation\n"
                             "screen instead - it works the same.", {}, ok);
            return;
        }

        // A double-click may arrive before any manual setup ran.
        if (! associationRegistered())
            registerAssociation();

        activateAndReport (licenseFile);
    }

    void shutdown() override { window = nullptr; }   // release the window before JUCE teardown

private:
    void activateAndReport (const juce::File& f)
    {
        const bool wasLicensed = goa::License::isLicensed();
        juce::String err;

        if (goa::License::activateFile (f, err))
        {
            const juce::String detail = wasLicensed
                ? "GoaSynth was already licensed on this computer.\nThe license file refreshed the activation.\n\nSerial: "
                  + maskSerial (goa::License::storedSerial())
                : "GoaSynth is licensed on this computer.\n\nSerial: "
                  + maskSerial (goa::License::storedSerial());

            showWindow ("GOASYNTH ACTIVATION", wasLicensed ? "ACTIVATED (REFRESHED)" : "ACTIVATED",
                        detail, {}, true);
        }
        else
        {
            showWindow ("GOASYNTH ACTIVATION", "ACTIVATION FAILED",
                        err + "\n\nIf this is not the computer the key was issued for,\n"
                              "send your MACHINE ID below to the seller for the right key.",
                        "MACHINE ID: " + goa::License::machineId(), false);
        }
    }

    void showWindow (const juce::String& head, const juce::String& status,
                     const juce::String& detail, const juce::String& footnote, bool success)
    {
        window = std::make_unique<ResultWindow> (new ResultPanel (head, status, detail, footnote, success));
    }

    // register -> verify -> unregister, then log the result for automation.
    void runSelfTest()
    {
        const bool reg  = registerAssociation();
        const bool seen = associationRegistered();
        unregisterAssociation();
        const bool gone = ! associationRegistered();

        juce::String report;
        report << "register=" << (reg  ? "ok" : "fail") << " "
               << "verify="   << (seen ? "ok" : "fail") << " "
               << "unregister=" << (gone ? "ok" : "fail") << "\n";
        selftestFile().replaceWithText (report);
    }

    std::unique_ptr<ResultWindow> window;
};

START_JUCE_APPLICATION (GoaSynthLicenseApp)
