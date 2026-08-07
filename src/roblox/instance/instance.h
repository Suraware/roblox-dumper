#pragma once
#include "roblox/offsets.h"
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace roblox {
    class Instance {
      public:
        Instance() = default;
        explicit Instance(std::uint64_t address) : m_address(address) {};

        auto is_valid() const -> bool;
        auto get_name() const -> std::optional<std::string>;
        auto get_class_name() const -> std::optional<std::string>;
        auto get_children() const -> std::vector<Instance>;
        auto get_parent() const -> std::optional<Instance>;
        auto find_first_child(std::string_view name) const -> std::optional<Instance>;

        template <typename T = Instance>
        auto find_first_child_of_class(std::string_view name) const -> std::optional<T> {
            for (const auto& child : get_children()) {
                const auto class_name = child.get_class_name();

                if (class_name && *class_name == name) {
                    return T(child.get_address());
                }
            }
            return std::nullopt;
        }

        // Recursive variant: descends into every child (with cycle guard).
        template <typename T = Instance>
        auto find_first_child_of_class_deep(std::string_view name,
                                            int max_depth = 12) const -> std::optional<T> {
            if (max_depth <= 0) return std::nullopt;
            for (const auto& child : get_children()) {
                const auto class_name = child.get_class_name();
                if (class_name && *class_name == name) {
                    return T(child.get_address());
                }
                if (auto found = child.find_first_child_of_class_deep<T>(name, max_depth - 1)) {
                    return found;
                }
            }
            return std::nullopt;
        }

        auto get_address() const -> std::uint64_t;

      private:
        std::uint64_t m_address = 0;
    };
} // namespace roblox