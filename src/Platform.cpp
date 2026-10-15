#include "Platform.h"

#if defined(_WIN32)
// Kept in its own translation unit so windows.h never meets the OpenGL
// headers in main.cpp (APIENTRY clashes) and its min/max macros can't
// leak anywhere else.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <vector>

std::filesystem::path ExecutableDirectory() {
    std::vector<wchar_t> buf(MAX_PATH);
    for (;;) {
        DWORD n = GetModuleFileNameW(nullptr, buf.data(), static_cast<DWORD>(buf.size()));
        if (n == 0) return {};
        if (n < buf.size()) return std::filesystem::path(std::wstring(buf.data(), n)).parent_path();
        buf.resize(buf.size() * 2); // path longer than the buffer: grow and retry
    }
}

#elif defined(__linux__)
#include <system_error>

std::filesystem::path ExecutableDirectory() {
    std::error_code ec;
    auto exe = std::filesystem::read_symlink("/proc/self/exe", ec);
    return ec ? std::filesystem::path{} : exe.parent_path();
}

#else
// macOS/other: not yet supported - ExoSim then keeps the launch directory.
std::filesystem::path ExecutableDirectory() { return {}; }
#endif
