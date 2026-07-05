#pragma once
/**
 * @file DockspaceGUI.h
 * @brief Creates the full-screen ImGui docking area used by the app.
 * @details
 * The dockspace is only layout infrastructure. It does not change renderer
 * state; it just gives the viewport and control panels a common parent where the
 * user can dock, move and resize windows.
 */

#include <imgui.h>

/** @brief Draws the invisible full-window docking host. */
void genDockspace()
{

	ImVec2 window_size = ImGui::GetIO().DisplaySize;

	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));

	ImGui::SetNextWindowSize(window_size);
	ImGui::SetNextWindowPos(ImVec2(0, 0));
	ImGui::Begin("dockspace", nullptr, ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove);

	ImGuiID dockspace_id = ImGui::GetID("Dockspace");
	ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f));

	ImGui::PopStyleVar();
	ImGui::End();
}
