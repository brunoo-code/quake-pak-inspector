#include "pak.hpp"

#include <algorithm>
#include <array>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace {

void list(pak::Archive& archive) {
    std::cout << "offset\tbytes\tpath\n";
    for (const auto& [name, entry] : archive.entries()) {
        std::cout << entry.offset << '\t' << entry.size << '\t' << name << '\n';
    }
}

void inspect(pak::Archive& archive, std::string_view name) {
    const auto entry = archive.entries().find(name);
    if (entry == archive.entries().end()) {
        throw std::runtime_error("Entry not found");
    }
    const auto [offset, size] = entry->second;
    std::array<char, 128> preview{};
    const auto count = std::min(preview.size(), static_cast<std::size_t>(size));
    archive.read(entry->second, 0, std::span{preview}.first(count));
    std::cout << entry->first << " | offset " << offset << " | " << size
              << " bytes | preview " << count << " bytes\n";
    std::cout << std::hex << std::setfill('0');
    for (std::size_t row = 0; row < count; row += 16) {
        std::cout << std::setw(8) << offset + row << "  ";
        const auto row_size = std::min<std::size_t>(16, count - row);
        for (std::size_t i = 0; i < 16; ++i) {
            if (i < row_size) {
                std::cout << std::setw(2)
                          << static_cast<unsigned>(static_cast<unsigned char>(preview[row + i])) << ' ';
            } else {
                std::cout << "   ";
            }
        }
        std::cout << " |";
        for (std::size_t i = 0; i < row_size; ++i) {
            const auto c = static_cast<unsigned char>(preview[row + i]);
            std::cout << (c >= 32 && c <= 126 ? static_cast<char>(c) : '.');
        }
        std::cout << "|\n";
    }
}

void diff(pak::Archive& before, pak::Archive& after) {
    for (const auto& [name, entry] : before.entries()) {
        const auto updated = after.entries().find(name);
        if (updated == after.entries().end()) {
            std::cout << "REMOVED\t" << name << '\n';
        } else if (!pak::same_content(before, entry, after, updated->second)) {
            std::cout << "CHANGED\t" << name << '\n';
        }
    }
    for (const auto& [name, entry] : after.entries()) {
        if (!before.entries().contains(name)) {
            std::cout << "ADDED\t" << name << '\n';
        }
    }
}

void overlay(std::string_view name, std::span<char*> paths) {
    std::vector<std::string_view> matches;
    for (const auto* path : paths) {
        pak::Archive archive(path);
        if (archive.entries().contains(name)) {
            matches.emplace_back(path);
        }
    }
    if (matches.empty()) {
        throw std::runtime_error("Entry not found in any layer");
    }
    // Print in load order, with the last match taking precedence.
    for (std::size_t i = 0; i < matches.size(); ++i) {
        std::cout << (i + 1 == matches.size() ? "ACTIVE" : "SHADOWED") << '\t' << matches[i] << '\n';
    }
}

void usage(std::ostream& out) {
    out << "Quake PAK Inspector\n"
           "  pak-inspector list FILE.pak\n"
           "  pak-inspector inspect FILE.pak ENTRY\n"
           "  pak-inspector diff BEFORE.pak AFTER.pak\n"
           "  pak-inspector overlay ENTRY BASE.pak [PATCH.pak ...]\n";
}

}

int main(int argc, char** argv) {
    const std::string_view command = argc > 1 ? argv[1] : "--help";
    try {
        if (command == "--help" && argc <= 2) {
            usage(std::cout);
        } else if (command == "list" && argc == 3) {
            pak::Archive archive(argv[2]);
            list(archive);
        } else if (command == "inspect" && argc == 4) {
            pak::Archive archive(argv[2]);
            inspect(archive, argv[3]);
        } else if (command == "diff" && argc == 4) {
            pak::Archive before(argv[2]), after(argv[3]);
            diff(before, after);
        } else if (command == "overlay" && argc >= 4) {
            overlay(argv[2], {argv + 3, static_cast<std::size_t>(argc - 3)});
        } else {
            usage(std::cerr);
            return 2;
        }
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << '\n';
        return 1;
    }
}
