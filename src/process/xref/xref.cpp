#include "process/xref/xref.h"
#include "process/memory/memory.h"
#include "process/process.h"
#include <spdlog/spdlog.h>
#include <algorithm>

namespace process {

    Xref::Xref() { ZydisDecoderInit(&m_decoder, ZYDIS_MACHINE_MODE_LONG_64, ZYDIS_STACK_WIDTH_64); }

    auto Xref::decode(const uint8_t* buffer, size_t length,
                      ZydisDecodedInstruction& out_instruction,
                      ZydisDecodedOperand* out_operands) const -> bool {
        return ZYAN_SUCCESS(
            ZydisDecoderDecodeFull(&m_decoder, buffer, length, &out_instruction, out_operands));
    }

    auto Xref::scan(uintptr_t address) const -> std::vector<uintptr_t> {
        std::vector<uintptr_t> xrefs;

        auto section = g_process.get_section(".text");
        if (!section)
            return xrefs;

        const uintptr_t section_start = section->first;
        const uintptr_t section_end = section_start + section->second;

        // Guard against bogus ELF section sizes in Sober's in-memory binary
        constexpr size_t k_max_region_read = 0x20000000; // 512 MiB per region chunk

        // Find the range in our regions that covers .text
        uintptr_t current = section_start;

        while (current < section_end) {
            // Find the region containing this address
            const auto& regions = g_process.get_regions();
            auto it = std::find_if(regions.begin(), regions.end(), [&](const MemoryRegion& r) {
                return current >= r.start && current < r.end && r.readable;
            });

            if (it == regions.end()) break;

            const uintptr_t region_start = it->start;
            const uintptr_t region_end = std::min(it->end, section_end);

            if (region_end - region_start > k_max_region_read) {
                current = region_end;
                continue;
            }

            auto buffer = Memory::read_bytes(region_start, region_end - region_start);

            if (!buffer.empty()) {
                uintptr_t offset = 0;
                while (offset < buffer.size()) {
                    ZydisDecodedInstruction instruction;
                    ZydisDecodedOperand operands[ZYDIS_MAX_OPERAND_COUNT];

                    if (!decode(buffer.data() + offset, buffer.size() - offset, instruction,
                                operands)) {
                        offset++;
                        continue;
                    }

                    for (int i = 0; i < instruction.operand_count_visible; i++) {
                        const auto& operand = operands[i];

                        if (operand.type == ZYDIS_OPERAND_TYPE_MEMORY &&
                            operand.mem.base == ZYDIS_REGISTER_RIP &&
                            operand.mem.disp.has_displacement) {
                            uintptr_t absolute = (region_start + offset) + instruction.length +
                                                 operand.mem.disp.value;
                            if (absolute == address) {
                                xrefs.push_back(region_start + offset);
                            }
                        } else if (operand.type == ZYDIS_OPERAND_TYPE_IMMEDIATE &&
                                   operand.imm.is_relative) {
                            ZyanU64 absolute = 0;
                            if (ZYAN_SUCCESS(ZydisCalcAbsoluteAddress(&instruction, &operand,
                                                                       region_start + offset,
                                                                       &absolute))) {
                                if (absolute == address) {
                                    xrefs.push_back(region_start + offset);
                                }
                            }
                        }
                    }

                    offset += instruction.length;
                }
            }

            current = region_end;
        }

        return xrefs;
    }

    auto Xref::instruction_scan(
        uintptr_t start, const std::vector<uint8_t>& buffer,
        const std::function<bool(const ZydisDecodedInstruction&, const ZydisDecodedOperand*)>&
            predicate) const -> std::optional<InstructionMatch> {
        uintptr_t offset = 0;

        while (offset < buffer.size()) {
            ZydisDecodedInstruction instruction;
            ZydisDecodedOperand operands[ZYDIS_MAX_OPERAND_COUNT];

            if (!decode(buffer.data() + offset, buffer.size() - offset, instruction, operands)) {
                offset++;
                continue;
            }

            if (predicate(instruction, operands)) {
                InstructionMatch match{};
                match.address = start + offset;
                match.instruction = instruction;
                std::memcpy(match.operands, operands, sizeof(operands));
                return match;
            }

            offset += instruction.length;
        }

        return std::nullopt;
    }

} // namespace process
