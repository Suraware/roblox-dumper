#include "weld.h"
#include "dumper/dumper.h"
#include "process/rtti/rtti.h"
#include <spdlog/spdlog.h>

namespace dumper::stages::weld {

    auto dump() -> bool {
        const auto weld = g_workspace->find_first_child_of_class_deep("Weld");
        if (!weld) {
            spdlog::warn("No Weld in Workspace");
            return false;
        }

        // Part0/Part1 = pointers to the two welded parts (CalibWeld: WeldA/WeldB)
        const auto parts = process::Rtti::find_all(weld->get_address(), "Part@RBX", 0x200, 0x8);
        if (parts.size() >= 2) {
            g_dumper.add_offset("Weld", "Part0", parts[0]);
            g_dumper.add_offset("Weld", "Part1", parts[1]);
        } else {
            spdlog::warn("Weld::Part0/Part1 not found (only {} Part refs)", parts.size());
        }

        return true;
    }

} // namespace dumper::stages::weld
