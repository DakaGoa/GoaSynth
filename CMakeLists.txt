cmake_minimum_required(VERSION 3.22)

project(GoaSynth VERSION 1.1.0 LANGUAGES C CXX)

enable_testing()   # so `ctest` picks up the RoundTripTest / FulfilTest targets

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(JUCE
    URL https://github.com/juce-framework/JUCE/archive/refs/tags/8.0.8.tar.gz
)
FetchContent_MakeAvailable(JUCE)

# Plugin formats. VST3 everywhere; AU (Logic) only on Apple, where the AU
# wrapper exists at all; LV2 on Linux. JUCE 8.0.8 has no CLAP wrapper, so CLAP
# cannot be built here without a third-party shim - see README.
# Standalone is the .exe that runs the synth without a DAW (all desktop OSes).
set(GOASYNTH_FORMATS VST3 Standalone)
if(APPLE)
    list(APPEND GOASYNTH_FORMATS AU AUv3)
elseif(UNIX)
    list(APPEND GOASYNTH_FORMATS LV2)
endif()

# Brand icon: juceaide turns these PNGs into the exe/bundle icons (Windows
# .ico inside the rc, macOS .icns, the VST3 bundle's Plugin.ico). Regenerate
# them with `python tools/make-brand.py` - see Assets/.
set(GOA_ICON_BIG   "${CMAKE_CURRENT_SOURCE_DIR}/Assets/icon_512.png")
set(GOA_ICON_SMALL "${CMAKE_CURRENT_SOURCE_DIR}/Assets/icon_32.png")
# GoaSynth-Setup carries the same face plus a teal install badge
# (make-brand.py's setup-icon_*).
set(GOA_SETUP_ICON_BIG   "${CMAKE_CURRENT_SOURCE_DIR}/Assets/setup-icon_512.png")
set(GOA_SETUP_ICON_SMALL "${CMAKE_CURRENT_SOURCE_DIR}/Assets/setup-icon_32.png")

juce_add_plugin(GoaSynth
    COMPANY_NAME "FreebuffAudio"
    PLUGIN_MANUFACTURER_CODE Frbf
    PLUGIN_CODE Gsyn
    FORMATS ${GOASYNTH_FORMATS}
    PRODUCT_NAME "GoaSynth"
    VST3_CATEGORIES Instrument Synth
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT TRUE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_AFTER_BUILD FALSE
    ICON_BIG ${GOA_ICON_BIG}
    ICON_SMALL ${GOA_ICON_SMALL}
)

target_sources(GoaSynth PRIVATE
    Source/License.cpp
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/SynthEngine.cpp
    Source/Parameters.cpp
    Source/AiPatchGen.cpp
    Source/AiCloudGen.cpp
    Source/LearnedPatches.h
)

target_compile_definitions(GoaSynth PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_MODAL_LOOPS_PERMITTED=0
)

target_link_libraries(GoaSynth
    PRIVATE
        juce::juce_audio_processors
        juce::juce_audio_formats
        juce::juce_audio_utils
        juce::juce_audio_devices
        juce::juce_dsp
        juce::juce_cryptography
        juce::juce_recommended_config_flags
)

target_include_directories(GoaSynth PRIVATE Source)

# State round-trip test (params + user waves serialize/load identically)
add_executable(RoundTripTest tests/RoundTrip.cpp)
target_link_libraries(RoundTripTest PRIVATE
    juce::juce_recommended_config_flags
    juce::juce_audio_processors
    juce::juce_dsp
    juce::juce_cryptography
)
target_include_directories(RoundTripTest PRIVATE Source tests)
target_compile_definitions(RoundTripTest PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_MODAL_LOOPS_PERMITTED=0
    GOA_TEST_BUILD=1   # use tests/TestMasterKey.h - never the seller's real key
)
target_sources(RoundTripTest PRIVATE
    Source/License.cpp
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/SynthEngine.cpp
    Source/Parameters.cpp
    Source/AiPatchGen.cpp
    Source/AiCloudGen.cpp
    Source/LearnedPatches.h
)
if (GOA_ASAN)
    target_compile_options(RoundTripTest PRIVATE /fsanitize=address)
    target_link_options(RoundTripTest PRIVATE /INCREMENTAL:NO)
    target_compile_definitions(RoundTripTest PRIVATE GOA_ASAN=1)
endif()
# Windows gives the main thread a 1 MB stack; the factory-preset table's
# static initializer (135 presets, built from a huge braced init-list) uses a
# frame large enough that calling it late in main overflowed the stack once
# the phase-18 render buffers joined main's scope. A 16 MB main stack removes
# the whole class of problem for every phase.
target_link_options(RoundTripTest PRIVATE "/STACK:16777216")
add_test(NAME RoundTripTest COMMAND RoundTripTest)

# Overlay smoke test: builds the real editor offscreen and drives the header
# buttons, asserting every overlay lands on screen with laid-out children.
# Exists because the MOD matrix shipped twice-broken (never attached, then an
# empty resized()) and nothing caught either.
add_executable(OverlayTest tests/OverlayTest.cpp)
target_link_libraries(OverlayTest PRIVATE
    juce::juce_recommended_config_flags
    juce::juce_audio_processors
    juce::juce_gui_basics
    juce::juce_dsp
    juce::juce_cryptography
)
target_include_directories(OverlayTest PRIVATE Source tests)
target_compile_definitions(OverlayTest PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_MODAL_LOOPS_PERMITTED=0
    GOA_TEST_BUILD=1   # use tests/TestMasterKey.h - never the seller's real key
    GOASYNTH_DOCS_ASSETS="${CMAKE_CURRENT_SOURCE_DIR}/docs/assets"
)
target_sources(OverlayTest PRIVATE
    Source/License.cpp
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/SynthEngine.cpp
    Source/Parameters.cpp
    Source/AiPatchGen.cpp
    Source/AiCloudGen.cpp
    Source/LearnedPatches.h
)
add_test(NAME OverlayTest COMMAND OverlayTest)

# Standalone key generator (seller-side console tool)
juce_add_console_app(GoaSynthKeygen PRODUCT_NAME "GoaSynthKeygen")
target_sources(GoaSynthKeygen PRIVATE Keygen/GoaSynthKeygen.cpp)
target_compile_definitions(GoaSynthKeygen PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_MODAL_LOOPS_PERMITTED=0
)
target_link_libraries(GoaSynthKeygen PRIVATE
    juce::juce_core
    juce::juce_cryptography
    juce::juce_recommended_config_flags
)
target_include_directories(GoaSynthKeygen PRIVATE Source)

# Order fulfilment (seller-side console tool, never shipped): reads the
# store's order exports, signs a serial per buyer through the same KeygenCore
# as the keygen, and writes the .goalicense file plus a reply-ready .eml.
juce_add_console_app(GoaSynthFulfil PRODUCT_NAME "GoaSynthFulfil")
target_sources(GoaSynthFulfil PRIVATE
    Fulfil/GoaSynthFulfil.cpp
    Fulfil/Fulfil.cpp
)
target_compile_definitions(GoaSynthFulfil PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_MODAL_LOOPS_PERMITTED=0
)
target_link_libraries(GoaSynthFulfil PRIVATE
    juce::juce_core
    juce::juce_cryptography
    juce::juce_recommended_config_flags
)
target_include_directories(GoaSynthFulfil PRIVATE Source Keygen Fulfil)

# Headless check of the REAL activation path (no GOA_TEST_BUILD: compiles
# against the seller's real LicenseKeys.h, so its verdict is the VST3's).
# Storage goes through the usual env hooks; not a ctest entry on purpose.
juce_add_console_app(VerifySerial PRODUCT_NAME "VerifySerial")
target_sources(VerifySerial PRIVATE
    tools/VerifySerial.cpp
    Source/License.cpp
)
target_compile_definitions(VerifySerial PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_MODAL_LOOPS_PERMITTED=0
)
target_link_libraries(VerifySerial PRIVATE
    juce::juce_core
    juce::juce_cryptography
    juce::juce_recommended_config_flags
)
target_include_directories(VerifySerial PRIVATE Source tools)

# Fulfilment test: runs the scan against a throwaway keypair redirected with
# GOASYNTH_KEYGEN_DIR, so the seller's real keys and ledger are never touched.
add_executable(FulfilTest tests/Fulfil.cpp Fulfil/Fulfil.cpp)
target_link_libraries(FulfilTest PRIVATE
    juce::juce_recommended_config_flags
    juce::juce_core
    juce::juce_cryptography
)
target_include_directories(FulfilTest PRIVATE Source Keygen Fulfil)
target_compile_definitions(FulfilTest PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_MODAL_LOOPS_PERMITTED=0
)
add_test(NAME FulfilTest COMMAND FulfilTest)

# Publish preflight: docs/ IS the website, so nothing seller-side or secret may
# live there. Plain C++17, no JUCE - it only reads files. Also runnable on its
# own before you push:  ./build/Release/PublishCheck
add_executable(PublishCheck tests/PublishCheck.cpp)
set_target_properties(PublishCheck PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
target_compile_definitions(PublishCheck PRIVATE
    GOA_DOCS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/docs"
)
add_test(NAME PublishCheck COMMAND PublishCheck "${CMAKE_CURRENT_SOURCE_DIR}/docs")

# Self-test for the preflight: a fresh copy of docs/ must pass, and every way of
# leaking that the checker claims to catch must be caught.
add_executable(PublishCheckTest tests/PublishCheckTest.cpp)
set_target_properties(PublishCheckTest PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
target_compile_definitions(PublishCheckTest PRIVATE
    GOA_DOCS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/docs"
)
add_test(NAME PublishCheckTest COMMAND PublishCheckTest)

# Drift guard: the site's facts (price, preset bank, AI defaults, local
# storage, trial length) must still match the plugin. PublishCheck guards leaks;
# this guards accuracy - the site went false once when the activation gate and
# the cloud AI engines landed in Source/ after the pages were written.
add_executable(DocsCheck tests/DocsCheck.cpp)
set_target_properties(DocsCheck PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
target_compile_definitions(DocsCheck PRIVATE
    GOA_DOCS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/docs"
    GOA_REPO_ROOT="${CMAKE_CURRENT_SOURCE_DIR}"
)
add_test(NAME DocsCheck COMMAND DocsCheck "${CMAKE_CURRENT_SOURCE_DIR}/docs" "${CMAKE_CURRENT_SOURCE_DIR}")

# Self-test for the drift guard: the real tree must pass, and every kind of
# drift it claims to catch must be caught.
add_executable(DocsCheckTest tests/DocsCheckTest.cpp)
set_target_properties(DocsCheckTest PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
target_compile_definitions(DocsCheckTest PRIVATE
    GOA_DOCS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/docs"
    GOA_REPO_ROOT="${CMAKE_CURRENT_SOURCE_DIR}"
)
add_test(NAME DocsCheckTest COMMAND DocsCheckTest)

# Double-click helper for .goalicense files (ships to buyers). Shares the
# plugin's License core so a double-click runs the identical activation path.
juce_add_gui_app(GoaSynthLicense PRODUCT_NAME "GoaSynthLicense" COMPANY_NAME "GoaSynth"
    ICON_BIG ${GOA_ICON_BIG}
    ICON_SMALL ${GOA_ICON_SMALL})
target_sources(GoaSynthLicense PRIVATE
    Helper/GoaSynthLicense.cpp
    Source/License.cpp
)
target_compile_definitions(GoaSynthLicense PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_MODAL_LOOPS_PERMITTED=0
)
target_link_libraries(GoaSynthLicense PRIVATE
    juce::juce_core
    juce::juce_gui_basics
    juce::juce_cryptography
    juce::juce_recommended_config_flags
)
target_include_directories(GoaSynthLicense PRIVATE Source)

# Buyer-facing installer (Windows): embeds the built VST3 bundle and the
# standalone exe as RCDATA resources and installs them to the standard VST3
# folder (elevated) or a custom folder, with Add/Remove-Programs + Start-menu
# entries and a --silent mode. Depends on both payloads, so a normal
# "cmake --build build --config Release" builds everything in order.
if (WIN32)
    # JUCE places the artefacts under <build>/GoaSynth_artefacts/<Config>/.
    # The installer targets the Release payload, like make-release.py.
    set(GOA_ARTEFACTS "${CMAKE_BINARY_DIR}/GoaSynth_artefacts/Release")
    set(GOA_PAYLOAD_PLUGIN     "${GOA_ARTEFACTS}/VST3/GoaSynth.vst3/Contents/x86_64-win/GoaSynth.vst3")
    set(GOA_PAYLOAD_MODULEINFO "${GOA_ARTEFACTS}/VST3/GoaSynth.vst3/Contents/Resources/moduleinfo.json")
    set(GOA_PAYLOAD_STANDALONE "${GOA_ARTEFACTS}/Standalone/GoaSynth.exe")

    string(REPLACE "." "," GOA_VERSION_CSV "${PROJECT_VERSION}")

    configure_file(Installer/installer.rc.in "${CMAKE_CURRENT_BINARY_DIR}/generated/installer.rc" @ONLY)

    juce_add_gui_app(GoaSynthSetup PRODUCT_NAME "GoaSynth-Setup" COMPANY_NAME "GoaSynth"
        ICON_BIG ${GOA_SETUP_ICON_BIG}
        ICON_SMALL ${GOA_SETUP_ICON_SMALL})
    target_sources(GoaSynthSetup PRIVATE
        Installer/GoaSynthSetup.cpp
        "${CMAKE_CURRENT_BINARY_DIR}/generated/installer.rc"
    )
    target_compile_definitions(GoaSynthSetup PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_MODAL_LOOPS_PERMITTED=0
        GOASYNTH_VERSION="${PROJECT_VERSION}"
    )
    target_include_directories(GoaSynthSetup PRIVATE
        Installer
        "${CMAKE_CURRENT_BINARY_DIR}/generated"
    )
    target_link_libraries(GoaSynthSetup PRIVATE
        juce::juce_core
        juce::juce_gui_basics
        juce::juce_recommended_config_flags
    )
    add_dependencies(GoaSynthSetup GoaSynth_VST3 GoaSynth_Standalone)
endif()