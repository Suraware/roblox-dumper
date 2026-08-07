#include "seat.h"
#include "dumper/dumper.h"
#include "process/helpers/helpers.h"
#include "process/rtti/rtti.h"
#include <spdlog/spdlog.h>

namespace dumper::stages::seat {

    auto dump() -> bool {
        const auto seat = g_workspace->find_first_child_of_class("Seat");
        if (!seat) {
            spdlog::error("Failed to find Seat in Workspace");
            return false;
        }

        // Occupant = pointer to the seated Humanoid (SittingNPC is seated at runtime)
        const auto occupant = process::Rtti::find(seat->get_address(), "Humanoid@RBX", 0x400, 0x8);
        if (occupant) {
            g_dumper.add_offset("Seat", "Occupant", *occupant);
        } else {
            spdlog::warn("Seat::Occupant not found (nobody seated)");
        }

        return true;
    }

} // namespace dumper::stages::seat
