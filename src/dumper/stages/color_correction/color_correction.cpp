#include "color_correction.h"
#include "dumper/dumper.h"
#include "process/helpers/helpers.h"
#include <glm/glm.hpp>
#include <spdlog/spdlog.h>

namespace dumper::stages::color_correction {

    auto dump() -> bool {
        if (!g_lighting) return false;

        const auto cc = g_lighting->find_first_child_of_class("ColorCorrectionEffect");
        if (!cc) {
            spdlog::warn("No ColorCorrectionEffect in Lighting");
            return false;
        }
        const auto addr = cc->get_address();

        const auto brightness = process::helpers::find_offset<float>(addr, 0.75f, 0x300, 0x4);
        if (brightness) g_dumper.add_offset("ColorCorrectionEffect", "Brightness", *brightness);

        const auto contrast = process::helpers::find_offset<float>(addr, 0.5f, 0x300, 0x4);
        if (contrast) g_dumper.add_offset("ColorCorrectionEffect", "Contrast", *contrast);

        const glm::vec3 tint(0.1f, 0.9f, 0.2f);
        const auto tint_off = process::helpers::find_vec_offset<glm::vec3>(addr, tint, 0x300, 0.01f, 0x4);
        if (tint_off) g_dumper.add_offset("ColorCorrectionEffect", "TintColor", *tint_off);

        for (size_t off = 0x180; off < 0x190; ++off) {
            auto v = process::Memory::read<uint8_t>(addr + off);
            if (v && (*v == 0 || *v == 1)) {
                g_dumper.add_offset("ColorCorrectionEffect", "Enabled", off);
                break;
            }
        }

        return true;
    }

} // namespace dumper::stages::color_correction
