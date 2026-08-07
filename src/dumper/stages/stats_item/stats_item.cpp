#include "stats_item.h"
#include "dumper/dumper.h"
#include "process/memory/memory.h"
#include <spdlog/spdlog.h>

namespace dumper::stages::stats_item {

    auto dump() -> bool {
        const auto stats = g_data_model.find_first_child_of_class("Stats");
        if (!stats) {
            spdlog::warn("Stats service not found in DataModel");
            return false;
        }

        const auto item = stats->find_first_child_of_class("StatsItem");
        if (!item) {
            spdlog::warn("No StatsItem under Stats");
            return false;
        }

        // StatsItem::Value is a double. Filter: not a heap pointer, plausible value.
        for (size_t off = 0xB0; off < 0x200; off += 8) {
            auto v = process::Memory::read<uint64_t>(item->get_address() + off);
            if (!v) continue;
            if (*v >= 0x10000 && *v <= 0x7fffffffffffULL) continue; // looks like a pointer
            if (*v == 0 || *v == 0xFFFFFFFFFFFFFFFFULL) continue;

            auto d = process::Memory::read<double>(item->get_address() + off);
            if (d && !std::isnan(*d) && !std::isinf(*d) && *d >= 0.0 && *d < 1e15) {
                g_dumper.add_offset("StatsItem", "Value", off);
                break;
            }
        }

        return true;
    }

} // namespace dumper::stages::stats_item
