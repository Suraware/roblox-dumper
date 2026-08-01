#include "humanoid.h"
#include "dumper/dumper.h"
#include "process/helpers/helpers.h"
#include "process/memory/memory.h"
#include "roblox/instance/instance.h"
#include <glm/glm.hpp>
#include <spdlog/spdlog.h>

namespace dumper::stages::humanoid {

    static auto find_humanoid_deep(roblox::Instance parent) -> std::optional<roblox::Instance> {
        for (const auto& child : parent.get_children()) {
            auto cn = child.get_class_name();
            if (cn && *cn == "Humanoid") return child;
            auto found = find_humanoid_deep(child);
            if (found) return found;
        }
        return std::nullopt;
    }

    static auto read_float(uintptr_t addr) -> std::optional<float> {
        return process::Memory::read<float>(addr);
    }

    static auto read_u32(uintptr_t addr) -> std::optional<uint32_t> {
        return process::Memory::read<uint32_t>(addr);
    }

    auto dump() -> bool {
        if (!dumper::g_workspace) return false;
        auto humanoid = find_humanoid_deep(*dumper::g_workspace);
        if (!humanoid) return false;
        auto ha = humanoid->get_address();

        // === Anchor: Health at 0x178 ===
        // Search [0x150, 0x200) for float 50-100
        auto health_off = std::optional<size_t>{};
        for (size_t off = 0x150; off < 0x200; off += 4) {
            auto v = read_float(ha + off);
            if (v && *v >= 50.0f && *v <= 100.0f) { health_off = off; break; }
        }
        if (!health_off) return false;
        auto H = *health_off;
        dumper::g_dumper.add_offset("Humanoid", "Health", H);

        // Relative layout (from working dumper, offsets relative to Health at 0x178):
        //   0x17C = H+4: MaxHealth
        //   0x180 = H+8: HealthDisplayDistance
        //   0x184 = H+0xC: HipHeight
        //   0x190 = H+0x18: JumpHeight
        //   0x194 = H+0x1C: JumpPower

        // MaxHealth = H+4, float >= 90
        auto mh = read_float(ha + H + 4);
        if (mh && *mh >= 90.0f && *mh <= 100.0f) dumper::g_dumper.add_offset("Humanoid", "MaxHealth", H + 4);

        // HealthDisplayDistance = H+8, float [50, 500]
        auto hdd = read_float(ha + H + 8);
        if (hdd && *hdd >= 50.0f && *hdd <= 500.0f) dumper::g_dumper.add_offset("Humanoid", "HealthDisplayDistance", H + 8);

        // HipHeight = H+0xC, float [1, 8]
        auto hh = read_float(ha + H + 0xC);
        if (hh && *hh >= 1.0f && *hh <= 8.0f) dumper::g_dumper.add_offset("Humanoid", "HipHeight", H + 0xC);

        // JumpHeight = H+0x18, float [1, 150]
        auto jh = read_float(ha + H + 0x18);
        if (jh && *jh >= 1.0f && *jh <= 150.0f) dumper::g_dumper.add_offset("Humanoid", "JumpHeight", H + 0x18);

        // JumpPower = H+0x1C, float [48, 52]
        auto jp = read_float(ha + H + 0x1C);
        if (jp && *jp >= 48.0f && *jp <= 52.0f) dumper::g_dumper.add_offset("Humanoid", "JumpPower", H + 0x1C);

        // === WalkSpeed at 0x1C0 ===
        // Search [0x1B0, 0x1D0) for float 14-30
        auto ws_off = std::optional<size_t>{};
        for (size_t off = 0x1B0; off < 0x1D0; off += 4) {
            auto v = read_float(ha + off);
            if (v && *v >= 14.0f && *v <= 30.0f) { ws_off = off; break; }
        }
        if (ws_off) dumper::g_dumper.add_offset("Humanoid", "WalkSpeed", *ws_off);

        // === RigType at 0x1A4, u32 [0, 4] ===
        for (size_t off = 0x1A0; off < 0x1B0; off += 4) {
            auto v = read_u32(ha + off);
            if (v && *v >= 0 && *v <= 4) {
                dumper::g_dumper.add_offset("Humanoid", "RigType", off);
                break;
            }
        }

        // === DisplayDistanceType at 0x1B0, u32 [0, 5] ===
        for (size_t off = 0x1A8; off < 0x1C0; off += 4) {
            auto v = read_u32(ha + off);
            if (v && *v >= 0 && *v <= 5) {
                dumper::g_dumper.add_offset("Humanoid", "DisplayDistanceType", off);
                break;
            }
        }

        // === NameDisplayDistance at 0x1A8, float [50, 500] ===
        for (size_t off = 0x1A0; off < 0x1C0; off += 4) {
            auto v = read_float(ha + off);
            if (v && *v >= 50.0f && *v <= 500.0f) {
                dumper::g_dumper.add_offset("Humanoid", "NameDisplayDistance", off);
                break;
            }
        }

        // === MaxSlopeAngle at 0x1AC, float [30, 90] ===
        for (size_t off = 0x1A0; off < 0x1C0; off += 4) {
            auto v = read_float(ha + off);
            if (v && *v >= 30.0f && *v <= 90.0f) {
                dumper::g_dumper.add_offset("Humanoid", "MaxSlopeAngle", off);
                break;
            }
        }

        // === Bool cluster at 0x1B8-0x1BC ===
        for (size_t off = 0x1A0; off < 0x1D0; off++) {
            auto v0 = process::Memory::read<uint8_t>(ha + off);
            if (!v0 || (*v0 != 0 && *v0 != 1)) continue;
            auto v2 = process::Memory::read<uint8_t>(ha + off + 2);
            auto v3 = process::Memory::read<uint8_t>(ha + off + 3);
            if (v2 && v3 && (*v2 == 0 || *v2 == 1) && (*v3 == 0 || *v3 == 1)) {
                dumper::g_dumper.add_offset("Humanoid", "AutoJumpEnabled", off);
                dumper::g_dumper.add_offset("Humanoid", "AutoRotate", off + 2);
                dumper::g_dumper.add_offset("Humanoid", "Sit", off + 3);
                auto v4 = process::Memory::read<uint8_t>(ha + off + 4);
                if (v4 && (*v4 == 0 || *v4 == 1))
                    dumper::g_dumper.add_offset("Humanoid", "EvaluateStateMachine", off + 4);
                break;
            }
        }

        // === CameraOffset at 0x36C, vec3 near (0, 0.5, 0) ===
        auto co = process::helpers::find_vec3_pred(ha, [](const std::array<float,3>& v) {
            return std::abs(v[0]) < 5.0f && std::abs(v[1] - 0.5f) < 0.5f && std::abs(v[2]) < 5.0f;
        }, 0x800, 4);
        if (co && *co > 0x300) dumper::g_dumper.add_offset("Humanoid", "CameraOffset", *co);
        else {
            co = process::helpers::find_vec3_pred(ha, [](const std::array<float,3>& v) {
                return std::abs(v[0]) < 50.0f && std::abs(v[1]) < 50.0f && std::abs(v[2]) < 50.0f;
            }, 0x800, 4);
            if (co && *co > 0x300) dumper::g_dumper.add_offset("Humanoid", "CameraOffset", *co);
        }

        // === WalkToPoint at 0x390, vec3 ===
        auto wtp = process::helpers::find_vec3_pred(ha, [](const std::array<float,3>& v) {
            return std::abs(v[0]) < 100000.0f;
        }, 0x500, 4);
        if (wtp && *wtp > 0x300 && *wtp < 0x400) dumper::g_dumper.add_offset("Humanoid", "WalkToPoint", *wtp);

        // === TargetPoint at 0x3DC, vec3 ===
        auto tp = process::helpers::find_vec3_pred(ha, [](const std::array<float,3>& v) {
            return std::abs(v[0]) < 100000.0f && std::abs(v[1]) < 100000.0f && std::abs(v[2]) < 100000.0f;
        }, 0x500, 4);
        if (tp && *tp > 0x3A0) dumper::g_dumper.add_offset("Humanoid", "TargetPoint", *tp);

        // === MoveDirection at 0x90, vec3 ===
        auto md = process::helpers::find_vec3_pred(ha, [](const std::array<float,3>& v) {
            return std::abs(v[0]) < 100.0f && std::abs(v[1]) < 100.0f && std::abs(v[2]) < 100.0f;
        }, 0x200, 4);
        if (md && *md >= 0x80 && *md < 0xA0) dumper::g_dumper.add_offset("Humanoid", "MoveDirection", *md);

        // === HumanoidRootPart at 0x458 ===
        auto hrp = process::helpers::find_rtti_ptr(ha, "Part@RBX", 0x800, 8);
        if (hrp && *hrp > 0x400) dumper::g_dumper.add_offset("Humanoid", "HumanoidRootPart", *hrp);

        // === SeatPart ===
        auto sp = process::helpers::find_rtti_ptr(ha, "Part", 0x400, 8);
        if (sp && *sp > 0x200) dumper::g_dumper.add_offset("Humanoid", "SeatPart", *sp);

        return true;
    }

} // namespace dumper::stages::humanoid
