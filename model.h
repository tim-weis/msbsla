#pragma once

#include "char_encoding_utils.h"
#include "date_time_utils.h"

#include <nlohmann/json.hpp>
#include <wil/resource.h>

#include <algorithm>
#include <cassert>
#include <fstream>
#include <iterator>
#include <map>
#include <numeric>
#include <optional>
#include <string>
#include <type_traits>
#include <vector>


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

    [[nodiscard]] static consteval size_t header_size() noexcept
    {
        return sizeof(unsigned char) + sizeof(unsigned char);
    }
    [[nodiscard]] auto payload_size() const noexcept { return ::std::distance(begin_, end_) - header_size(); }
    [[nodiscard]] auto data() const noexcept { return begin_; };
    // Return typed value at specific offset. The offset is relative to the payload.
    template <typename T>
    [[nodiscard]] std::add_const_t<T> value(size_t const offset) const noexcept
    {
        assert(offset + sizeof(T) <= payload_size());
        return *reinterpret_cast<std::add_const_t<T>*>(begin_ + header_size() + offset);
    }
    [[nodiscard]] auto type() const noexcept { return *begin_; }

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

    [[nodiscard]] auto const& directory() const noexcept { return directory_; }

private:
    // std::wstring path_name_;
    ::wil::unique_handle file_mapping_;
    unsigned char const* memory_begin_;
    unsigned char const* memory_end_;
    ::std::vector<data_proxy> directory_;
};


// Packet information handling
enum struct payload_type
{
    unknown,
    ui8,
    ui16,
    ui32,
    file_time,

    last_value = file_time
};

struct payload_element
{
    size_t offset;
    size_t size;
    payload_type type;
    ::std::optional<::std::wstring> comment;
};

using payload_elements_container = ::std::vector<::payload_element>;

struct packet_description
{
    ::std::optional<::std::wstring> name;
    // TODO: Add field to allow users to provide a confidence level (unknown .. known beyond doubt)
    ::payload_elements_container elements;
};

using payload_container = ::std::map<unsigned char, ::packet_description>;


NLOHMANN_JSON_SERIALIZE_ENUM(::payload_type, { { ::payload_type::unknown, nullptr },
                                               { ::payload_type::ui8, "ui8" },
                                               { ::payload_type::ui16, "ui16" },
                                               { ::payload_type::ui32, "ui32" },
                                               { ::payload_type::file_time, "file_time" } })


// Declare actual model for use by clients
struct model
{
    explicit model(wchar_t const* path_name) : data_ { path_name }
    {
        // Initialize filter
        filter_.resize(data_.directory().size());
        ::std::iota(begin(filter_), end(filter_), 0);

        // TEMP --- VVV --- Filtering on a specific date/time range
        // auto const tp_from { ::to_uint(::to_filetime(2019, 5, 30, 6, 0, 0)) };
        // auto const tp_to { ::to_uint(::to_filetime(2019, 5, 30, 7, 0, 0)) };

        // size_t index_from { 0 };
        // size_t index_to { data_.directory().size() };

        // size_t index_current { 0 };
        // for (auto const& packet : data_.directory())
        //{
        //    // Filter on [TIMESTAMP] packets only for now
        //    if (packet.type() == 0x0)
        //    {
        //        auto const time_stamp { *reinterpret_cast<uint64_t const*>(packet.data() + packet.header_size()) };

        //        if (time_stamp < tp_from)
        //        {
        //            index_from = index_current;
        //        }

        //        if (time_stamp >= tp_to)
        //        {
        //            index_to = index_current;
        //            break;
        //        }
        //    }

        //    ++index_current;
        //}

        // filter_.resize(index_to - index_from);
        //::std::iota(begin(filter_), end(filter_), index_from);
        // TEMP --- AAA

        // Initialize sort mapping
        sort_map_ = filter_;

        // Read (known) packet descriptions from JSON file. The JSON file needs to have the following layout:

        // { "descriptions": [
        //   { "type": "0x00",      /* type: string (needs to be a string due to numbers not supporting hex) */
        //     "name": "[name]",    /* name: string (optional) */
        //     "elements": [
        //       { "offset": 0,     /* offset: number */
        //         "length": 8,     /* length: number */
        //         "display_type": "file_time",     /* display_type: string (serialized ::payload_type enumeration) */
        //         "comment": "<some comment>"      /* comment: string (optional) */
        //       },
        //       ...
        //     ]
        //   },
        //   ...
        // ]}

        // TODO: Prepend with executable path
        auto ifs { ::std::ifstream { L"packet_descriptions.json" } };
        ::nlohmann::json j {};
        ifs >> j;
        for (auto const& descr : j.at("descriptions"))
        {
            // Read index; this is stored as a string because JSON doesn't support hexadecimal encoding.
            size_t const index { static_cast<size_t const>(::std::stoll(descr.at("type").get<::std::string>(), 0, 0)) };
            auto& packet_description { packet_descriptions_[static_cast<uint8_t>(index)] };

            // Set optional name
            if (auto name_it { descr.find("name") }; name_it != end(descr))
            {
                packet_description.name = ::to_utf16(name_it->get<::std::string>());
            }

            // Append elements list
            for (auto const& element : descr.at("elements"))
            {
                auto const offset { element.at("offset").get<size_t>() };
                auto const length { element.at("length").get<size_t>() };
                auto const display_type { element.at("display_type").get<::payload_type>() };
                auto const comment { element.contains("comment") ? ::std::optional<::std::wstring> { ::to_utf16(
                                         element.at("comment").get<::std::string>()) }
                                                                 : ::std::nullopt };

                packet_description.elements.emplace_back(::payload_element { offset, length, display_type, comment });
            }
        }
    }

    // Returns packet at index applying the current sort map
    [[nodiscard]] auto const& packet(size_t const index) const noexcept
    {
        assert(index < sort_map_.size());
        auto const mapped_index { sort_map_[index] };
        assert(mapped_index < data_.directory().size());
        return data_.directory()[mapped_index];
    }

    // Returns the mapped index (after filtering and sorting is applied)
    [[nodiscard]] auto packet_index(size_t const index) const noexcept
    {
        assert(index < sort_map_.size());
        return sort_map_[index];
    }

    [[nodiscard]] auto packet_count() const noexcept { return sort_map_.size(); }

    // auto& data() noexcept { return data_; }
    // auto const& data() const noexcept { return data_; }
    [[nodiscard]] auto const& packet_descriptions() const noexcept { return packet_descriptions_; }

    // Apply sorting
    // Defaults to natural sorting (sequential order as in the raw binary data)
    void sort(sort_predicate const pred = sort_predicate::index, sort_direction const dir = sort_direction::asc)
    {
        assert(filter_.size() > 0 && filter_.size() <= data_.directory().size());
        // Initialize sort map
        sort_map_ = filter_;
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
                return (dir == sort_direction::asc) ? data_.directory()[lhs].type() < data_.directory()[rhs].type()
                                                    : data_.directory()[lhs].type() > data_.directory()[rhs].type();
            });
            break;

        case sort_predicate::size:
            ::std::stable_sort(begin(sort_map_), end(sort_map_), [&](size_t const lhs, size_t const rhs) {
                return (dir == sort_direction::asc)
                           ? data_.directory()[lhs].payload_size() < data_.directory()[rhs].payload_size()
                           : data_.directory()[lhs].payload_size() > data_.directory()[rhs].payload_size();
            });
            break;

        default:
            assert(!"Unexpected sort_predicate; update this method whenever sort_predicate changes.");
            break;
        }
    }

private:
    raw_data data_;
    payload_container packet_descriptions_;
    // Sorted (and filtered) index container
    ::std::vector<size_t> sort_map_;
    // Filtered index container (this will be used for sorting again)
    ::std::vector<size_t> filter_;
};
