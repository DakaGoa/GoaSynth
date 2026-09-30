#include <juce_gui_basics/juce_gui_basics.h>

#include "Vst3Resources.h"

#if JUCE_WINDOWS
 #include <windows.h>
 #include <shellapi.h>
#endif

//==============================================================================
// GoaSynthSetup - the buyer-facing installer.
//
// Double-clicking this .exe asks WHERE to put the plugin (the standard VST3
// folder every DAW scans, or any custom folder for hosts configured
// elsewhere), copies the embedded VST3 bundle there, drops the standalone
// GoaSynth.exe next to it, adds a Start-menu folder and an
// "Add / Remove Programs" entry, and can remove the whole thing again.
//
//   GoaSynth-Setup.exe               installer window
//   GoaSynth-Setup.exe --silent [dir]
//                                    no UI: install to dir (default: standard
//                                    VST3 folder, elevated when needed).
//                                    Exit code 0 = installed, 1 = failed.
//   GoaSynth-Setup.exe --uninstall   remove files, shortcuts and registry entry
//   GoaSynth-Setup.exe --status      print current install state (dev/CI)
//
// Admin rights are NOT baked into the exe. The standard VST3 folder lives
// under Program Files, so when the user picks it (or runs --silent with no
// dir) the copy runs through a freshly elevated copy of this same exe, which
// reports back through a temp file. A custom folder under the user's profile
// installs directly - no UAC prompt at all.
namespace
{

constexpr const char* productName = "GoaSynth";

// The payload: the actual plugin + standalone bytes, embedded as RCDATA
// resources at build time (see installer.rc.in). Loaded once at startup.
juce::MemoryBlock pluginBlob, moduleInfoBlob, standaloneBlob;

juce::String loadPayload()
{
   #if JUCE_WINDOWS
    if (! Vst3Res::load (Vst3Res::pluginResource, pluginBlob))
        return "the VST3 payload is missing from this exe (broken build)";
    if (! Vst3Res::load (Vst3Res::moduleInfoResource, moduleInfoBlob))
        return "the moduleinfo payload is missing from this exe (broken build)";
    if (! Vst3Res::load (Vst3Res::standaloneResource, standaloneBlob))
        return "the standalone payload is missing from this exe (broken build)";
    return {};
   #else
    return "the installer is Windows-only";
   #endif
}

juce::File standardVst3Dir()
{
   #if JUCE_WINDOWS
    // The real per-machine VST3 location. Resolve Program Files via the
    // environment, which follows the OS language and skips the WOW64
    // redirect on 64-bit processes anyway.
    auto envPf = juce::SystemStats::getEnvironmentVariable ("ProgramW6432", {});
    if (envPf.trim().isEmpty())
        envPf = juce::SystemStats::getEnvironmentVariable ("ProgramFiles", {});
    if (envPf.trim().isEmpty())
        envPf = "C:\\Program Files";
    return juce::File (envPf).getChildFile ("Common Files").getChildFile ("VST3");
   #else
    return juce::File ("/Library/Audio/Plug-Ins/VST3");
   #endif
}

juce::File installedMarker()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
              .getChildFile ("GoaSynth").getChildFile ("install.txt");
}

juce::File elevationReportFile()
{
    return juce::File::getSpecialLocation (juce::File::tempDirectory)
              .getChildFile ("goasynth_setup_install_result.txt");
}

struct InstallPlan
{
    juce::File vst3Dir;            // folder the bundle goes INTO
    bool needsElevation = false;
};

// A real write test beats every permission guess: create the folder if it is
// missing, then drop and remove a probe file. Only the standard Program Files
// location is assumed to need elevation before the test runs.
InstallPlan planFor (const juce::File& chosenDir)
{
    InstallPlan plan;
    plan.vst3Dir = chosenDir;

   #if JUCE_WINDOWS
    plan.needsElevation = (chosenDir == standardVst3Dir());

    if (! plan.needsElevation)
    {
        chosenDir.createDirectory();
        const juce::File probe = chosenDir.getChildFile ("goasynth_write_probe.tmp");
        const bool wrote = probe.replaceWithText ("probe");
        probe.deleteFile();
        plan.needsElevation = ! wrote;
    }
   #else
    ignoreUnused (chosenDir);
   #endif

    return plan;
}

// ---- Add / Remove Programs entry ---------------------------------------------

#if JUCE_WINDOWS
juce::String quote (const juce::String& s) { return "\"" + s + "\""; }

// Elevated install = HKLM (machine-wide); a per-user custom folder = HKCU, so
// no admin rights are needed at all for that path.
juce::String uninstallKeyRoot (bool machineWide)
{
    return juce::String (machineWide ? "HKEY_LOCAL_MACHINE" : "HKEY_CURRENT_USER")
         + "\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\GoaSynth";
}
#endif

void writeUninstallEntry (bool machineWide, const juce::File& vst3Dir,
                          const juce::File& setupExe, const juce::File& standaloneExe)
{
   #if JUCE_WINDOWS
    const juce::String root = uninstallKeyRoot (machineWide);

    juce::WindowsRegistry::setValue (root + "\\DisplayName",    productName);
    juce::WindowsRegistry::setValue (root + "\\DisplayVersion", GOASYNTH_VERSION);
    juce::WindowsRegistry::setValue (root + "\\Publisher",      "FreebuffAudio");
    juce::WindowsRegistry::setValue (root + "\\InstallLocation", vst3Dir.getFullPathName());
    juce::WindowsRegistry::setValue (root + "\\DisplayIcon",    standaloneExe.getFullPathName() + ",0");
    juce::WindowsRegistry::setValue (root + "\\UninstallString",
                                     quote (setupExe.getFullPathName()) + " --uninstall");

    // EstimatedSize is a KB dword, so the list can show the disk footprint.
    const juce::int64 kbytes = juce::jmax<juce::int64> (1, (pluginBlob.getSize() + standaloneBlob.getSize()) / 1024);
    juce::WindowsRegistry::setValue (root + "\\EstimatedSize", (juce::uint32) kbytes);
    juce::WindowsRegistry::setValue (root + "\\NoModify", (juce::uint32) 1);
    juce::WindowsRegistry::setValue (root + "\\NoRepair", (juce::uint32) 1);
   #else
    ignoreUnused (machineWide, vst3Dir, setupExe, standaloneExe);
   #endif
}

void removeUninstallEntry (bool machineWide)
{
   #if JUCE_WINDOWS
    juce::WindowsRegistry::deleteKey (uninstallKeyRoot (machineWide));
   #else
    ignoreUnused (machineWide);
   #endif
}

// ---- the actual install ---------------------------------------------------------

// Returns an error string, or {} on success.
juce::String installInto (const juce::File& vst3Dir, bool machineWide)
{
    const juce::File bundleDir = vst3Dir.getChildFile ("GoaSynth.vst3");
    const juce::File binaryDir = bundleDir.getChildFile ("Contents").getChildFile ("x86_64-win");
    const juce::File resDir    = bundleDir.getChildFile ("Contents").getChildFile ("Resources");

    if (! binaryDir.createDirectory().wasOk())  return "could not create " + binaryDir.getFullPathName();
    if (! resDir.createDirectory().wasOk())     return "could not create " + resDir.getFullPathName();

    // Refuse to fight a DAW holding the old DLL open.
    const juce::File target = binaryDir.getChildFile ("GoaSynth.vst3");
    if (target.existsAsFile() && ! target.deleteFile())
        return "the existing plugin file is locked (close your DAW and retry)";

    if (! target.replaceWithData (pluginBlob.getData(), pluginBlob.getSize()))
        return "could not write " + target.getFullPathName();

    if (! resDir.getChildFile ("moduleinfo.json")
                .replaceWithData (moduleInfoBlob.getData(), moduleInfoBlob.getSize()))
        return "could not write moduleinfo.json";

    const juce::File standaloneExe = vst3Dir.getChildFile ("GoaSynth.exe");
    const juce::File setupExe      = vst3Dir.getChildFile ("GoaSynth-Setup.exe");

    if (! standaloneExe.replaceWithData (standaloneBlob.getData(), standaloneBlob.getSize()))
        return "could not write " + standaloneExe.getFullPathName();

    // Self-copy so Add/Remove Programs can uninstall later without the
    // original download being kept around.
    const juce::File self = juce::File::getSpecialLocation (juce::File::currentApplicationFile);
    if (self != setupExe && ! self.copyFileTo (setupExe))
        return "could not copy the uninstaller";

    writeUninstallEntry (machineWide, vst3Dir, setupExe, standaloneExe);
    installedMarker().replaceWithText (vst3Dir.getFullPathName());

    return {};
}

juce::StringArray collectInstalledPaths (juce::String& rememberedDir)
{
    rememberedDir = installedMarker().existsAsFile()
                        ? installedMarker().loadFileAsString().trim() : juce::String();

    const juce::File dir = rememberedDir.isNotEmpty() ? juce::File (rememberedDir) : standardVst3Dir();

    juce::StringArray paths;
    paths.add (dir.getChildFile ("GoaSynth.vst3").getFullPathName());
    paths.add (dir.getChildFile ("GoaSynth.exe").getFullPathName());
    paths.add (dir.getChildFile ("GoaSynth-Setup.exe").getFullPathName());
    return paths;
}

bool performUninstall (juce::String& report)
{
    juce::String remembered;
    const juce::StringArray paths = collectInstalledPaths (remembered);
    const juce::File dir = remembered.isNotEmpty() ? juce::File (remembered) : standardVst3Dir();

    removeUninstallEntry (true);    // try both hives; one is normally absent
    removeUninstallEntry (false);
    installedMarker().deleteFile();

    juce::StringArray removed, locked;
    for (const auto& p : paths)
    {
        const juce::File f (p);
        if (! f.exists()) continue;
        if (f.deleteRecursively()) removed.add (f.getFileName());
        else                       locked.add (f.getFileName());
    }

    // Remove the install dir too when we emptied it (never the shared VST3 root).
    if (dir != standardVst3Dir() && dir.exists()
        && dir.getNumberOfChildFiles (juce::File::findFilesAndDirectories) == 0)
        dir.deleteRecursively();

    report = "Removed: " + (removed.isEmpty() ? juce::String ("nothing was installed")
                                              : removed.joinIntoString (", "));
    if (! locked.isEmpty())
        report += "\nLocked (close your DAW, then run the installer again): "
                  + locked.joinIntoString (", ");

    return locked.isEmpty();
}

// ---- elevated pass ---------------------------------------------------------------

#if JUCE_WINDOWS
// Runs the SAME exe elevated with --elevated-child <dir> (UAC prompt), then
// waits for it and checks its exit code. The child leaves a human-readable
// result in a temp file for the error message.
bool runElevatedChild (const juce::File& vst3Dir)
{
    const juce::File self   = juce::File::getSpecialLocation (juce::File::currentApplicationFile);
    const juce::File report = elevationReportFile();
    report.deleteFile();

    ::SHELLEXECUTEINFOW sei {};
    sei.cbSize = sizeof (sei);
    sei.lpVerb = L"runas";                       // what triggers the UAC prompt
    sei.lpFile = self.getFullPathName().toWideCharPointer();
    const juce::String params = "--elevated-child " + quote (vst3Dir.getFullPathName());
    sei.lpParameters = params.toWideCharPointer();
    sei.nShow = SW_SHOWNORMAL;
    sei.fMask = SEE_MASK_NOCLOSEPROCESS;

    if (! ::ShellExecuteExW (&sei) || sei.hProcess == nullptr)
        return false;

    // The child only copies ~8 MB, but an antivirus scan can stretch it.
    if (::WaitForSingleObject (sei.hProcess, 60000) == WAIT_TIMEOUT)
    {
        ::CloseHandle (sei.hProcess);
        return false;
    }

    DWORD code = 1;
    ::GetExitCodeProcess (sei.hProcess, &code);
    ::CloseHandle (sei.hProcess);
    return code == 0;
}
#endif

juce::String elevatedChildError()
{
    const juce::String summary = elevationReportFile().loadFileAsString().trim();
    return summary.startsWith ("ERROR:")
             ? summary.fromFirstOccurrenceOf ("ERROR:", false, false).trim()
             : juce::String ("the administrator install did not finish");
}

juce::File parseDirArgument (const juce::String& args, const juce::String& flag)
{
    const int at = args.indexOfIgnoreCase (flag);
    juce::String rest = args.substring (at + flag.length()).trim();

    if (rest.startsWithChar ('"'))
    {
        const int end = rest.indexOfChar (1, '"');
        if (end > 0) rest = rest.substring (1, end);
    }
    else
    {
        rest = rest.upToFirstOccurrenceOf (" ", false, false);
    }

    return juce::File (rest.unquoted());
}

} // namespace

//==============================================================================
// The installer window.
namespace
{

constexpr uint32_t bgDark     = 0xff0d0b16;
constexpr uint32_t bgPanel    = 0xff16121f;
constexpr uint32_t accentA    = 0xff40e0c0;
constexpr uint32_t accentV    = 0xff9b5cff;
constexpr uint32_t textDim    = 0xff8a86a0;
constexpr uint32_t textBright = 0xfff0eef8;
constexpr uint32_t okGreen    = 0xff7de8a3;
constexpr uint32_t badRed     = 0xffe05a6a;

juce::String bytesToText (juce::int64 n)
{
    if (n >= 1024 * 1024) return juce::String (n / (1024.0 * 1024.0), 1) + " MB";
    if (n >= 1024)        return juce::String (n / 1024) + " kB";
    return juce::String (n) + " bytes";
}

struct SetupPanel : juce::Component
{
    enum class Mode { choose, working, done, failed };

    SetupPanel()
    {
        headLabel.setText ("GOASYNTH " + juce::String (GOASYNTH_VERSION) + " SETUP",
                           juce::dontSendNotification);
        headLabel.setFont (juce::Font (juce::FontOptions (11.0f, juce::Font::bold)));
        headLabel.setColour (juce::Label::textColourId, juce::Colour (accentA));
        addAndMakeVisible (headLabel);

        titleLabel.setText ("Install the GoaSynth synthesizer", juce::dontSendNotification);
        titleLabel.setFont (juce::Font (juce::FontOptions (17.0f, juce::Font::bold)));
        titleLabel.setColour (juce::Label::textColourId, juce::Colour (textBright));
        addAndMakeVisible (titleLabel);

        infoLabel.setText (juce::String ("Installs the GoaSynth.vst3 plugin for your DAW and the standalone GoaSynth app (")
                           + bytesToText (pluginBlob.getSize() + standaloneBlob.getSize())
                           + ").\nThe standalone app runs without a DAW; the plugin needs one.",
                           juce::dontSendNotification);
        infoLabel.setFont (juce::Font (juce::FontOptions (12.0f)));
        infoLabel.setColour (juce::Label::textColourId, juce::Colour (textDim));
        addAndMakeVisible (infoLabel);

        standardBtn.setButtonText ("Standard VST3 folder (all DAWs)");
        standardBtn.setClickingTogglesState (true);
        standardBtn.setRadioGroupId (777);
        standardBtn.setToggleState (true, juce::dontSendNotification);
        standardBtn.setColour (juce::ToggleButton::textColourId, juce::Colour (textBright));
        standardBtn.setColour (juce::ToggleButton::tickColourId, juce::Colour (accentA));
        standardBtn.setColour (juce::ToggleButton::tickDisabledColourId, juce::Colour (textDim));
        standardBtn.onClick = [this]
        {
            dirBox.setText (standardVst3Dir().getFullPathName(), juce::dontSendNotification);
            noteLabel.setText ("Windows will ask for administrator permission once.",
                               juce::dontSendNotification);
        };
        addAndMakeVisible (standardBtn);

        customBtn.setButtonText ("Custom folder");
        customBtn.setClickingTogglesState (true);
        customBtn.setRadioGroupId (777);
        customBtn.setColour (juce::ToggleButton::textColourId, juce::Colour (textBright));
        customBtn.setColour (juce::ToggleButton::tickColourId, juce::Colour (accentA));
        customBtn.setColour (juce::ToggleButton::tickDisabledColourId, juce::Colour (textDim));
        customBtn.onClick = [this]
        {
            noteLabel.setText ("Pick a folder your DAW scans (some hosts use their own).",
                               juce::dontSendNotification);
        };
        addAndMakeVisible (customBtn);

        dirBox.setFont (juce::Font (juce::FontOptions (12.0f)));
        dirBox.setText (standardVst3Dir().getFullPathName(), juce::dontSendNotification);
        addAndMakeVisible (dirBox);

        browseBtn.setButtonText ("BROWSE...");
        browseBtn.setColour (juce::TextButton::buttonColourId, juce::Colour (bgPanel));
        browseBtn.setColour (juce::TextButton::textColourOffId, juce::Colour (accentA));
        browseBtn.onClick = [this]
        {
            chooser = std::make_unique<juce::FileChooser> ("Choose the VST3 folder",
                                                           juce::File (dirBox.getText()));
            chooser->launchAsync (juce::FileBrowserComponent::openMode
                                    | juce::FileBrowserComponent::canSelectDirectories,
                                  [this] (const juce::FileChooser&)
            {
                const juce::File f = chooser->getResult();
                if (f != juce::File())
                {
                    dirBox.setText (f.getFullPathName(), juce::dontSendNotification);
                    customBtn.setToggleState (true, juce::dontSendNotification);
                    noteLabel.setText ("Pick a folder your DAW scans (some hosts use their own).",
                                       juce::dontSendNotification);
                }
                chooser = nullptr;
            });
        };
        addAndMakeVisible (browseBtn);

        noteLabel.setFont (juce::Font (juce::FontOptions (10.5f)));
        noteLabel.setColour (juce::Label::textColourId, juce::Colour (textDim));
        noteLabel.setText ("Windows will ask for administrator permission once.",
                           juce::dontSendNotification);
        addAndMakeVisible (noteLabel);

        statusLabel.setFont (juce::Font (juce::FontOptions (12.0f)));
        statusLabel.setJustificationType (juce::Justification::topLeft);
        statusLabel.setColour (juce::Label::textColourId, juce::Colour (textDim));
        addAndMakeVisible (statusLabel);

        installBtn.setButtonText ("INSTALL");
        installBtn.setColour (juce::TextButton::buttonColourId, juce::Colour (bgPanel));
        installBtn.setColour (juce::TextButton::textColourOffId, juce::Colour (accentA));
        installBtn.onClick = [this]
        {
            const juce::File target = juce::File (dirBox.getText().trim().unquoted());
            if (target == juce::File() || target.getFullPathName().trim().isEmpty())
            {
                setStatus (Mode::failed, "Choose a destination folder.");
                return;
            }

            setStatus (Mode::working, "Installing to " + target.getFullPathName() + " ...");

            juce::Thread::launch ([this, target]
            {
                const InstallPlan plan = planFor (target);

                juce::String err;
                if (plan.needsElevation)
                {
                   #if JUCE_WINDOWS
                    if (! runElevatedChild (target))
                        err = elevatedChildError();
                   #else
                    err = installInto (target, true);
                   #endif
                }
                else
                {
                    err = installInto (target, false);
                }

                juce::MessageManager::callAsync ([this, err, target]
                {
                    if (err.isEmpty())
                        setStatus (Mode::done,
                                   "Installed.\nPlugin:  " + target.getChildFile ("GoaSynth.vst3").getFullPathName()
                                   + "\nStandalone:  " + target.getChildFile ("GoaSynth.exe").getFullPathName()
                                   + "\nRescan plugins in your DAW, then add GoaSynth to an instrument track.");
                    else
                        setStatus (Mode::failed, err);
                });
            });
        };
        addAndMakeVisible (installBtn);

        uninstallBtn.setButtonText ("UNINSTALL");
        uninstallBtn.setColour (juce::TextButton::buttonColourId, juce::Colour (bgPanel));
        uninstallBtn.setColour (juce::TextButton::textColourOffId, juce::Colour (textDim));
        uninstallBtn.onClick = [this]
        {
            setStatus (Mode::working, "Removing ...");

            juce::Thread::launch ([this]
            {
                juce::String report;
                const bool ok = performUninstall (report);

                juce::MessageManager::callAsync ([this, ok, report]
                {
                    setStatus (ok ? Mode::done : Mode::failed, report);
                });
            });
        };
        addAndMakeVisible (uninstallBtn);

        closeBtn.setButtonText ("CLOSE");
        closeBtn.setColour (juce::TextButton::buttonColourId, juce::Colour (bgPanel));
        closeBtn.setColour (juce::TextButton::textColourOffId, juce::Colour (accentA));
        closeBtn.onClick = [] { juce::JUCEApplication::quit(); };
        addAndMakeVisible (closeBtn);
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colour (bgDark));

        g.setGradientFill (juce::ColourGradient (juce::Colour (accentV), 0.0f, 0.0f,
                                                 juce::Colour (accentA), (float) getWidth(), 0.0f, false));
        g.fillRect (0, 0, getWidth(), 2);

        g.setColour (juce::Colour (bgPanel).withAlpha (0.4f));
        g.fillRoundedRectangle (destBox.toFloat().expanded (8.0f), 6.0f);
    }

    void resized() override
    {
        auto b = getLocalBounds().reduced (20, 14);

        headLabel.setBounds  (b.removeFromTop (16));
        titleLabel.setBounds (b.removeFromTop (26));
        infoLabel.setBounds  (b.removeFromTop (36));
        b.reduce (0, 2);

        auto modeRow = b.removeFromTop (22);
        standardBtn.setBounds (modeRow.removeFromLeft (240));
        customBtn.setBounds   (modeRow.removeFromLeft (110));

        auto dirRow = b.removeFromTop (26);
        browseBtn.setBounds (dirRow.removeFromRight (100).withSizeKeepingCentre (96, 24));
        dirBox.setBounds    (dirRow);

        noteLabel.setBounds (b.removeFromTop (16));
        b.reduce (0, 2);

        auto btnRow = b.removeFromBottom (30);
        closeBtn.setBounds     (btnRow.removeFromRight (96).withSizeKeepingCentre (88, 26));
        uninstallBtn.setBounds (btnRow.removeFromRight (110).withSizeKeepingCentre (102, 26));
        installBtn.setBounds   (btnRow.removeFromRight (104).withSizeKeepingCentre (96, 26));

        statusLabel.setBounds (b.removeFromBottom (64));

        destBox = b;
    }

    void setStatus (Mode m, const juce::String& text)
    {
        juce::Colour c = juce::Colour (textDim);
        if      (m == Mode::done)    c = juce::Colour (okGreen);
        else if (m == Mode::failed)  c = juce::Colour (badRed);
        else if (m == Mode::working) c = juce::Colour (textBright);

        statusLabel.setText (text, juce::dontSendNotification);
        statusLabel.setColour (juce::Label::textColourId, c);

        installBtn.setEnabled   (m != Mode::working);
        browseBtn.setEnabled    (m != Mode::working);
        uninstallBtn.setEnabled (m != Mode::working);
    }

    juce::Label headLabel, titleLabel, infoLabel, noteLabel, statusLabel;
    juce::ToggleButton standardBtn, customBtn;
    juce::TextEditor dirBox;
    juce::TextButton browseBtn, installBtn, uninstallBtn, closeBtn;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::Rectangle<int> destBox;
};

struct SetupWindow : juce::DocumentWindow
{
    SetupWindow()
        : juce::DocumentWindow ("GoaSynth Setup", juce::Colour (bgDark),
                                juce::DocumentWindow::closeButton)
    {
        setUsingNativeTitleBar (true);
        setContentOwned (new SetupPanel(), true);
        centreWithSize (560, 360);
        setVisible (true);
    }

    void closeButtonPressed() override { juce::JUCEApplication::quit(); }
};

} // namespace

//==============================================================================
class GoaSynthSetupApp : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName()    override { return "GoaSynthSetup"; }
    const juce::String getApplicationVersion() override { return GOASYNTH_VERSION; }
    bool moreThanOneInstanceAllowed()          override { return true; }

    void initialise (const juce::String& commandLine) override
    {
        const juce::String payloadErr = loadPayload();
        if (payloadErr.isNotEmpty())
        {
            // A broken payload cannot install anything; say so and leave.
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                                                    "GoaSynth Setup", payloadErr);
            return;
        }

        const juce::String args = commandLine.trim();

        if (args.containsIgnoreCase ("--silent"))
        {
            const juce::File target = parseDirArgument (args, "--silent");
            const juce::File dir = target == juce::File() ? standardVst3Dir() : target;
            const InstallPlan plan = planFor (dir);

            juce::String err;
           #if JUCE_WINDOWS
            if (plan.needsElevation)
            {
                if (! runElevatedChild (dir))
                    err = elevatedChildError();
            }
            else
           #endif
            {
                err = installInto (dir, plan.needsElevation);
            }

            std::exit (err.isEmpty() ? 0 : 1);   // exit code is the contract
        }

        if (args.containsIgnoreCase ("--uninstall"))
        {
            juce::String report;
            const bool ok = performUninstall (report);
            std::exit (ok ? 0 : 1);
        }

        if (args.containsIgnoreCase ("--status"))
        {
            juce::String remembered;
            const juce::StringArray paths = collectInstalledPaths (remembered);
            std::exit (0);
        }

        if (args.containsIgnoreCase ("--elevated-child"))
        {
            const juce::File dir = parseDirArgument (args, "--elevated-child");
            const juce::String err = installInto (dir, true);
            elevationReportFile().replaceWithText (err.isEmpty() ? "OK" : "ERROR: " + err);
            std::exit (err.isEmpty() ? 0 : 1);
        }

        window = std::make_unique<SetupWindow>();
    }

    void shutdown() override { window = nullptr; }

private:
    std::unique_ptr<SetupWindow> window;
};

START_JUCE_APPLICATION (GoaSynthSetupApp)
