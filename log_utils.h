#pragma once

#include "date_time_utils.h"

#include <wil/resource.h>

#include <Windows.h>

#include <cstdint>
#include <string_view>


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

//! \brief Verifies whether a given file holds sensor log data.
//!
//! \param[in]      path_name Fully qualified path name to the file system
//!                           object.
//! \param[out,opt] timestamp Pointer to a `FILETIME` that receives the starting
//!                           timestamp in case of a valid sensor log file. This
//!                           argument can be a null pointer if the timestamp
//!                           is not required.
//!
//! \return Returns `true` if the file holds sensor log data, `false` otherwise.
//!         If this function returns `true`, and timestamp is a valid pointer,
//!         the starting timestamp of the sensor log is returned through this
//!         pointer.
//!
//! \remark This function makes a decision based on the first 10 bytes of the
//!         file. It verifies that it consists of a timestamp packet (type 0x00,
//!         length 0x08), followed by eight bytes interpreted as a `FILETIME`.
//!         If that matches, and the `FILETIME` falls between the years 1900 and
//!         2200, the file is assumed to hold sensor log data.
//!         This does not run a deep analysis on the remainder of the data, and
//!         subsequent interpretation can still fail.
//!         The implementation is mainly intended to be used to filter arbitrary
//!         files when populating the sensor log list interface.
//!
[[nodiscard]] inline bool is_sensor_log(::std::wstring_view const path_name,
                                        ::FILETIME* const timestamp = nullptr) noexcept
{
    wil::unique_hfile f { ::CreateFileW(path_name.data(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                        FILE_ATTRIBUTE_NORMAL, nullptr) };
    if (!f.is_valid())
    {
        return false;
    }

    // Read first two bytes (packet header)
    uint16_t header {};
    DWORD bytes_read {};
    if (!::ReadFile(f.get(), reinterpret_cast<char*>(&header), sizeof(header), &bytes_read, nullptr)
        || bytes_read != sizeof(header) || header != 0x0800)
    {
        return false;
    };

    // Read the timestamp
    uint64_t value {};
    if (!::ReadFile(f.get(), reinterpret_cast<char*>(&value), sizeof(value), &bytes_read, nullptr)
        || bytes_read != sizeof(value))
    {
        return false;
    }

    // Check whether the value falls into a sane range
    auto const datetime_min { to_uint(to_filetime(1900, 1, 1, 0, 0, 0)) };
    auto const datetime_max { to_uint(to_filetime(2200, 1, 1, 0, 0, 0)) };
    if (value < datetime_min || value > datetime_max)
    {
        return false;
    }

    // Everthing looks fine at this point

    if (timestamp != nullptr)
    {
        *timestamp = to_filetime(value);
    }

    return true;
}
