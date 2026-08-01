#include "special_mesh.h"
#include "dumper/dumper.h"
#include "process/helpers/helpers.h"
#include <spdlog/spdlog.h>

namespace dumper::stages::special_mesh {

    auto dump() -> bool {
        const auto parent = dumper::g_workspace->find_first_child("SpecialMeshPart");
        if (!parent) return false;

        const auto special_mesh = parent->find_first_child_of_class("SpecialMesh");
        if (!special_mesh) return false;

        auto addr = special_mesh->get_address();

        const auto mesh_id = process::helpers::find_sso_string_offset(
            addr, "http://www.roblox.com/Asset/?id=9982590", 0x800, 0x8, true);
        if (mesh_id) dumper::g_dumper.add_offset("SpecialMesh", "MeshId", *mesh_id);

        const auto texture_id = process::helpers::find_sso_string_offset(
            addr, "rbxassetid://9982590", 0x800, 0x8, true);
        if (texture_id) dumper::g_dumper.add_offset("SpecialMesh", "TextureId", *texture_id);

        glm::vec3 offset(1.4f, 10.5f, 11.2f);
        const auto mesh_offset = process::helpers::find_vec_offset<glm::vec3>(
            addr, offset, 0x400, 0.01f, 0x4);
        if (mesh_offset) g_dumper.add_offset("SpecialMesh", "Offset", *mesh_offset);

        glm::vec3 scale(2.2f, 4.4f, 1.2f);
        const auto scale_offset = process::helpers::find_vec_offset<glm::vec3>(
            addr, scale, 0x400, 0.01f, 0x4);
        if (scale_offset) g_dumper.add_offset("SpecialMesh", "Scale", *scale_offset);

        return true;
    }
} // namespace dumper::stages::special_mesh
