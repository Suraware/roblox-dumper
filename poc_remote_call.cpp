// PoC v2: foreign-thunk injection WITHOUT ptrace.
//
// Sober maps ~12 copies of libroblox.so; only one is live. This PoC:
//   1. finds the ACTIVE copy (its Luau::GlobalStatePointer slot holds a heap
//      pointer) and an INACTIVE copy,
//   2. plants a tiny counter thunk in the INACTIVE copy's text (dead code -
//      nothing executes there) plus a counter slot in its .bss,
//   3. overwrites the ACTIVE copy's luaD_checkstack prologue with a jmp to
//      the thunk; the thunk increments the counter, replays the displaced
//      original 15 bytes (push/push/sub - no rip-relative insns) and jumps
//      back,
//   4. the external side polls the counter through /proc/pid/mem until it
//      rises - proving arbitrary in-process code execution.
//
// All writes go through /proc/pid/mem (root bypasses page protections).
// Restore mode removes every trace.

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <dirent.h>
#include <fcntl.h>
#include <cerrno>
#include <unistd.h>

struct Region {
    uintptr_t start, end;
    std::string perms, path;
    unsigned long offset;
};

static auto read_all_regions(pid_t pid, std::vector<Region>& out) -> bool {
    FILE* f = fopen(("/proc/" + std::to_string(pid) + "/maps").c_str(), "r");
    if (!f) return false;
    char line[512];
    while (fgets(line, sizeof line, f)) {
        Region r{};
        char perms[8] = "";
        char path[256] = "";
        if (sscanf(line, "%lx-%lx %7s %lx %*s %*s %255[^\n]", &r.start, &r.end,
                   perms, &r.offset, path) >= 4) {
            r.perms = perms; // assign AFTER parse: data() write wouldn't set size
            r.path = path;
            out.push_back(r);
        }
    }
    fclose(f);
    return true;
}

struct Mem {
    int fd;
    auto read(uintptr_t a, size_t n) const -> std::vector<uint8_t> {
        std::vector<uint8_t> buf(n);
        ssize_t got = pread(fd, buf.data(), n, (off_t)a);
        buf.resize(got > 0 ? (size_t)got : 0);
        return buf;
    }
    auto u64(uintptr_t a) const -> uint64_t {
        auto d = read(a, 8);
        uint64_t v = 0;
        if (d.size() == 8) memcpy(&v, d.data(), 8);
        return v;
    }
    auto write(uintptr_t a, const void* p, size_t n) const -> bool {
        return pwrite(fd, p, n, (off_t)a) == (ssize_t)n;
    }
};

static constexpr uint64_t k_aux_rva = 0x6F81040; // Luau::VmAuxStruct slot

int main(int argc, char** argv) {
    if (argc < 2) { fprintf(stderr, "usage: %s <pid>\n", argv[0]); return 1; }
    const pid_t pid = atoi(argv[1]);

    std::vector<Region> regions;
    if (!read_all_regions(pid, regions)) { perror("maps"); return 1; }

    Mem mem{-1};
    {
        std::string p = "/proc/" + std::to_string(pid) + "/mem";
        mem.fd = open(p.c_str(), O_RDWR);
        if (mem.fd < 0) { perror("open mem"); return 1; }
    }

    // Group ELF bases: big memfd exec mappings starting at file offset 0.
    std::vector<uintptr_t> bases;
    for (const auto& r : regions) {
        static int dbg = 0;
        if (dbg < 4 && (r.end - r.start) > 100ull * 1024 * 1024) {
            printf("[dbg] %#lx-%#lx perms=%s off=%#lx path=%s\n", r.start,
                   r.end, r.perms.c_str(), r.offset, r.path.c_str());
            dbg++;
        }

        if ((r.perms.find('x') != std::string::npos) &&
            r.path.find("memfd") != std::string::npos &&
            r.offset == 0 && (r.end - r.start) > 100ull * 1024 * 1024) {
            bases.push_back(r.start);
        }
    }
    printf("[+] %zu image copies\n", bases.size());
    if (bases.size() < 2) { fprintf(stderr, "need >=2 copies\n"); return 1; }

    // Active copy: aux slot holds a plausible heap/module pointer.
    uintptr_t active = 0, victim = 0;
    for (const auto b : bases) {
        const uint64_t v = mem.u64(b + k_aux_rva);
        if (v > 0x10000 && v < 0x7fffffffffffULL) active = b;
        else victim = victim ? victim : b;
    }
    if (!active || !victim) { fprintf(stderr, "active/victim not resolved\n"); return 1; }
    printf("[+] active image %#llx | victim (scratch) image %#llx\n",
           (unsigned long long)active, (unsigned long long)victim);

    // Scratch counter slot: right past the victim's aux slot (unused there).
    const uintptr_t counter = victim + k_aux_rva + 8;

    // luaD_checkstack RVA (from Luau stage output).
    constexpr uint64_t k_check_rva = 0x26EC197;
    const uintptr_t check_active = active + k_check_rva;

    // Read the original prologue we will displace (15 whole instructions =
    // exactly 15 bytes, verified in IDA: push rbp; mov rbp,rsp; push x4;
    // push rbx; sub rsp,0x10).
    constexpr size_t k_disp_len = 15;
    auto orig = mem.read(check_active, k_disp_len);
    if (orig.size() != k_disp_len || orig[0] != 0x55) {
        fprintf(stderr, "checkstack prologue mismatch\n");
        return 1;
    }

    // Thunk site: victim copy text, well past headers (offset 0x200000).
    const uintptr_t thunk = victim + 0x200000;

    // Build thunk:
    //   push rax                       50
    //   movabs rax, counter            48 B8 imm64
    //   inc qword [rax]                FF 00
    //   pop rax                        58
    //   <orig 15 bytes>                replay displaced prologue
    //   mov rax, check_active+15       48 B8 imm64   (rip-free absolute jmp)
    //   jmp rax                        FF E0
    uint8_t code[64];
    size_t n = 0;
    code[n++] = 0x50;
    code[n++] = 0x48; code[n++] = 0xB8;
    memcpy(code + n, &counter, 8); n += 8;
    code[n++] = 0xFF; code[n++] = 0x00;
    code[n++] = 0x58;
    memcpy(code + n, orig.data(), k_disp_len); n += k_disp_len;
    code[n++] = 0x48; code[n++] = 0xB8;
    const uintptr_t ret_to = check_active + k_disp_len;
    memcpy(code + n, &ret_to, 8); n += 8;
    code[n++] = 0xFF; code[n++] = 0xE0;

    if (!mem.write(thunk, code, n)) { perror("write thunk"); return 1; }
    printf("[+] thunk planted at %#llx (%zu bytes)\n",
           (unsigned long long)thunk, n);

    // Zero the counter, snapshot before value.
    const uint64_t zero = 0;
    mem.write(counter, &zero, 8);

    // Redirect: jmp rel32 from active checkstack -> thunk.
    // E9 disp32; disp = thunk - (check_active + 5).
    uint8_t jmp[5] = {0xE9, 0, 0, 0, 0};
    int32_t disp = (int32_t)((int64_t)thunk - (int64_t)(check_active + 5));
    memcpy(jmp + 1, &disp, 4);
    if (!mem.write(check_active, jmp, 5)) { perror("write hook"); return 1; }
    printf("[+] luaD_checkstack hooked (jmp -> thunk)\n");

    // Poll counter.
    uint64_t before = mem.u64(counter);
    for (int i = 0; i < 40; ++i) {
        usleep(100 * 1000);
        const uint64_t now = mem.u64(counter);
        if (now > before) {
            printf("[+] EXECUTING IN TARGET: counter rose %llu -> %llu in ~%d ms\n",
                   (unsigned long long)before, (unsigned long long)now, (i + 1) * 100);
            printf("[+] foreign thunk PoC OK\n");
            return 0;
        }
    }
    fprintf(stderr, "[-] counter never rose (game idle or hook failed)\n");
    return 1;
}
