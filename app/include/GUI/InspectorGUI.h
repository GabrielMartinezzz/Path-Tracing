#pragma once
/**
 * @file InspectorGUI.h
 * @brief ImGui inspector for editing scene objects and materials.
 * @details
 * The inspector edits the active scene on the CPU. When genInspector() returns
 * true, application.cpp calls ActiveScene::syncSceneData() and
 * Renderer::updateSceneBuffers() so the shader receives the new material,
 * transform or PBR parameter values.
 *
 * It does not rebuild the BVH. Scene changes and BVH heuristic changes are
 * handled separately in application.cpp because they change the geometry buffers.
 */

#include <algorithm>
#include <cmath>
#include <cfloat>
#include <string>

#include <imgui.h>

#include "scenes/Scene1.hpp"

/**
 * @brief Builds a readable mesh label for the inspector combo box.
 * @param path Mesh file path.
 * @param index Model index in the scene.
 * @return Label with index and file name.
 */
inline std::string inspectorDisplayName(const std::string& path, int index)
{
	const size_t slash = path.find_last_of("/\\");
	const std::string fileName = slash == std::string::npos ? path : path.substr(slash + 1);
	return "Mesh " + std::to_string(index) + ": " + fileName;
}

/**
 * @brief Builds a readable sphere label, including PBR/light hints.
 */
inline std::string sphereDisplayName(const ActiveScene& activeScene, int index)
{
	std::string label = "Sphere " + std::to_string(index);
	if (activeScene.spherePBRTextures[index].enabled()) {
		label += " (PBR)";
	}
	else if (activeScene.spheres[index].material.getEmissionStrength() > 0.0f) {
		label += " (Light)";
	}
	return label;
}

/**
 * @brief Draws material controls for the packed ray tracing material.
 * @param material Material edited in-place.
 * @return True when any widget changed.
 *
 * The edited material is later uploaded to the sphere/model buffers and read by
 * TraceRay() in the compute shader when a ray hits that object.
 */
inline bool editRaytracingMaterial(RaytracingMaterial& material)
{
	bool changed = false;

	static const char* materialTypes[] = { "Default", "Checker Pattern", "Glass" };
	int materialType = static_cast<int>(material.getType());
	if (ImGui::Combo("Type", &materialType, materialTypes, IM_ARRAYSIZE(materialTypes))) {
		material.setType(static_cast<RaytracingMaterial::Type>(materialType));
		changed = true;
	}

	glm::vec3 color = material.getColor();
	if (ImGui::ColorEdit3("Color", &color.x)) {
		material.setColor(color);
		changed = true;
	}

	glm::vec3 emissionColor = material.getEmissionColor();
	if (ImGui::ColorEdit3("Emission Color", &emissionColor.x)) {
		material.setEmissionColor(emissionColor);
		changed = true;
	}

	float emissionStrength = material.getEmissionStrength();
	if (ImGui::SliderFloat("Emission Strength", &emissionStrength, 0.0f, 50.0f, "%.2f")) {
		material.setEmissionStrength(emissionStrength);
		changed = true;
	}

	glm::vec3 specularColor = material.getSpecularColor();
	if (ImGui::ColorEdit3("Specular Color", &specularColor.x)) {
		material.setSpecularColor(specularColor);
		changed = true;
	}

	float smoothness = material.getSmoothness();
	if (ImGui::SliderFloat("Smoothness", &smoothness, 0.0f, 1.0f, "%.3f")) {
		material.setSmoothness(smoothness);
		changed = true;
	}

	float specularProbability = material.getSpecularProbability();
	if (ImGui::SliderFloat("Specular Probability", &specularProbability, 0.0f, 1.0f, "%.3f")) {
		material.setSpecularProbability(specularProbability);
		changed = true;
	}

	return changed;
}

/**
 * @brief Draws position/rotation/scale controls for a model instance.
 * @return True when the transform changed and localToWorld was recomputed.
 *
 * Moving a model changes its matrices in ModelInfo, not the raw triangle
 * positions. The shader transforms rays into the model's local space, so the
 * same mesh/BVH can still be reused.
 */
inline bool editModelTransform(ModelInstance& model)
{
	bool changed = false;

	if (ImGui::DragFloat3("Position", &model.position.x, 0.05f, -FLT_MAX, FLT_MAX, "%.3f")) {
		changed = true;
	}

	if (ImGui::DragFloat3("Rotation", &model.rotationDeg.x, 0.25f, -FLT_MAX, FLT_MAX, "%.2f")) {
		changed = true;
	}

	if (ImGui::DragFloat3("Scale", &model.scale.x, 0.01f, 0.001f, 1000.0f, "%.3f")) {
		changed = true;
	}

	if (changed) {
		constexpr float minScale = 0.001f;
		constexpr float maxScale = 1000.0f;
		model.scale.x = std::clamp(std::isfinite(model.scale.x) ? model.scale.x : 1.0f, minScale, maxScale);
		model.scale.y = std::clamp(std::isfinite(model.scale.y) ? model.scale.y : 1.0f, minScale, maxScale);
		model.scale.z = std::clamp(std::isfinite(model.scale.z) ? model.scale.z : 1.0f, minScale, maxScale);
		model.position.x = std::isfinite(model.position.x) ? model.position.x : 0.0f;
		model.position.y = std::isfinite(model.position.y) ? model.position.y : 0.0f;
		model.position.z = std::isfinite(model.position.z) ? model.position.z : 0.0f;
		model.rotationDeg.x = std::isfinite(model.rotationDeg.x) ? model.rotationDeg.x : 0.0f;
		model.rotationDeg.y = std::isfinite(model.rotationDeg.y) ? model.rotationDeg.y : 0.0f;
		model.rotationDeg.z = std::isfinite(model.rotationDeg.z) ? model.rotationDeg.z : 0.0f;
		updateModelTransform(model);
	}

	return changed;
}

/**
 * @brief Draws runtime PBR controls for texture scale, normal, roughness,
 * metallic and parallax parameters.
 * @return True when any PBR multiplier changed.
 *
 * These controls tune how existing texture layers are sampled. They do not load
 * new image files; texture loading happens when Renderer is created for a scene.
 */
inline bool editPBRTextureSettings(PBRTextureSet& textures)
{
	if (!textures.enabled()) {
		return false;
	}

	bool changed = false;
	ImGui::TextUnformatted("PBR textures enabled");
	// Runtime multipliers are sent through pbrParameters/pbrExtraParameters and
	// applied in the compute shader.
	changed |= ImGui::SliderFloat("UV Scale", &textures.uvScale, 0.1f, 10.0f, "%.2f");
	changed |= ImGui::SliderFloat("Normal Strength", &textures.normalStrength, 0.0f, 2.0f, "%.2f");
	changed |= ImGui::SliderFloat("Roughness Map Strength", &textures.roughnessMultiplier, 0.0f, 2.0f, "%.2f");
	changed |= ImGui::SliderFloat("Roughness Bias", &textures.roughnessBias, -1.0f, 1.0f, "%.2f");
	if (ImGui::IsItemHovered()) {
		ImGui::SetTooltip("Adds to the roughness map. Negative values make the surface glossier.");
	}
	changed |= ImGui::SliderFloat("Metallic Map Strength", &textures.metallicMultiplier, 0.0f, 2.0f, "%.2f");
	changed |= ImGui::SliderFloat("Metallic Base", &textures.metallicBase, 0.0f, 1.0f, "%.2f");
	if (ImGui::IsItemHovered()) {
		ImGui::SetTooltip("Minimum metallic value. Useful when the metallic texture is black.");
	}
	changed |= ImGui::SliderFloat("Parallax Strength", &textures.parallaxStrength, 0.0f, 0.12f, "%.3f");
	if (ImGui::IsItemHovered()) {
		ImGui::SetTooltip("Offsets texture UVs using the height map. It adds fake depth, not real displacement or silhouette changes.");
	}
	return changed;
}

/**
 * @brief Draws the full scene inspector window.
 * @param activeScene Mutable scene being rendered.
 * @param disabled Disables editing when camera-control mode owns input.
 * @return True when any scene value changed and GPU buffers must be updated.
 *
 * application.cpp is responsible for acting on the return value. That keeps this
 * file focused on UI and keeps OpenGL buffer updates inside Renderer.
 */
inline bool genInspector(ActiveScene& activeScene, bool disabled)
{
	if (disabled) { ImGui::BeginDisabled(); }
	ImGui::Begin("Inspector");
	bool changed = false;

	if (ImGui::CollapsingHeader("Meshes", ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::PushID("MeshMaterialEditor");
		if (activeScene.preset.models.empty()) {
			ImGui::TextUnformatted("No meshes in this scene.");
		}
		else {
			static int selectedModel = 0;
			static bool editAllModels = false;
			selectedModel = std::clamp(selectedModel, 0, static_cast<int>(activeScene.preset.models.size()) - 1);

			ImGui::Checkbox("Edit all meshes", &editAllModels);

			if (!editAllModels) {
				std::string preview = inspectorDisplayName(activeScene.preset.models[selectedModel].meshPath, selectedModel);
				if (ImGui::BeginCombo("Mesh", preview.c_str())) {
					for (int i = 0; i < static_cast<int>(activeScene.preset.models.size()); ++i) {
						const std::string label = inspectorDisplayName(activeScene.preset.models[i].meshPath, i);
						const bool selected = selectedModel == i;
						if (ImGui::Selectable(label.c_str(), selected)) {
							selectedModel = i;
						}
						if (selected) {
							ImGui::SetItemDefaultFocus();
						}
					}
					ImGui::EndCombo();
				}
			}

			RaytracingMaterial& material = activeScene.preset.models[editAllModels ? 0 : selectedModel].material;
			if (!editAllModels) {
				ImGui::SeparatorText("Transform");
				changed |= editModelTransform(activeScene.preset.models[selectedModel]);
			}

			ImGui::SeparatorText("Material");
			if (editRaytracingMaterial(material)) {
				if (editAllModels) {
					for (ModelInstance& model : activeScene.preset.models) {
						model.material = material;
					}
				}
				changed = true;
			}

			if (!editAllModels && activeScene.preset.models[selectedModel].pbrTextures.enabled()) {
				ImGui::SeparatorText("PBR Textures");
				changed |= editPBRTextureSettings(activeScene.preset.models[selectedModel].pbrTextures);
			}
		}
		ImGui::PopID();
	}

	if (ImGui::CollapsingHeader("Spheres", ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::PushID("SphereMaterialEditor");
		static int selectedSphere = 0;
		selectedSphere = std::clamp(selectedSphere, 0, MAX_SCENE_SPHERES - 1);
		if (activeScene.spheres[selectedSphere].radius <= 0.0f) {
			for (int i = 0; i < MAX_SCENE_SPHERES; ++i) {
				if (activeScene.spheres[i].radius > 0.0f) {
					selectedSphere = i;
					break;
				}
			}
		}

		if (ImGui::BeginCombo("Sphere", sphereDisplayName(activeScene, selectedSphere).c_str())) {
			for (int i = 0; i < MAX_SCENE_SPHERES; ++i) {
				if (activeScene.spheres[i].radius <= 0.0f) {
					continue;
				}
				const std::string label = sphereDisplayName(activeScene, i);
				const bool selected = selectedSphere == i;
				if (ImGui::Selectable(label.c_str(), selected)) {
					selectedSphere = i;
				}
				if (selected) {
					ImGui::SetItemDefaultFocus();
				}
			}
			ImGui::EndCombo();
		}

		if (activeScene.spheres[selectedSphere].radius > 0.0f) {
			changed |= editRaytracingMaterial(activeScene.spheres[selectedSphere].material);
			if (activeScene.spherePBRTextures[selectedSphere].enabled()) {
				ImGui::SeparatorText("PBR Textures");
				ImGui::Text("Texture Layer: %d", activeScene.spheres[selectedSphere].pbrTextureInfo.x);
				changed |= editPBRTextureSettings(activeScene.spherePBRTextures[selectedSphere]);
			}
			else {
				ImGui::TextUnformatted("No PBR textures on this sphere.");
			}
		}
		else {
			ImGui::TextUnformatted("No active spheres in this scene.");
		}
		ImGui::PopID();
	}

	ImGui::End();
	if (disabled) { ImGui::EndDisabled(); }
	return changed;
}
