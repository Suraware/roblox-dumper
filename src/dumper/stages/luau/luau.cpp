// Luau VM offsets.
//
// Discovery: signature-scan the client's executable regions for luaD_checkstack
// (statically identified in IDA as sub_26EC197). Its body both validates the
// match (8000-slot stack limit immediate) and reveals lua_State field offsets:
//
//   a1[1]  (+0x08)  top        (TValue*, StkIdRel)
//   a1[2]  (+0x10)  stack_end
//   a1[3]  (+0x18)  related state ptr
//   a1[5]  (+0x28)  stack base
//   slot math: (top - stack) >> 4  => sizeof(TValue) == 16
//
// The prologue also loads a module-global pointer (mov r15,[rip+...]); its
// target is emitted as Luau::VmAuxStruct - purpose not yet fully identified.
#include "luau.h"
#include "dumper/dumper.h"
#include "process/memory/memory.h"
#include <algorithm>
#include <cstring>
#include <spdlog/spdlog.h>
#include <vector>

namespace dumper::stages::luau {

    namespace {

        // sub_26EC197 prologue through the 8000-slot compare. Wildcards cover
        // the rip-relative displacement of the global load.
        // 55 48 89 e5 41 57 41 56 41 54 53 48 83 ec 10
        // 4c 8b 3d ?? ?? ?? ??
        // 49 8b 07 48 89 45 d8 31 db 81 fe 40 1f 00 00 7f ??
        struct Pattern {
            std::vector<uint8_t> bytes;
            std::vector<bool> mask; // true = must match
        };

        Pattern make_pattern() {
            const uint8_t raw[] = {
                0x55, 0x48, 0x89, 0xE5, 0x41, 0x57, 0x41, 0x56, 0x41, 0x54,
                0x53, 0x48, 0x83, 0xEC, 0x10,
                0x4C, 0x8B, 0x3D, 0x00, 0x00, 0x00, 0x00, // rip-disp wildcarded
                0x49, 0x8B, 0x07, 0x48, 0x89, 0x45, 0xD8, 0x31, 0xDB,
                0x81, 0xFE, 0x40, 0x1F, 0x00, 0x00, 0x7F};
            const bool skip[] = {false, false, false, false, false, false,
                                 false, false, false, false, false, false,
                                 false, false, false,
                                 false, false, false,
                                 true,  true,  true,  true,
                                 false, false, false, false, false, false,
                                 false, false, false,
                                 false, false, false, false, false, false,
                                 false};
            Pattern p;
            for (size_t i = 0; i < sizeof(raw); ++i) {
                p.bytes.push_back(raw[i]);
                p.mask.push_back(skip[i]);
            }
            return p;
        }

        // Global-pointer load: mov r15,[rip+disp32] at pattern offset 15,
        // instruction length 7.
        constexpr size_t k_rip_mov_off = 15;
        constexpr size_t k_rip_mov_len = 7;

    } // namespace


    // IDA-derived function RVAs for build 199da195b4fd47 (prologues embedded
    // as validation signatures - if the client updates, these fail cleanly).
    struct InternalFunc {
        const char* name;
        size_t rva;
        uint8_t prologue[8];
        size_t prologue_len;
    };

    auto dump_internal_functions() -> bool {
        const InternalFunc funcs[] = {
            {"InstanceNew",          0x26F7744, {0x55, 0x48, 0x89, 0xE5, 0x41, 0x57, 0x41, 0x56}, 8},
            {"InstanceCreateCore",   0x2409CCE, {0x55, 0x48, 0x89, 0xE5, 0x41, 0x57, 0x41, 0x56}, 8},
            {"ClassNameToDescriptor",0x2187550, {0x55, 0x48, 0x89, 0xE5, 0x53, 0x50, 0x48, 0x89}, 8},
            {"LuaDCall",             0x26DFAB4, {0x55, 0x48, 0x89, 0xE5, 0x41, 0x57, 0x41, 0x56}, 8},
            {"LuaResume",            0x65A4DA0, {0x55, 0x48, 0x89, 0xE5, 0x0F, 0xB6, 0x47, 0x03}, 8},
        };

        // Base-library C API from the luaL_Reg table at 0x6C40230
        // (names resolved statically against this exact build).
        const InternalFunc base_api[] = {
            {"CollectGarbage", 0x4240072, {0}, 0},
            {"GetFEnv",        0x4240B26, {0}, 0},
            {"SetFEnv",        0x4240CD8, {0}, 0},
            {"Require",        0x26E5E10, {0}, 0},
            {"Warn",           0x286CBF2, {0}, 0},
            {"PluginManager",  0x42406EE, {0}, 0},
            {"UserSettings",   0x271693E, {0}, 0},
            {"VersionFn",      0x4240A80, {0}, 0},
            {"StatsFn",        0x42408C0, {0}, 0},
            {"LuaGRunError",   0x2875E09, {0}, 0},
        };

        bool ok = true;
        for (const auto& fn : funcs) {
            const uintptr_t addr = process::g_process.get_module_base() + fn.rva;
            auto bytes = process::Memory::read_bytes(addr, fn.prologue_len);
            if (bytes.size() != fn.prologue_len ||
                std::memcmp(bytes.data(), fn.prologue, fn.prologue_len) != 0) {
                spdlog::warn("luau: {} prologue mismatch - skipping", fn.name);
                ok = false;
                continue;
            }
            g_dumper.add_internal_offset("Functions", fn.name, fn.rva, "",
                                         FieldType::Pointer);
        }
        // No prologue data for the reg-table entries - readability check only.
        for (const auto& fn : base_api) {
            const uintptr_t addr = process::g_process.get_module_base() + fn.rva;
            auto bytes = process::Memory::read_bytes(addr, 1);
            if (bytes.empty()) {
                spdlog::warn("luau: {} unreadable - skipping", fn.name);
                continue;
            }
            g_dumper.add_internal_offset("Functions", fn.name, fn.rva, "",
                                         FieldType::Pointer);
        }
        return ok;
    }

    auto dump() -> bool {
        const auto pattern = make_pattern();
        const auto module_base = process::g_process.get_module_base();

        // Sober maps ~12 copies of libroblox.so at descending addresses; the
        // active one may sit below the first mapping. Collect every copy's
        // ELF base so RVAs are computed against the containing image.
        std::vector<uintptr_t> image_bases;
        for (const auto& region : process::g_process.get_regions()) {
            if (!region.readable) continue;
            if (region.pathname.find("memfd") == std::string::npos &&
                !region.pathname.empty()) {
                continue;
            }
            image_bases.push_back(region.start);
        }

        auto resolve_image_base = [&](uintptr_t addr) -> uintptr_t {
            // Candidates include mid-image text chunks; a real ELF base has
            // the \x7fELF magic. Pick the nearest valid one below addr.
            std::vector<uintptr_t> cands = image_bases;
            cands.push_back(module_base);
            std::sort(cands.rbegin(), cands.rend());
            for (const auto base : cands) {
                if (base > addr) continue;
                auto hdr = process::Memory::read_bytes(base, 4);
                if (hdr.size() == 4 && hdr[0] == 0x7F && hdr[1] == 'E' &&
                    hdr[2] == 'L' && hdr[3] == 'F') {
                    return base;
                }
            }
            return module_base;
        };

        auto find_region = [&](uintptr_t addr) -> const process::MemoryRegion* {
            for (const auto& r : process::g_process.get_regions()) {
                if (addr >= r.start && addr < r.end) return &r;
            }
            return nullptr;
        };

        uintptr_t check_addr = 0;
        uintptr_t global_ptr_rva = 0;
        size_t budget_bytes = 0;
        int rejected = 0;

        for (const auto& region : process::g_process.get_regions()) {
            if (!region.readable || !region.executable) continue;
            if (region.pathname.find("memfd") == std::string::npos) continue;
            if (region.size > 512ull * 1024 * 1024) continue;

            for (uintptr_t off = 0; off < region.size;) {
                const size_t take =
                    std::min<size_t>(4ull * 1024 * 1024, region.size - off);
                auto buf = process::Memory::read_bytes(region.start + off, take);
                off += take;
                if (buf.empty()) continue;
                budget_bytes += take;
                if (budget_bytes > 256ull * 1024 * 1024 || rejected > 50) break;

                for (size_t i = 0; i + pattern.bytes.size() <= buf.size(); ++i) {
                    bool ok = true;
                    for (size_t j = 0; j < pattern.bytes.size(); ++j) {
                        if (!pattern.mask[j] && buf[i + j] != pattern.bytes[j]) {
                            ok = false;
                            break;
                        }
                    }
                    if (!ok) continue;

                    const uintptr_t cand =
                        region.start + off - take + i;
                    // A real hit lives in executable memory and its rip-global
                    // target lives in writable memory (bss/data).
                    const auto* cr = find_region(cand);
                    if (!cr || !cr->executable) {
                        rejected++;
                        continue;
                    }
                    const uintptr_t gtarget =
                        cand + k_rip_mov_off + 3 + 4 +
                        [&] {
                            int32_t d = 0;
                            std::memcpy(&d,
                                        buf.data() + i + k_rip_mov_off + 3, 4);
                            return d;
                        }();
                    const auto* gr = find_region(gtarget);
                    if (!gr || !gr->writable) {
                        rejected++;
                        continue;
                    }

                    check_addr = cand;
                    // Resolve the global pointer's rip-relative target
                    // (absolute for now; converted to RVA after we know which
                    // image contains it).
                    const size_t disp_off = i + k_rip_mov_off + 3;
                    int32_t disp = 0;
                    std::memcpy(&disp, buf.data() + disp_off, 4);
                    global_ptr_rva =
                        region.start + off - take + disp_off + 4 + disp;
                    break;
                }
                if (check_addr) break;
            }
            if (check_addr) break;
        }

        if (!check_addr) {
            spdlog::error("luau: luaD_checkstack signature not found");
            return false;
        }

        const uintptr_t image_base = resolve_image_base(check_addr);
        const size_t check_rva = check_addr - image_base;
        g_dumper.add_internal_offset("Luau", "LuaDCheckStack", check_rva, "",
                            FieldType::Pointer);
        if (global_ptr_rva) {
            g_dumper.add_internal_offset("Luau", "VmAuxStruct",
                                global_ptr_rva - image_base, "",
                                FieldType::Pointer);
        }

        // lua_State field offsets:
        //   top/stack       - luaD_checkstack accesses
        //   global          - luaT_gettmbyobj: *(L+0x20) = global_State
        g_dumper.add_internal_offset("lua_State", "top", 0x8, "", FieldType::Pointer);
        g_dumper.add_internal_offset("lua_State", "stack_end", 0x10, "",
                                     FieldType::Pointer);
        g_dumper.add_internal_offset("lua_State", "global", 0x20, "",
                                     FieldType::Pointer);
        g_dumper.add_internal_offset("lua_State", "stack", 0x28, "",
                                     FieldType::Pointer);

        // global_State offsets (from luaT_gettmbyobj TM lookups):
        //   *(global + 8*event + 808)  -> tagmethod cache entry
        //   *(global + 8*event + 1088) -> tagmethod name string
        g_dumper.add_internal_offset("global_State", "TmCache", 808, "",
                                     FieldType::Pointer);
        g_dumper.add_internal_offset("global_State", "TmNames", 1088, "",
                                     FieldType::Pointer);

        // GC control/state (from lua_gc switch + luaC_fullgc):
        g_dumper.add_internal_offset("global_State", "GCGoal", 56, "",
                                     FieldType::UInt32);
        g_dumper.add_internal_offset("global_State", "GCStepMul", 60, "",
                                     FieldType::UInt32);
        g_dumper.add_internal_offset("global_State", "GCMulParam", 64, "",
                                     FieldType::UInt32);
        g_dumper.add_internal_offset("global_State", "GCPaused", 72, "",
                                     FieldType::UInt64);
        g_dumper.add_internal_offset("global_State", "TotalBytes", 80, "",
                                     FieldType::UInt64);
        g_dumper.add_internal_offset("global_State", "GCRunning", 89, "",
                                     FieldType::Bool);

        // External-getgc enumeration entry: circular page list.
        //   head = *(global + 0x300); next page = *(page + 0x18);
        //   end when next == head. Mark byte at page + 3.
        g_dumper.add_internal_offset("global_State", "GCPageListHead", 0x300, "",
                                     FieldType::Pointer);
        g_dumper.add_internal_offset("GCoPage", "Next", 0x18, "",
                                     FieldType::Pointer);
        g_dumper.add_internal_offset("GCoPage", "Mark", 0x3, "", FieldType::UInt8);

        // TValue: value union at +0, type tag (dword) at +0xC;
        // sizeof(TValue) == 16 ((top - stack) >> 4 in checkstack).
        g_dumper.add_internal_offset("TValue", "Value", 0x0, "", FieldType::None);
        g_dumper.add_internal_offset("TValue", "Tag", 0xC, "", FieldType::UInt32);

        spdlog::info("Luau: luaD_checkstack at RVA 0x{:X}, global ptr slot 0x{:X}",
                     check_rva, global_ptr_rva);

        dump_internal_functions();
        return true;
    }

} // namespace dumper::stages::luau
