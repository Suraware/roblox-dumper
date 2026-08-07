#include "animator.h"
#include "dumper/dumper.h"
#include "process/memory/memory.h"
#include "process/rtti/rtti.h"
#include <spdlog/spdlog.h>

namespace dumper::stages::animator {

    auto get_local_player() -> std::optional<uintptr_t> {
        const auto players = g_data_model.find_first_child_of_class("Players");
        if (!players) return std::nullopt;

        const auto lp_off = process::Rtti::find(players->get_address(), "Player@RBX", 0x400, 0x8);
        if (!lp_off) return std::nullopt;

        auto lp = process::Memory::read<uintptr_t>(players->get_address() + *lp_off);
        if (!lp || *lp < 0x10000) return std::nullopt;
        return *lp;
    }

    auto dump() -> bool {
        uintptr_t humanoid_addr = 0;

        const auto lp = get_local_player();
        if (lp) {
            const auto char_off = process::Rtti::find(*lp, "ModelInstance@RBX", 0x400, 0x8);
            if (char_off) {
                auto char_addr = process::Memory::read<uintptr_t>(*lp + *char_off);
                if (char_addr && *char_addr >= 0x10000) {
                    const auto hum = roblox::Instance(*char_addr).find_first_child_of_class("Humanoid");
                    if (hum) humanoid_addr = hum->get_address();
                }
            }
        }

        if (!humanoid_addr) {
            const auto npc = g_workspace->find_first_child("WalkingNpc");
            if (npc) {
                const auto hum = npc->find_first_child_of_class("Humanoid");
                if (hum) humanoid_addr = hum->get_address();
            }
        }

        if (!humanoid_addr) {
            spdlog::warn("No Humanoid available for Animator stage");
            return false;
        }

        // Animator is referenced from the Humanoid (runtime-only object)
        const auto animator_off = process::Rtti::find(humanoid_addr, "Animator@RBX", 0x1000, 0x8);
        if (!animator_off) {
            spdlog::warn("Animator not found in Humanoid");
            return false;
        }

        auto animator_addr = process::Memory::read<uintptr_t>(humanoid_addr + *animator_off);
        if (!animator_addr || *animator_addr < 0x10000) {
            spdlog::warn("Animator pointer invalid");
            return false;
        }

        // ActiveAnimations = vector field; its begin pointer points at a live
        // AnimationTrack while the calibration track is playing.
        const auto track_ptr = process::Rtti::find(*animator_addr, "AnimationTrack@RBX", 0x400, 0x8);
        if (track_ptr) {
            g_dumper.add_offset("Animator", "ActiveAnimations", *track_ptr);
        } else {
            spdlog::warn("Animator::ActiveAnimations not found (no track playing)");
        }

        return true;
    }

} // namespace dumper::stages::animator
