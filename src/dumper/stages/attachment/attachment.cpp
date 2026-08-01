#include "attachment.h"
#include "dumper/dumper.h"
#include "process/helpers/helpers.h"
#include "roblox/instance/instance.h"
#include <array>
#include <cmath>
#include <optional>
#include <spdlog/spdlog.h>

namespace dumper::stages::attachment {

    static auto find_attachment_deep(roblox::Instance parent) -> std::optional<roblox::Instance> {
        for (const auto& child : parent.get_children()) {
            auto cn = child.get_class_name();
            if (cn && *cn == "Attachment") return child;
            auto found = find_attachment_deep(child);
            if (found) return found;
        }
        return std::nullopt;
    }

    auto dump() -> bool {
        if (!dumper::g_workspace) return false;
        auto attachment = find_attachment_deep(*dumper::g_workspace);
        if (!attachment) return false;
        auto addr = attachment->get_address();

        auto pos = process::helpers::find_vec3_pred(
            addr + 0xF0, [](const std::array<float, 3>& v) {
                return std::abs(v[0]) < 100.0f && std::abs(v[1]) < 100.0f &&
                       std::abs(v[2]) < 100.0f;
            },
            0x40, 4);
        if (pos) dumper::g_dumper.add_offset("Attachment", "Position", 0xF0 + *pos);

        return true;
    }

} // namespace dumper::stages::attachment
