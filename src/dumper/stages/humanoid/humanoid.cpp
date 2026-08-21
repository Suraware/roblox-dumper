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

    static auto read_u8(uintptr_t addr) -> std::optional<uint8_t> {
        return process::Memory::read<uint8_t>(addr);
    }

    // Collect up to limit Humanoids (DFS under parent).
    static void collect_humanoids(roblox::Instance parent, std::vector<roblox::Instance>& out,
                                  size_t limit) {
        if (out.size() >= limit) return;
        for (const auto& child : parent.get_children()) {
            auto cn = child.get_class_name();
            if (cn && *cn == "Humanoid") {
                out.push_back(child);
                if (out.size() >= limit) return;
            }
            collect_humanoids(child, out, limit);
            if (out.size() >= limit) return;
        }
    }

    // Fingerprint of the Health-cluster layout (thunk-derived order):
    //   X-0x08 HealthDisplayDistance, X Health, X+0x04 HipHeight,
    //   X+0x10 JumpHeight, X+0x14 JumpPower, X+0x18 MaxHealth,
    //   X+0x1C MaxSlopeAngle
    static auto health_cluster_matches(uintptr_t ha, size_t x) -> bool {
        auto health = read_float(ha + x);
        auto maxh = read_float(ha + x + 0x18);
        auto hdd = read_float(ha + x - 0x8);
        auto jh = read_float(ha + x + 0x10);
        auto jp = read_float(ha + x + 0x14);
        auto msa = read_float(ha + x + 0x1C);

        if (!health || !maxh || !hdd || !jh || !jp || !msa) return false;
        if (*health <= 0.0f || *health > 1000000.0f) return false;
        if (*maxh < *health || *maxh > 1000000.0f) return false;
        if (*hdd < 1.0f || *hdd > 1000.0f) return false;
        if (*jh < 0.0f || *jh > 1000.0f) return false;
        if (*jp < 0.0f || *jp > 1000.0f) return false;
        if (*msa < 0.0f || *msa > 360.0f) return false;
        return true;
    }

    auto dump() -> bool {
        if (!dumper::g_workspace) return false;

        std::vector<roblox::Instance> humanoids;
        collect_humanoids(*dumper::g_workspace, humanoids, 3);
        if (humanoids.empty()) return false;

        // Anchor: Health offset X where every humanoid matches the cluster layout.
        auto X = std::optional<size_t>{};
        for (size_t off = 0x140; off <= 0x200; off += 4) {
            bool all = true;
            for (const auto& h : humanoids) {
                if (!health_cluster_matches(h.get_address(), off)) {
                    all = false;
                    break;
                }
            }
            if (all) {
                X = off;
                break;
            }
        }
        if (!X) {
            // Fall back to single-humanoid match if only one instance existed.
            auto ha0 = humanoids[0].get_address();
            for (size_t off = 0x140; off <= 0x200; off += 4) {
                if (health_cluster_matches(ha0, off)) {
                    X = off;
                    break;
                }
            }
        }
        if (!X) return false;

        const size_t H = *X;
        auto ha = humanoids[0].get_address();

        dumper::g_dumper.add_offset("Humanoid", "HealthDisplayDistance", H - 0x8);
        dumper::g_dumper.add_offset("Humanoid", "Health", H);
        dumper::g_dumper.add_offset("Humanoid", "HipHeight", H + 0x4);
        dumper::g_dumper.add_offset("Humanoid", "JumpHeight", H + 0x10);
        dumper::g_dumper.add_offset("Humanoid", "JumpPower", H + 0x14);
        dumper::g_dumper.add_offset("Humanoid", "MaxHealth", H + 0x18);
        dumper::g_dumper.add_offset("Humanoid", "MaxSlopeAngle", H + 0x1C);
        dumper::g_dumper.add_offset("Humanoid", "NameDisplayDistance", H + 0x20);
        dumper::g_dumper.add_offset("Humanoid", "RigType", H + 0x30);
        dumper::g_dumper.add_offset("Humanoid", "WalkSpeed", H + 0x40);

        // Bool cluster (validate 0/1 before emitting).
        const std::pair<std::string, size_t> bools[] = {
            {"AutoJumpEnabled", H + 0x44},
            {"AutoRotate", H + 0x45},
            {"EvaluateStateMachine", H + 0x48},
            {"Sit", H + 0x4E},
        };
        for (const auto& [name, off] : bools) {
            auto v = read_u8(ha + off);
            if (v && (*v == 0 || *v == 1))
                dumper::g_dumper.add_offset("Humanoid", name, off);
        }

        // Enum cluster.
        auto ddt = read_u32(ha + H - 0x10);
        if (ddt && *ddt <= 5)
            dumper::g_dumper.add_offset("Humanoid", "DisplayDistanceType", H - 0x10);

        // Vec3s from the same interior block.
        auto cam = read_float(ha + H - 0x68);
        if (cam) dumper::g_dumper.add_offset("Humanoid", "CameraOffset", H - 0x68);
        auto md = read_float(ha + H - 0x50);
        if (md) dumper::g_dumper.add_offset("Humanoid", "MoveDirection", H - 0x50);

        // === HumanoidRootPart at 0x458 ===
        auto hrp = process::helpers::find_rtti_ptr(ha, "Part@RBX", 0x800, 8);
        if (hrp && *hrp > 0x400) dumper::g_dumper.add_offset("Humanoid", "HumanoidRootPart", *hrp);

        // === SeatPart ===
        auto sp = process::helpers::find_rtti_ptr(ha, "Part", 0x400, 8);
        if (sp && *sp > 0x200) dumper::g_dumper.add_offset("Humanoid", "SeatPart", *sp);

        return true;
    }

} // namespace dumper::stages::humanoid
