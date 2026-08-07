#include "air_properties.h"
#include "dumper/dumper.h"
#include "process/helpers/helpers.h"
#include "process/memory/memory.h"
#include <glm/glm.hpp>
#include <spdlog/spdlog.h>

namespace dumper::stages::air_properties {

    auto dump() -> bool {
        const auto world_off = g_dumper.get_offset("Workspace", "World");
        if (!world_off) {
            spdlog::warn("Workspace::World offset missing");
            return false;
        }

        auto world_addr = process::Memory::read<uintptr_t>(g_workspace->get_address() + *world_off);
        if (!world_addr || *world_addr < 0x10000) {
            spdlog::warn("World pointer invalid");
            return false;
        }

        // AirDensity default 1.225 — search the World object directly (the
        // AirProperties sub-object was not reachable via pointer scan).
        const auto density = process::helpers::find_offset<float>(*world_addr, 1.225f, 0x1400, 0x4);
        if (density) {
            const auto air_obj = *world_addr + *density;
            g_dumper.add_offset("AirProperties", "AirDensity", *density);

            // GlobalWind = Vector3 near AirDensity (default ~ (0,0,0))
            const auto wind = process::helpers::find_vec3_range(air_obj, -50.0f, 50.0f, 0x80, 0x4);
            if (wind) g_dumper.add_offset("AirProperties", "GlobalWind", *density + *wind);
        } else {
            // Fallback: pointer to an object containing AirDensity
            const auto air = process::helpers::find_offset_in_pointer<float>(
                *world_addr, 1.225f, 0x1000, 0x300, 0x8, 0x4);
            if (air) {
                g_dumper.add_offset("World", "AirProperties", air->first);
                g_dumper.add_offset("AirProperties", "AirDensity", air->second);
            } else {
                spdlog::warn("AirProperties not found in World");
            }
        }

        return true;
    }

} // namespace dumper::stages::air_properties
