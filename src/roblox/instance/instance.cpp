#include "instance.h"
#include "process/memory/memory.h"
#include "roblox/offsets.h"

namespace roblox {
    auto Instance::is_valid() const -> bool { return m_address != 0; }

    auto Instance::get_name() const -> std::optional<std::string> {
        if (!is_valid()) {
            return std::nullopt;
        }

        const auto name_pointer =
            process::Memory::read<uintptr_t>(m_address + offsets::Instance::Name);
        if (!name_pointer) {
            return std::nullopt;
        }

        return process::Memory::read_roblox_string(*name_pointer);
    }

    auto Instance::get_class_name() const -> std::optional<std::string> {
        if (!is_valid()) {
            return std::nullopt;
        }

        const auto class_descriptor =
            process::Memory::read<uintptr_t>(m_address + offsets::Instance::ClassDescriptor);

        if (!class_descriptor) {
            return std::nullopt;
        }

        const auto class_name_pointer =
            process::Memory::read<uintptr_t>(*class_descriptor + offsets::Instance::ClassName);

        if (!class_name_pointer) {
            return std::nullopt;
        }

        return process::Memory::read_roblox_string(*class_name_pointer);
    }

    auto Instance::get_children() const -> std::vector<Instance> {
        std::vector<Instance> children;

        if (!is_valid()) {
            return children;
        }

        const auto h = process::Memory::read<uintptr_t>(m_address + offsets::Instance::ChildrenStart);
        if (!h || *h < 0x10000) {
            return children;
        }

        const auto array_start_val = process::Memory::read<uintptr_t>(*h);
        if (!array_start_val || *array_start_val < 0x10000) {
            return children;
        }

        const auto array_end_val = process::Memory::read<uintptr_t>(*h + offsets::Instance::ChildrenEnd);
        if (!array_end_val || *array_end_val < 0x10000) {
            return children;
        }

        constexpr size_t MAX = 8192;
        for (uint64_t p = *array_start_val; p < *array_end_val && p; p += 0x10) {
            if ((p - *array_start_val) / 0x10 >= MAX) break;
            auto v = process::Memory::read<uintptr_t>(p);
            if (!v || *v < 0x10000) continue;
            children.emplace_back(*v);
        }

        return children;
    }

    auto Instance::get_parent() const -> std::optional<Instance> {
        return process::Memory::read<Instance>(m_address + offsets::Instance::Parent);
    }

    auto Instance::find_first_child(std::string_view name) const -> std::optional<Instance> {
        if (!is_valid()) {
            return std::nullopt;
        }

        for (const auto& child : get_children()) {
            const auto child_name = child.get_name();
            if (child_name && *child_name == name) {
                return child;
            }
        }

        return std::nullopt;
    }

    auto Instance::get_address() const -> std::uint64_t { return m_address; };

} // namespace roblox