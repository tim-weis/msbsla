#pragma once

#include <wil/resource.h>

#include <Windows.h>

#include <cstdint>


/// Returns timestamp recorded as the first entry in a sensor log capture.
inline FILETIME get_start_timestamp(wchar_t const* path_name)
{
    wil::unique_hfile f { ::CreateFileW(path_name, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                        FILE_ATTRIBUTE_NORMAL, nullptr) };
    THROW_LAST_ERROR_IF(!f.is_valid());
    // Skip first 2 bytes
    ::SetFilePointer(f.get(), 2, nullptr, FILE_BEGIN);

    uint64_t timestamp {};
    DWORD bytes_read {};
    THROW_IF_WIN32_BOOL_FALSE(
        ::ReadFile(f.get(), reinterpret_cast<char*>(&timestamp), sizeof(timestamp), &bytes_read, nullptr));
    FILETIME const ft { .dwLowDateTime = static_cast<DWORD>(timestamp),
                        .dwHighDateTime = static_cast<DWORD>(timestamp >> 32) };

    return ft;
}
