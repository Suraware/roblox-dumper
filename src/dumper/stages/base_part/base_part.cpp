#include "base_part.h"
#include "bridge/bridge.h"
#include "dumper/dumper.h"
#include "dumper/macros.h"
#include "process/helpers/helpers.h"
#include "process/memory/memory.h"
#include "process/rtti/rtti.h"
#include <glm/glm.hpp>
#include <spdlog/spdlog.h>

namespace dumper::stages::base_part {

    struct PartData {
        std::string name;
        uintptr_t address;
        uintptr_t primitive_address;
        bridge::PartProperty props;
    };

    auto dump() -> bool {
        auto parts_insts_raw = dumper::g_workspace->get_children();
        std::vector<roblox::Instance> parts_insts;
        for (auto& c : parts_insts_raw) {
            auto cn = c.get_class_name();
            if (cn && *cn == "Part") parts_insts.push_back(c);
        }
        if (parts_insts.empty()) {
            spdlog::error("No Part instances found");
            return false;
        }

        size_t primitive_offset = 0;
        for (auto& p : parts_insts) {
            auto po = process::Rtti::find(p.get_address(), "Primitive@RBX");
            if (po) { primitive_offset = *po; break; }
        }

        if (!primitive_offset) {
            spdlog::error("Failed to find Primitive offset in BasePart");
            return false;
        }
        dumper::g_dumper.add_offset("BasePart", "Primitive", primitive_offset);

        // Collect Primitive addresses
        std::vector<uintptr_t> prim_addrs;
        for (auto& p : parts_insts) {
            auto pp = process::Memory::read<uintptr_t>(p.get_address() + primitive_offset);
            if (pp && *pp >= 0x10000 && *pp <= 0x7fffffffffffULL)
                prim_addrs.push_back(*pp);
        }

        if (prim_addrs.size() < 2) {
            spdlog::error("Not enough primitives");
            return false;
        }

        // CFrame/Position via identity matrix scan
        auto cf = process::helpers::find_cframe(prim_addrs[0], 0x400, 4);
        if (cf) {
            dumper::g_dumper.add_offset("Primitive", "CFrame", *cf);
            dumper::g_dumper.add_offset("Primitive", "Position", *cf + 0x24);
            dumper::g_dumper.add_offset("Primitive", "Rotation", *cf);
            dumper::g_dumper.add_offset("Primitive", "Orientation", *cf);
        }

        // Size = vec3 (0.01, 100) near 0x1B8 (reference window)
        auto sz = process::helpers::find_vec3_pred(prim_addrs[0] + 0x1B0, [](const std::array<float,3>& v) {
            return v[0] > 0.01f && v[0] < 100.0f && v[1] > 0.01f && v[1] < 100.0f && v[2] > 0.01f && v[2] < 100.0f;
        }, 0x20, 4);
        if (sz) dumper::g_dumper.add_offset("Primitive", "Size", 0x1B0 + *sz);

        // Transparency = float [0,1] near 0x44 (reference window)
        auto trans = process::helpers::find_float_range(prim_addrs[0] + 0x38, 0.0f, 1.0f, 0x18, 4);
        if (trans) dumper::g_dumper.add_offset("Primitive", "Transparency", 0x38 + *trans);

        // Assembly velocities near 0x1B0 (reference window)
        auto alv = process::helpers::find_vec3_pred(
            prim_addrs[0] + 0x1A8, [](const std::array<float, 3>& v) {
                return std::abs(v[0]) < 1000.0f && std::abs(v[1]) < 1000.0f &&
                       std::abs(v[2]) < 1000.0f;
            },
            0x18, 4);
        if (alv) {
            const auto off = 0x1A8 + *alv;
            dumper::g_dumper.add_offset("Primitive", "AssemblyLinearVelocity", off);
            dumper::g_dumper.add_offset("Primitive", "AssemblyAngularVelocity", off + 0xC);
        }

        // PrimitiveFlags
        for (size_t off = 0; off < 0x300; off += 1) {
            bool ok = true, varied = false;
            uint8_t first = 0;
            for (size_t i = 0; i < prim_addrs.size(); i++) {
                auto v = process::Memory::read<uint8_t>(prim_addrs[i] + off);
                if (!v) { ok = false; break; }
                if (i == 0) first = *v;
                else if (*v != first) varied = true;
            }
            if (ok && varied) {
                dumper::g_dumper.add_offset("Primitive", "PrimitiveFlags", off);
                break;
            }
        }

        // BasePart fields: scan on part addresses
        std::vector<uintptr_t> part_addrs;
        for (auto& p : parts_insts) part_addrs.push_back(p.get_address());

        // Size on BasePart too (some games use this)
        auto bp_sz = process::helpers::find_vec3_pred(part_addrs[0], [](const std::array<float,3>& v) {
            return v[0] > 0.01f && v[0] < 100.0f && v[1] > 0.01f && v[1] < 100.0f && v[2] > 0.01f && v[2] < 100.0f;
        }, 0x400, 4);
        if (bp_sz) dumper::g_dumper.add_offset("BasePart", "Size", *bp_sz);

        // Color3 = vec3 with equal components (gray) in [0.1, 1], near 0x194
        auto col3 = process::helpers::find_vec3_pred(
            part_addrs[0] + 0x180, [](const std::array<float, 3>& v) {
                return v[0] > 0.1f && v[0] <= 1.0f && std::abs(v[0] - v[1]) < 0.01f &&
                       std::abs(v[1] - v[2]) < 0.01f;
            },
            0x40, 4);
        if (col3) dumper::g_dumper.add_offset("BasePart", "Color3", 0x180 + *col3);

        // Transparency = float [0,1] near 0x164
        auto bpt = process::helpers::find_float_range(part_addrs[0] + 0x150, 0.0f, 1.0f, 0x30, 4);
        if (bpt) dumper::g_dumper.add_offset("BasePart", "Transparency", 0x150 + *bpt);

        // Reflectance = float [0,1] near 0xE0
        auto bpr = process::helpers::find_float_range(part_addrs[0] + 0xD0, 0.0f, 1.0f, 0x20, 4);
        if (bpr) dumper::g_dumper.add_offset("BasePart", "Reflectance", 0xD0 + *bpr);

        // Shape = u32 [0,10] near 0x1B0
        auto shape = process::helpers::find_u32_range(part_addrs[0] + 0x1A8, 0, 10, 0x18, 4);
        if (shape) dumper::g_dumper.add_offset("BasePart", "Shape", 0x1A8 + *shape);

        // Bools: Locked, Massless, CastShadow
        for (size_t off = 0x1A0; off < 0x1B0; off++) {
            auto v = process::Memory::read<uint8_t>(part_addrs[0] + off);
            if (!v || (*v != 0 && *v != 1)) continue;
            auto v2 = process::Memory::read<uint8_t>(part_addrs[0] + off + 2);
            auto v3 = process::Memory::read<uint8_t>(part_addrs[0] + off + 3);
            if (v2 && v3 && (*v2 == 0 || *v2 == 1) && (*v3 == 0 || *v3 == 1)) {
                dumper::g_dumper.add_offset("BasePart", "Locked", off);
                dumper::g_dumper.add_offset("BasePart", "Massless", off + 2);
                dumper::g_dumper.add_offset("BasePart", "CastShadow", off + 3);
                break;
            }
        }

        return true;
    }

} // namespace dumper::stages::base_part
