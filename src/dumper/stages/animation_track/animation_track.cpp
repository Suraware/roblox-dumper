#include "animation_track.h"
#include "dumper/dumper.h"
#include "process/helpers/helpers.h"
#include "process/memory/memory.h"
#include "process/rtti/rtti.h"
#include <spdlog/spdlog.h>

namespace dumper::stages::animation_track {

    auto dump() -> bool {
        // Find any playing track via the NPC Animator (CalibrationDriver plays one)
        const auto npc = g_workspace->find_first_child("WalkingNpc");
        if (!npc) return false;
        const auto hum = npc->find_first_child_of_class("Humanoid");
        if (!hum) return false;

        const auto animator_off = process::Rtti::find(hum->get_address(), "Animator@RBX", 0x400, 0x8);
        if (!animator_off) return false;

        auto animator_addr = process::Memory::read<uintptr_t>(hum->get_address() + *animator_off);
        if (!animator_addr || *animator_addr < 0x10000) return false;

        const auto track_offsets =
            process::Rtti::find_all(*animator_addr, "AnimationTrack@RBX", 0x400, 0x8);
        if (track_offsets.empty()) return false;

        auto track_addr = process::Memory::read<uintptr_t>(*animator_addr + track_offsets[0]);
        if (!track_addr || *track_addr < 0x10000) return false;
        const auto track = *track_addr;

        // Speed = 1.5 (set by CalibrationDriver), Looped = true, playing = true
        const auto speed = process::helpers::find_offset<float>(track, 1.5f, 0x300, 0x4);
        if (speed) g_dumper.add_offset("AnimationTrack", "Speed", *speed);

        // TimePosition: increasing float in [0, 600]
        const auto time_pos = process::helpers::find_float_range(track, 0.0f, 600.0f, 0x300, 0x4);
        if (time_pos) g_dumper.add_offset("AnimationTrack", "TimePosition", *time_pos);

        // Animation = pointer to the Animation instance
        const auto anim = process::Rtti::find(track, "Animation@RBX", 0x300, 0x8);
        if (anim) g_dumper.add_offset("AnimationTrack", "Animation", *anim);

        // Animator = back-pointer
        const auto animator = process::Rtti::find(track, "Animator@RBX", 0x300, 0x8);
        if (animator) g_dumper.add_offset("AnimationTrack", "Animator", *animator);

        // Looped / IsPlaying: bool pair region near Speed
        for (size_t off = 0x100; off < 0x280; ++off) {
            auto v = process::Memory::read<uint8_t>(track + off);
            auto nx = process::Memory::read<uint8_t>(track + off + 1);
            if (v && nx && (*v == 0 || *v == 1) && (*nx == 0 || *nx == 1) && *v != *nx) {
                g_dumper.add_offset("AnimationTrack", "Looped", off);
                g_dumper.add_offset("AnimationTrack", "IsPlaying", off + 1);
                break;
            }
        }

        return true;
    }

} // namespace dumper::stages::animation_track
