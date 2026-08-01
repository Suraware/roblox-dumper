#include "misc.h"
#include "dumper/dumper.h"
#include "process/memory/memory.h"
#include "roblox/instance/instance.h"
#include <optional>
#include <spdlog/spdlog.h>

namespace dumper::stages::misc {

    static constexpr const char* VALUE_CLASSES[] = {
        "IntValue",     "NumberValue", "DoubleValue", "StringValue", "BoolValue",
        "Color3Value",  "CFrameValue", "ObjectValue", "BrickColorValue",
    };

    static auto find_value_deep(roblox::Instance parent) -> std::optional<roblox::Instance> {
        for (const auto& child : parent.get_children()) {
            auto cn = child.get_class_name();
            for (const char* cls : VALUE_CLASSES) {
                if (cn && *cn == cls) return child;
            }
            auto found = find_value_deep(child);
            if (found) return found;
        }
        return std::nullopt;
    }

    auto dump() -> bool {
        if (!dumper::g_workspace) return false;
        auto value = find_value_deep(*dumper::g_workspace);
        if (!value) return false;
        auto addr = value->get_address();

        for (size_t off = 0xB0; off < 0xD0; off += 4) {
            auto v = process::Memory::read<uint32_t>(addr + off);
            if (v && *v > 0 && *v < 1000000) {
                dumper::g_dumper.add_offset("Misc", "Value", off);
                break;
            }
        }

        return true;
    }

} // namespace dumper::stages::misc
