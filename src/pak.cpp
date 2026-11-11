#include "pak.hpp"

#include <algorithm>
#include <array>
#include <stdexcept>
#include <string_view>

namespace pak {
namespace {

constexpr std::uint32_t header_size = 12;
constexpr std::uint32_t record_size = 64;
constexpr std::size_t name_size = 56;
constexpr std::uint32_t max_entries = 65536;

std::uint32_t little_u32(std::span<const char, 4> bytes) {
    std::uint32_t result = 0;
    for (unsigned i = 0; i < bytes.size(); ++i) {
        result |= static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[i])) << (i * 8);
    }
    if (result > 0x7fffffffU) {
        throw std::runtime_error("Negative 32-bit offset or length");
    }
    return result;
}

std::string read_name(std::span<const char, name_size> bytes) {
    const auto end = std::ranges::find(bytes, '\0');
    if (end == bytes.begin() || end == bytes.end()) {
        throw std::runtime_error("Empty or unterminated entry name");
    }
    if (std::any_of(bytes.begin(), end, [](unsigned char c) { return c < 32 || c >= 127; })) {
        throw std::runtime_error("Entry name contains a non-ASCII or control character");
    }
    return {bytes.begin(), end};
}

}

Archive::Archive(const std::filesystem::path& path)
    : file_(path, std::ios::binary | std::ios::ate) {
    if (!file_) {
        throw std::runtime_error("Cannot open archive");
    }
    const auto end = file_.tellg();
    if (end < header_size) {
        throw std::runtime_error("Truncated PACK header");
    }
    const auto file_size = static_cast<std::uint64_t>(end);
    std::array<char, header_size> header{};
    read_at(0, header);
    if (std::string_view(header.data(), 4) != "PACK") {
        throw std::runtime_error("Expected Quake PACK signature");
    }

    const std::span header_bytes{header};
    const auto directory = little_u32(header_bytes.subspan<4, 4>());
    const auto length = little_u32(header_bytes.subspan<8, 4>());
    const auto directory_end = static_cast<std::uint64_t>(directory) + length;
    if (directory < header_size || directory_end > file_size ||
        length % record_size != 0 || length / record_size > max_entries) {
        throw std::runtime_error("Invalid PACK directory range or record count");
    }

    std::array<char, record_size> record{};
    const std::span bytes{record};
    for (std::uint64_t cursor = directory; cursor < directory_end; cursor += record_size) {
        read_at(cursor, record);
        auto name = read_name(bytes.first<name_size>());
        const Entry entry{little_u32(bytes.subspan<56, 4>()), little_u32(bytes.subspan<60, 4>())};
        const auto entry_end = static_cast<std::uint64_t>(entry.offset) + entry.size;
        if (entry_end > file_size || (entry.size != 0 &&
            (entry.offset < header_size ||
             (length != 0 && entry.offset < directory_end && entry_end > directory)))) {
            throw std::runtime_error("Invalid data range for " + name);
        }
        if (!entries_.emplace(std::move(name), entry).second) {
            throw std::runtime_error("Duplicate entry name in PACK directory");
        }
    }
}

void Archive::read_at(std::uint64_t offset, std::span<char> destination) {
    file_.seekg(static_cast<std::streamoff>(offset));
    file_.read(destination.data(), static_cast<std::streamsize>(destination.size()));
    if (!file_) {
        throw std::runtime_error("Could not read the requested archive bytes");
    }
}

void Archive::read(const Entry& entry, std::uint32_t start, std::span<char> destination) {
    if (start > entry.size || destination.size() > entry.size - start) {
        throw std::runtime_error("Read would exceed entry size");
    }
    if (!destination.empty()) {
        read_at(static_cast<std::uint64_t>(entry.offset) + start, destination);
    }
}

bool same_content(Archive& left, const Entry& a, Archive& right, const Entry& b) {
    if (a.size != b.size) {
        return false;
    }
    std::array<char, 64 * 1024> left_buffer{}, right_buffer{};
    for (std::uint32_t offset = 0; offset < a.size;) {
        const auto count = std::min(static_cast<std::uint32_t>(left_buffer.size()), a.size - offset);
        auto first = std::span{left_buffer}.first(count);
        auto second = std::span{right_buffer}.first(count);
        left.read(a, offset, first);
        right.read(b, offset, second);
        if (!std::ranges::equal(first, second)) {
            return false;
        }
        offset += count;
    }
    return true;
}

}
