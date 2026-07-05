#pragma once
/**
 * @file SceneSelectorGUI.h
 * @brief Small ImGui window for choosing which scene preset is active.
 * @details
 * This widget only changes the selected preset index. application.cpp notices
 * that change afterwards, rebuilds ActiveScene, reconstructs the mesh/BVH data
 * with BVH::constructScene(), and creates a fresh Renderer for the new buffers.
 */

#include <imgui.h>

/**
 * @brief Draws the scene preset combo box.
 * @param sceneNames Names produced from buildScenePresets().
 * @param sceneCount Number of available presets.
 * @param selectedScene Index edited by the combo.
 * @param was_IMGUI_input Set to true so progressive accumulation restarts.
 * @param disabled True while camera-control mode owns keyboard/mouse input.
 */
inline void scene_selector_gui(
    const char* const* sceneNames,
    int sceneCount,
    int& selectedScene,
    bool& was_IMGUI_input,
    bool disabled)
{
    if (disabled) { ImGui::BeginDisabled(); }
    ImGui::Begin("Scene");

    if (ImGui::Combo("Preset", &selectedScene, sceneNames, sceneCount)) {
        // The rebuild does not happen inside the widget. The main loop handles it
        // after all GUI windows have run, keeping rendering state changes in one
        // place.
        was_IMGUI_input = true;
    }

    ImGui::End();
    if (disabled) { ImGui::EndDisabled(); }
}
