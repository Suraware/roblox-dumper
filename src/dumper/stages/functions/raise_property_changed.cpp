#include "dumper/dumper.h"
#include "dumper/stages/registry.h"
#include "process/helpers/helpers.h"
#include "process/memory/memory.h"
#include "process/process.h"
#include "process/xref/xref.h"
#include <spdlog/spdlog.h>

namespace dumper::stages::raise_property_changed {

    auto dump() -> bool {
        const auto base = process::g_process.get_module_base();

        const auto string_results = process::Memory::scan_string(
            "Attempted unsafe deferred signal invocation - this signal invocation "
            "isn't safe during parallel execution.",
            ".rdata");

        if (string_results.empty()) {
            spdlog::error("RaisePropertyChanged: Failed to find anchor string");
            return false;
        }

        const auto xrefs = process::g_xref.scan(string_results.front());
        if (xrefs.empty()) {
            spdlog::error("RaisePropertyChanged: No xrefs to anchor string");
            return false;
        }

        for (size_t i = 0; i < xrefs.size(); i++) {
            auto func_start = process::helpers::find_function_start(xrefs[i]);
            if (!func_start)
                continue;

            size_t dist = xrefs[i] - *func_start;
            size_t func_size = process::helpers::estimate_function_size(*func_start);

            if (func_size >= 0x180 && func_size <= 0x350 && dist >= 0x80 && dist <= 0x220) {
                dumper::g_dumper.add_offset("Functions", "RaisePropertyChanged",
                                            *func_start - base);
                return true;
            }
        }

        spdlog::error("RaisePropertyChanged: No matching function found");
        return false;
    }

} // namespace dumper::stages::raise_property_changed

REGISTER_STAGE(raise_property_changed)
