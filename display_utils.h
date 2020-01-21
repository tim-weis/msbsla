#pragma once

#include <Windows.h>

#include <iomanip>
#include <sstream>
#include <string>


inline ::std::wstring format_timestamp(::FILETIME const& timestamp)
{
    ::SYSTEMTIME st {};
    ::FileTimeToSystemTime(&timestamp, &st);

    ::std::wostringstream oss {};

    oss << std::setfill(L'0') << std::setw(4) << st.wYear << L"-" << std::setfill(L'0') << std::setw(2) << st.wMonth
        << L"-" << std::setfill(L'0') << std::setw(2) << st.wDay << L" " << std::setfill(L'0') << std::setw(2)
        << st.wHour << L":" << std::setfill(L'0') << std::setw(2) << st.wMinute << L":" << std::setfill(L'0')
        << std::setw(2) << st.wSecond << L"." << std::setfill(L'0') << std::setw(3) << st.wMilliseconds;

    return oss.str();
}
