#pragma once
/**
 * @file SkyBoxGUI.h
 * @brief ImGui controls for the procedural sky gradient.
 * @details
 * The sky is not a mesh or texture. These colors are copied by application.cpp
 * into ray tracing uniforms and used by ComputeRayTracing.comp when a ray misses
 * all objects and samples the background.
 */

#include <imgui.h>
#include <glm/glm.hpp>

/**
* @brief Wrap an ImGui widget and mark that accumulation must restart if it changed.
* @param code - the code to be wrapped
*/
#define IMGUI_INPUT(code) \
    if (code) { \
        was_IMGUI_input = true; \
    }

/**
 * @brief Draws sky/background controls.
 *
 * Changing these values only affects shader uniforms. It does not rebuild scene
 * geometry, but it still invalidates the accumulated image because old samples
 * were computed with different sky lighting.
 */
void genSkyboxGUI(glm::vec3& groundColor, glm::vec3& skyColorHorizon, glm::vec3& skyColorZenith, bool& displaySkybox, bool& was_IMGUI_input, bool disabled)
{
	if (disabled) { ImGui::BeginDisabled(); }
	ImGui::Begin("Skybox Settings");

	IMGUI_INPUT(ImGui::Checkbox("Display Skybox", &displaySkybox));
	ImGui::Separator();
	IMGUI_INPUT(ImGui::ColorEdit3("GroundColor", &groundColor.x));
	IMGUI_INPUT(ImGui::ColorEdit3("SkyColorHorizon", &skyColorHorizon.x));
	IMGUI_INPUT(ImGui::ColorEdit3("SkyColorZenith", &skyColorZenith.x));

	ImGui::End();
	if (disabled) { ImGui::EndDisabled(); }
}