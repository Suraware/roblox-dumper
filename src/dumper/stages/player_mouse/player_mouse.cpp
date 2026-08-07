#include "player_mouse.h"
#include "dumper/dumper.h"
#include "process/helpers/helpers.h"
#include "process/memory/memory.h"
#include "process/rtti/rtti.h"
#include <spdlog/spdlog.h>

namespace dumper::stages::player_mouse {

    auto dump() -> bool {
        const auto players = g_data_model.find_first_child_of_class("Players");
        if (!players) return false;

        const auto lp_off = process::Rtti::find(players->get_address(), "Player@RBX", 0x400, 0x8);
        if (!lp_off) return false;

        auto lp = process::Memory::read<uintptr_t>(players->get_address() + *lp_off);
        if (!lp || *lp < 0x10000) return false;

        // Player::Mouse -> PlayerMouse object
        const auto mouse_off = process::Rtti::find(*lp, "PlayerMouse@RBX", 0x1000, 0x8);
        if (!mouse_off) {
            spdlog::warn("PlayerMouse not found in Player");
            return false;
        }
        g_dumper.add_offset("Player", "Mouse", *mouse_off);

        auto mouse_addr = process::Memory::read<uintptr_t>(*lp + *mouse_off);
        if (!mouse_addr || *mouse_addr < 0x10000) return false;

        const auto workspace = process::Rtti::find(*mouse_addr, "Workspace@RBX", 0x300, 0x8);
        if (workspace) g_dumper.add_offset("PlayerMouse", "Workspace", *workspace);

        // Icon: default cursor content string
        const auto icon = process::helpers::find_string_by_regex(
            *mouse_addr, R"(rbxasset[^"]{0,64})", 0x300, 0x8, 128, true);
        if (icon) g_dumper.add_offset("PlayerMouse", "Icon", *icon);

        return true;
    }

} // namespace dumper::stages::player_mouse
