#include "surface_appearance.h"
#include "dumper/dumper.h"
#include "process/helpers/helpers.h"
#include <glm/glm.hpp>
#include <spdlog/spdlog.h>

namespace dumper::stages::surface_appearance {

    auto dump() -> bool {
        const auto mesh = g_workspace->find_first_child("Mesh");
        if (!mesh) {
            spdlog::warn("No Mesh part in Workspace");
            return false;
        }

        const auto sa = mesh->find_first_child_of_class("SurfaceAppearance");
        if (!sa) {
            spdlog::warn("No SurfaceAppearance in Mesh");
            return false;
        }
        const auto addr = sa->get_address();

        const glm::vec3 color(0.11f, 0.22f, 0.33f);
        const auto color_off = process::helpers::find_vec_offset<glm::vec3>(addr, color, 0x400, 0.01f, 0x4);
        if (color_off) g_dumper.add_offset("SurfaceAppearance", "Color", *color_off);

        const glm::vec3 tint(0.4f, 0.5f, 0.6f);
        const auto tint_off = process::helpers::find_vec_offset<glm::vec3>(addr, tint, 0x400, 0.01f, 0x4);
        if (tint_off) g_dumper.add_offset("SurfaceAppearance", "EmissiveTint", *tint_off);

        const auto strength = process::helpers::find_offset<float>(addr, 1.5f, 0x400, 0x4);
        if (strength) g_dumper.add_offset("SurfaceAppearance", "EmissiveStrength", *strength);

        struct StringProp { const char* name; const char* value; };
        const StringProp string_props[] = {
            {"ColorMap", "rbxassetid://6196665106"},
            {"NormalMap", "rbxassetid://6412503613"},
            {"RoughnessMap", "rbxassetid://6444320592"},
            {"MetalnessMap", "rbxassetid://6444884337"},
        };

        for (const auto& prop : string_props) {
            const auto off = process::helpers::find_content_string(
                addr, prop.value, 0x400, 0x8);
            if (off) g_dumper.add_offset("SurfaceAppearance", prop.name, *off);
        }

        // EmissiveMaskContent was not settable in this Studio; regex fallback for a
        // 5th distinct "rbxassetid://" string (default empty -> may be skipped)
        const auto extra = process::helpers::find_string_by_regex(
            addr, R"(rbxassetid://[0-9]+)", 0x400, 0x8, 256, true);
        if (extra) {
            bool duplicate = false;
            for (const auto& prop : string_props) {
                const auto off = process::helpers::find_sso_string_offset(
                    addr, prop.value, 0x400, 0x8, true);
                if (off && *off == *extra) { duplicate = true; break; }
            }
            if (!duplicate) {
                g_dumper.add_offset("SurfaceAppearance", "EmissiveMaskContent", *extra);
            }
        }

        // AlphaMode: enum u32 in [0,1] within the string region
        const auto alpha = process::helpers::find_u32_range(addr, 0, 1, 0x300, 0x4);
        if (alpha) g_dumper.add_offset("SurfaceAppearance", "AlphaMode", *alpha);

        return true;
    }

} // namespace dumper::stages::surface_appearance
