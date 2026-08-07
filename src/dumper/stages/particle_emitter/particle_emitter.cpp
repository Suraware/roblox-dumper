#include "particle_emitter.h"
#include "dumper/dumper.h"
#include "process/helpers/helpers.h"
#include <glm/glm.hpp>
#include <spdlog/spdlog.h>

namespace dumper::stages::particle_emitter {

    auto dump() -> bool {
        const auto emitter = g_workspace->find_first_child_of_class_deep("ParticleEmitter");
        if (!emitter) {
            spdlog::warn("No ParticleEmitter in Workspace");
            return false;
        }
        const auto addr = emitter->get_address();

        const auto texture = process::helpers::find_sso_string_offset(
            addr, "rbxassetid://6196665106", 0x800, 0x8, true);
        if (texture) g_dumper.add_offset("ParticleEmitter", "Texture", *texture);

        struct FloatProp { const char* name; float value; };
        const FloatProp float_props[] = {
            {"Lifetime", 2.5f},    {"Rate", 15.0f},        {"Rotation", 45.0f},
            {"RotSpeed", 90.0f},   {"Speed", 7.5f},        {"Drag", 0.1f},
            {"TimeScale", 1.5f},   {"VelocityInheritance", 0.25f},
            {"ZOffset", 0.6f},     {"Brightness", 0.75f},  {"LightEmission", 0.5f},
            {"LightInfluence", 0.3f},
        };

        for (const auto& prop : float_props) {
            const auto off = process::helpers::find_offset<float>(addr, prop.value, 0x800, 0x4);
            if (off) g_dumper.add_offset("ParticleEmitter", prop.name, *off);
        }

        // SpreadAngle = Vector2 (30, 30)
        const auto spread = process::helpers::find_offset<float>(addr, 30.0f, 0x800, 0x4);
        if (spread) g_dumper.add_offset("ParticleEmitter", "SpreadAngle", *spread);

        // Acceleration = Vector3 (0, -9.81, 0)
        const glm::vec3 accel(0.0f, -9.81f, 0.0f);
        const auto accel_off = process::helpers::find_vec_offset<glm::vec3>(addr, accel, 0x800, 0.01f, 0x4);
        if (accel_off) g_dumper.add_offset("ParticleEmitter", "Acceleration", *accel_off);

        return true;
    }

} // namespace dumper::stages::particle_emitter
