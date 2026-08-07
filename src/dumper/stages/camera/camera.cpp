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

    // FieldOfView = float 70.0 (default anchor), tolerant of client variations
    auto fov = process::helpers::find_float_range(ca, 40.0f, 130.0f, 0x400, 4);
    if (fov) g_dumper.add_offset("Camera", "FieldOfView", *fov);
    if (!fov) {
        // Some clients push vertical+horizontal FOV together; anchor on 70.0/120.0
        auto fov70 = process::helpers::find_offset<float>(ca, 70.0f, 0x400, 4);
        if (fov70) g_dumper.add_offset("Camera", "FieldOfView", *fov70);
    }

    // ImagePlaneDepth = 0.5 * tan(FOV/2), stored near FieldOfView
    if (fov) {
        for (size_t off = *fov > 0x20 ? *fov - 0x20 : 0; off < *fov + 0x20; off += 4) {
            auto v = process::Memory::read<float>(ca + off);
            if (v && !std::isnan(*v) && !std::isinf(*v) && *v > 0.2f && *v < 1.0f) {
                g_dumper.add_offset("Camera", "ImagePlaneDepth", off);
                break;
            }
        }
    }

    // ViewportSize via bridge, or fallback to int16 heuristic
    auto viewport_info = bridge::g_bridge.read_camera_information();
    if (viewport_info) {
        glm::vec2 vs(viewport_info->viewport_width, viewport_info->viewport_height);
        auto vo = process::helpers::find_vec_offset(ca, vs, 0x400, 5.0f, 0x4);
        if (vo) {
            g_dumper.add_offset("Camera", "ViewportSize", *vo);
            g_dumper.add_offset("Camera", "Viewport_Width", *vo);
            g_dumper.add_offset("Camera", "Viewport_Height", *vo + 4);

            // ViewportSpoof = u32 == packed (w | h<<16) anywhere in Camera
            uint32_t packed = (static_cast<uint32_t>(static_cast<uint16_t>(viewport_info->viewport_width))) |
                              (static_cast<uint32_t>(static_cast<uint16_t>(viewport_info->viewport_height)) << 16);
            for (size_t off = 0; off < 0x500; off += 4) {
                auto vp = process::Memory::read<uint32_t>(ca + off);
                if (vp && *vp == packed) {
                    g_dumper.add_offset("Camera", "ViewportSpoof", off);
                    break;
                }
            }
            for (size_t off = 0; off < 0x500; off += 4) {
                auto vp = process::Memory::read<uint32_t>(ca + off);
                if (vp) spdlog::info("[dbg camera] ca+0x{:X} u32 = 0x{:X}", off, *vp);
            }
        }

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
