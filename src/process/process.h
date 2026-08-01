#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>
#include <glm/glm.hpp>

namespace process {

    struct MemoryRegion {
        uintptr_t start;
        uintptr_t end;
        size_t size;
        bool readable;
        bool writable;
        bool executable;
        bool is_shared;
        std::string pathname;
    };

    class Process {
      public:
        Process();
        ~Process();

        auto attach(std::string_view process_name) -> bool;
        auto get_pid() const -> int { return m_pid; }
        auto get_module_base() const -> uintptr_t { return m_module_base; }
        auto get_regions() const -> const std::vector<MemoryRegion>& { return m_regions; }
        auto get_version() const -> std::optional<std::string>;
        auto get_window_dimensions() const -> std::optional<glm::vec2>;

        auto get_section(std::string_view section_name) const
            -> std::optional<std::pair<uintptr_t, size_t>>;

        auto read_memory(uintptr_t address, void* buffer, size_t size) const -> bool;
        auto write_memory(uintptr_t address, const void* buffer, size_t size) -> bool;

        // Working dumper compatibility
        auto get_maps_str() const -> const std::string& { return m_maps_str; }
        auto get_base() const -> uintptr_t { return m_module_base; }

      private:
        auto find_process_by_id(std::string_view process_name) -> std::optional<int>;
        auto parse_maps() -> bool;
        auto get_elf_section(std::string_view section_name) const
            -> std::optional<std::pair<uintptr_t, size_t>>;

      private:
        int m_pid = 0;
        uintptr_t m_module_base = 0;
        bool m_attached = false;
        std::vector<MemoryRegion> m_regions;
        std::string m_maps_str;
        int m_mem_fd = -1;
    };

    inline Process g_process;

} // namespace process
