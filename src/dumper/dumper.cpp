#include "dumper.h"
#include "bridge/bridge.h"
#include "process/memory/memory.h"
#include "stages/stages.h"
#include <cstring>
#include <fstream>
#include <spdlog/spdlog.h>
#include <thread>
#include <vector>

namespace dumper {

    Dumper::~Dumper() {}

    auto Dumper::start() -> bool {
        spdlog::info("Dumper starting.\n");

        if (!stages::visual_engine::dump()) {
            spdlog::error("Failed to dump VisualEngine");
            return false;
        }

        if (!stages::data_model::dump_ptr()) {
            spdlog::error("Failed to find DataModel pointer");
            return false;
        }

        if (!stages::instance::dump()) {
            spdlog::error("Failed to dump Instance");
            return false;
        }

        g_data_model = roblox::Instance(g_data_model_addr);

        if (!stages::value::dump()) {
            spdlog::warn("Failed to dump Value offset (non-critical)");
        }

        if (!bridge::g_bridge.initialize()) {
            spdlog::warn("Bridge not available — some informational offsets will be skipped");
        }

        if (!stages::data_model::dump()) {
            spdlog::warn("DataModel info stage failed (non-critical)");
        }

        stages::workspace::dump();

        g_data_model = roblox::Instance(g_data_model_addr);
        g_lighting = g_data_model.find_first_child_of_class("Lighting");
        g_workspace = g_data_model.find_first_child_of_class("Workspace");

        stages::team::dump();

        auto safe = [](auto fn, std::string name) {
            return [fn, name]() {
                try { fn(); }
                catch (const std::exception& e) {
                    spdlog::error("Stage '{}' crashed: {}", name, e.what());
                }
            };
        };

        std::vector<std::thread> threads;

        threads.emplace_back(safe([]() { stages::player::dump(); }, "player"));
        threads.emplace_back(safe([]() { stages::base_part::dump(); }, "base_part"));
        threads.emplace_back(safe([]() { stages::humanoid::dump(); }, "humanoid"));
        threads.emplace_back(safe([]() { stages::lighting::dump(); }, "lighting"));
        threads.emplace_back(safe([]() { stages::sky::dump(); }, "sky"));
        threads.emplace_back(safe([]() { stages::mesh_part::dump(); }, "mesh_part"));
        threads.emplace_back(safe([]() { stages::atmosphere::dump(); }, "atmosphere"));
        threads.emplace_back(safe([]() { stages::bloom_effect::dump(); }, "bloom_effect"));
        threads.emplace_back(safe([]() { stages::camera::dump(); }, "camera"));
        threads.emplace_back(safe([]() { stages::special_mesh::dump(); }, "special_mesh"));
        threads.emplace_back(safe([]() { stages::terrain::dump(); }, "terrain"));
        threads.emplace_back(safe([]() { stages::gui_object::dump(); }, "gui_object"));
        threads.emplace_back(safe([]() { stages::gui_base2d::dump(); }, "gui_base2d"));
        threads.emplace_back(safe([]() { stages::mouse_service::dump(); }, "mouse_service"));
        threads.emplace_back(safe([]() { stages::character_mesh::dump(); }, "character_mesh"));
        threads.emplace_back(safe([]() { stages::tool::dump(); }, "tool"));
        threads.emplace_back(safe([]() { stages::proximity_prompt::dump(); }, "proximity_prompt"));
        threads.emplace_back(safe([]() { stages::text_label::dump(); }, "text_label"));
        threads.emplace_back(safe([]() { stages::text_button::dump(); }, "text_button"));
        threads.emplace_back(safe([]() { stages::script::dump(); }, "script"));
        threads.emplace_back(safe([]() { stages::task_scheduler::dump(); }, "task_scheduler"));
        threads.emplace_back(safe([]() { stages::script_context::dump(); }, "script_context"));
        threads.emplace_back(safe([]() { stages::vehicle_seat::dump(); }, "vehicle_seat"));
        // Disabled on this client: value scans matched uninitialized memory instead of
        // property defaults (no distinctive anchors present in the objects).
        // threads.emplace_back(safe([]() { stages::sound::dump(); }, "sound"));
        // threads.emplace_back(safe([]() { stages::depth_of_field::dump(); }, "depth_of_field"));
        // threads.emplace_back(safe([]() { stages::sun_rays_effect::dump(); }, "sun_rays_effect"));
        // threads.emplace_back(safe([]() { stages::misc::dump(); }, "misc"));
        threads.emplace_back(safe([]() { stages::beam::dump(); }, "beam"));
        threads.emplace_back(safe([]() { stages::attachment::dump(); }, "attachment"));
        threads.emplace_back(safe([]() { stages::model::dump(); }, "model"));

        for (auto& thread : threads) {
            thread.join();
        }

        return true;
    }

    auto Dumper::add_offset(const std::string& namespace_name, const std::string& offset_name,
                            size_t offset) -> void {
        std::lock_guard<std::mutex> lock(m_offset_mutex);
        m_offsets[namespace_name].push_back({offset_name, offset});
        spdlog::info("Added offset: {}::{} = 0x{:X}", namespace_name, offset_name, offset);
    }

    auto Dumper::get_offset(const std::string& namespace_name, const std::string& offset_name) const
        -> std::optional<size_t> {
        std::lock_guard<std::mutex> lock(m_offset_mutex);
        auto it = m_offsets.find(namespace_name);
        if (it == m_offsets.end()) {
            return std::nullopt;
        }

        for (const auto& entry : it->second) {
            if (entry.name == offset_name) {
                return entry.offset;
            }
        }

        return std::nullopt;
    }

} // namespace dumper