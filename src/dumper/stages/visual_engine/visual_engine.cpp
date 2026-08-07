#include "visual_engine.h"
#include "dumper/dumper.h"
#include "dumper/macros.h"
#include "process/helpers/helpers.h"
#include "process/memory/memory.h"
#include "process/rtti/rtti.h"
#include <array>
#include <spdlog/spdlog.h>

namespace dumper::stages::visual_engine {

    static uintptr_t g_ve_addr = 0;

    // Use working dumper's scan_rtti_range to find VisualEngine
    static auto find_visual_engine() -> bool {
        auto& proc = process::g_process;
        uintptr_t base = proc.get_base();
        const auto& maps = proc.get_maps_str();

        spdlog::info("Scanning for VisualEngine (base=0x{:x})", base);

        uintptr_t ve_abs = process::scan_rtti_range(maps, base, "VisualEngine@Graphics@RBX");
        if (!ve_abs) {
            spdlog::error("Failed to find VisualEngine RTTI");
            return false;
        }

        // Dereference to get the actual VisualEngine object
        auto ve_obj = process::Memory::read<uintptr_t>(ve_abs);
        if (!ve_obj) {
            spdlog::error("Failed to dereference VisualEngine pointer");
            return false;
        }

        g_ve_addr = *ve_obj;
        g_visual_engine = g_ve_addr;
        spdlog::info("VisualEngine at 0x{:x} (abs=0x{:x})", g_ve_addr, ve_abs);

        // Emit the RVA of the pointer slot that holds the VisualEngine object,
        // so external code can resolve it as base + VisualEngine::Pointer.
        g_dumper.add_offset("VisualEngine", "Pointer", ve_abs - base);
        return true;
    }

    static auto dump_render_view() -> bool {
        const auto render_view_offset =
            process::Rtti::find(g_ve_addr, "RenderView@Graphics@RBX");

        if (!render_view_offset) {
            spdlog::error("Failed to get RenderView offset from VisualEngine");
            return false;
        }

        g_dumper.add_offset("VisualEngine", "RenderView", *render_view_offset);

        const auto render_view =
            process::Memory::read<uintptr_t>(g_ve_addr + *render_view_offset);

        if (!render_view) {
            spdlog::error("Failed to dereference RenderView");
            return false;
        }

        FIND_AND_ADD_OFFSET(*render_view, RenderView, uint16_t, LightingValid, 257, 0x300, 0x2);
        g_dumper.add_offset("RenderView", "SkyboxValid", 0x28d);

        return true;
    };

    static auto dump_view_matrix() -> bool {
        auto is_valid_view_matrix = [](const float mat[16]) -> bool {
            if (std::abs(mat[11] - 0.1f) > 0.01f)
                return false;
            if (std::abs(mat[14] + 1.0f) < 0.01f && std::abs(mat[15]) < 0.01f)
                return false;
            if (std::abs(mat[15]) < 10.0f || std::abs(mat[15]) > 10000.0f)
                return false;
            for (int i = 0; i < 16; i++) {
                if (std::isnan(mat[i]) || std::isinf(mat[i]))
                    return false;
            }
            return true;
        };

        for (size_t offset = 0; offset < 0x2000; offset += 0x10) {
            float mat[16];
            bool valid_read = true;

            for (int i = 0; i < 16; i++) {
                auto val = process::Memory::read<float>(g_ve_addr + offset + (i * 4));
                if (!val) { valid_read = false; break; }
                mat[i] = *val;
            }
            if (!valid_read) continue;
            if (is_valid_view_matrix(mat)) {
                g_dumper.add_offset("VisualEngine", "ViewMatrix", offset);
                return true;
            }
        }

        spdlog::error("Failed to find ViewMatrix");
        return false;
    }

    auto dump() -> bool {
        if (!find_visual_engine()) return false;

        dump_render_view();
        dump_view_matrix();

        spdlog::warn("Skipping Dimensions offset (not available on Sober)");

        const auto fake_data_model = process::Rtti::find(g_ve_addr, "DataModel@RBX");
        if (!fake_data_model) {
            spdlog::error("Failed to get FakeDataModel offset from VisualEngine");
            return false;
        }

        g_dumper.add_offset("VisualEngine", "FakeDataModel", *fake_data_model);
        return true;
    }
} // namespace dumper::stages::visual_engine
