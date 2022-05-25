#pragma once

#include <wil/resource.h>

#include <Windows.h>

#include <cstdint>


//! \brief Returns the timestamp of a sensor log file.
//!
//! \param[in] path_name Fully qualified path name to the sensor log file.
//!
//! \return The timestamp when recording into the provided sensor log file
//!         started. The timestamp is (presumably) in UTC as observed by the MS
//!         Band device.
//!
[[nodiscard]] inline FILETIME get_start_timestamp(wchar_t const* path_name)
{
    wil::unique_hfile f { ::CreateFileW(path_name, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                        FILE_ATTRIBUTE_NORMAL, nullptr) };
    THROW_LAST_ERROR_IF(!f.is_valid());
    // Skip first 2 bytes (packet type and length)
    THROW_LAST_ERROR_IF(::SetFilePointer(f.get(), 2, nullptr, FILE_BEGIN) == INVALID_SET_FILE_POINTER);

    uint64_t timestamp {};
    DWORD bytes_read {};
    THROW_IF_WIN32_BOOL_FALSE(
        ::ReadFile(f.get(), reinterpret_cast<char*>(&timestamp), sizeof(timestamp), &bytes_read, nullptr));
    THROW_LAST_ERROR_IF(bytes_read != sizeof(timestamp));

    FILETIME const ft { .dwLowDateTime = static_cast<DWORD>(timestamp),
                        .dwHighDateTime = static_cast<DWORD>(timestamp >> 32) };

    return ft;
}
