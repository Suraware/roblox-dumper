#include "process.h"
#include "process/memory/memory.h"
#include <algorithm>
#include <cstring>
#include <cstdio>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <regex>
#include <set>
#include <spdlog/spdlog.h>
#include <sys/uio.h>
#include <unistd.h>

namespace process {

    Process::Process() {}

    Process::~Process() {
        if (m_mem_fd >= 0) {
            ::close(m_mem_fd);
            m_mem_fd = -1;
        }
    }

    auto Process::attach(std::string_view process_name) -> bool {
        if (m_attached && m_mem_fd >= 0) {
            ::close(m_mem_fd);
            m_mem_fd = -1;
            m_attached = false;
            m_module_base = 0;
            m_regions.clear();
        }

        const auto pid = find_process_by_id(process_name);
        if (!pid) {
            return false;
        }

        m_pid = *pid;

        std::string mem_path = "/proc/" + std::to_string(m_pid) + "/mem";
        m_mem_fd = ::open(mem_path.c_str(), O_RDWR);
        if (m_mem_fd < 0) {
            spdlog::error("Failed to open /proc/{}/mem (try sudo)", m_pid);
            return false;
        }

        if (!parse_maps()) {
            ::close(m_mem_fd);
            m_mem_fd = -1;
            return false;
        }

        m_attached = true;
        spdlog::info("Attached to PID {} (base=0x{:x})", m_pid, m_module_base);
        return true;
    }

    auto Process::read_memory(uintptr_t address, void* buffer, size_t size) const -> bool {
        if (m_mem_fd < 0) return false;

        // Try process_vm_readv first (faster, no seeking)
        struct iovec local_iov = { buffer, size };
        struct iovec remote_iov = { reinterpret_cast<void*>(address), size };
        ssize_t ret = process_vm_readv(m_pid, &local_iov, 1, &remote_iov, 1, 0);
        if (ret == static_cast<ssize_t>(size)) return true;

        // Fallback: pread from /proc/pid/mem
        ssize_t n = ::pread(m_mem_fd, buffer, size, address);
        return n == static_cast<ssize_t>(size);
    }

    auto Process::write_memory(uintptr_t address, const void* buffer, size_t size) -> bool {
        if (m_mem_fd < 0) return false;

        struct iovec local_iov = { const_cast<void*>(buffer), size };
        struct iovec remote_iov = { reinterpret_cast<void*>(address), size };
        ssize_t ret = process_vm_writev(m_pid, &local_iov, 1, &remote_iov, 1, 0);
        if (ret == static_cast<ssize_t>(size)) return true;

        // Fallback: pwrite to /proc/pid/mem
        return ::pwrite(m_mem_fd, buffer, size, address) == static_cast<ssize_t>(size);
    }

    auto Process::find_process_by_id(std::string_view process_name) -> std::optional<int> {
        namespace fs = std::filesystem;
        std::string target(process_name);

        std::optional<int> cmdline_fallback;
        std::optional<int> sober_pid;
        std::optional<int> main_pid;

        for (const auto& entry : fs::directory_iterator("/proc")) {
            if (!entry.is_directory()) continue;

            std::string pid_str = entry.path().filename().string();
            if (!std::all_of(pid_str.begin(), pid_str.end(), ::isdigit)) continue;

            std::ifstream comm(entry.path() / "comm");
            if (!comm.is_open()) continue;

            std::string comm_name;
            std::getline(comm, comm_name);

            // Exact comm match (non-sober targets)
            if (target != "sober" && (comm_name == target || comm_name == target + ".exe")) {
                return std::stoi(pid_str);
            }

            // For "sober" target: find child "Main" process (the actual Roblox instance)
            if (target == "sober") {
                if (comm_name == "sober" || comm_name == "Main") {
                    if (comm_name == "Main") main_pid = std::stoi(pid_str);
                    else sober_pid = std::stoi(pid_str);
                }
                // Cmdline fallback
                std::ifstream cmdline(entry.path() / "cmdline");
                std::string cl((std::istreambuf_iterator<char>(cmdline)), {});
                if (cl.find("sober") != std::string::npos && comm_name != "bwrap") {
                    cmdline_fallback = std::stoi(pid_str);
                }
            }
        }

        // Priority: Main child > sober child of Main > sober parent > cmdline fallback
        if (target == "sober") {
            // If we found a "Main" process, check if it has memfd mappings
            if (main_pid) {
                std::ifstream c_maps(fs::path("/proc") / std::to_string(*main_pid) / "maps");
                std::string ml;
                while (std::getline(c_maps, ml)) {
                    if (ml.find("memfd") != std::string::npos) return main_pid;
                }
            }

            // Find children of sober_pid or main_pid that have memfd
            std::optional<int> root = main_pid ? main_pid : sober_pid;
            if (root) {
                for (const auto& entry : fs::directory_iterator("/proc")) {
                    if (!entry.is_directory()) continue;
                    std::string pid_str = entry.path().filename().string();
                    if (!std::all_of(pid_str.begin(), pid_str.end(), ::isdigit)) continue;

                    std::ifstream status(entry.path() / "status");
                    std::string sl;
                    while (std::getline(status, sl)) {
                        if (sl.substr(0, 5) == "PPid:") {
                            int ppid = std::stoi(sl.substr(5));
                            if (ppid == *root) {
                                std::ifstream c_maps(entry.path() / "maps");
                                std::string ml;
                                while (std::getline(c_maps, ml)) {
                                    if (ml.find("memfd") != std::string::npos) {
                                        return std::stoi(pid_str);
                                    }
                                }
                            }
                            break;
                        }
                    }
                }
                // Fallback to root itself
                return root;
            }
        }

        if (cmdline_fallback) return cmdline_fallback;

        spdlog::error("Process '{}' not found. Is it running?", target);
        return std::nullopt;
    }

    auto Process::parse_maps() -> bool {
        m_regions.clear();
        m_maps_str.clear();
        m_module_base = 0;

        std::ifstream maps("/proc/" + std::to_string(m_pid) + "/maps");
        if (!maps.is_open()) {
            spdlog::error("Failed to read /proc/{}/maps", m_pid);
            return false;
        }

        m_maps_str.assign((std::istreambuf_iterator<char>(maps)),
                           std::istreambuf_iterator<char>());
        maps.clear();
        maps.seekg(0);

        std::set<std::string> seen_paths;
        std::string line;

        // First pass: find base from largest executable memfd/(deleted) region at offset 0
        // (matching working dumper's approach for Sober)
        {
            std::istringstream iss(m_maps_str);
            uint64_t best_base = 0;
            uint64_t best_span = 0;
            while (std::getline(iss, line)) {
                uint64_t start = 0, end = 0, off = 0;
                char perm[8] = {}; char path[4096] = {};
                int n = std::sscanf(line.c_str(), "%lx-%lx %4s %lx %*x:%*x %*d %4095s",
                                    &start, &end, perm, &off, path);
                if (n < 4) continue;
                std::string spath = (n >= 5) ? path : "";
                if (spath.find("memfd") == std::string::npos &&
                    spath.find("(deleted)") == std::string::npos) continue;
                if (!std::strchr(perm, 'x')) continue;
                if (off != 0) continue;
                uint64_t span = end - start;
                if (span > 50 * 1024 * 1024 && span > best_span) {
                    best_base = start;
                    best_span = span;
                }
            }
            m_module_base = best_base;
        }

        // Second pass: build region list (no longer needs to find base)
        {
            std::istringstream iss(m_maps_str);
            while (std::getline(iss, line)) {
                MemoryRegion r{};
                char perm[5] = {};
                char path[4096] = {};
                int scanned = std::sscanf(line.c_str(), "%lx-%lx %4s %*x %*x:%*x %*d %4095s",
                                          &r.start, &r.end, perm, path);

                r.size = r.end - r.start;
                r.readable = perm[0] == 'r';
                r.writable = perm[1] == 'w';
                r.executable = perm[2] == 'x';
                r.is_shared = perm[3] == 's';
                if (scanned >= 4) r.pathname = path;

                m_regions.push_back(r);
            }
        }

        if (m_module_base == 0) {
            spdlog::warn("No memfd-based base found, using first region as fallback");
            if (!m_regions.empty()) {
                m_module_base = m_regions[0].start;
            } else {
                spdlog::error("No memory regions found");
                return false;
            }
        }

        spdlog::info("Module base: 0x{:x}, {} regions", m_module_base, m_regions.size());
        return true;
    }

    static bool elf_check_file(const uint8_t* ident) {
        return ident[0] == 0x7f && ident[1] == 'E' && ident[2] == 'L' && ident[3] == 'F';
    }

    auto Process::get_elf_section(std::string_view section_name) const
        -> std::optional<std::pair<uintptr_t, size_t>> {
        // Read ELF header from process memory to find section headers
        uint8_t ident[16];
        if (!read_memory(m_module_base, ident, 16) || !elf_check_file(ident)) {
            return std::nullopt;
        }

        bool is_64 = (ident[4] == 2); // ELFCLASS64

        if (!is_64) {
            return std::nullopt;
        }

        // Read ELF64 header
        struct {
            uint8_t ident[16];
            uint16_t type;
            uint16_t machine;
            uint32_t version;
            uint64_t entry;
            uint64_t phoff;
            uint64_t shoff;
            uint32_t flags;
            uint16_t ehsize;
            uint16_t phentsize;
            uint16_t phnum;
            uint16_t shentsize;
            uint16_t shnum;
            uint16_t shstrndx;
        } ehdr;

        if (!read_memory(m_module_base, &ehdr, sizeof(ehdr))) {
            return std::nullopt;
        }

        if (ehdr.shoff == 0 || ehdr.shnum == 0) {
            return std::nullopt;
        }

        // Sanity checks: Sober's in-memory ELF can report bogus section tables
        constexpr size_t k_max_sections = 4096;
        constexpr size_t k_max_strtab = 16 * 1024 * 1024;
        if (ehdr.shnum > k_max_sections || ehdr.shentsize > 128) {
            return std::nullopt;
        }

        // Section headers might not be mapped in memory (they're in the file, not a PT_LOAD segment).
        // Try reading them. If this fails, fall back to maps-based section detection.
        size_t shdr_table_size = static_cast<size_t>(ehdr.shentsize) * ehdr.shnum;
        std::vector<uint8_t> shdr_data;
        try {
            shdr_data.resize(shdr_table_size);
        } catch (const std::bad_alloc&) {
            return std::nullopt;
        }

        if (!read_memory(m_module_base + ehdr.shoff, shdr_data.data(), shdr_table_size)) {
            return std::nullopt;
        }

        // Read section name string table
        uint64_t shstrtab_off = ehdr.shoff + static_cast<uint64_t>(ehdr.shentsize) * ehdr.shstrndx;
        struct {
            uint32_t sh_name;
            uint32_t sh_type;
            uint64_t sh_flags;
            uint64_t sh_addr;
            uint64_t sh_offset;
            uint64_t sh_size;
            uint32_t sh_link;
            uint32_t sh_info;
            uint64_t sh_addralign;
            uint64_t sh_entsize;
        } shstrtab_shdr;

        if (!read_memory(m_module_base + shstrtab_off, &shstrtab_shdr, sizeof(shstrtab_shdr))) {
            return std::nullopt;
        }

        if (shstrtab_shdr.sh_size > k_max_strtab) {
            return std::nullopt;
        }

        std::vector<char> shstrtab_data;
        try {
            shstrtab_data.resize(shstrtab_shdr.sh_size);
        } catch (const std::bad_alloc&) {
            return std::nullopt;
        }
        if (!read_memory(m_module_base + shstrtab_shdr.sh_offset,
                         shstrtab_data.data(), shstrtab_shdr.sh_size)) {
            return std::nullopt;
        }

        for (uint16_t i = 0; i < ehdr.shnum; i++) {
            uint64_t shdr_addr = ehdr.shoff + static_cast<uint64_t>(ehdr.shentsize) * i;

            struct {
                uint32_t sh_name;
                uint32_t sh_type;
                uint64_t sh_flags;
                uint64_t sh_addr;
                uint64_t sh_offset;
                uint64_t sh_size;
                uint32_t sh_link;
                uint32_t sh_info;
                uint64_t sh_addralign;
                uint64_t sh_entsize;
            } shdr;

            if (!read_memory(m_module_base + shdr_addr, &shdr, sizeof(shdr))) {
                continue;
            }

            const char* name = (shdr.sh_name < shstrtab_data.size())
                                   ? shstrtab_data.data() + shdr.sh_name
                                   : "";

            if (section_name == name && shdr.sh_addr != 0) {
                return std::make_pair(m_module_base + shdr.sh_addr, shdr.sh_size);
            }
        }

        return std::nullopt;
    }

    auto Process::get_section(std::string_view section_name) const
        -> std::optional<std::pair<uintptr_t, size_t>> {
        // First try ELF section headers
        auto elf_result = get_elf_section(section_name);
        if (elf_result) {
            return elf_result;
        }

        // Fallback: use maps-based heuristics
        // .text = first executable region of the main binary
        // .data = writable region at lower addresses
        // .rdata = read-only non-executable region
        std::optional<MemoryRegion> text_region;
        std::optional<MemoryRegion> data_region;
        std::optional<MemoryRegion> rdata_region;

        std::string main_binary;
        for (const auto& r : m_regions) {
            if (!r.pathname.empty()) {
                main_binary = r.pathname;
                break;
            }
        }

        for (const auto& r : m_regions) {
            if (r.pathname != main_binary) continue;

            if (r.executable && r.readable && !r.writable && !text_region)
                text_region = r;
            else if (r.writable && r.readable && !r.executable && !data_region)
                data_region = r;
            else if (r.readable && !r.writable && !r.executable && !rdata_region)
                rdata_region = r;
        }

        if (section_name == ".text" && text_region)
            return std::make_pair(text_region->start, text_region->size);

        if (section_name == ".data" && data_region)
            return std::make_pair(data_region->start, data_region->size);

        if ((section_name == ".rdata" || section_name == ".rodata") && rdata_region)
            return std::make_pair(rdata_region->start, rdata_region->size);

        return std::nullopt;
    }

    auto Process::get_window_dimensions() const -> std::optional<glm::vec2> {
        // Not implemented on Linux — VisualEngine stage falls back to memory scan
        return std::nullopt;
    }

    auto Process::get_version() const -> std::optional<std::string> {
        if (!m_pid) return std::nullopt;

        // Read /proc/pid/cmdline for Sober version info
        std::ifstream cmdline("/proc/" + std::to_string(m_pid) + "/cmdline");
        std::string cl((std::istreambuf_iterator<char>(cmdline)), {});

        // Look for version-xxxxxxxxxxxxxxxx pattern (Sober)
        std::regex version_regex(R"(version-[a-f0-9]{16,40})");
        std::smatch match;
        if (std::regex_search(cl, match, version_regex)) {
            return match.str();
        }

        // Alternatively, try reading /proc/pid/exe symlink
        char exe_path[4096] = {};
        std::string proc_exe = "/proc/" + std::to_string(m_pid) + "/exe";
        ssize_t len = ::readlink(proc_exe.c_str(), exe_path, sizeof(exe_path) - 1);
        if (len > 0) {
            exe_path[len] = '\0';
            std::string path(exe_path);
            if (std::regex_search(path, match, version_regex)) {
                return match.str();
            }
        }

        return std::nullopt;
    }

} // namespace process
