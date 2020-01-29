#pragma once

#include <wil/resource.h>

#include <algorithm>
#include <cassert>
#include <iterator>
#include <map>
#include <numeric>
#include <vector>


namespace
{
static constexpr auto const k_header_size { 2 };
}


// Enums to control sorting
enum struct sort_direction
{
    asc,
    desc
};

enum struct sort_predicate
{
    index,
    type,
    size
};


// Raw data access
struct data_proxy
{
    data_proxy() = delete;
    data_proxy(unsigned char const* begin, unsigned char const* end) : begin_ { begin }, end_ { end } {}
    data_proxy(data_proxy const&) = default;

    auto size() const noexcept { return ::std::distance(begin_, end_) - k_header_size; }
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

        // Initialize identity mapping (i.e. no sorting)
        sort_map_.resize(directory_.size());
        ::std::iota(begin(sort_map_), end(sort_map_), 0);
    }

    auto const& directory() const noexcept { return directory_; }
    auto const& sort_map() const noexcept
    {
        assert(sort_map_.size() == directory_.size());
        return sort_map_;
    }
    // Returns packet at index applying the current sort map
    auto const& packet(size_t const index) const noexcept
    {
        assert(index < sort_map_.size());
        auto const mapped_index { sort_map_[index] };
        assert(mapped_index < directory_.size());
        return directory_[mapped_index];
    }
    // Apply sorting
    // Defaults to natural sorting (sequential order as in the raw binary data)
    void sort(sort_predicate const pred = sort_predicate::index, sort_direction const dir = sort_direction::asc)
    {
        // Initialize sort map
        sort_map_.resize(directory_.size());
        ::std::iota(begin(sort_map_), end(sort_map_), 0);
        // Special-case sorting by index
        switch (pred)
        {
        case sort_predicate::index:
            if (dir == sort_direction::desc)
            {
                ::std::reverse(begin(sort_map_), end(sort_map_));
            }
            // Nothing to do for index/asc
            break;

        case sort_predicate::type:
            ::std::stable_sort(begin(sort_map_), end(sort_map_), [&](size_t const lhs, size_t const rhs) {
                return (dir == sort_direction::asc) ? directory_[lhs].type() < directory_[rhs].type()
                                                    : directory_[lhs].type() > directory_[rhs].type();
            });
            break;

        case sort_predicate::size:
            ::std::stable_sort(begin(sort_map_), end(sort_map_), [&](size_t const lhs, size_t const rhs) {
                return (dir == sort_direction::asc) ? directory_[lhs].size() < directory_[rhs].size()
                                                    : directory_[lhs].size() > directory_[rhs].size();
            });
            break;

        default:
            assert(!"Unexpected sort_predicate; update this method whenever sort_predicate changes.");
            break;
        }
    }

private:
    // std::wstring path_name_;
    ::wil::unique_handle file_mapping_;
    unsigned char const* memory_begin_;
    unsigned char const* memory_end_;
    ::std::vector<data_proxy> directory_;
    // Mapping for sorting; each element sort_map_[n] stores the index into the directory, given the current sorting.
    ::std::vector<size_t> sort_map_;
};


// Packet information handling
enum struct payload_type
{
    unknown,
    ui8,
    ui32,
    file_time,

    last_value = file_time
};

struct payload_element
{
    size_t offset;
    size_t size;
    payload_type type;
};

using payload_elements_container = ::std::vector<::payload_element>;
using payload_container = ::std::map<unsigned char, payload_elements_container>;


// Declare actual model for use by clients
struct model
{
    explicit model(wchar_t const* path_name) : data_ { path_name }
    {
        // TODO: Read known types from file; initializing with a constant collection for testing
        known_types_[0x00].emplace_back(::payload_element { 0, 8, payload_type::file_time });
        known_types_[0x0f].emplace_back(::payload_element { 0, 4, payload_type::ui32 });
    }

    auto& data() noexcept { return data_; }
    auto const& data() const noexcept { return data_; }
    auto const& known_types() const noexcept { return known_types_; }

private:
    raw_data data_;
    payload_container known_types_;
};
