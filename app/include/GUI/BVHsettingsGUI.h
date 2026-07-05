#pragma once
/**
 * @file BVHsettingsGUI.h
 * @brief ImGui controls for BVH debug views and BVH construction heuristic.
 * @details
 * This panel mixes two kinds of controls:
 * - Changing the heuristic affects CPU construction. application.cpp detects the
 *   change, rebuilds the BVH with BVH::constructScene(), and recreates Renderer.
 * - Heatmap and pixel-data toggles are lightweight debug uniforms consumed by
 *   ComputeRayTracing.comp and shown in the viewport tooltip.
 */

#include <imgui.h>

// This header is intentionally small and header-only for the GUI. To avoid
// pulling the whole ObjParser.h dependency into this panel, it mirrors only the
// enum/stat structs that the widget needs to display and edit.
#ifndef heuristicEnum
#define heuristicEnum
namespace BVH
{
    enum class Heuristic {
        OBJECT_MEDIAN_SPLIT,
        SPATIAL_MIDDLE_SPLIT,
        SURFACE_AREA_HEURISTIC
    };

    struct Metrics {
        unsigned int nodeCount;
        unsigned int leafCount;
        unsigned int leafDepthMin;
        unsigned int leafDepthMax;
        float leafDepthMean;
        unsigned int leafTrianglesMin;
        unsigned int leafTrianglesMax;
        float leafTrianglesMean;
    };
}
#endif


/**
* @brief Wrap an ImGui widget and mark that accumulation must restart if it changed.
* @param code - the code to be wrapped
*/
#define IMGUI_INPUT(code) \
    if (code) { \
        was_IMGUI_input = true; \
    }


/**
* @brief Draws the BVH debug settings window.
* @param display_BVH Whether the shader should color pixels by AABB traversal count.
* @param display_TRI_heatmap Whether the shader should color pixels by triangle-test count.
* @param active_heuristic BVH construction strategy selected in the GUI.
* @param BVH_tree_depth Maximum traversal depth shown to the user and sent to the shader.
* @param metrics CPU-computed BVH statistics shown for study/debugging.
* @param was_IMGUI_input Marks that previous accumulated samples are no longer valid.
* @param disabled Disables editing while camera-control mode owns input.
* */
void BVH_settings_GUI(bool& display_BVH, bool& display_TRI_heatmap, BVH::Heuristic& active_heuristic, int BVH_tree_depth, const BVH::Metrics& metrics, int& bvh_heatmap_color_limit, int& triangle_heatmap_color_limit, bool& showPixelData, bool& was_IMGUI_input, bool disabled) {
    ImGuiWindowFlags BVH_window_flags = 0;
    BVH_window_flags |= ImGuiWindowFlags_NoCollapse;
    BVH_window_flags |= ImGuiWindowFlags_NoTitleBar;

    if (disabled) { ImGui::BeginDisabled(); }
    ImGui::Begin("BVH Settings", NULL, BVH_window_flags);

    ImGui::Text("BVH Max Traversal Depth: %d", BVH_tree_depth);
    ImGui::Text("Node Count: %u", metrics.nodeCount);
    ImGui::Text("Leaf Count: %u", metrics.leafCount);
    if (metrics.leafCount > 0) {
        ImGui::Text("Leaf Depth (min/max/mean): %u / %u / %.2f", metrics.leafDepthMin, metrics.leafDepthMax, metrics.leafDepthMean);
        ImGui::Text("Leaf Triangles (min/max/mean): %u / %u / %.2f", metrics.leafTrianglesMin, metrics.leafTrianglesMax, metrics.leafTrianglesMean);
    }

    static const char* heuristicNames[] = {
        "Object Median Split",
        "Spatial Middle Split",
        "Surface Area Heuristic"
    };
    int heuristicIndex = static_cast<int>(active_heuristic);
    if (ImGui::Combo("BVH Heuristic", &heuristicIndex, heuristicNames, IM_ARRAYSIZE(heuristicNames))) {
        // This does not rebuild inside the GUI. application.cpp compares the new
        // value with the applied value and then reconstructs the scene geometry.
        active_heuristic = static_cast<BVH::Heuristic>(heuristicIndex);
        was_IMGUI_input = true;
    }

    ImGui::SeparatorText("Visual");
    // Heatmaps are shader debug modes. They change how pixels are colored, but
    // they do not modify scene geometry or the BVH itself.
    if (ImGui::Checkbox("Show BVH heatmap", &display_BVH)) {
        was_IMGUI_input = true;
        if (display_BVH) {
            display_TRI_heatmap = false;
        }
        showPixelData = true;
    }
    IMGUI_INPUT(ImGui::SliderInt("BVH Heatmap intersections limit", &bvh_heatmap_color_limit, 10, 10000, NULL, ImGuiSliderFlags_Logarithmic));

    if (ImGui::Checkbox("Show triangle-test heatmap", &display_TRI_heatmap)) {
        was_IMGUI_input = true;
        if (display_TRI_heatmap) {
            display_BVH = false;
        }
        showPixelData = true;
    }
    IMGUI_INPUT(ImGui::SliderInt("Triangle-test heatmap limit", &triangle_heatmap_color_limit, 1, 100));

    if (!display_BVH && !display_TRI_heatmap) {
        ImGui::BeginDisabled();
        showPixelData = false; 
    }
    ImGui::Checkbox("Show pixel-data", &showPixelData);
    if (!display_BVH && !display_TRI_heatmap) { ImGui::EndDisabled(); }

    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::BeginItemTooltip())
    {
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
        ImGui::TextUnformatted("A heatmap option must be ENABLED to display pixel-data. When a heatmap is enabled and you hover over pixels in the viewport, a tooltip will appear. The tooltip provides detailed information about the pixel under the cursor, including its position, color values and the number of intersections.");

        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }

    ImGui::End();
	if (disabled) { ImGui::EndDisabled(); }
}
