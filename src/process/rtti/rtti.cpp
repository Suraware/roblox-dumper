#include "process/rtti/rtti.h"
#include "process/memory/memory.h"
#include "rtti.h"
#include <algorithm>
#include <cstring>
#include <sstream>
#include <spdlog/spdlog.h>
#include <vector>

namespace process {

    auto demangle_rtti(const std::string &m) -> std::string {
        // Handle MSVC: .?AV...@@ -> ... (strip .?AV prefix and @@ suffix)
        if (m.size() > 4 && m.substr(0, 4) == ".?AV") {
            auto r = m.substr(4);
            size_t at = r.find("@@");
            if (at != std::string::npos) r = r.substr(0, at);
            return r;
        }
        // Handle Itanium: NxxxExxx -> xxx@xxx
        if (m.size() > 2 && m[0] == 'N') {
            std::vector<std::string> parts;
            size_t p = 1;
            while (p < m.size() && m[p] != 'E') {
                if (!std::isdigit(m[p])) return m;
                size_t end = p;
                while (end < m.size() && std::isdigit(m[end])) ++end;
                int n = std::stoi(m.substr(p, end - p));
                p = end;
                if (p + n > m.size()) return m;
                parts.push_back(m.substr(p, n));
                p += n;
            }
            if (parts.empty()) return m;
            std::reverse(parts.begin(), parts.end());
            std::string r;
            for (auto &pt : parts) {
                if (!r.empty()) r += "@";
                r += pt;
            }
            return r;
        }
        return m;
    }

    auto rtti_lookup(uintptr_t object_addr) -> std::optional<std::string> {
        auto vtable = Memory::read<uintptr_t>(object_addr);
        if (!vtable || *vtable < 0x10000 || *vtable > 0x7fffffffffffULL) {
            return std::nullopt;
        }

        auto col_ptr = Memory::read<uintptr_t>(*vtable - 8);
        if (!col_ptr || *col_ptr < 0x10000 || *col_ptr > 0x7fffffffffffULL) {
            return std::nullopt;
        }

        // COL+8 = direct pointer to TypeDescriptor (working dumper approach)
        auto td_ptr = Memory::read<uintptr_t>(*col_ptr + 8);
        if (!td_ptr || *td_ptr < 0x10000 || *td_ptr > 0x7fffffffffffULL) {
            return std::nullopt;
        }

        // *td_ptr is already the name string (type_info+8 = name pointer)
        auto name_buf = Memory::read_bytes(*td_ptr, 256);
        if (name_buf.empty()) return std::nullopt;

        std::string raw(reinterpret_cast<const char*>(name_buf.data()));
        auto it = raw.find('\0');
        if (it != std::string::npos) raw = raw.substr(0, it);
        if (raw.empty()) return std::nullopt;

        return demangle_rtti(raw);
    }

    auto scan_rtti_range(const std::string& maps_str, uintptr_t base,
                         const std::string& needle) -> uintptr_t {
        const size_t chunk_size = 512 * 1024;

        struct Region { uint64_t start, end; };
        std::vector<Region> regs;
        std::istringstream iss(maps_str);
        std::string line;
        while (std::getline(iss, line)) {
            char perm[8]={}; uint64_t s=0,e=0;
            std::sscanf(line.c_str(), "%lx-%lx %4s", &s, &e, perm);
            if (s < base || s >= base + 0x10000000) continue;
            std::string p(perm);
            if (p == "rw-p" || p == "r--p")
                regs.push_back({s, e});
        }

        std::sort(regs.begin(), regs.end(),
                  [](auto &a, auto &b) { return a.start < b.start; });

        std::vector<Region> merged;
        for (auto &r : regs) {
            if (!merged.empty() && r.start <= merged.back().end + 0x1000)
                merged.back().end = std::max(merged.back().end, r.end);
            else
                merged.push_back(r);
        }

        for (auto &mr : merged) {
            uint64_t off = 0;
            while (off < mr.end - mr.start) {
                uint64_t addr = mr.start + off;
                size_t chunk_sz = std::min(chunk_size, (size_t)(mr.end - mr.start - off));
                auto chunk = Memory::read_bytes(addr, chunk_sz);
                if (chunk.empty()) { off += chunk_size; continue; }

                size_t lim = (chunk.size() / 8) * 8;
                for (size_t i = 0; i < lim; i += 8) {
                    uint64_t p = 0;
                    std::memcpy(&p, chunk.data() + i, 8);
                    if (p < 0x10000 || p > 0x7fffffffffffULL) continue;

                    auto nm = rtti_lookup(p);
                    if (nm && *nm == needle) {
                        return addr + i;
                    }
                }
                off += chunk_size;
            }
        }
        return 0;
    }

    auto Rtti::scan_rtti(uintptr_t address) -> std::optional<RttiInfo> {
        // Use the working dumper's approach: Itanium C++ ABI
        // vtable[-1] = type_info*, type_info+8 = name pointer
        auto nm = rtti_lookup(address);
        if (!nm) return std::nullopt;

        RttiInfo info{};
        info.name = *nm;
        // type_descriptor and hierarchy not easily available in Itanium ABI
        info.type_descriptor = 0;
        info.class_hierarchy_descriptor = 0;
        return info;
    }

    auto Rtti::find(uintptr_t base_address, const std::string& target_class, size_t max_offset,
                    size_t alignment) -> std::optional<size_t> {
        for (size_t offset = 0; offset < max_offset; offset += alignment) {
            uintptr_t current_address = base_address + offset;
            auto pointer_value = Memory::read<uintptr_t>(current_address);

            if (!pointer_value || *pointer_value < 0x10000) {
                continue;
            }

            auto rtti = scan_rtti(*pointer_value);
            if (rtti && rtti->name == target_class) {
                return offset;
            }
        }

        return std::nullopt;
    }

    auto Rtti::find_all(uintptr_t base_address, const std::string& target_class, size_t max_offset,
                        size_t alignment) -> std::vector<size_t> {
        std::vector<size_t> matches;

        for (size_t offset = 0; offset < max_offset; offset += alignment) {
            uintptr_t current_address = base_address + offset;
            auto pointer_value = Memory::read<uintptr_t>(current_address);

            if (!pointer_value || *pointer_value < 0x10000) {
                continue;
            }

            auto rtti = scan_rtti(*pointer_value);
            if (rtti && rtti->name == target_class) {
                matches.push_back(offset);
            }
        }

        return matches;
    }

    auto Rtti::find_deref(uintptr_t base_address, const std::string& target_class,
                          size_t max_offset, size_t alignment) -> std::optional<size_t> {
        for (size_t offset = 0; offset < max_offset; offset += alignment) {
            auto ptr = Memory::read<uintptr_t>(base_address + offset);
            if (!ptr || *ptr < 0x10000)
                continue;

            auto ptr2 = Memory::read<uintptr_t>(*ptr);
            if (!ptr2 || *ptr2 < 0x10000)
                continue;

            auto names = get_all_names(*ptr2);

            for (const auto& name : names) {
                if (name.find(target_class) != std::string::npos) {
                    return offset;
                }
            }
        }
        return std::nullopt;
    }

    auto Rtti::get_all_names(uintptr_t address) -> std::vector<std::string> {
        std::vector<std::string> names;

        auto vtable = Memory::read<uintptr_t>(address);
        if (!vtable)
            return names;

        auto col_ptr = Memory::read<uintptr_t>(*vtable - 0x8);
        if (!col_ptr)
            return names;

        auto signature = Memory::read<uint32_t>(*col_ptr);
        if (!signature || *signature != 1)
            return names;

        auto self_offset = Memory::read<int>(*col_ptr + 0x14);
        if (!self_offset)
            return names;

        uintptr_t module_base = *col_ptr - *self_offset;

        auto col_bytes = Memory::read_bytes(*col_ptr, sizeof(RttiCompleteObjectLocatorX64));
        if (col_bytes.empty())
            return names;

        RttiCompleteObjectLocatorX64 col;
        std::memcpy(&col, col_bytes.data(), sizeof(col));

        auto hierarchy =
            Memory::read<RttiClassHierarchyDescriptor>(module_base + col.class_descriptor_offset);
        if (!hierarchy || hierarchy->numBaseClasses == 0 || hierarchy->numBaseClasses >= 25)
            return names;

        uintptr_t base_class_table = module_base + hierarchy->pBaseClassArray;

        for (uint32_t i = 0; i < hierarchy->numBaseClasses; i++) {
            auto base_offset = Memory::read<uint32_t>(base_class_table + (4 * i));
            if (!base_offset)
                break;

            auto base_class = Memory::read<RttiBaseClassDescriptor>(module_base + *base_offset);
            if (!base_class)
                continue;

            auto td_bytes = Memory::read_bytes(module_base + base_class->pTypeDescriptor,
                                               sizeof(TypeDescriptor));
            if (td_bytes.empty())
                continue;

            TypeDescriptor td;
            std::memcpy(&td, td_bytes.data(), sizeof(td));

            std::string name(td.name, strnlen(td.name, 255));
            names.push_back(name);
        }

        return names;
    }

} // namespace process