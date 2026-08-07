#include "diag.h"
#include "dumper/dumper.h"
#include "process/helpers/helpers.h"
#include "process/memory/memory.h"
#include "process/rtti/rtti.h"
#include "roblox/instance/instance.h"
#include <spdlog/spdlog.h>

namespace dumper::stages::diag {

    namespace {
        void walk(roblox::Instance node, int depth, int& budget) {
            if (budget <= 0 || depth > 12) return;
            --budget;
            auto cn = node.get_class_name();
            auto nm = node.get_name();
            spdlog::info("[diag] {}{} ({})",
                         std::string(depth, ' '), nm.value_or("?"), cn.value_or("?"));
            for (const auto& child : node.get_children()) {
                walk(child, depth + 1, budget);
            }
        }

        void dump_rtti_names(uintptr_t addr, size_t max_off, const char* label) {
            for (size_t off = 0; off < max_off; off += 8) {
                auto v = process::Memory::read<uintptr_t>(addr + off);
                if (!v || *v < 0x10000) continue;
                auto rtti = process::Rtti::scan_rtti(*v);
                if (rtti && !rtti->name.empty()) {
                    spdlog::info("[diag] {} +0x{:03x} -> {}", label, off, rtti->name);
                }
            }
        }

        void dump_strings(uintptr_t addr, size_t max_off, const char* label) {
            for (size_t off = 0; off < max_off; off += 8) {
                auto v = process::Memory::read<uintptr_t>(addr + off);
                if (!v || *v < 0x10000) continue;
                auto s = process::Memory::read_roblox_string(*v);
                if (s && s->size() > 4 && s->find("asset") != std::string::npos) {
                    spdlog::info("[diag] {} +0x{:03x} -> '{}'", label, off, *s);
                }
            }
        }
    } // namespace

    auto dump() -> bool {
        spdlog::info("[diag] === workspace tree ===");
        int budget = 400;
        if (dumper::g_workspace) {
            walk(*dumper::g_workspace, 0, budget);
        }

        // Bridge diagnostics: ReplicatedStorage children and StringValue reads
        {
            const auto rs = dumper::g_data_model.find_first_child_of_class("ReplicatedStorage");
            if (!rs) {
                spdlog::info("[diag] ReplicatedStorage NOT FOUND");
            } else {
                spdlog::info("[diag] ReplicatedStorage addr = 0x{:x}", rs->get_address());
                int idx = 0;
                for (const auto& child : rs->get_children()) {
                    auto cn = child.get_class_name();
                    auto nm = child.get_name();
                    std::string val = "-";
                    auto s = process::Memory::read_sso_string(
                        child.get_address() + roblox::offsets::Value::Value);
                    if (s) {
                        if (s->size() > 60) val = s->substr(0, 60);
                        else val = *s;
                    }
                    spdlog::info("[diag] RS child '{}' ({}) Value->'{}'", nm.value_or("?"),
                                 cn.value_or("?"), val);
                    if (cn && *cn == "StringValue") {
                        const auto a = child.get_address();
                        const auto voff = roblox::offsets::Value::Value;
                        auto hdr = process::Memory::read<uint8_t>(a + voff);
                        std::string via_roblox = "-";
                        if (hdr) {
                            auto r = process::Memory::read_roblox_string(a + voff);
                            if (r) via_roblox = (r->size() > 50) ? r->substr(0, 50) : *r;
                        }
                        std::string via_longptr = "-";
                        auto q0 = process::Memory::read<uintptr_t>(a + voff);
                        auto q1 = process::Memory::read<uintptr_t>(a + voff + 8);
                        auto q2 = process::Memory::read<uintptr_t>(a + voff + 0x10);
                        if (q0 && q2 && *q0 < 0x10000 && *q2 >= 0x10000 && *q2 <= 0x7fffffffffffULL) {
                            auto rr = process::Memory::read_string(*q2, std::min<uint64_t>(*q1, 0x2000));
                            if (rr) via_longptr = (rr->size() > 50) ? rr->substr(0, 50) : *rr;
                        }
                        spdlog::info("[diag]   val@+0x{:x}: roblox='{}' longptr='{}' q0={} q1={}",
                                     voff, via_roblox, via_longptr, q0 ? *q0 : 0, q1 ? *q1 : 0);

                        // Name field decode attempts
                        const auto noff = roblox::offsets::Instance::Name;
                        auto nhdr = process::Memory::read<uint8_t>(a + noff);
                        std::string n_inline = "-";
                        if (nhdr) {
                            auto r = process::Memory::read_roblox_string(a + noff);
                            if (r) n_inline = *r;
                        }
                        auto nq0 = process::Memory::read<uintptr_t>(a + noff);
                        std::string n_deref = "-";
                        if (nq0 && *nq0 >= 0x10000 && *nq0 <= 0x7fffffffffffULL) {
                            auto r1 = process::Memory::read_roblox_string(*nq0);
                            auto r2 = process::Memory::read_sso_string(*nq0);
                            auto r3 = process::Memory::read_string(*nq0, 64);
                            if (r1) n_deref = "roblox:'" + *r1 + "'";
                            else if (r2) n_deref = "sso:'" + *r2 + "'";
                            else if (r3) n_deref = "raw:'" + *r3 + "'";
                            auto nlen = process::Memory::read<uint64_t>(*nq0 + 8);
                            auto nptr = process::Memory::read<uintptr_t>(*nq0);
                            auto r4 = (nptr && *nptr >= 0x10000)
                                          ? process::Memory::read_string(*nptr, std::min<uint64_t>(nlen ? *nlen : 0, 64))
                                          : std::nullopt;
                            if (r4) n_deref += " libc++:'" + *r4 + "'";
                        }
                        spdlog::info("[diag]   name@+0x{:x}: inline='{}' q0=0x{:x} deref:{}", noff,
                                     n_inline, nq0 ? *nq0 : 0, n_deref);
                    }
                    ++idx;
                }
            }
        }

        // Local player
        const auto players = dumper::g_data_model.find_first_child_of_class("Players");
        if (players) {
            const auto lp_off = process::Rtti::find(players->get_address(), "Player@RBX", 0x400, 0x8);
            spdlog::info("[diag] Players found, LocalPlayer@ +0x{:x}", lp_off ? *lp_off : 0xFFFF);
            if (lp_off) {
                auto lp = process::Memory::read<uintptr_t>(players->get_address() + *lp_off);
                if (lp && *lp >= 0x10000) {
                    spdlog::info("[diag] Player addr = 0x{:x}", *lp);
                    dump_rtti_names(*lp, 0x1000, "Player");

                    const auto char_off = process::Rtti::find(*lp, "ModelInstance@RBX", 0x400, 0x8);
                    if (char_off) {
                        auto char_addr = process::Memory::read<uintptr_t>(*lp + *char_off);
                        if (char_addr && *char_addr >= 0x10000) {
                            auto hum = roblox::Instance(*char_addr).find_first_child_of_class("Humanoid");
                            if (hum) {
                                spdlog::info("[diag] Humanoid addr = 0x{:x}", hum->get_address());
                                dump_rtti_names(hum->get_address(), 0x1000, "Humanoid");
                            } else {
                                spdlog::info("[diag] no Humanoid in character");
                            }
                        }
                    }
                }
            }
        }

        // UserInputService
        const auto uis = dumper::g_data_model.find_first_child_of_class("UserInputService");
        if (uis) {
            spdlog::info("[diag] UIS addr = 0x{:x}", uis->get_address());
            dump_rtti_names(uis->get_address(), 0x1000, "UIS");
        }

        // Do the target classes exist anywhere in the binary?
        {
            auto names = process::helpers::find_rtti_globals({
                "Animator@RBX", "PlayerMouse@RBX", "WindowInputState@RBX",
                "AirProperties@RBX", "Weld@RBX", "WeldConstraint@RBX",
                "ClickDetector@RBX", "ParticleEmitter@RBX", "UnionOperation@RBX",
                "AnimationTrack@RBX", "Sound@RBX", "Humanoid@RBX"});
            for (const auto& [cls, addr] : names) {
                spdlog::info("[diag] global RTTI '{}' first at 0x{:x}", cls,
                             addr ? *addr : 0);
            }
        }

        // World
        const auto world_off = dumper::g_dumper.get_offset("Workspace", "World");
        if (world_off) {
            auto world_addr = process::Memory::read<uintptr_t>(
                dumper::g_workspace->get_address() + *world_off);
            if (world_addr && *world_addr >= 0x10000) {
                spdlog::info("[diag] World addr = 0x{:x}", *world_addr);
                dump_rtti_names(*world_addr, 0x1000, "World");
                const auto dens = process::helpers::find_offset<float>(*world_addr, 1.225f, 0x1400, 0x4);
                spdlog::info("[diag] World 1.225f at +0x{:x}", dens ? *dens : 0xFFFF);
                if (dens) {
                    for (size_t k = *dens; k < *dens + 0x60 && k + 4 <= 0x1400; k += 4) {
                        auto f = process::Memory::read<float>(*world_addr + k);
                        if (f) spdlog::info("[diag]   World +0x{:03x} = {}", k, *f);
                    }
                }
            }
        }

        // Decal / Texture strings
        if (dumper::g_workspace) {
            const auto spawn = dumper::g_workspace->find_first_child("SpawnLocation");
            if (spawn) {
                const auto decal = spawn->find_first_child_of_class("Decal");
                if (decal) {
                    spdlog::info("[diag] Decal addr = 0x{:x}", decal->get_address());
                    dump_strings(decal->get_address(), 0x800, "Decal");
                    auto slot = process::Memory::read<uintptr_t>(decal->get_address() + 0x178);
                    if (slot && *slot >= 0x10000) {
                        auto b = process::Memory::read_bytes(*slot, 0x40);
                        if (!b.empty()) {
                            std::string hex;
                            for (size_t k = 0; k < b.size(); ++k) {
                                char buf[4];
                                std::snprintf(buf, sizeof(buf), "%02x ", b[k]);
                                hex += buf;
                            }
                            spdlog::info("[diag] Decal content obj@0x{:x}: {}", *slot, hex);
                        }
                        auto len = process::Memory::read<uint64_t>(*slot + 8);
                        auto dp = process::Memory::read<uintptr_t>(*slot + 0x10);
                        spdlog::info("[diag] Decal content q0=0x{:x} len={} dataptr=0x{:x}",
                                     slot ? *slot : 0,
                                     len ? *len : 0, dp ? *dp : 0);
                    }
                } else {
                    spdlog::info("[diag] no Decal in SpawnLocation");
                }
            }
            const auto baseplate = dumper::g_workspace->find_first_child("Baseplate");
            if (baseplate) {
                const auto texture = baseplate->find_first_child_of_class("Texture");
                if (texture) {
                    spdlog::info("[diag] Texture addr = 0x{:x}", texture->get_address());
                    dump_strings(texture->get_address(), 0x800, "Texture");
                } else {
                    spdlog::info("[diag] no Texture in Baseplate");
                }
            }
        }

        return true;
    }

} // namespace dumper::stages::diag
