#pragma once

#include "date_time_utils.h"
#include "model.h"

#include <Windows.h>

#include <cassert>
#include <format>
#include <optional>
#include <span>
#include <string>


//! \brief Creates a human readable string representation for a timestamp value
//!        according to [ISO 8601](https://en.wikipedia.org/wiki/ISO_8601).
//!
//! \param[in] timestamp The timestamp value to format. This value is assumed to
//!                      be in UTC.
//!
//! \return The string representation of the given timestamp value down to
//!         millisecond precision.
//!
[[nodiscard]] inline ::std::wstring to_iso8601(::FILETIME const& timestamp)
{
    auto const st { to_systemtime(timestamp) };

    return ::std::format(L"{:04d}-{:02d}-{:02d}T{:02d}:{:02d}:{:02d}.{:03d}Z", st.wYear, st.wMonth, st.wDay, st.wHour,
                         st.wMinute, st.wSecond, st.wMilliseconds);
}

//! \brief Converts a byte value into its hexadecimal string representation.
//!
//! \param[in] value The byte value to convert.
//!
//! \return The hexadecimal string representation of the byte value.
//!
//! \remark This function uses uppercase hex digits. The result is zero-padded
//!         in case the byte value is less than 16. It isn't otherwise prefixed
//!         (e.g. with `0x`), and the returned string is guaranteed to be of
//!         length 2.
//!
[[nodiscard]] inline constexpr ::std::wstring to_hex_string(unsigned char const value)
{
    constexpr auto to_hex_digit = [](unsigned char const value) noexcept -> wchar_t {
        constexpr auto& digits = L"0123456789ABCDEF";
        return digits[value & 0xF];
    };

    return { to_hex_digit(value >> 4), to_hex_digit(value & 0xF) };
}

//! \brief Converts a byte buffer into its hexadecimal string representation.
//!
//! \param[in] data A view into the byte buffer to convert. This span may be
//!                 empty.
//!
//! \return If the input designates an emtpy span, this function returns an
//!         empty string. Otherwise, this function returns a string where each
//!         byte is represented by two hexadecimal digits. Individual bytes are
//!         delimited by a space character.
//!
[[nodiscard]] inline constexpr ::std::wstring to_hex_string(::std::span<unsigned char const> const data)
{
    ::std::wstring result {};

    if (data.size() > 0)
    {
        // Reserve enough room up front; 2 hex digits per value plus a delimiter
        // (except for the final one)
        result.reserve(data.size() * 3 - 1);
        for (auto const value : data)
        {
            // Append delimiter unless this is the first value
            if (!result.empty())
            {
                result.append(L" ");
            }
            result.append(to_hex_string(value));
        }
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
                auto const& ft { *reinterpret_cast<::FILETIME const*>(packet.data() + packet.header_size()
                                                                      + el.offset) };
                ret += ::to_iso8601(ft);
            }
            break;

            case payload_type::ui32: {
                assert(el.size == sizeof(uint32_t));
                ret += ::std::to_wstring(
                    *reinterpret_cast<uint32_t const*>(packet.data() + packet.header_size() + el.offset));
            }
            break;

            case payload_type::ui16: {
                assert(el.size == sizeof(uint16_t));
                assert(el.offset + el.size <= packet.payload_size());
                ret += ::std::to_wstring(
                    *reinterpret_cast<uint16_t const*>(packet.data() + packet.header_size() + el.offset));
            }
            break;

            case payload_type::ui8: {
                ret += ::std::to_wstring(*(packet.data() + packet.header_size() + el.offset));
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
