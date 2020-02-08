#pragma once


#include <wil/result.h>

#include <Windows.h>

#include <cstdint>


constexpr auto invalid_filetime() noexcept
{
    return ::FILETIME { .dwLowDateTime = 0xFFFFFFFF, .dwHighDateTime = 0xFFFFFFFF };
}


constexpr auto operator==(::FILETIME const lhs, ::FILETIME const rhs) noexcept
{
    return (lhs.dwLowDateTime == rhs.dwLowDateTime) && (lhs.dwHighDateTime == rhs.dwHighDateTime);
}


// Converts a time point to a filetime value. Month and day are 1-based; hour, minute, and second are 0-based.
auto to_filetime(uint16_t year, uint16_t month, uint16_t day, uint16_t hour, uint16_t minute, uint16_t second)
{
    ::SYSTEMTIME const st {
        .wYear = year, .wMonth = month, .wDay = day, .wHour = hour, .wMinute = minute, .wSecond = second
    };

    ::FILETIME ft {};
    THROW_IF_WIN32_BOOL_FALSE(::SystemTimeToFileTime(&st, &ft));

    return ft;
}


constexpr auto to_uint(::FILETIME const ft) noexcept
{
    ::ULARGE_INTEGER uli { .LowPart = ft.dwLowDateTime, .HighPart = ft.dwHighDateTime };
    return uint64_t { uli.QuadPart };
}


auto to_systemtime(::FILETIME const ft)
{
    ::SYSTEMTIME st {};
    THROW_IF_WIN32_BOOL_FALSE(::FileTimeToSystemTime(&ft, &st));
    return st;
}
