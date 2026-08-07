#include "helpers.h"
#include "process/memory/memory.h"
#include "process/process.h"
#include "process/rtti/rtti.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <spdlog/spdlog.h>

namespace process::helpers {
    auto find_pointer_by_rtti(std::string_view section_name,
                              const std::vector<std::string>& class_names, size_t alignment)
        -> std::unordered_map<std::string, std::optional<size_t>> {
        std::unordered_map<std::string, std::optional<size_t>> results;
        std::unordered_map<std::string, std::vector<uintptr_t>> all_matches;

        for (const auto& class_name : class_names) {
            results[class_name] = std::nullopt;
            all_matches[class_name] = {};
        }

        auto section = process::g_process.get_section(section_name);
        if (!section) {
            spdlog::error("Failed to find section: {}", section_name);
            return results;
        }

        auto [section_start, section_size] = *section;
        auto module_base = process::g_process.get_module_base();

        for (size_t offset = 0; offset < section_size; offset += alignment) {
            auto potential_ptr = process::Memory::read<uintptr_t>(section_start + offset);
            if (!potential_ptr || *potential_ptr < 0x10000) {
                continue;
            }

            auto rtti = process::Rtti::scan_rtti(*potential_ptr);
            if (!rtti) {
                continue;
            }

            for (const auto& class_name : class_names) {
                if (rtti->name == class_name) {
                    size_t final_offset = (section_start + offset) - module_base;
                    all_matches[class_name].push_back(final_offset);
                }
            }
        }

        for (const auto& class_name : class_names) {
            auto& matches = all_matches[class_name];

            if (matches.empty()) {
                spdlog::warn("Failed to find class: {}", class_name);
                continue;
            }

            if (class_name == "DataModel@RBX") {
                std::sort(matches.begin(), matches.end(),
                          [](uintptr_t a, uintptr_t b) { return a > b; });

                if (matches.size() >= 2) {
                    results[class_name] = matches[1];
                } else {
                    spdlog::warn("Found DataModel but not enough instances");
                }
            } else {
                results[class_name] = matches[0];
            }
        }

        return results;
    }

    auto find_sso_string_offset(uintptr_t base_address, const std::string& target_string,
                                size_t max_offset, size_t alignment, bool direct)
        -> std::optional<size_t> {
        for (size_t offset = 0; offset < max_offset; offset += alignment) {
            if (direct) {
                auto str = Memory::read_sso_string(base_address + offset);
                if (str && *str == target_string) return offset;
                auto rstr = Memory::read_roblox_string(base_address + offset);
                if (rstr && *rstr == target_string) return offset;
            } else {
                auto string_ptr = Memory::read<uintptr_t>(base_address + offset);
                if (!string_ptr || *string_ptr < 0x10000) continue;

                auto str = Memory::read_sso_string(*string_ptr);
                if (str && *str == target_string) return offset;
                auto rstr = Memory::read_roblox_string(*string_ptr);
                if (rstr && *rstr == target_string) return offset;
            }
        }

        return std::nullopt;
    }

    auto find_content_string(uintptr_t base, const std::string& substring,
                             size_t max_offset, size_t alignment) -> std::optional<size_t> {
        for (size_t offset = 0; offset < max_offset; offset += alignment) {
            auto string_ptr = Memory::read<uintptr_t>(base + offset);
            if (!string_ptr || *string_ptr < 0x10000) continue;

            auto rstr = Memory::read_roblox_string(*string_ptr);
            if (rstr && rstr->find(substring) != std::string::npos) return offset;

            auto str = Memory::read_sso_string(*string_ptr);
            if (str && str->find(substring) != std::string::npos) return offset;
        }

        return std::nullopt;
    }

    auto find_roblox_string_offset(uintptr_t base_address, const std::string& target_string,
                                   size_t max_offset, size_t alignment)
        -> std::optional<size_t> {
        for (size_t offset = 0; offset < max_offset; offset += alignment) {
            auto string_ptr = Memory::read<uintptr_t>(base_address + offset);
            if (!string_ptr || *string_ptr < 0x10000) continue;
            auto str = Memory::read_roblox_string(*string_ptr);
            if (str && *str == target_string) return offset;
        }
        return std::nullopt;
    }

    auto find_string_offset(uintptr_t base_address, const std::string& target_string,
                            size_t max_offset, size_t alignment, size_t max_string_length,
                            bool direct) -> std::optional<size_t> {
        for (size_t offset = 0; offset < max_offset; offset += alignment) {
            if (direct) {
                auto str = Memory::read_string(base_address + offset, max_string_length);
                if (str && *str == target_string) {
                    return offset;
                }
            } else {
                auto string_ptr = Memory::read<uintptr_t>(base_address + offset);
                if (!string_ptr || *string_ptr < 0x10000) {
                    continue;
                }

                auto str = Memory::read_string(*string_ptr, max_string_length);
                if (str && *str == target_string) {
                    return offset;
                }
            }
        }

        return std::nullopt;
    }

    auto find_string_by_regex(uintptr_t base_address, const std::string& regex_pattern,
                              size_t max_offset, size_t alignment, size_t max_string_length,
                              bool direct) -> std::optional<size_t> {
        std::regex pattern(regex_pattern);

        for (size_t offset = 0; offset < max_offset; offset += alignment) {
            if (direct) {
                auto str = Memory::read_string(base_address + offset, max_string_length);
                if (str && std::regex_match(*str, pattern)) {
                    return offset;
                }
            } else {
                auto string_ptr = Memory::read<uintptr_t>(base_address + offset);
                if (!string_ptr || *string_ptr < 0x10000) {
                    continue;
                }

                auto str = Memory::read_string(*string_ptr, max_string_length);
                if (str && std::regex_match(*str, pattern)) {
                    return offset;
                }
            }
        }

        return std::nullopt;
    }

    auto find_pointer_offset(uintptr_t base_address, uintptr_t target_pointer, size_t max_offset,
                             size_t alignment) -> std::optional<size_t> {
        for (size_t offset = 0; offset < max_offset; offset += alignment) {
            auto ptr = Memory::read<uintptr_t>(base_address + offset);
            if (ptr && *ptr == target_pointer) {
                return offset;
            }
        }
        return std::nullopt;
    }

    auto find_color3_offset(const std::vector<uintptr_t>& addresses,
                            std::function<std::tuple<uint8_t, uint8_t, uint8_t>(size_t)> get_rgb,
                            size_t max_offset) -> std::optional<size_t> {
        for (size_t offset = 0; offset < max_offset; offset += 1) {
            bool all_match = true;

            for (size_t i = 0; i < addresses.size(); i++) {
                auto [exp_r, exp_g, exp_b] = get_rgb(i);

                auto r = Memory::read<uint8_t>(addresses[i] + offset);
                auto g = Memory::read<uint8_t>(addresses[i] + offset + 1);
                auto b = Memory::read<uint8_t>(addresses[i] + offset + 2);

                if (!r || !g || !b || *r != exp_r || *g != exp_g || *b != exp_b) {
                    all_match = false;
                    break;
                }
            }

            if (all_match) {
                return offset;
            }
        }

        return std::nullopt;
    }

    auto find_rtti_globals(const std::vector<std::string>& class_names)
        -> std::unordered_map<std::string, std::optional<uintptr_t>> {
        std::unordered_map<std::string, std::optional<uintptr_t>> results;
        std::unordered_map<std::string, std::vector<uintptr_t>> all_matches;

        for (const auto& class_name : class_names) {
            results[class_name] = std::nullopt;
            all_matches[class_name] = {};
        }

        for (const auto& region : g_process.get_regions()) {
            if (!region.readable || !region.pathname.empty()) continue;

            for (size_t offset = 0; offset + 8 < region.size; offset += 8) {
                auto potential_ptr = Memory::read<uintptr_t>(region.start + offset);
                if (!potential_ptr || *potential_ptr < 0x10000 ||
                    *potential_ptr > 0x7fffffffffffULL) {
                    continue;
                }

                auto rtti = process::Rtti::scan_rtti(*potential_ptr);
                if (!rtti) continue;

                for (const auto& class_name : class_names) {
                    if (rtti->name == class_name) {
                        all_matches[class_name].push_back(region.start + offset);
                    }
                }
            }
        }

        for (const auto& class_name : class_names) {
            auto& matches = all_matches[class_name];

            if (matches.empty()) {
                spdlog::warn("Failed to find class: {}", class_name);
                continue;
            }

            if (class_name == "DataModel@RBX") {
                std::sort(matches.begin(), matches.end(),
                          [](uintptr_t a, uintptr_t b) { return a > b; });

                if (matches.size() >= 2) {
                    results[class_name] = matches[1];
                } else {
                    spdlog::warn("Found DataModel but not enough instances");
                }
            } else {
                results[class_name] = matches[0];
            }
        }

        return results;
    }

    // -----------------------------------------------------------------------
    // Range-based heuristic helpers (no bridge needed)
    // -----------------------------------------------------------------------

    auto find_float_range(uintptr_t base, float lo, float hi, size_t max, size_t align)
        -> std::optional<size_t> {
        for (size_t off = 0; off < max; off += align) {
            auto v = Memory::read<float>(base + off);
            if (!v || std::isnan(*v) || std::isinf(*v)) continue;
            if (*v >= lo && *v <= hi) return off;
        }
        return std::nullopt;
    }

    auto find_vec3_range(uintptr_t base, float lo, float hi, size_t max, size_t align)
        -> std::optional<size_t> {
        return find_vec3_pred(base, [lo, hi](const std::array<float,3>& v) {
            return v[0] >= lo && v[0] <= hi && v[1] >= lo && v[1] <= hi && v[2] >= lo && v[2] <= hi;
        }, max, align);
    }

    auto find_vec3_pred(uintptr_t base,
                        std::function<bool(const std::array<float,3>&)> pred,
                        size_t max, size_t align) -> std::optional<size_t> {
        for (size_t off = 0; off < max; off += align) {
            auto x = Memory::read<float>(base + off);
            auto y = Memory::read<float>(base + off + 4);
            auto z = Memory::read<float>(base + off + 8);
            if (!x || !y || !z) continue;
            if (std::isnan(*x) || std::isinf(*x)) continue;
            if (std::isnan(*y) || std::isinf(*y)) continue;
            if (std::isnan(*z) || std::isinf(*z)) continue;
            std::array<float,3> v = {*x, *y, *z};
            if (pred(v)) return off;
        }
        return std::nullopt;
    }

    auto find_u32_range(uintptr_t base, uint32_t lo, uint32_t hi, size_t max, size_t align)
        -> std::optional<size_t> {
        for (size_t off = 0; off < max; off += align) {
            auto v = Memory::read<uint32_t>(base + off);
            if (v && *v >= lo && *v <= hi) return off;
        }
        return std::nullopt;
    }

    auto find_i64_nonzero(uintptr_t base, size_t max, size_t align)
        -> std::optional<size_t> {
        for (size_t off = 0; off < max; off += align) {
            auto v = Memory::read<int64_t>(base + off);
            if (!v || *v <= 0 || *v >= 100'000'000'000LL) continue;
            auto u = Memory::read<uintptr_t>(base + off);
            if (!u || *u < 0x10000 || *u > 0x7fffffffffffULL) return off;
        }
        return std::nullopt;
    }

    auto find_rtti_ptr(uintptr_t base, const std::string& needle, size_t max, size_t align)
        -> std::optional<size_t> {
        for (size_t off = 0; off < max; off += align) {
            auto ptr = Memory::read<uintptr_t>(base + off);
            if (!ptr || *ptr < 0x10000 || *ptr > 0x7fffffffffffULL) continue;
            auto rtti = process::Rtti::scan_rtti(*ptr);
            if (rtti && rtti->name.find(needle) != std::string::npos) return off;
        }
        return std::nullopt;
    }

    auto find_cframe(uintptr_t base, size_t max, size_t align) -> std::optional<size_t> {
        for (size_t off = 0; off < max; off += align) {
            auto data = Memory::read_bytes(base + off, 48);
            if (data.size() < 48) continue;
            float f[12];
            std::memcpy(f, data.data(), 48);
            bool bad = false;
            for (int i = 0; i < 12; ++i)
                if (std::isnan(f[i]) || std::isinf(f[i]) || std::abs(f[i]) > 1e8f) { bad = true; break; }
            if (bad) continue;
            bool ok = true;
            for (int i = 0; i < 3; ++i) {
                float len = std::sqrt(f[i*3]*f[i*3] + f[i*3+1]*f[i*3+1] + f[i*3+2]*f[i*3+2]);
                if (std::abs(len - 1.0f) > 0.01f) { ok = false; break; }
            }
            if (ok) return off;
        }
        return std::nullopt;
    }

} // namespace process::helpers