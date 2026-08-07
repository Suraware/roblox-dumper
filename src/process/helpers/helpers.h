#pragma once
#include "process/memory/memory.h"
#include <cmath>
#include <functional>
#include <optional>
#include <regex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace process::helpers {
    auto find_pointer_by_rtti(std::string_view section_name,
                              const std::vector<std::string>& class_names, size_t alignment = 8)
        -> std::unordered_map<std::string, std::optional<size_t>>;

    // Scan all readable memory for RTTI-backed objects (for Sober/Linux where
    // Roblox objects are heap-allocated, not in a named section).
    auto find_rtti_globals(const std::vector<std::string>& class_names)
        -> std::unordered_map<std::string, std::optional<uintptr_t>>;

    auto find_sso_string_offset(uintptr_t base_address, const std::string& target_string,
                                size_t max_offset = 0x1000, size_t alignment = 8,
                                bool direct = false) -> std::optional<size_t>;

    auto find_roblox_string_offset(uintptr_t base_address, const std::string& target_string,
                                   size_t max_offset = 0x1000, size_t alignment = 8)
        -> std::optional<size_t>;

    auto find_string_offset(uintptr_t base_address, const std::string& target_string,
                            size_t max_offset = 0x1000, size_t alignment = 8,
                            size_t max_string_length = 256, bool direct = false)
        -> std::optional<size_t>;

    auto find_string_by_regex(uintptr_t base_address, const std::string& regex_pattern,
                              size_t max_offset = 0x1000, size_t alignment = 8,
                              size_t max_string_length = 256, bool direct = false)
        -> std::optional<size_t>;

    auto find_pointer_offset(uintptr_t base_address, uintptr_t target_pointer,
                             size_t max_offset = 0x1000, size_t alignment = 8)
        -> std::optional<size_t>;

    auto find_color3_offset(const std::vector<uintptr_t>& addresses,
                            std::function<std::tuple<uint8_t, uint8_t, uint8_t>(size_t)> get_rgb,
                            size_t max_offset = 0x300) -> std::optional<size_t>;

    template <typename T>
    auto find_offset(uintptr_t base_address, const T& value, size_t max_offset = 0x1000,
                     size_t alignment = 8) -> std::optional<size_t> {
        for (size_t offset = 0; offset < max_offset; offset += alignment) {
            auto read_value = Memory::read<T>(base_address + offset);
            if (read_value && read_value.value() == value) {
                return offset;
            }
        }
        return std::nullopt;
    }

    // allow tolerance for floats instead of using the generic func
    template <>
    inline auto find_offset<float>(uintptr_t base_address, const float& value, size_t max_offset,
                                   size_t alignment) -> std::optional<size_t> {
        constexpr float TOLERANCE = 0.0001f;

        for (size_t offset = 0; offset < max_offset; offset += alignment) {
            auto read_value = Memory::read<float>(base_address + offset);

            if (!read_value) {
                continue;
            }

            if (std::isnan(*read_value) || std::isinf(*read_value)) {
                continue;
            }

            if (std::abs(*read_value - value) < TOLERANCE) {
                return offset;
            }
        }

        return std::nullopt;
    }

    template <typename VecType>
    auto find_vec_offset(uintptr_t base_address, const VecType& value, size_t max_offset = 0x1000,
                         float tolerance = 5.0f, size_t alignment = 4) -> std::optional<size_t> {
        constexpr size_t components = VecType::length();

        for (size_t offset = 0; offset < max_offset; offset += alignment) {
            bool all_match = true;

            for (size_t i = 0; i < components; i++) {
                auto component_value = Memory::read<float>(base_address + offset + (i * 4));

                if (!component_value || std::isnan(*component_value) ||
                    std::isinf(*component_value)) {
                    all_match = false;
                    break;
                }

                if (std::abs(*component_value - value[i]) >= tolerance) {
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

    template <typename T>
    auto find_offset_with_getter(const std::vector<uintptr_t>& addresses,
                                 std::function<T(size_t)> get_expected_value, size_t max_offset,
                                 size_t alignment,
                                 const std::unordered_set<size_t>& ignored_offsets = {})
        -> std::optional<size_t> {
        for (size_t offset = 0; offset < max_offset; offset += alignment) {
            if (ignored_offsets.contains(offset)) {
                continue;
            }

            bool all_match = true;

            for (size_t i = 0; i < addresses.size(); i++) {
                auto read_value = Memory::read<T>(addresses[i] + offset);
                T expected = get_expected_value(i);

                if (!read_value || *read_value != expected) {
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

    template <typename VecType>
    auto find_vec3_offset_multi(const std::vector<uintptr_t>& addresses,
                                std::function<VecType(size_t)> get_expected_vec,
                                size_t max_offset = 0x1000, float tolerance = 0.01f)
        -> std::optional<size_t> {
        for (size_t offset = 0; offset < max_offset; offset += 4) {
            bool all_match = true;

            for (size_t i = 0; i < addresses.size(); i++) {
                VecType expected = get_expected_vec(i);

                auto x = Memory::read<float>(addresses[i] + offset);
                auto y = Memory::read<float>(addresses[i] + offset + 4);
                auto z = Memory::read<float>(addresses[i] + offset + 8);

                if (!x || !y || !z || std::abs(*x - expected.x) > tolerance ||
                    std::abs(*y - expected.y) > tolerance ||
                    std::abs(*z - expected.z) > tolerance) {
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

    // -----------------------------------------------------------------------
    // Range-based heuristic helpers (no bridge needed)
    // -----------------------------------------------------------------------

    auto find_float_range(uintptr_t base, float lo, float hi,
                          size_t max = 0x1000, size_t align = 4) -> std::optional<size_t>;

    auto find_vec3_range(uintptr_t base, float lo, float hi,
                         size_t max = 0x1000, size_t align = 4) -> std::optional<size_t>;

    auto find_vec3_pred(uintptr_t base,
                        std::function<bool(const std::array<float,3>&)> pred,
                        size_t max = 0x1000, size_t align = 4) -> std::optional<size_t>;

    auto find_u32_range(uintptr_t base, uint32_t lo, uint32_t hi,
                        size_t max = 0x1000, size_t align = 4) -> std::optional<size_t>;

    auto find_i64_nonzero(uintptr_t base,
                          size_t max = 0x1000, size_t align = 8) -> std::optional<size_t>;

    auto find_rtti_ptr(uintptr_t base, const std::string& needle,
                       size_t max = 0x1000, size_t align = 8) -> std::optional<size_t>;

    // Content/asset-id strings: Roblox strings pointed to from pointer slots.
    // Returns the slot offset whose dereferenced string contains `substring`.
    auto find_content_string(uintptr_t base, const std::string& substring,
                             size_t max = 0x1000, size_t align = 8) -> std::optional<size_t>;

    auto find_cframe(uintptr_t base,
                     size_t max = 0x1000, size_t align = 4) -> std::optional<size_t>;

    template <typename T>
    auto find_offset_in_pointer(uintptr_t base_address, const T& value,
                                size_t max_pointer_offset = 0x1000,
                                size_t max_value_offset = 0x1000, size_t pointer_alignment = 8,
                                size_t value_alignment = 4)
        -> std::optional<std::pair<size_t, size_t>> {
        for (size_t ptr_offset = 0; ptr_offset < max_pointer_offset;
             ptr_offset += pointer_alignment) {
            auto ptr = Memory::read<uintptr_t>(base_address + ptr_offset);
            if (!ptr || *ptr < 0x10000) {
                continue;
            }

            auto value_offset = find_offset<T>(*ptr, value, max_value_offset, value_alignment);
            if (value_offset) {
                return std::make_pair(ptr_offset, *value_offset);
            }
        }

        return std::nullopt;
    }

} // namespace process::helpers