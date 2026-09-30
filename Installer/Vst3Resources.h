// Resource identifiers for the installer payload (see installer.rc.in).
#pragma once

#if JUCE_WINDOWS
 #include <windows.h>
#endif

namespace Vst3Res
{
    constexpr int pluginResource      = 201;   // RCDATA: Contents/x86_64-win/GoaSynth.vst3
    constexpr int moduleInfoResource  = 202;   // RCDATA: Contents/Resources/moduleinfo.json
    constexpr int standaloneResource  = 203;   // RCDATA: Standalone/GoaSynth.exe

#if JUCE_WINDOWS
    // Loads an RCDATA resource of this exe into a MemoryBlock.
    inline bool load (int resourceId, juce::MemoryBlock& dest)
    {
        const HRSRC handle = ::FindResourceW (nullptr, MAKEINTRESOURCEW (resourceId),
                                             (LPCWSTR) RT_RCDATA);
        if (handle == nullptr)
            return false;

        const HGLOBAL loaded = ::LoadResource (nullptr, handle);
        if (loaded == nullptr)
            return false;

        const void* data = ::LockResource (loaded);
        const DWORD size = ::SizeofResource (nullptr, handle);
        if (data == nullptr || size == 0)
            return false;

        dest = juce::MemoryBlock (data, (size_t) size);
        return true;
    }
#endif
}
