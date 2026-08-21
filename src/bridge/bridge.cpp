#include "bridge.h"
#include "dumper/dumper.h"
#include "process/memory/memory.h"
#include "roblox/offsets.h"
#include <spdlog/spdlog.h>

namespace bridge {

    auto Bridge::initialize() -> bool {
        const auto replicated_storage =
            dumper::g_data_model.find_first_child_of_class("ReplicatedStorage");

        if (!replicated_storage) {
            spdlog::warn("ReplicatedStorage not found — bridge unavailable");
            return false;
        }

        const auto find = [&](const char* name) -> std::optional<roblox::Instance> {
            const auto inst = replicated_storage->find_first_child(name);
            return inst;
        };

        m_game_information = find("GameInformation");
        m_player_information = find("PlayerInformation");
        m_parts_information = find("PartsInformation");
        m_humanoid_information = find("NPCsInformation");
        m_camera_information = find("CameraInformation");
        m_character_mesh_information = find("CharacterMeshInformation");
        m_frames_information = find("FramesInformation");
        m_frame_absolutes_information = find("FrameAbsolutesInformation");
        m_tools_information = find("ToolsInformation");
        m_proximity_prompts_information = find("ProximityPromptsInformation");
        m_text_labels_information = find("TextLabelsInformation");
        m_text_buttons_information = find("TextButtonsInformation");
        m_highlights_information = find("HighlightsInformation");

        const bool ok = m_game_information && m_player_information && m_parts_information &&
                        m_humanoid_information && m_camera_information &&
                        m_character_mesh_information && m_frames_information &&
                        m_frame_absolutes_information && m_tools_information &&
                        m_proximity_prompts_information && m_text_labels_information &&
                        m_text_buttons_information;

        if (ok)
            spdlog::info("Bridge initialized successfully");

        return ok;
    }

    auto Bridge::read_value(const roblox::Instance& instance) -> std::optional<json> {
        const auto value_addr = instance.get_address() + roblox::offsets::Value::Value;
        const auto str = process::Memory::read_roblox_string(value_addr);

        if (!str || str->empty()) {
            return std::nullopt;
        }

        spdlog::debug("bridge raw value: {}", str->substr(0, 200));
        try {
            return json::parse(*str);
        } catch (const json::exception& e) {
            spdlog::debug("bridge json parse failed: {}", e.what());
            return std::nullopt;
        }
    }

    template <typename T>
    auto Bridge::read_as(const roblox::Instance& instance, const char*) -> std::optional<T> {
        const auto j = read_value(instance);
        if (!j)
            return std::nullopt;
        try {
            return j->get<T>();
        } catch (const json::exception&) {
            return std::nullopt;
        }
    }

    auto Bridge::read_game_information() -> std::optional<DataModelInfo> {
        if (!m_game_information) return std::nullopt;
        return read_as<DataModelInfo>(*m_game_information, "GameInformation");
    }

    auto Bridge::read_player_information() -> std::optional<PlayerInfo> {
        if (!m_player_information) return std::nullopt;
        return read_as<PlayerInfo>(*m_player_information, "PlayerInformation");
    }

    auto Bridge::read_parts_information() -> std::optional<PartPropertiesInfo> {
        if (!m_parts_information) return std::nullopt;
        return read_as<PartPropertiesInfo>(*m_parts_information, "PartsInformation");
    }

    auto Bridge::read_humanoid_information() -> std::optional<HumanoidPropertiesInfo> {
        if (!m_humanoid_information) return std::nullopt;
        return read_as<HumanoidPropertiesInfo>(*m_humanoid_information, "NPCsInformation");
    }

    auto Bridge::read_camera_information() -> std::optional<ViewportInfo> {
        if (!m_camera_information) return std::nullopt;
        return read_as<ViewportInfo>(*m_camera_information, "CameraInformation");
    }

    auto Bridge::read_character_mesh_information() -> std::optional<CharacterMeshPropertiesInfo> {
        if (!m_character_mesh_information) return std::nullopt;
        return read_as<CharacterMeshPropertiesInfo>(*m_character_mesh_information,
                                                    "CharacterMeshInformation");
    }

    auto Bridge::read_frames_information() -> std::optional<FramePropertiesInfo> {
        if (!m_frames_information) return std::nullopt;
        return read_as<FramePropertiesInfo>(*m_frames_information, "FramesInformation");
    }

    auto Bridge::read_frame_absolutes_information() -> std::optional<FrameAbsolutesInfo> {
        if (!m_frame_absolutes_information) return std::nullopt;
        return read_as<FrameAbsolutesInfo>(*m_frame_absolutes_information,
                                           "FrameAbsolutesInformation");
    }

    auto Bridge::read_tools_information() -> std::optional<ToolPropertiesInfo> {
        if (!m_tools_information) return std::nullopt;
        return read_as<ToolPropertiesInfo>(*m_tools_information, "ToolsInformation");
    }

    auto Bridge::read_proximity_prompts_information()
        -> std::optional<ProximityPromptPropertiesInfo> {
        if (!m_proximity_prompts_information) return std::nullopt;
        return read_as<ProximityPromptPropertiesInfo>(*m_proximity_prompts_information,
                                                      "ProximityPromptsInformation");
    }

    auto Bridge::read_text_labels_information() -> std::optional<TextLabelPropertiesInfo> {
        if (!m_text_labels_information) return std::nullopt;
        return read_as<TextLabelPropertiesInfo>(*m_text_labels_information,
                                                "TextLabelsInformation");
    }

    auto Bridge::read_text_buttons_information() -> std::optional<TextButtonPropertiesInfo> {
        if (!m_text_buttons_information) return std::nullopt;
        return read_as<TextButtonPropertiesInfo>(*m_text_buttons_information,
                                                 "TextButtonsInformation");
    }

    auto Bridge::read_highlights_information() -> std::optional<HighlightPropertiesInfo> {
        if (!m_highlights_information) return std::nullopt;
        return read_as<HighlightPropertiesInfo>(*m_highlights_information,
                                                "HighlightsInformation");
    }

} // namespace bridge
