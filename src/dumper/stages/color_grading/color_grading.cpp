#include "color_grading.h"
#include "dumper/dumper.h"
#include "process/helpers/helpers.h"
#include <spdlog/spdlog.h>

namespace dumper::stages::color_grading {

    auto dump() -> bool {
        if (!g_lighting) return false;

        const auto cg = g_lighting->find_first_child_of_class("ColorGradingEffect");
        if (!cg) {
            spdlog::warn("No ColorGradingEffect in Lighting");
            return false;
        }
        const auto addr = cg->get_address();

        // TonemapperPreset = Retro (1)
        const auto preset = process::helpers::find_offset<uint32_t>(addr, 1, 0x300, 0x4);
        if (preset) g_dumper.add_offset("ColorGradingEffect", "TonemapperPreset", *preset);

        for (size_t off = 0x180; off < 0x190; ++off) {
            auto v = process::Memory::read<uint8_t>(addr + off);
            if (v && (*v == 0 || *v == 1)) {
                g_dumper.add_offset("ColorGradingEffect", "Enabled", off);
                break;
            }
        }

        return true;
    }

} // namespace dumper::stages::color_grading
