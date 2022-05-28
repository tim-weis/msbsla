#pragma once


#include <wil/result.h>

#include <Windows.h>

#include <cstdint>


[[nodiscard]] consteval inline auto invalid_filetime() noexcept
{
    return ::FILETIME { .dwLowDateTime = 0xFFFFFFFF, .dwHighDateTime = 0xFFFFFFFF };
}


[[nodiscard]] constexpr inline auto operator==(::FILETIME const lhs, ::FILETIME const rhs) noexcept
{
    return (lhs.dwLowDateTime == rhs.dwLowDateTime) && (lhs.dwHighDateTime == rhs.dwHighDateTime);
}


// Converts a time point to a filetime value. Month and day are 1-based; hour, minute, and second are 0-based.
[[nodiscard]] inline auto to_filetime(uint16_t year, uint16_t month, uint16_t day, uint16_t hour, uint16_t minute,
                                      uint16_t second)
{
    ::SYSTEMTIME const st {
        .wYear = year, .wMonth = month, .wDay = day, .wHour = hour, .wMinute = minute, .wSecond = second
    };

    ::FILETIME ft {};
    THROW_IF_WIN32_BOOL_FALSE(::SystemTimeToFileTime(&st, &ft));

    return ft;
}


[[nodiscard]] constexpr inline auto to_filetime(uint64_t timestamp) noexcept
{
    FILETIME const ft { .dwLowDateTime = static_cast<DWORD>(timestamp),
                        .dwHighDateTime = static_cast<DWORD>(timestamp >> 32) };
    return ft;
}


[[nodiscard]] constexpr inline auto to_uint(::FILETIME ft) noexcept
{
    return (static_cast<uint64_t>(ft.dwHighDateTime) << 32) | static_cast<uint64_t>(ft.dwLowDateTime);
}


[[nodiscard]] inline auto to_systemtime(::FILETIME ft)
{
    ::SYSTEMTIME st {};
    THROW_IF_WIN32_BOOL_FALSE(::FileTimeToSystemTime(&ft, &st));
    return st;
}
