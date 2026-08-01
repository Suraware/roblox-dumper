#include "bloom_effect.h"
#include "dumper/macros.h"
#include "process/helpers/helpers.h"
#include "process/rtti/rtti.h"
#include <spdlog/spdlog.h>

namespace dumper::stages::bloom_effect {
    auto dump() -> bool {
        const auto bloom_effect = dumper::g_lighting->find_first_child_of_class("BloomEffect");

        if (!bloom_effect) return false;

        auto addr = bloom_effect->get_address();

        FIND_AND_ADD_OFFSET(addr, BloomEffect, float, Intensity, 1.23f, 0x800, 0x4);

        FIND_AND_ADD_OFFSET(addr, BloomEffect, float, Size, 24.5, 0x800, 0x4);

        FIND_AND_ADD_OFFSET(addr, BloomEffect, float, Threshold, 2.1f, 0x800, 0x4);

        return true;
    }

} // namespace dumper::stages::bloom_effect