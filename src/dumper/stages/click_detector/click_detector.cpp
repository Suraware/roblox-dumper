#include "click_detector.h"
#include "dumper/dumper.h"
#include "process/helpers/helpers.h"
#include <spdlog/spdlog.h>

namespace dumper::stages::click_detector {

    auto dump() -> bool {
        const auto detector = g_workspace->find_first_child_of_class_deep("ClickDetector");
        if (!detector) {
            spdlog::warn("No ClickDetector in Workspace");
            return false;
        }
        const auto addr = detector->get_address();

        const auto max_dist = process::helpers::find_offset<float>(addr, 24.75f, 0x300, 0x4);
        if (max_dist) g_dumper.add_offset("ClickDetector", "MaxActivationDistance", *max_dist);

        const auto icon = process::helpers::find_sso_string_offset(
            addr, "rbxassetid://3333333333", 0x300, 0x8, true);
        if (icon) g_dumper.add_offset("ClickDetector", "MouseIcon", *icon);

        return true;
    }

} // namespace dumper::stages::click_detector
