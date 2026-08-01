#include "process/memory/memory.h"
#include <algorithm>
#include <cstring>

namespace process {
    auto Memory::read_bytes(uintptr_t address, size_t size) -> std::vector<uint8_t> {
        std::vector<uint8_t> buffer(size);
        if (!g_process.read_memory(address, buffer.data(), size)) {
            return {};
        }
        return buffer;
    }

    auto Memory::write_bytes(uintptr_t address, const std::vector<uint8_t>& data) -> bool {
        return g_process.write_memory(address, data.data(), data.size());
    }

    auto Memory::read_string(uintptr_t address, size_t max_length) -> std::optional<std::string> {
        auto bytes = read_bytes(address, max_length);
        if (bytes.empty()) {
            return std::nullopt;
        }

        auto null_pos = std::find(bytes.begin(), bytes.end(), '\0');
        auto str = std::string(bytes.begin(), null_pos);

        if (str.empty()) {
            return std::nullopt;
        }

        return str;
    }

    auto Memory::read_sso_string(uintptr_t address) -> std::optional<std::string> {
        if (!address) {
            return std::nullopt;
        }

        auto length = read<int32_t>(address + 0x10);
        if (!length || *length <= 0 || *length > 65536) {
            return std::nullopt;
        }

        uintptr_t data_ptr = (*length >= 16) ? *read<uintptr_t>(address) : address;

        return read_string(data_ptr, *length);
    }

    // Roblox custom string format (used by Instance::Name, ClassName, etc.):
    //   byte 0: (len << 1) | (is_long ? 1 : 0)
    //   if short (!is_long): bytes 1..len contain string data
    //   if long  (is_long):  bytes 0..7 contain a pointer (LSB masked), data at that pointer
    auto Memory::read_roblox_string(uintptr_t address) -> std::optional<std::string> {
        if (!address) return std::nullopt;
        auto hdr = read<uint8_t>(address);
        if (!hdr) return std::nullopt;
        uint8_t len = *hdr >> 1;
        if (len == 0) return std::nullopt;
        if (*hdr & 1) {
            auto p = read<uintptr_t>(address);
            if (!p || *p < 0x10000) return std::nullopt;
            return read_string(*p & ~1ULL, len);
        }
        return read_string(address + 1, len);
    }

    auto Memory::scan_string(const std::string& target, std::string_view section)
        -> std::vector<uintptr_t> {
        std::vector<uintptr_t> matches;

        if (target.empty()) {
            return matches;
        }

        if (!section.empty()) {
            auto sec = g_process.get_section(section);
            if (!sec) {
                return matches;
            }

            auto buffer = read_bytes(sec->first, sec->second);
            if (buffer.size() < target.size()) {
                return matches;
            }

            for (size_t offset = 0; offset <= buffer.size() - target.size(); offset++) {
                if (std::memcmp(buffer.data() + offset, target.data(), target.size()) == 0) {
                    matches.push_back(sec->first + offset);
                }
            }

            return matches;
        }

        // Scan all readable regions
        for (const auto& region : g_process.get_regions()) {
            if (!region.readable) continue;

            auto buffer = read_bytes(region.start, region.size);
            if (buffer.size() < target.size()) continue;

            for (size_t offset = 0; offset <= buffer.size() - target.size(); offset++) {
                if (std::memcmp(buffer.data() + offset, target.data(), target.size()) == 0) {
                    matches.push_back(region.start + offset);
                }
            }
        }

        return matches;
    }

} // namespace process
