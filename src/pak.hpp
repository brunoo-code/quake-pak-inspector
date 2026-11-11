#pragma once

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <span>
#include <string>

namespace pak {

struct Entry {
    std::uint32_t offset;
    std::uint32_t size;
};

class Archive {
public:
    explicit Archive(const std::filesystem::path& path);

    const auto& entries() const { return entries_; }
    void read(const Entry& entry, std::uint32_t start, std::span<char> destination);

private:
    void read_at(std::uint64_t offset, std::span<char> destination);

    std::ifstream file_;
    std::map<std::string, Entry, std::less<>> entries_;
};

bool same_content(Archive& left, const Entry& a, Archive& right, const Entry& b);

}
