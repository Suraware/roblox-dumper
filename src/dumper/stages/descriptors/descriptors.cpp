// Port of charm's descriptor offset acquisition (offset_acquisition_method.md)
// from Windows to Linux/Sober:
//
//   descriptor slots are 0x10-aligned; each descriptor holds
//     +0x08  pointer to the exact method-name string
//     +0x78  bound function pointer (must be executable)
//     +0x90  getter implementation pointer (property descriptors only)
//     +0x08  final getter function pointer (inside getter impl)
//
//   descriptor RVA = descriptor_address - module_base
//
// Target order (from silent.cpp): MouseHit, MouseLowerHit, Raycast,
// FindPartOnRayWithIgnoreList.
#include "descriptors.h"
#include "dumper/dumper.h"
#include "process/memory/memory.h"
#include "process/process.h"
#include <optional>
#include <set>
#include <spdlog/spdlog.h>
#include <string>
#include <vector>

namespace dumper::stages::descriptors {

    namespace {
        constexpr size_t k_desc_name_off = 0x8;
        constexpr size_t k_desc_bound_fn = 0x78;
        constexpr size_t k_desc_getter_off = 0x90;
        constexpr size_t k_getter_fn_off = 0x8;
        constexpr size_t k_desc_alignment = 0x10;
        constexpr size_t k_scan_window = 0x8000;
        constexpr size_t k_cluster_radius = 0x28000; // charm descriptor_scan_radius

        struct Target {
            const char* name;
            const char* offset_name;
            bool property; // has getter at +0x90
        };

        constexpr Target k_targets[] = {
            {"MouseHit", "kMouseHitDescriptorRva", true},
            {"MouseLowerHit", "kMouseLowerHitDescriptorRva", true},
            {"Raycast", "kWorldRootRaycastDescriptorRva", false},
            {"FindPartOnRayWithIgnoreList",
             "kWorldRootFindPartOnRayWithIgnoreListDescriptorRva", false},
        };

        struct SlotResult {
            uintptr_t address;
            bool bound_ok;
            bool getter_ok;
        };

        auto is_executable(uintptr_t addr) -> bool {
            for (const auto& region : process::g_process.get_regions()) {
                if (region.executable && addr >= region.start && addr < region.end) {
                    return true;
                }
            }
            return false;
        }

        // Validate a candidate slot: +0x8 must point exactly at the target
        // string, +0x78 must be an executable pointer. For property
        // descriptors, +0x90 (getter impl) and its +0x8 must be executable.
        auto check_slot(uintptr_t slot, uintptr_t string_addr, bool property) -> SlotResult {
            SlotResult result{slot, false, false};

            auto name_ptr = process::Memory::read<uintptr_t>(slot + k_desc_name_off);
            if (!name_ptr || *name_ptr != string_addr) {
                return result;
            }

            auto bound_fn = process::Memory::read<uintptr_t>(slot + k_desc_bound_fn);
            if (!bound_fn || !is_executable(*bound_fn)) {
                return result;
            }
            result.bound_ok = true;

            if (property) {
                auto getter = process::Memory::read<uintptr_t>(slot + k_desc_getter_off);
                if (!getter || !is_executable(*getter)) {
                    return result;
                }
                auto final_fn = process::Memory::read<uintptr_t>(*getter + k_getter_fn_off);
                if (final_fn && is_executable(*final_fn)) {
                    result.getter_ok = true;
                }
            }

            return result;
        }

        // Find all descriptor candidates for a target string.
        auto find_candidates(const Target& target)
            -> std::vector<SlotResult> {
            std::vector<SlotResult> candidates;
            std::set<uintptr_t> seen;

            for (const auto match : process::Memory::scan_string_module(target.name)) {
                for (size_t off = 0; off <= k_scan_window; off += k_desc_alignment) {
                    const uintptr_t slots[] = {match - off, match + off};
                    for (const auto slot : slots) {
                        if (seen.contains(slot)) {
                            continue;
                        }
                        seen.insert(slot);

                        auto result = check_slot(slot, match, target.property);
                        if (result.bound_ok) {
                            candidates.push_back(result);
                        }
                    }
                }
            }

            return candidates;
        }

        auto distance(uintptr_t a, uintptr_t b) -> size_t {
            return a > b ? a - b : b - a;
        }

        // Prefer candidates clustered near previously-found descriptors.
        auto pick_candidate(const std::vector<SlotResult>& candidates,
                            const std::vector<uintptr_t>& found) -> SlotResult {
            if (candidates.empty()) {
                return {};
            }
            if (candidates.size() == 1 || found.empty()) {
                return candidates[0];
            }

            size_t best_score = SIZE_MAX;
            SlotResult best{};
            for (const auto& candidate : candidates) {
                size_t min_dist = SIZE_MAX;
                for (const auto known : found) {
                    min_dist = std::min(min_dist, distance(candidate.address, known));
                }
                // Getter-validated candidates win ties.
                const size_t score =
                    (candidate.getter_ok ? 0 : 1) * 0x100000 + min_dist;
                if (score < best_score) {
                    best_score = score;
                    best = candidate;
                }
            }
            return best;
        }
    } // namespace

    auto dump() -> bool {
        const auto module_base = process::g_process.get_module_base();
        std::vector<uintptr_t> found;
        bool ok = true;

        spdlog::info("Descriptors: scanning for reflection descriptor slots (base=0x{:X})",
                     module_base);

        for (const auto& target : k_targets) {
            const auto candidates = find_candidates(target);

            if (candidates.empty()) {
                spdlog::error("Failed to find descriptor for '{}'", target.name);
                ok = false;
                continue;
            }

            if (candidates.size() > 1) {
                spdlog::warn("Descriptor '{}': {} candidates, picking clustered one",
                             target.name, candidates.size());
            }

            const auto best = pick_candidate(candidates, found);
            if (!best.bound_ok) {
                spdlog::error("Failed to find descriptor for '{}'", target.name);
                ok = false;
                continue;
            }

            // Runtime validation: descriptor + 0x8 resolves to the expected name.
            auto name_ptr = process::Memory::read<uintptr_t>(best.address + k_desc_name_off);
            if (name_ptr) {
                const auto resolved =
                    process::Memory::read_string(*name_ptr, target.name ? 64 : 0);
                if (resolved && *resolved != target.name) {
                    spdlog::warn("Descriptor '{}': name mismatch '{}'", target.name,
                                 *resolved);
                }
            }

            const size_t rva = best.address - module_base;
            dumper::g_dumper.add_offset("Descriptors", target.offset_name, rva);
            found.push_back(best.address);

            spdlog::info(
                "Descriptor '{}' at 0x{:X} (RVA 0x{:X}){}", target.name,
                best.address, rva,
                target.property ? (best.getter_ok ? " [getter verified]" : " [getter unverified]")
                                : "");
        }

        return ok;
    }
} // namespace dumper::stages::descriptors
