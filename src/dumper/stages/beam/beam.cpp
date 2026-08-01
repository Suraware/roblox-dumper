#include "beam.h"
#include "dumper/dumper.h"
#include "process/helpers/helpers.h"
#include "process/memory/memory.h"
#include "roblox/instance/instance.h"
#include <cmath>
#include <optional>
#include <spdlog/spdlog.h>

namespace dumper::stages::beam {

    static auto find_beam_deep(roblox::Instance parent) -> std::optional<roblox::Instance> {
        for (const auto& child : parent.get_children()) {
            auto cn = child.get_class_name();
            if (cn && *cn == "Beam") return child;
            auto found = find_beam_deep(child);
            if (found) return found;
        }
        return std::nullopt;
    }

    static void add_float(roblox::Instance& beam, const char* name, float lo, float hi,
                          size_t start, size_t end) {
        for (size_t off = start; off < end; off += 4) {
            auto v = process::Memory::read<float>(beam.get_address() + off);
            if (v && !std::isnan(*v) && !std::isinf(*v) && *v >= lo && *v <= hi) {
                dumper::g_dumper.add_offset("Beam", name, off);
                return;
            }
        }
    }

    auto dump() -> bool {
        if (!dumper::g_workspace) return false;
        auto beam = find_beam_deep(*dumper::g_workspace);
        if (!beam) return false;
        auto addr = beam->get_address();

        auto a0 = process::helpers::find_rtti_ptr(addr, "Attachment@RBX", 0x168, 8);
        if (a0) {
            dumper::g_dumper.add_offset("Beam", "Attachment0", *a0);
            auto a1 = process::helpers::find_rtti_ptr(addr + *a0 + 8, "Attachment@RBX", 0x30, 8);
            if (a1) {
                const auto off = *a0 + 8 + *a1;
                if (off != *a0) dumper::g_dumper.add_offset("Beam", "Attachment1", off);
            }
        }

        add_float(*beam, "Brightness", 0.0f, 10.0f, 0x178, 0x190);
        add_float(*beam, "CurveSize0", -100.0f, 100.0f, 0x17C, 0x194);
        add_float(*beam, "CurveSize1", -100.0f, 100.0f, 0x180, 0x198);
        add_float(*beam, "LightEmission", 0.0f, 10.0f, 0x184, 0x19C);
        add_float(*beam, "LightInfluence", 0.0f, 10.0f, 0x188, 0x1A0);
        add_float(*beam, "TextureLength", 0.0f, 100.0f, 0x194, 0x1AC);
        add_float(*beam, "TextureSpeed", -100.0f, 100.0f, 0x19C, 0x1B4);
        add_float(*beam, "Width0", 0.0f, 100.0f, 0x1A0, 0x1B8);
        add_float(*beam, "Width1", 0.0f, 100.0f, 0x1A4, 0x1BC);
        add_float(*beam, "ZOffset", -100.0f, 100.0f, 0x1A8, 0x1C0);

        return true;
    }

} // namespace dumper::stages::beam
