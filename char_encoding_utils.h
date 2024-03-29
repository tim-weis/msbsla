#pragma once


#include <Windows.h>

#include <wil/result.h>

#include <cassert>
#include <string>
#include <string_view>


//! \brief Re-encodes a UTF-16 encoded string into a UTF-8 encoded string.
//!
//! \param[in] utf16_str A string view into a UTF-16 encoded string.
//!
//! \return A UTF-8 encoded string equivalent to the provided input. This
//!         function fails with an exception in case the input isn't well-formed
//!         UTF-8.
//!
[[nodiscard]] inline auto to_utf8(::std::wstring_view const utf16_str)
{
    auto const len_required { ::WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, utf16_str.data(),
                                                    static_cast<int>(utf16_str.length()), nullptr, 0, nullptr,
                                                    nullptr) };
    THROW_LAST_ERROR_IF(len_required == 0);

    ::std::string utf8(len_required, '\0');
    auto const bytes_written { ::WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, utf16_str.data(),
                                                     static_cast<int>(utf16_str.length()), utf8.data(),
                                                     static_cast<int>(utf8.length()), nullptr, nullptr) };
    THROW_LAST_ERROR_IF(bytes_written != len_required);

    return utf8;
}

//! \brief Re-encodes a UTF-8 encoded string into a UTF-16 encoded string.
//!
//! \param[in] utf8_str A string view into a UTF-8 encoded string.
//!
//! \return A UTF-16 encoded string equivalent to the provided input. This
//!         function fails with an exception in case the input isn't well-formed
//!         UTF-16.
//!
[[nodiscard]] inline auto to_utf16(::std::string_view const utf8_str)
{
    auto const len_required { ::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8_str.data(),
                                                    static_cast<int>(utf8_str.length()), nullptr, 0) };
    THROW_LAST_ERROR_IF(len_required == 0);

    ::std::wstring utf16(len_required, L'\0');
    auto const bytes_written { ::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8_str.data(),
                                                     static_cast<int>(utf8_str.length()), utf16.data(),
                                                     static_cast<int>(utf16.length())) };
    THROW_LAST_ERROR_IF(bytes_written != len_required);

    return utf16;
}
