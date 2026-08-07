#include "blur_effect.h"
#include "dumper/dumper.h"
#include "process/helpers/helpers.h"
#include <spdlog/spdlog.h>

namespace dumper::stages::blur_effect {

    auto dump() -> bool {
        if (!g_lighting) return false;

        const auto blur = g_lighting->find_first_child_of_class("BlurEffect");
        if (!blur) {
            spdlog::warn("No BlurEffect in Lighting");
            return false;
        }
        const auto addr = blur->get_address();

        const auto size = process::helpers::find_offset<float>(addr, 8.5f, 0x300, 0x4);
        if (size) g_dumper.add_offset("BlurEffect", "Size", *size);

        for (size_t off = 0x180; off < 0x190; ++off) {
            auto v = process::Memory::read<uint8_t>(addr + off);
            if (v && (*v == 0 || *v == 1)) {
                g_dumper.add_offset("BlurEffect", "Enabled", off);
                break;
            }
        }

        return true;
    }

} // namespace dumper::stages::blur_effect
