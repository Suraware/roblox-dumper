#include "textures.h"
#include "dumper/dumper.h"
#include "process/helpers/helpers.h"
#include <spdlog/spdlog.h>

namespace dumper::stages::textures {

    auto dump() -> bool {
        const auto spawn = g_workspace->find_first_child("SpawnLocation");
        if (spawn) {
            const auto decal = spawn->find_first_child_of_class("Decal");
            if (decal) {
                const auto tex = process::helpers::find_content_string(
                    decal->get_address(), "6196665106", 0x800, 0x8);
                if (tex) {
                    g_dumper.add_offset("Textures", "Decal_Texture", *tex);
                }
            }
        }

        const auto baseplate = g_workspace->find_first_child("Baseplate");
        if (baseplate) {
            const auto texture = baseplate->find_first_child_of_class("Texture");
            if (texture) {
                const auto tex = process::helpers::find_content_string(
                    texture->get_address(), "6412503613", 0x800, 0x8);
                if (tex) {
                    g_dumper.add_offset("Textures", "Texture_Texture", *tex);
                }
            }
        }

        return true;
    }

} // namespace dumper::stages::textures
