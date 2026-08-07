#include "user_input_service.h"
#include "dumper/dumper.h"
#include "process/helpers/helpers.h"
#include "process/memory/memory.h"
#include "process/rtti/rtti.h"
#include <spdlog/spdlog.h>

namespace dumper::stages::user_input_service {

    auto dump() -> bool {
        const auto uis = g_data_model.find_first_child_of_class("UserInputService");
        if (!uis) {
            spdlog::warn("UserInputService not found in DataModel");
            return false;
        }
        const auto addr = uis->get_address();

        // WindowInputState: pointer to the input state object
        const auto state_off = process::Rtti::find(addr, "WindowInputState@RBX", 0x1000, 0x8);
        if (!state_off) {
            spdlog::warn("WindowInputState not found in UserInputService");
            return false;
        }
        g_dumper.add_offset("UserInputService", "WindowInputState", *state_off);

        auto state_addr = process::Memory::read<uintptr_t>(addr + *state_off);
        if (!state_addr || *state_addr < 0x10000) return true;

        // CurrentTextBox: non-null pointer in the state object (nil when not typing)
        for (size_t off = 0x20; off < 0x100; off += 8) {
            auto v = process::Memory::read<uintptr_t>(*state_addr + off);
            if (v && *v >= 0x10000 && *v <= 0x7fffffffffffULL) {
                g_dumper.add_offset("WindowInputState", "CurrentTextBox", off);
                break;
            }
        }

        // CapsLock: bool in [0,1]
        for (size_t off = 0x100; off < 0x180; ++off) {
            auto v = process::Memory::read<uint8_t>(*state_addr + off);
            if (v && (*v == 0 || *v == 1)) {
                g_dumper.add_offset("WindowInputState", "CapsLock", off);
                break;
            }
        }

        return true;
    }

} // namespace dumper::stages::user_input_service
