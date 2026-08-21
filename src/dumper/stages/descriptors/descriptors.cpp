// Descriptor offset acquisition for Linux/Sober, adapted from charm's
// offset_acquisition_method.md plus static analysis of this client's registrar.
//
// This build's reflection descriptors are built at runtime by sub_1D2A302:
//   desc+0x00  vtable
//   desc+0x08  interned Name object (32-byte heap block, inline libc++ string)
//   desc+0x28  interned Name("Function")
//   desc+0x30  owner/context object
//   desc+0x80  bound wrapper function (stored separately by the caller)
//
// Unlike the Windows client, +0x8 is NOT a raw char* to the literal, so the
// scan works in two passes:
//   1. find the interned Name objects on the anonymous heap by locating the
//      method-name bytes (short form: data at obj+1; long form: data ptr at
//      obj+0x10)
//   2. scan module data regions for 0x10-aligned slots holding those object
//      pointers, then validate the bound function at +0x80/+0x78
//
// Target order (from charm silent.cpp): MouseHit, MouseLowerHit, Raycast,
// FindPartOnRayWithIgnoreList.
#include "descriptors.h"
#include "dumper/dumper.h"
#include "process/memory/memory.h"
#include "process/process.h"
#include <algorithm>
#include <cstring>
#include <optional>
#include <spdlog/spdlog.h>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace dumper::stages::descriptors {

    namespace {
        constexpr size_t k_desc_name_off = 0x8;
        constexpr size_t k_bound_fn_linux = 0x80;
        constexpr size_t k_bound_fn_windows = 0x78;
        constexpr size_t k_desc_alignment = 0x10;
        constexpr size_t k_chunk_size = 1024 * 1024;

        struct Target {
            const char* name;
            const char* offset_name;
        };

        // NOTE: MouseHit/MouseLowerHit do not exist in this client (Windows-only
        // method names); only the WorldRoot ray descriptors are available.
        constexpr Target k_targets[] = {
            {"Raycast", "kWorldRootRaycastDescriptorRva"},
            {"FindPartOnRayWithIgnoreList",
             "kWorldRootFindPartOnRayWithIgnoreListDescriptorRva"},
        };

        auto is_executable(uintptr_t addr) -> bool {
            for (const auto& region : process::g_process.get_regions()) {
                if (region.executable && addr >= region.start && addr < region.end) {
                    return true;
                }
            }
            return false;
        }

        auto is_heap_candidate(uintptr_t addr) -> bool {
            if (addr < 0x10000 || addr > 0x7fffffffffffULL) return false;
            for (const auto& region : process::g_process.get_regions()) {
                if (addr >= region.start && addr < region.end) {
                    // Interned names live on the anonymous heap (mimalloc),
                    // not in the module image.
                    return region.executable == false &&
                           (region.pathname.empty() ||
                            region.pathname.rfind("[anon", 0) == 0);
                }
            }
            return false;
        }

        struct RegionRef {
            uintptr_t start;
            size_t size;
        };

        // Anonymous writable regions (the malloc'd heap), described not cached.
        // The allocator here is mimalloc, whose segments show up in maps as
        // "[anon:mimalloc]" - a non-empty pathname - so accept [anon* too.
        auto is_anon_heap(const std::string& pathname) -> bool {
            return pathname.empty() || pathname.rfind("[anon", 0) == 0;
        }

        auto heap_regions() -> std::vector<RegionRef> {
            std::vector<RegionRef> out;
            for (const auto& region : process::g_process.get_regions()) {
                if (!region.readable || !region.writable) continue;
                if (!is_anon_heap(region.pathname)) continue;
                if (region.size > 512ull * 1024 * 1024) continue; // guard huge reservations
                out.push_back({region.start, region.size});
            }
            return out;
        }

        // Pass A (streaming): locate interned Name objects.
        //  - short form: header byte (len<<1, even) directly before the chars
        //  - long form : chars live elsewhere; collect their addresses first,
        //    then a second streaming pass finds qwords equal to them
        //    (obj+0x10 holds the data pointer).
        auto find_name_objects(const std::vector<RegionRef>& regions)
            -> std::unordered_map<size_t, std::unordered_set<uintptr_t>> {
            std::unordered_map<size_t, std::unordered_set<uintptr_t>> out;
            std::unordered_map<uintptr_t, size_t> long_data_addrs; // data addr -> target idx

            for (const auto& reg : regions) {
                for (uintptr_t off = 0; off < reg.size;) {
                    const size_t take = std::min<size_t>(k_chunk_size, reg.size - off);
                    auto chunk = process::Memory::read_bytes(reg.start + off, take);
                    off += take;
                    if (chunk.empty()) continue;

                    for (size_t ti = 0; ti < std::size(k_targets); ++ti) {
                        const std::string name = k_targets[ti].name;
                        const size_t nlen = name.size();
                        size_t pos = 0;
                        while (true) {
                            pos = static_cast<size_t>(
                                std::search(chunk.begin() + pos, chunk.end(),
                                            name.begin(), name.end()) -
                                chunk.begin());
                            if (pos + nlen > chunk.size()) break;

                            if (pos >= 1) {
                                const uint8_t hdr = chunk[pos - 1];
                                if (hdr == (nlen << 1) && !(hdr & 1)) {
                                    out[ti].insert(reg.start + off - take + pos - 1);
                                } else {
                                    long_data_addrs.emplace(reg.start + off - take + pos, ti);
                                }
                            }
                            pos += 1;
                        }
                    }
                }
            }

            if (!long_data_addrs.empty()) {
                for (const auto& reg : regions) {
                    for (uintptr_t off = 0; off < reg.size;) {
                        const size_t take = std::min<size_t>(k_chunk_size, reg.size - off);
                        auto chunk = process::Memory::read_bytes(reg.start + off, take);
                        off += take;
                        if (chunk.empty()) continue;

                        for (size_t q = 0x10; q + 8 <= chunk.size(); q += 8) {
                            uintptr_t v = 0;
                            std::memcpy(&v, chunk.data() + q, 8);
                            const auto it = long_data_addrs.find(v);
                            if (it == long_data_addrs.end()) continue;
                            if (chunk[q - 0x10] & 1) { // capacity flag bit set
                                out[it->second].insert(reg.start + off - take + q - 0x10);
                            }
                        }
                    }
                }
            }
            return out;
        }

        // Scan module data regions for 0x10-aligned slots whose +0x8 holds one
        // of the interned Name object addresses.
        auto find_descriptor_slots(
            const std::unordered_map<size_t, std::unordered_set<uintptr_t>>&
                name_objects)
            -> std::unordered_map<size_t, std::vector<uintptr_t>> {
            std::unordered_map<size_t, std::vector<uintptr_t>> hits;

            // Flat lookup: object address -> target index.
            std::unordered_map<uintptr_t, size_t> obj_to_target;
            for (const auto& [ti, objs] : name_objects) {
                for (const auto o : objs) obj_to_target.emplace(o, ti);
            }
            if (obj_to_target.empty()) return hits;

            for (const auto& region : process::g_process.get_regions()) {
                if (!region.readable || region.executable) continue;

                // Descriptor globals live in .bss, which the kernel backs with
                // ANONYMOUS pages appended to the memfd mappings - so scan both
                // memfd-backed data sections and anonymous rw regions.
                const bool memfd =
                    region.pathname.find("memfd") != std::string::npos;
                const bool anon =
                    region.writable && is_anon_heap(region.pathname);
                if (!memfd && !anon) continue;

                for (uintptr_t off = 0; off < region.size;) {
                    const size_t take =
                        std::min<size_t>(k_chunk_size, region.size - off);
                    auto chunk =
                        process::Memory::read_bytes(region.start + off, take);
                    if (!chunk.empty()) {
                        const uintptr_t chunk_base = region.start + off;
                        // Descriptor globals are 8-mod-16 aligned in this build;
                        // scan every 8-aligned slot.
                        for (size_t q = k_desc_name_off; q + 8 <= chunk.size();
                             q += 8) {
                            uintptr_t v = 0;
                            std::memcpy(&v, chunk.data() + q, 8);
                            const auto it = obj_to_target.find(v);
                            if (it == obj_to_target.end()) continue;
                            const uintptr_t slot =
                                chunk_base + q - k_desc_name_off;
                            hits[it->second].push_back(slot);
                        }
                    }
                    off += take;
                }
            }
            return hits;
        }
    } // namespace
    // Statically-derived descriptor RVAs for known client builds (IDA xref of
    // the method-name literal -> registration call -> lea rdi descriptor
    // global; wrapper stored at desc+0x80). Validated against live memory
    // before use, so a mismatched build falls back to scanning.
    struct StaticDesc {
        const char* name;
        const char* offset_name;
        size_t desc_rva;
        size_t wrapper_rva;
    };

    constexpr StaticDesc k_static_descs[] = {
        {"Raycast", "kWorldRootRaycastDescriptorRva", 0x750BD78, 0x63F020A},
        {"FindPartOnRayWithIgnoreList",
         "kWorldRootFindPartOnRayWithIgnoreListDescriptorRva", 0x750C650,
         0x63EBB4E},
    };

    auto try_static(const StaticDesc& sd, uintptr_t module_base) -> bool {
        const uintptr_t desc = module_base + sd.desc_rva;

        // The bound-function slot must hold exactly the known wrapper.
        auto wrapper = process::Memory::read<uintptr_t>(desc + k_bound_fn_linux);
        if (!wrapper || *wrapper != module_base + sd.wrapper_rva) {
            return false;
        }
        if (!is_executable(*wrapper)) {
            return false;
        }

        // Name sanity: desc+0x8 -> interned Name object -> libc++ string.
        auto name_obj = process::Memory::read<uintptr_t>(desc + k_desc_name_off);
        if (name_obj && *name_obj > 0x10000) {
            const auto s = process::Memory::read_roblox_string(*name_obj);
            if (s && *s != sd.name) {
                spdlog::warn("Descriptors: '{}' name mismatch '{}' - skipping",
                             sd.name, *s);
                return false;
            }
        }

        dumper::g_dumper.add_offset("Descriptors", sd.offset_name, sd.desc_rva);
        dumper::g_dumper.add_offset("Descriptors",
                                    (std::string(sd.offset_name) + "BoundFn").c_str(),
                                    sd.wrapper_rva);
        spdlog::info("Descriptor '{}' validated at RVA 0x{:X} (wrapper 0x{:X})",
                     sd.name, sd.desc_rva, sd.wrapper_rva);
        return true;
    }

    // Slow path for unknown builds: stream the heap for interned Name objects,
    // then locate descriptor slots in module/bss data.
    auto scan_missing(std::vector<uintptr_t>& found, uintptr_t module_base)
        -> bool {
        spdlog::warn("Descriptors: falling back to memory scan (slow)");
        const auto regions = heap_regions();
        const auto name_objects = find_name_objects(regions);
        const auto slots = find_descriptor_slots(name_objects);

        bool ok = true;
        for (size_t ti = 0; ti < std::size(k_targets); ++ti) {
            const auto it = slots.find(ti);
            if (it == slots.end() || it->second.empty()) {
                spdlog::error("Failed to find descriptor for '{}'", k_targets[ti].name);
                ok = false;
                continue;
            }

            std::vector<uintptr_t> valid;
            for (const auto slot : it->second) {
                for (const size_t bound_off :
                     {k_bound_fn_linux, k_bound_fn_windows}) {
                    auto fn = process::Memory::read<uintptr_t>(slot + bound_off);
                    if (fn && *fn > 0x10000 && is_executable(*fn)) {
                        valid.push_back(slot);
                        break;
                    }
                }
            }
            if (valid.empty()) {
                spdlog::error("Descriptor '{}': no callable bound function",
                              k_targets[ti].name);
                ok = false;
                continue;
            }

            uintptr_t best = valid.front();
            if (valid.size() > 1 && !found.empty()) {
                size_t best_dist = SIZE_MAX;
                for (const auto slot : valid) {
                    for (const auto known : found) {
                        const size_t d = slot > known ? slot - known : known - slot;
                        if (d < best_dist) best_dist = d, best = slot;
                    }
                }
            }

            const size_t rva = best - module_base;
            dumper::g_dumper.add_offset("Descriptors", k_targets[ti].offset_name, rva);
            auto wrapper = process::Memory::read<uintptr_t>(best + k_bound_fn_linux);
            if (wrapper && is_executable(*wrapper)) {
                dumper::g_dumper.add_offset(
                    "Descriptors",
                    (std::string(k_targets[ti].offset_name) + "BoundFn").c_str(),
                    *wrapper - module_base);
            }
            found.push_back(best);
            spdlog::info("Descriptor '{}' at 0x{:X} (RVA 0x{:X})", k_targets[ti].name,
                         best, rva);
        }
        return ok;
    }

    auto dump() -> bool {
        const auto module_base = process::g_process.get_module_base();
        spdlog::info("Descriptors: acquiring reflection descriptor RVAs (base=0x{:X})",
                     module_base);

        bool ok = true;
        bool need_scan = false;
        std::vector<uintptr_t> found;

        for (const auto& sd : k_static_descs) {
            if (try_static(sd, module_base)) {
                found.push_back(module_base + sd.desc_rva);
            } else {
                spdlog::warn("Descriptors: static candidate for '{}' did not "
                             "validate; will scan",
                             sd.name);
                need_scan = true;
            }
        }

        if (need_scan) {
            ok = scan_missing(found, module_base);
        }

        return ok;
    }

} // namespace dumper::stages::descriptors
