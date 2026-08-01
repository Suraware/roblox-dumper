#include "sun_rays_effect.h"
#include "dumper/dumper.h"
#include "process/helpers/helpers.h"
#include "process/memory/memory.h"
#include <cmath>
#include <optional>
#include <spdlog/spdlog.h>

namespace dumper::stages::sun_rays_effect {

    static void add_float(roblox::Instance& sr, const char* name, float lo, float hi,
                          size_t start, size_t end) {
        for (size_t off = start; off < end; off += 4) {
            auto v = process::Memory::read<float>(sr.get_address() + off);
            if (v && !std::isnan(*v) && !std::isinf(*v) && *v >= lo && *v <= hi) {
                dumper::g_dumper.add_offset("SunRaysEffect", name, off);
                return;
            }
        }
    }

    auto dump() -> bool {
        if (!dumper::g_lighting) return false;
        auto sr = dumper::g_lighting->find_first_child_of_class("SunRaysEffect");
        if (!sr) return false;
        auto addr = sr->get_address();

        add_float(*sr, "Intensity", 0.0f, 1.0f, 0x100, 0x114);

        auto intensity = dumper::g_dumper.get_offset("SunRaysEffect", "Intensity");
        if (intensity) {
            for (size_t off = *intensity; off < *intensity + 8; off += 4) {
                auto v = process::Memory::read<float>(addr + off);
                if (v && !std::isnan(*v) && !std::isinf(*v) && *v >= 0.0f && *v <= 10.0f &&
                    off != *intensity) {
                    dumper::g_dumper.add_offset("SunRaysEffect", "Spread", off);
                    break;
                }
            }
        }

        for (size_t off = 0x18C; off < 0x198; ++off) {
            auto v = process::Memory::read<uint8_t>(addr + off);
            if (v && (*v == 0 || *v == 1)) {
                dumper::g_dumper.add_offset("SunRaysEffect", "Enabled", off);
                break;
            }
        }

        return true;
    }

} // namespace dumper::stages::sun_rays_effect
