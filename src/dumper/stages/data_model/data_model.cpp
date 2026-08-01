#include "data_model.h"
#include "bridge/bridge.h"
#include "dumper/dumper.h"
#include "dumper/macros.h"
#include "process/helpers/helpers.h"
#include "process/memory/memory.h"
#include "process/process.h"
#include "process/rtti/rtti.h"
#include "spdlog/spdlog.h"

namespace dumper::stages::data_model {

    static auto dump_real_data_model() -> bool {
        const auto fake_data_model = process::Memory::read<uintptr_t>(
            g_visual_engine + *dumper::g_dumper.get_offset("VisualEngine", "FakeDataModel"));

        if (!fake_data_model) {
            spdlog::error("Failed to read FakeDataModel pointer.");
            return false;
        }

        const auto real_data_model = process::Rtti::find(*fake_data_model, "DataModel@RBX");
        if (!real_data_model) {
            spdlog::error("Failed to get real DataModel.");
            return false;
        }

        dumper::g_dumper.add_offset("FakeDataModel", "RealDataModel", *real_data_model);

        const auto data_model = process::Memory::read<uintptr_t>(*fake_data_model + *real_data_model);
        if (!data_model) {
            spdlog::error("Failed to get real DataModel.");
            return false;
        }

        dumper::g_data_model_addr = *data_model;
        return true;
    }

    auto dump_ptr() -> bool {
        if (!dump_real_data_model()) return false;

        const auto workspace_offset = process::Rtti::find(dumper::g_data_model_addr, "Workspace@RBX");
        if (!workspace_offset) {
            spdlog::error("Failed to get Workspace from DataModel");
            return false;
        }

        dumper::g_dumper.add_offset("DataModel", "Workspace", *workspace_offset);
        return true;
    }

    auto dump() -> bool {
        auto dm = dumper::g_data_model_addr;

        // Try bridge first
        auto data = bridge::g_bridge.read_game_information();
        if (data) {
            FIND_AND_ADD_OFFSET(dm, DataModel, int64_t, PlaceId, data->place_id, 0x1000, 0x8);
            FIND_AND_ADD_OFFSET(dm, DataModel, int64_t, GameId, data->game_id, 0x1000, 0x8);
            FIND_AND_ADD_OFFSET(dm, DataModel, int64_t, CreatorId, data->creator_id, 0x1000, 0x8);

            auto job_id = process::helpers::find_string_offset(dm, data->job_id, 0x1000, 8);
            if (job_id) dumper::g_dumper.add_offset("DataModel", "JobId", *job_id);

            FIND_AND_ADD_OFFSET(dm, DataModel, uint32_t, GameLoaded, 31, 0x1000, 0x4);
            return true;
        }

        // Fallback: heuristic scan
        // PlaceId/CreatorId/GameId = u64 that aren't pointers
        for (size_t off = 0xD0; off < 0x1000; off += 8) {
            auto v = process::Memory::read<uint64_t>(dm + off);
            if (!v) continue;
            if (*v > 100'000'000'000LL) continue;
            auto u = process::Memory::read<uintptr_t>(dm + off);
            if (u && *u >= 0x50000 && *u <= 0x7fffffffffffULL) continue;
            if (dumper::g_dumper.get_offset("DataModel", "PlaceId") &&
                dumper::g_dumper.get_offset("DataModel", "CreatorId") &&
                dumper::g_dumper.get_offset("DataModel", "GameId")) break;
            if (!dumper::g_dumper.get_offset("DataModel", "PlaceId"))
                dumper::g_dumper.add_offset("DataModel", "PlaceId", off);
            else if (!dumper::g_dumper.get_offset("DataModel", "CreatorId"))
                dumper::g_dumper.add_offset("DataModel", "CreatorId", off);
            else if (!dumper::g_dumper.get_offset("DataModel", "GameId"))
                dumper::g_dumper.add_offset("DataModel", "GameId", off);
        }

        // JobId = GUID string scan
        for (size_t off = 8; off < 0x200; off += 8) {
            auto ptr = process::Memory::read<uintptr_t>(dm + off);
            if (!ptr || *ptr < 0x10000 || *ptr > 0x7fffffffffffULL) continue;
            auto s = process::Memory::read_string(*ptr, 64);
            if (s && s->size() == 36 && (*s)[8] == '-' && (*s)[13] == '-'
                && (*s)[18] == '-' && (*s)[23] == '-') {
                dumper::g_dumper.add_offset("DataModel", "JobId", off);
                break;
            }
        }

        // GameLoaded = bool at 0xCC8-0xCD0
        for (size_t off = 0xCC8; off < 0xCD0; off++) {
            auto v = process::Memory::read<uint8_t>(dm + off);
            if (v && (*v == 0 || *v == 1)) {
                dumper::g_dumper.add_offset("DataModel", "GameLoaded", off);
                break;
            }
        }

        // PlaceVersion = u32 [0, 999999]
        auto pv = process::helpers::find_u32_range(dm, 0, 999999, 0x300, 4);
        if (pv) dumper::g_dumper.add_offset("DataModel", "PlaceVersion", *pv);

        // PrimitiveCount = u32 [0, 1000000], search at higher offsets
        auto pc = process::helpers::find_u32_range(dm, 0, 1000000, 0x300, 4);
        if (pc && (!pv || *pc != *pv) && *pc > 0x100)
            dumper::g_dumper.add_offset("DataModel", "PrimitiveCount", *pc);

        return true;
    }
} // namespace dumper::stages::data_model
