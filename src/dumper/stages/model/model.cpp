#include "model.h"
#include "dumper/dumper.h"
#include "process/helpers/helpers.h"
#include "process/memory/memory.h"
#include "process/rtti/rtti.h"
#include "roblox/instance/instance.h"
#include <optional>
#include <spdlog/spdlog.h>

namespace dumper::stages::model {

    static auto find_model_deep(roblox::Instance parent) -> std::optional<roblox::Instance> {
        for (const auto& child : parent.get_children()) {
            auto cn = child.get_class_name();
            if (cn && *cn == "Model") return child;
            auto found = find_model_deep(child);
            if (found) return found;
        }
        return std::nullopt;
    }

    static auto get_model_address() -> std::optional<uintptr_t> {
        // Preferred: LocalPlayer.Character (a Model)
        const auto players = dumper::g_data_model.find_first_child_of_class("Players");
        if (players) {
            const auto lp_off = process::Rtti::find(players->get_address(), "Player@RBX");
            if (lp_off) {
                auto lp = process::Memory::read<uintptr_t>(players->get_address() + *lp_off);
                if (lp && *lp >= 0x10000) {
                    const auto char_off = process::Rtti::find(*lp, "ModelInstance@RBX");
                    if (char_off) {
                        auto char_addr = process::Memory::read<uintptr_t>(*lp + *char_off);
                        if (char_addr && *char_addr >= 0x10000) return *char_addr;
                    }
                }
            }
        }

        // Fallback: any Model deep in Workspace
        if (dumper::g_workspace) {
            auto model = find_model_deep(*dumper::g_workspace);
            if (model) return model->get_address();
        }

        return std::nullopt;
    }

    auto dump() -> bool {
        auto addr = get_model_address();
        if (!addr) return false;

        // PrimaryPart is usually nil on rigs; only add if a part is referenced
        auto pp = process::helpers::find_rtti_ptr(*addr, "Part@RBX", 0x110, 8);
        if (!pp) pp = process::helpers::find_rtti_ptr(*addr, "BasePart@RBX", 0x110, 8);
        if (pp) dumper::g_dumper.add_offset("Model", "PrimaryPart", *pp);

        return true;
    }

} // namespace dumper::stages::model
