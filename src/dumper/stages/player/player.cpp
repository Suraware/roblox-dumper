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

        for (size_t off = 0; off < 0x300; off += 4) {
            auto u = process::Memory::read<uint32_t>(lp + off);
            auto f = process::Memory::read<float>(lp + off);
            if (u) spdlog::info("[dbg lp] +0x{:X} u32=0x{:08X} f={}", off, *u, f ? *f : 0.0f);
        }

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
            spdlog::info("[dbg player] bridge user_id={} account_age={} display_name='{}' locale='{}'",
                         player_info->user_id, player_info->account_age,
                         player_info->display_name, player_info->locale_id);
            // UserId = u64 == bridge user_id, full scan
            if (player_info->user_id != 0) {
                for (size_t off = 0x10; off < 0x600; off += 8) {
                    auto v = process::Memory::read<uint64_t>(lp + off);
                    if (v && *v == player_info->user_id) {
                        dumper::g_dumper.add_offset("Player", "UserId", off);
                        break;
                    }
                }
            }
            // AccountAge = u32 == bridge account_age, full scan
            for (size_t off = 0x0; off < 0x600; off += 4) {
                auto v = process::Memory::read<uint32_t>(lp + off);
                if (v && *v == player_info->account_age) {
                    dumper::g_dumper.add_offset("Player", "AccountAge", off);
                    break;
                }
            }
            // DisplayName = rbx string (len<<1|1, len@+8, data@+0x10) == bridge display_name
            if (!player_info->display_name.empty()) {
                for (size_t off = 0x0; off < 0x600; off += 8) {
                    auto hdr = process::Memory::read<uint64_t>(lp + off);
                    if (!hdr || (*hdr & 1ULL) == 0) continue;
                    auto len = *hdr >> 1;
                    if (len == 0 || len > 128) continue;
                    auto dptr = process::Memory::read<uintptr_t>(lp + off + 0x10);
                    auto dstr = dptr ? process::Memory::read_string(*dptr, len) : std::nullopt;
                    if (dstr && *dstr == player_info->display_name) {
                        dumper::g_dumper.add_offset("Player", "DisplayName", off);
                        break;
                    }
                }
            }
            // MembershipType = u32 [0, 4] near 0x100
            for (size_t off = 0xFC; off < 0x10C; off += 4) {
                auto v = process::Memory::read<uint32_t>(lp + off);
                if (v && *v >= 0 && *v <= 4) {
                    dumper::g_dumper.add_offset("Player", "MembershipType", off);
                    break;
                }
            }
            // DisplayName = rbx string at +0 (len<<1|1, data at +0x10)
            auto dn = process::Memory::read<uint64_t>(lp);
            if (dn && (*dn & 1ULL) && ((*dn >> 1) < 256)) {
                auto dptr = process::Memory::read<uintptr_t>(lp + 0x10);
                auto dstr = dptr ? process::Memory::read_string(*dptr, 64) : std::nullopt;
                if (dstr && (!player_info->display_name.empty()
                                 ? *dstr == player_info->display_name
                                 : !dstr->empty()))
                    dumper::g_dumper.add_offset("Player", "DisplayName", 0);
            }
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

        // MaxZoomDistance = 97.5 (set by CalibrationClient); MinZoomDistance (0.5) nearby
        const auto max_zoom = process::helpers::find_offset<float>(lp, 97.5f, 0x800, 0x4);
        if (max_zoom) {
            g_dumper.add_offset("Player", "MaxZoomDistance", *max_zoom);
            for (size_t off = *max_zoom > 0x40 ? *max_zoom - 0x40 : 0; off < *max_zoom + 0x40;
                 off += 4) {
                auto v = process::Memory::read<float>(lp + off);
                if (v && !std::isnan(*v) && std::abs(*v - 0.5f) < 0.0001f) {
                    g_dumper.add_offset("Player", "MinZoomDistance", off);
                    break;
                }
            }
            // CameraMode = u32 [1, 2] near the zoom floats
            for (size_t off = *max_zoom > 0x40 ? *max_zoom - 0x40 : 0; off < *max_zoom + 0x40;
                 off += 4) {
                auto v = process::Memory::read<uint32_t>(lp + off);
                if (v && (*v == 1 || *v == 2)) {
                    g_dumper.add_offset("Player", "CameraMode", off);
                    break;
                }
            }
        }

        // Player::Mouse -> PlayerMouse (also dumped by player_mouse stage)
        const auto mouse = process::Rtti::find(lp, "PlayerMouse@RBX", 0x400, 0x8);
        if (mouse) g_dumper.add_offset("Player", "Mouse", *mouse);

        // LocaleId = regex [a-z]{2}-[a-z]{2}
        auto locale_id = process::helpers::find_string_by_regex(
            lp, R"([a-z]{2}-[a-z]{2})", 0x800, 0x8, 32, true);
        if (locale_id) dumper::g_dumper.add_offset("Player", "LocaleId", *locale_id);

        // DisplayName raw fallback: inline short rbx-string (byte0 = len*2,
        // data at +1). Pick the longest printable name-like string (no
        // punctuation; e.g. "LinxuMint" @ 0x120, not instance GUIDs).
        if (!dumper::g_dumper.get_offset("Player", "DisplayName")) {
            size_t best = 0, best_len = 0;
            for (size_t off = 0; off < 0x600; off++) {
                auto hdr = process::Memory::read<uint8_t>(lp + off);
                if (!hdr || (*hdr & 1u) || *hdr == 0) continue;
                size_t len = *hdr >> 1;
                if (len < 3 || len > 24) continue;
                auto buf = process::Memory::read_bytes(lp + off + 1, len);
                if (buf.empty()) continue;
                bool name_like = true, has_letter = false;
                for (size_t i = 0; i < len; i++) {
                    auto c = buf[i];
                    if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                          (c >= '0' && c <= '9') || c == '_' || c == ' ')) {
                        name_like = false;
                        break;
                    }
                    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) has_letter = true;
                }
                if (!name_like || !has_letter) continue;
                if (len > best_len) { best_len = len; best = off; }
            }
            if (best_len >= 3) {
                auto b = process::Memory::read_bytes(lp + best + 1, best_len);
                if (!b.empty()) {
                    spdlog::info("[dbg lp] DisplayName fallback 0x{:X} = '{}'",
                                 best, std::string(b.begin(), b.end()));
                    dumper::g_dumper.add_offset("Player", "DisplayName", best);
                }
            }
        }

        return true;
    }
} // namespace dumper::stages::player
