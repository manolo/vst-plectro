// Windows only: let the plugin load in a host that opens it with a plain LoadLibraryW.
//
// The VST3 SDK's own module loader (public.sdk/source/vst/hosting/module_win32.cpp, which is what
// MuseScore 4.x uses) calls LoadLibraryW with no flags. Windows then resolves the plugin's
// dependent DLLs against the host executable's directory, the system directories and PATH, and
// never against the directory the plugin itself was loaded from. FluidSynth and its glib chain
// ride inside the bundle, beside the plugin binary, so that load fails with ERROR_MOD_NOT_FOUND
// and the host writes the plugin off as incompatible.
//
// So the plugin must not depend on the loader finding those DLLs on its own. FluidSynth is delay
// loaded (see the /DELAYLOAD link option in CMakeLists.txt), which leaves it out of the import
// table, and the hook below resolves it at the first call by absolute path from this module's own
// directory. LOAD_WITH_ALTERED_SEARCH_PATH makes that directory the first place the loader looks
// for the DLL's own dependencies too, so glib and the rest resolve from beside it.
//
// Deliberately not SetDllDirectory, AddDllDirectory, SetDefaultDllDirectories or a PATH change:
// each of those changes the search for the whole host process, and can break other plugins or the
// host itself.

#include <windows.h>

#include <delayimp.h>

#include <string>

namespace
{
// The directory this very module was loaded from, with a trailing separator. Empty if it cannot be
// determined, which leaves the caller to fall back on the loader's own search.
std::wstring moduleDirectory()
{
    HMODULE self = nullptr;
    if (!GetModuleHandleExW (GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS
                                 | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                             reinterpret_cast<LPCWSTR> (&moduleDirectory),
                             &self))
        return {};

    std::wstring path (MAX_PATH, L'\0');
    for (;;)
    {
        const DWORD written = GetModuleFileNameW (self, path.data(), static_cast<DWORD> (path.size()));
        if (written == 0)
            return {};
        if (written < path.size()) // the whole path fit, so it is not truncated
        {
            path.resize (written);
            break;
        }
        path.resize (path.size() * 2);
    }

    const auto separator = path.find_last_of (L"\\/");
    if (separator == std::wstring::npos)
        return {};

    path.resize (separator + 1);
    return path;
}

// The name the delay load helper hands us is narrow, and the path we build has to be wide so it
// survives a bundle sitting under a non ASCII user directory.
std::wstring widen (const char* narrow)
{
    const int length = MultiByteToWideChar (CP_ACP, 0, narrow, -1, nullptr, 0);
    if (length <= 1)
        return {};

    std::wstring wide (static_cast<size_t> (length), L'\0');
    MultiByteToWideChar (CP_ACP, 0, narrow, -1, wide.data(), length);
    wide.resize (static_cast<size_t> (length) - 1); // drop the terminator it wrote
    return wide;
}

FARPROC WINAPI delayLoadHook (unsigned reason, DelayLoadInfo* info)
{
    // Every other notification, and any failure to work out where we are, falls through to the
    // helper's default behaviour. Returning null here is not an error.
    if (reason != dliNotePreLoadLibrary || info == nullptr || info->szDll == nullptr)
        return nullptr;

    const std::wstring directory = moduleDirectory();
    const std::wstring name = widen (info->szDll);
    if (directory.empty() || name.empty())
        return nullptr;

    const std::wstring full = directory + name;
    return reinterpret_cast<FARPROC> (LoadLibraryExW (full.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH));
}
} // namespace

// delayimp.h declares this as a const extern "C" hook, and the helper in delayimp.lib reads it on
// the first delay loaded call. Defining it here replaces the library's own null default, so no
// header of ours has to be included anywhere. The const matters: without it MSVC rejects the
// definition as a redefinition with different type modifiers.
const PfnDliHook __pfnDliNotifyHook2 = delayLoadHook;
