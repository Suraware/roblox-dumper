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

        // Size = vec3 (0.01, 100)
        auto sz = process::helpers::find_vec3_pred(prim_addrs[0], [](const std::array<float,3>& v) {
            return v[0] > 0.01f && v[0] < 100.0f && v[1] > 0.01f && v[1] < 100.0f && v[2] > 0.01f && v[2] < 100.0f;
        }, 0x400, 4);
        if (sz) dumper::g_dumper.add_offset("Primitive", "Size", *sz);

        // Color = u32 > 0 && <= 0xFFFFFF
        for (size_t off = 0; off < 0x300; off += 4) {
            auto v = process::Memory::read<uint32_t>(prim_addrs[0] + off);
            if (v && *v > 0 && *v <= 0xFFFFFF) {
                dumper::g_dumper.add_offset("Primitive", "Color", off);
                break;
            }
        }

        // Transparency = float [0,1]
        auto trans = process::helpers::find_float_range(prim_addrs[0], 0.0f, 1.0f, 0x300, 4);
        if (trans) dumper::g_dumper.add_offset("Primitive", "Transparency", *trans);

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

        // AssemblyLinearVelocity/AngularVelocity
        auto alv = process::helpers::find_vec3_pred(prim_addrs[0], [](const std::array<float,3>& v) {
            return std::abs(v[0]) < 1e6f;
        }, 0x300, 4);
        if (alv) {
            dumper::g_dumper.add_offset("Primitive", "AssemblyLinearVelocity", *alv);
            dumper::g_dumper.add_offset("Primitive", "AssemblyAngularVelocity", *alv + 0xC);
        }

        // BasePart fields: scan on part addresses
        std::vector<uintptr_t> part_addrs;
        for (auto& p : parts_insts) part_addrs.push_back(p.get_address());

        // Size on BasePart too (some games use this)
        auto bp_sz = process::helpers::find_vec3_pred(part_addrs[0], [](const std::array<float,3>& v) {
            return v[0] > 0.01f && v[0] < 100.0f && v[1] > 0.01f && v[1] < 100.0f && v[2] > 0.01f && v[2] < 100.0f;
        }, 0x400, 4);
        if (bp_sz) dumper::g_dumper.add_offset("BasePart", "Size", *bp_sz);

        // Color3 = vec3 [0,1]
        auto col3 = process::helpers::find_vec3_range(part_addrs[0], 0.0f, 1.0f, 0x400, 4);
        if (col3) dumper::g_dumper.add_offset("BasePart", "Color3", *col3);

        // Transparency/Reflectance
        auto bpt = process::helpers::find_float_range(part_addrs[0], 0.0f, 1.0f, 0x300, 4);
        if (bpt) dumper::g_dumper.add_offset("BasePart", "Transparency", *bpt);

        // Reflectance: scan past Transparency's offset
        std::optional<size_t> bpr;
        if (bpt) {
            for (size_t off = *bpt + 4; off < 0x300; off += 4) {
                auto v = process::Memory::read<float>(part_addrs[0] + off);
                if (v && *v >= 0.0f && *v <= 1.0f) { bpr = off; break; }
            }
        } else {
            bpr = process::helpers::find_float_range(part_addrs[0], 0.0f, 1.0f, 0x300, 4);
        }
        if (bpr) dumper::g_dumper.add_offset("BasePart", "Reflectance", *bpr);

        // Shape = u32 [0,10]
        auto shape = process::helpers::find_u32_range(part_addrs[0], 0, 10, 0x400, 4);
        if (shape) dumper::g_dumper.add_offset("BasePart", "Shape", *shape);

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
