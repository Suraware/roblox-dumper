#include "sound.h"
#include "dumper/dumper.h"
#include "process/helpers/helpers.h"
#include "process/memory/memory.h"
#include "roblox/instance/instance.h"
#include <optional>
#include <spdlog/spdlog.h>

namespace dumper::stages::sound {

    static auto find_sound_deep(roblox::Instance parent) -> std::optional<roblox::Instance> {
        for (const auto& child : parent.get_children()) {
            auto cn = child.get_class_name();
            if (cn && *cn == "Sound") return child;
            auto found = find_sound_deep(child);
            if (found) return found;
        }
        return std::nullopt;
    }

    auto dump() -> bool {
        if (!dumper::g_workspace) return false;
        auto sound = find_sound_deep(*dumper::g_workspace);
        if (!sound) return false;
        auto addr = sound->get_address();

        // Volume default 0.5 — distinctive anchor. Scan only the property region.
        auto vol_off = process::helpers::find_offset<float>(addr + 0xC0, 0.5f, 0x60, 4);
        if (vol_off) {
            const auto vol = 0xC0 + *vol_off;
            dumper::g_dumper.add_offset("Sound", "Volume", vol);

            // PlaybackSpeed default 1.0 near Volume
            auto ps = process::helpers::find_offset<float>(addr + vol, 1.0f, 0x40, 4);
            if (ps) dumper::g_dumper.add_offset("Sound", "PlaybackSpeed", vol + *ps);

            auto roll_max = process::helpers::find_offset<float>(addr + vol, 1000.0f, 0x80, 4);
            if (roll_max) {
                const auto rm = vol + *roll_max;
                dumper::g_dumper.add_offset("Sound", "RollOffMaxDistance", rm);
                auto roll_min = process::helpers::find_offset<float>(addr + rm, 10.0f, 0x20, 4);
                if (roll_min)
                    dumper::g_dumper.add_offset("Sound", "RollOffMinDistance", rm + *roll_min);
            }

            for (size_t off = vol - 8; off < vol + 8; ++off) {
                auto v = process::Memory::read<uint8_t>(addr + off);
                auto nx = process::Memory::read<uint8_t>(addr + off + 1);
                if (v && nx && (*v == 0 || *v == 1) && (*nx == 0 || *nx == 1) && *v != *nx) {
                    dumper::g_dumper.add_offset("Sound", "Looped", off);
                    break;
                }
            }
        } else {
            // Fallback: loose range scan
            for (size_t off = 0xD0; off < 0x140; off += 4) {
                auto v = process::Memory::read<float>(addr + off);
                if (v && !std::isnan(*v) && !std::isinf(*v) && *v >= 0.0f && *v <= 10.0f) {
                    dumper::g_dumper.add_offset("Sound", "Volume", off);
                    break;
                }
            }
        }

        auto sg = process::helpers::find_rtti_ptr(addr, "SoundGroup", 0x110, 8);
        if (sg) dumper::g_dumper.add_offset("Sound", "SoundGroup", *sg);

        return true;
    }

} // namespace dumper::stages::sound
