#include "camera.h"
#include "bridge/bridge.h"
#include "dumper/macros.h"
#include "process/helpers/helpers.h"
#include "process/memory/memory.h"
#include <glm/glm.hpp>
#include <spdlog/spdlog.h>

auto dumper::stages::camera::dump() -> bool {
    const auto camera = dumper::g_workspace->find_first_child_of_class("Camera");
    if (!camera) {
        spdlog::error("Failed to find Camera in Workspace");
        return false;
    }
    auto ca = camera->get_address();

    // CameraSubject via RTTI
    auto cs = process::helpers::find_rtti_ptr(ca, "Humanoid@RBX", 0x400, 8);
    if (cs) g_dumper.add_offset("Camera", "CameraSubject", *cs);

    // CameraType = u32 [0,10]
    auto ct = process::helpers::find_u32_range(ca, 0, 10, 0x400, 4);
    if (ct) g_dumper.add_offset("Camera", "CameraType", *ct);

    // CFrame via rotation matrix identity check
    auto cf = process::helpers::find_cframe(ca, 0x400, 4);
    if (cf) {
        g_dumper.add_offset("Camera", "CFrame", *cf);
        g_dumper.add_offset("Camera", "Position", *cf + 0x24);
        g_dumper.add_offset("Camera", "Rotation", *cf);
    }

    // FieldOfView = float [30, 120]
    auto fov = process::helpers::find_float_range(ca, 30.0f, 120.0f, 0x400, 4);
    if (fov) g_dumper.add_offset("Camera", "FieldOfView", *fov);

    // ViewportSize via bridge, or fallback to int16 heuristic
    auto viewport_info = bridge::g_bridge.read_camera_information();
    if (viewport_info) {
        glm::vec2 vs(viewport_info->viewport_width, viewport_info->viewport_height);
        auto vo = process::helpers::find_vec_offset(ca, vs, 0x400, 5.0f, 0x4);
        if (vo) g_dumper.add_offset("Camera", "ViewportSize", *vo);

        auto vx = static_cast<int16_t>(viewport_info->viewport_width);
        auto vy = static_cast<int16_t>(viewport_info->viewport_height);
        for (size_t off = 0; off < 0x400; off += 2) {
            auto x = process::Memory::read<int16_t>(ca + off);
            auto y = process::Memory::read<int16_t>(ca + off + 2);
            if (x && y && *x == vx && *y == vy) {
                g_dumper.add_offset("Camera", "ViewportInt16", off);
                break;
            }
        }
    } else {
        // Fallback: int16 heuristic for viewport
        for (size_t off = 0x280; off < 0x400; off += 2) {
            auto x = process::Memory::read<int16_t>(ca + off);
            auto y = process::Memory::read<int16_t>(ca + off + 4);
            if (x && y && *x > 100 && *x < 10000 && *y > 100 && *y < 10000) {
                float ratio = (float)*x / (float)*y;
                if (ratio > 0.5f && ratio < 3.0f) {
                    g_dumper.add_offset("Camera", "ViewportSize", off);
                    g_dumper.add_offset("Camera", "Viewport_Width", off);
                    g_dumper.add_offset("Camera", "Viewport_Height", off + 4);
                    break;
                }
            }
        }
    }

    return true;
}
