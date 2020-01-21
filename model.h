#pragma once

#include <wil/resource.h>

#include <iterator>
#include <vector>


struct data_proxy
{
    data_proxy() = delete;
    data_proxy(unsigned char const* begin, unsigned char const* end) : begin_ { begin }, end_ { end } {}
    data_proxy(data_proxy const&) = default;

    auto size() const noexcept { return ::std::distance(begin_, end_); }
    auto data() const noexcept { return begin_; };
    auto type() const noexcept { return *begin_; }

private:
    unsigned char const* begin_;
    unsigned char const* end_;
};

struct raw_data
{
    explicit raw_data(wchar_t const* path_name)
    {
        // Open file
        wil::unique_hfile f { ::CreateFileW(path_name, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                            FILE_ATTRIBUTE_NORMAL, nullptr) };
        if (!f)
        {
            THROW_LAST_ERROR();
        }

        // Create file mapping
        LARGE_INTEGER file_size {};
        THROW_IF_WIN32_BOOL_FALSE(::GetFileSizeEx(f.get(), &file_size));

        file_mapping_.reset(
            ::CreateFileMapping(f.get(), nullptr, PAGE_READONLY, file_size.HighPart, file_size.LowPart, nullptr));
        THROW_LAST_ERROR_IF(file_mapping_ == nullptr);

        // Map view
        memory_begin_
            = static_cast<unsigned char const*>(::MapViewOfFile(file_mapping_.get(), FILE_MAP_READ, 0x0, 0x0, 0x0));
        THROW_LAST_ERROR_IF_NULL(memory_begin_);
        memory_end_ = memory_begin_ + file_size.QuadPart;

        // Build directory
        auto current_pos { memory_begin_ };
        while (current_pos < memory_end_)
        {
            // Full size of packet is the size stored at offset plus the header (type: byte, size: byte).
            auto const size { *(current_pos + 1) + 2 };
            directory_.push_back({ current_pos, current_pos + size });
            current_pos += size;
        }
    }

    auto const& directory() const noexcept { return directory_; }

private:
    // std::wstring path_name_;
    ::wil::unique_handle file_mapping_;
    unsigned char const* memory_begin_;
    unsigned char const* memory_end_;
    ::std::vector<data_proxy> directory_;
};
