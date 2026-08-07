#include "spawn_location.h"
#include "dumper/dumper.h"
#include "process/helpers/helpers.h"
#include <spdlog/spdlog.h>

namespace dumper::stages::spawn_location {

    static auto find_bool_pair(uintptr_t base, size_t start, size_t end)
        -> std::optional<size_t> {
        for (size_t off = start; off < end; ++off) {
            auto v = process::Memory::read<uint8_t>(base + off);
            auto nx = process::Memory::read<uint8_t>(base + off + 1);
            if (v && nx && (*v == 0 || *v == 1) && (*nx == 0 || *nx == 1) && *v != *nx) {
                return off;
            }
        }
        return std::nullopt;
    }

    auto dump() -> bool {
        const auto spawn = g_workspace->find_first_child_of_class("SpawnLocation");
        if (!spawn) {
            spdlog::warn("No SpawnLocation in Workspace");
            return false;
        }
        const auto addr = spawn->get_address();

        // TeamColor = BrickColor "Really red" (1004)
        const auto team_color = process::helpers::find_offset<uint32_t>(addr, 1004, 0x800, 0x4);
        if (team_color) g_dumper.add_offset("SpawnLocation", "TeamColor", *team_color);

        // Enabled=true, Neutral=true, AllowTeamChangeOnTouch=false (defaults)
        // Bools are stored near TeamColor in this client; best-effort heuristic.
        const auto enabled = find_bool_pair(addr, 0x40, 0x200);
        if (enabled) g_dumper.add_offset("SpawnLocation", "Enabled", *enabled);

        auto neutral = find_bool_pair(addr, 0x40, 0x200);
        if (neutral && enabled && *neutral == *enabled) {
            auto n = find_bool_pair(addr, *neutral + 2, 0x200);
            if (n) neutral = n;
        }
        if (neutral && enabled && *neutral == *enabled) {
            auto n = find_bool_pair(addr, 0x40, *enabled - 1 > 0x40 ? *enabled - 1 : 0x40);
            if (n) neutral = n;
        }
        if (neutral) g_dumper.add_offset("SpawnLocation", "Neutral", *neutral);

        // ForcefieldDuration: float range [0, 600] near the bools (default 0; best-effort)
        const auto ff = process::helpers::find_float_range(addr, 0.0f, 600.0f, 0x200, 0x4);
        if (ff) g_dumper.add_offset("SpawnLocation", "ForcefieldDuration", *ff);

        return true;
    }

} // namespace dumper::stages::spawn_location
