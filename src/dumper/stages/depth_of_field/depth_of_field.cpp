#include "depth_of_field.h"
#include "dumper/dumper.h"
#include "process/helpers/helpers.h"
#include "process/memory/memory.h"
#include <cmath>
#include <optional>
#include <spdlog/spdlog.h>

namespace dumper::stages::depth_of_field {

    static void add_float(roblox::Instance& dof, const char* name, float lo, float hi,
                          size_t start, size_t end) {
        for (size_t off = start; off < end; off += 4) {
            auto v = process::Memory::read<float>(dof.get_address() + off);
            if (v && !std::isnan(*v) && !std::isinf(*v) && *v >= lo && *v <= hi) {
                dumper::g_dumper.add_offset("DepthOfFieldEffect", name, off);
                return;
            }
        }
    }

    auto dump() -> bool {
        if (!dumper::g_lighting) return false;
        auto dof = dumper::g_lighting->find_first_child_of_class("DepthOfFieldEffect");
        if (!dof) return false;
        auto addr = dof->get_address();

        add_float(*dof, "FocusDistance", 0.0f, 1000.0f, 0x100, 0x130);
        add_float(*dof, "InFocusRadius", 0.0f, 1000.0f, 0x110, 0x140);
        add_float(*dof, "NearIntensity", 0.0f, 10.0f, 0x100, 0x130);

        for (size_t off = 0x180; off < 0x190; ++off) {
            auto v = process::Memory::read<uint8_t>(addr + off);
            if (v && (*v == 0 || *v == 1)) {
                dumper::g_dumper.add_offset("DepthOfFieldEffect", "Enabled", off);
                break;
            }
        }

        return true;
    }

} // namespace dumper::stages::depth_of_field
