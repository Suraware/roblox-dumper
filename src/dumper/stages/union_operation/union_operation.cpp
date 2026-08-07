#include "union_operation.h"
#include "dumper/dumper.h"
#include "process/helpers/helpers.h"
#include <spdlog/spdlog.h>

namespace dumper::stages::union_operation {

    auto dump() -> bool {
        const auto union_op = g_workspace->find_first_child_of_class_deep("UnionOperation");
        if (!union_op) {
            spdlog::warn("No UnionOperation in Workspace");
            return false;
        }

        // AssetId = "rbxassetid://..." content string (CalibUnion set at runtime)
        const auto asset_id = process::helpers::find_string_by_regex(
            union_op->get_address(), R"(rbxassetid://[0-9]+)", 0x1000, 0x8, 256, true);
        if (asset_id) {
            g_dumper.add_offset("UnionOperation", "AssetId", *asset_id);
        } else {
            spdlog::warn("UnionOperation::AssetId not found");
        }

        return true;
    }

} // namespace dumper::stages::union_operation
