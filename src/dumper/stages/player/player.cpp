#include "player.h"
#include "bridge/bridge.h"
#include "dumper/dumper.h"
#include "dumper/macros.h"
#include "process/helpers/helpers.h"
#include "process/memory/memory.h"
#include "process/rtti/rtti.h"
#include "roblox/instance/instance.h"
#include <spdlog/spdlog.h>

namespace dumper::stages::player {
    auto dump() -> bool {
        const auto players = dumper::g_data_model.find_first_child_of_class("Players");
        if (!players) {
            spdlog::error("Failed to find Players instance inside DataModel");
            return false;
        }

        const auto local_player = process::Rtti::find(players->get_address(), "Player@RBX");
        if (!local_player) {
            spdlog::error("Failed to find LocalPlayer in Players");
            return false;
        }

        dumper::g_dumper.add_offset("Players", "LocalPlayer", *local_player);

        const auto local_player_addr = process::Memory::read<uintptr_t>(
            players->get_address() + *local_player);
        if (!local_player_addr) {
            spdlog::error("Failed to read LocalPlayer address");
            return false;
        }
        auto lp = *local_player_addr;

        // Character = RTTI - no bridge needed
        const auto character = process::Rtti::find(lp, "ModelInstance@RBX");
        if (!character) {
            spdlog::error("Failed to find Character offset for Player");
            return false;
        }
        dumper::g_dumper.add_offset("Player", "Character", *character);

        // Try bridge first
        auto player_info = bridge::g_bridge.read_player_information();
        if (player_info) {
            FIND_AND_ADD_OFFSET(lp, Player, uint64_t, UserId, player_info->user_id, 0x800, 0x8);
            auto display_name = process::helpers::find_string_offset(
                lp, player_info->display_name, 0x400, 0x8, 0x256, true);
            if (display_name)
                dumper::g_dumper.add_offset("Player", "DisplayName", *display_name);
            FIND_AND_ADD_OFFSET(lp, Player, uint32_t, AccountAge, player_info->account_age, 0x600, 0x4);
        } else {
            // Heuristic fallback
            // UserId = u64 [1, 99999999] not a pointer
            for (size_t off = 0x20; off < 0x600; off += 8) {
                auto v = process::Memory::read<uint64_t>(lp + off);
                if (!v || *v < 1 || *v > 99999999ULL) continue;
                auto u = process::Memory::read<uintptr_t>(lp + off);
                if (u && *u >= 0x10000 && *u <= 0x7fffffffffffULL) continue;
                dumper::g_dumper.add_offset("Player", "UserId", off);
                break;
            }
            // AccountAge = u32 [0, 20000]
            auto aa = process::helpers::find_u32_range(lp, 0, 20000, 0x600, 4);
            if (aa) dumper::g_dumper.add_offset("Player", "AccountAge", *aa);
            // MembershipType = u32 [0, 4]
            for (size_t off = 0x100; off < 0x130; off += 4) {
                auto v = process::Memory::read<uint32_t>(lp + off);
                if (v && *v >= 0 && *v <= 4 &&
                    process::Memory::read<uint32_t>(lp + off + 4) &&
                    *process::Memory::read<uint32_t>(lp + off + 4) >= 0 &&
                    *process::Memory::read<uint32_t>(lp + off + 4) <= 4) {
                    dumper::g_dumper.add_offset("Player", "MembershipType", off);
                    break;
                }
            }
            // DisplayName = string pointer at offset 0-0x200
            for (size_t off = 8; off < 0x200; off += 8) {
                auto ptr = process::Memory::read<uintptr_t>(lp + off);
                if (!ptr || *ptr < 0x10000 || *ptr > 0x7fffffffffffULL) continue;
                auto s = process::Memory::read_string(*ptr, 64);
                if (!s || s->empty() || (*s)[0] == '[' || (*s)[0] == '(') continue;
                if (s->find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_ ") != std::string::npos) continue;
                dumper::g_dumper.add_offset("Player", "DisplayName", off);
                break;
            }
        }

        // These use hardcoded values - always scan regardless of bridge
        FIND_AND_ADD_OFFSET(lp, Player, uint32_t, TeamColor, 1004, 0x800, 0x4);

        auto team_ptr = process::helpers::find_pointer_offset(lp, dumper::g_team_addr, 0x400, 0x8);
        if (team_ptr) dumper::g_dumper.add_offset("Player", "Team", *team_ptr);

        FIND_AND_ADD_OFFSET(lp, Player, float, HealthDisplayDistance, 87.12f, 0x800, 0x4);
        FIND_AND_ADD_OFFSET(lp, Player, float, NameDisplayDistance, 56.89f, 0x800, 0x4);

        // LocaleId = regex [a-z]{2}-[a-z]{2}
        auto locale_id = process::helpers::find_string_by_regex(
            lp, R"([a-z]{2}-[a-z]{2})", 0x800, 0x8, 32, true);
        if (locale_id) dumper::g_dumper.add_offset("Player", "LocaleId", *locale_id);

        return true;
    }
} // namespace dumper::stages::player
