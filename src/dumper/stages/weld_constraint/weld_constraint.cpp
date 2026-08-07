#include "weld_constraint.h"
#include "dumper/dumper.h"
#include "process/rtti/rtti.h"
#include <spdlog/spdlog.h>

namespace dumper::stages::weld_constraint {

    auto dump() -> bool {
        const auto constraint = g_workspace->find_first_child_of_class_deep("WeldConstraint");
        if (!constraint) {
            spdlog::warn("No WeldConstraint in Workspace");
            return false;
        }

        const auto parts = process::Rtti::find_all(constraint->get_address(), "Part@RBX", 0x200, 0x8);
        if (parts.size() >= 2) {
            g_dumper.add_offset("WeldConstraint", "Part0", parts[0]);
            g_dumper.add_offset("WeldConstraint", "Part1", parts[1]);
        } else {
            spdlog::warn("WeldConstraint::Part0/Part1 not found (only {} Part refs)", parts.size());
        }

        return true;
    }

} // namespace dumper::stages::weld_constraint
