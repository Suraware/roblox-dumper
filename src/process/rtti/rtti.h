#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace process {

    struct RttiInfo {
        std::string name;
        uintptr_t type_descriptor;
        uintptr_t class_hierarchy_descriptor;
        std::vector<std::string> base_classes;
    };

    struct TypeDescriptor {
        uintptr_t vftable;
        uintptr_t spare;
        char name[256];
    };

    struct RttiCompleteObjectLocatorX64 {
        uint32_t signature;
        uint32_t offset;
        uint32_t cd_offset;
        int type_descriptor_offset;
        int class_descriptor_offset;
        int self_offset;
    };

    struct RttiClassHierarchyDescriptor {
        uint32_t signature;
        uint32_t attributes;
        uint32_t numBaseClasses;
        uint32_t pBaseClassArray;
    };

    struct RttiBaseClassDescriptor {
        uint32_t pTypeDescriptor;
        uint32_t numContainedBases;
        int32_t mdisp;
        int32_t pdisp;
        int32_t vdisp;
        uint32_t attributes;
        uint32_t pClassDescriptor;
    };

    // Working dumper style: returns demangled RTTI name for an object pointer.
    // Reads vtable[-1] as COL*, then COL+8 as direct TypeDescriptor*.
    auto rtti_lookup(uintptr_t object_addr) -> std::optional<std::string>;

    // Working dumper style: scans memory within [base, base+0x10000000) for a class
    // matching 'needle'. Returns the memory address where the pointer to the object was found.
    auto scan_rtti_range(const std::string& maps_str, uintptr_t base,
                         const std::string& needle) -> uintptr_t;

    // Demangle MSVC (.?AV...@@) and Itanium (N...E) RTTI names
    auto demangle_rtti(const std::string& raw) -> std::string;

    class Rtti {
      public:
        static auto find(uintptr_t base_address, const std::string& target_class,
                         size_t max_offset = 0x1000, size_t alignment = 8) -> std::optional<size_t>;

        static auto scan_rtti(uintptr_t address) -> std::optional<RttiInfo>;

        static auto find_all(uintptr_t base_address, const std::string& target_class,
                             size_t max_offset = 0x1000, size_t alignment = 8)
            -> std::vector<size_t>;
        static auto find_deref(uintptr_t base_address, const std::string& target_class,
                               size_t max_offset = 0x1000, size_t alignment = 8)
            -> std::optional<size_t>;

        static auto get_all_names(uintptr_t address) -> std::vector<std::string>;
    };

} // namespace process