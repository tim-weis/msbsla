#pragma once

#include "model.h"

#include <Windows.h>

#include <cassert>
#include <iomanip>
#include <optional>
#include <sstream>
#include <string>


namespace
{

wchar_t to_hex_char(unsigned char value) { return value > 9u ? value - 10u + L'A' : value + L'0'; }

} // namespace


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

inline ::std::wstring to_hex_string(unsigned char const data)
{
    ::std::wstring result(2, L'\0');

    result[0] = ::to_hex_char(data >> 4);
    result[1] = ::to_hex_char(data & 0xF);

    return result;
}

inline ::std::wstring to_hex_string(unsigned char const* data, size_t length)
{
    assert(length > 0);

    // Fill result with space characters
    ::std::wstring result(length * 3 - 1, L' ');

    size_t src_index { 0 };
    while (src_index < length)
    {
        auto const value { data[src_index] };
        auto dest_index { src_index * 3 };
        result[dest_index] = ::to_hex_char(value >> 4);
        result[dest_index + 1] = ::to_hex_char(value & 0xF);

        ++src_index;
    }

    return result;
}

// TODO: Refactor to not use the intended-to-be-internal data_proxy
inline ::std::optional<::std::wstring> details_from_packet(::data_proxy const& packet,
                                                           ::payload_container const& package_descriptions)
{
    if (package_descriptions.contains(packet.type()))
    {
        auto const& description { package_descriptions.find(packet.type())->second };
        ::std::wstring ret { description.name.has_value() ? description.name.value() + L" " : L"<unknown>" };
        for (auto const& el : description.elements)
        {
            ret += L" {" + ::std::to_wstring(el.offset) + L":" + ::std::to_wstring(el.size) + L"} ";

            switch (el.type)
            {
            case payload_type::file_time: {
                assert(el.size == sizeof(::FILETIME));
                auto const& ft { *reinterpret_cast<::FILETIME const*>(packet.data() + k_header_size + el.offset) };
                ret += ::format_timestamp(ft);
            }
            break;

            case payload_type::ui32: {
                assert(el.size == sizeof(uint32_t));
                ret += ::std::to_wstring(*reinterpret_cast<uint32_t const*>(packet.data() + k_header_size + el.offset));
            }
            break;

            case payload_type::ui8: {
                ret += ::std::to_wstring(*(packet.data() + k_header_size + el.offset));
            }
            break;

            default:
                break;
            }
        }

        return ret;
    }

    // Unknown type
    return {};
}
