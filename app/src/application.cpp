/**
 * @file application.cpp
 * @brief Entry point and main loop for the interactive ray tracing app.
 * @details
 * This file is the coordinator of the project. Scene presets are created on the
 * CPU, ObjParser/BVH prepares mesh data, Renderer uploads that data to OpenGL,
 * and the compute shaders produce the final image shown inside an ImGui viewport.
 *
 * Per frame, the flow is:
 * 1. read camera/input and draw GUI controls;
 * 2. update uniforms or scene buffers when something changed;
 * 3. run ComputeRayTracing.comp into an HDR texture;
 * 4. run ComputePostProcessing.comp into the display texture;
 * 5. draw that texture in the Viewport window.
 */

// third party
#include <GL/glew.h>
#include <GLFW/glfw3.h>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/string_cast.hpp>

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

// standard
#include <iostream>
#include <fstream> 
#include <memory>
#include <string>
#include <sstream> 
#include <vector>
#include <stdio.h>

// core
#include "core/Renderer.h" 
#include "core/gl_util/OpenGLdebugFuncs.h"
#include "core/camera/CameraHandler.hpp"

// app
#include "GUI/CameraRenderSettingsGUI.h"
#include "GUI/DockspaceGUI.h"
#include "GUI/PerformanceCounterGUI.h"
#include "GUI/InspectorGUI.h"
#include "GUI/SkyBoxGUI.h"
#include "GUI/BVHsettingsGUI.h"
#include "GUI/SceneSelectorGUI.h"

#include "delta_lib/DeltaTime.h"
#include "scenes/Scene1.hpp"

const int SCREEN_WIDTH = 1920, SCREEN_HEIGHT = 1080;
int main() {
#ifdef __APPLE__
	std::cerr << "Ray-Tracing requires OpenGL 4.3+ compute shaders, but macOS only provides OpenGL 4.1." << std::endl;
	std::cerr << "Run this project on Windows/Linux, or port the renderer to a backend supported on macOS." << std::endl;
	return 1;
#endif

	//WINDOW SETUP
	GLFWwindow* window;
	if (!glfwInit()){
		glfwTerminate();
		ASSERT(false);
		return 1;}

	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
	
	window = glfwCreateWindow(SCREEN_WIDTH, SCREEN_HEIGHT, " ", NULL, NULL);
	if (!window) {
		glfwTerminate();
		ASSERT(false);
		return 1;}

	glfwMakeContextCurrent(window);

	std::cout << glGetString(GL_VERSION) << std::endl;
	glfwSwapInterval(false);

	if (glewInit() != GLEW_OK){
		if (!window){
			glfwDestroyWindow(window);
			glfwTerminate();
			ASSERT(false);
			return 1;}}

	{	
		float ASPECT = 16.0/9.0;
		float fov = 90;

		DeltaTime deltaTime;

		Camera camera = Camera(fov, ASPECT, deltaTime);
		CameraHandler cameraHandler(camera);
		std::vector<ScenePreset> scenePresets = buildScenePresets();
		std::vector<const char*> sceneNames;
		sceneNames.reserve(scenePresets.size());
		for (const ScenePreset& preset : scenePresets) {
			sceneNames.push_back(preset.name.c_str());
		}

		int selectedSceneIndex = 0;
		ActiveScene activeScene = makeActiveScene(scenePresets[selectedSceneIndex]);

		// Build static geometry once for the active scene. Mesh triangles and
		// BVH nodes are expensive to rebuild, so they are reconstructed only when
		// the scene or BVH heuristic changes.
		BVH::Heuristic active_heuristic = BVH::Heuristic::SURFACE_AREA_HEURISTIC;
		BVH::SceneGeometryData sceneGeometry = BVH::constructScene(activeScene.preset.models, active_heuristic);
		activeScene.syncPBRTextureLayers(sceneGeometry.PBR_TEXTURES);
		std::cout << "BVH height: " << sceneGeometry.BVH_tree_depth << std::endl;

		camera.posVec = activeScene.preset.camera.position;
		camera.rotAroundX = activeScene.preset.camera.rotationDeg.x;
		camera.rotAroundY = activeScene.preset.camera.rotationDeg.y;
		camera.rotAroundZ = activeScene.preset.camera.rotationDeg.z;
		camera.updateRotation();

		std::unique_ptr<Renderer> renderer = std::make_unique<Renderer>(activeScene.sceneData, sceneGeometry);
		
		// Setup Dear ImGui context
		IMGUI_CHECKVERSION();
		ImGui::CreateContext();

		glfwSetWindowUserPointer(window, &cameraHandler);
		glfwSetKeyCallback(window, CameraHandler::GLFWKeyCallback);
		glfwSetCursorPosCallback(window, CameraHandler::GLFWMousePositionCallback);
		
		ImGuiIO& io = ImGui::GetIO(); (void)io;
		io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
		io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls
		io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;         // Enable Docking
		io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;       // Enable Multi-Viewport / Platform Windows

		ImGui::StyleColorsDark();

		ImGui_ImplGlfw_InitForOpenGL(window, true);
		ImGui_ImplOpenGL3_Init("#version 460");

		//////////////////////////
		// ImGui widget variables.
		//
		// These variables live here because the GUI only edits CPU state. Later in
		// the loop they are copied into Renderer uniform structs, which are then
		// uploaded to the compute shaders.
		bool was_ImGui_Input;
		bool shouldPostProcess = false;
		bool shouldAccumulate = true;
		bool useACESToneMapping = true;
		float exposure = 0.0f;
		float displayGamma = 1.0f;
		bool bloomEnabled = false;
		float bloomThreshold = 1.0f;
		float bloomIntensity = 0.15f;
		bool denoiseEnabled = false;
		float denoiseStrength = 1.0f;
		int bvh_heatmap_color_limit = 350;
		int triangle_heatmap_color_limit = 100;
		int raysPerPixel = 1;
		int bouncesPerRay = 5;
		
		//skybox
		bool show_skybox = activeScene.preset.sky.enabled;

		glm::vec3 SkyGroundColor  = activeScene.preset.sky.groundColor;
		glm::vec3 SkyColorHorizon = activeScene.preset.sky.horizonColor;
		glm::vec3 SkyColorZenith  = activeScene.preset.sky.zenithColor;
		
		bool show_demo_window = true;
		int totalFrames = 0;
		double prevCamAspect = 0;
		ImVec2 prevViewportWindowSize = ImVec2(0.0f, 0.0f);
		ImVec2 prevViewportWindowPos = ImVec2(0.0f, 0.0f);
		ImVec2 viewportSize = ImVec2(0.0f, 0.0f);
		ImVec2 topLeftTextureCoords = ImVec2(0.0f, 0.0f);
		ImVec2 bottomLeftTextureCoords = ImVec2(0.0f, 0.0f);
		
		// BVH settings
		bool display_BVH = false;
		bool display_TRI_heatmap = false;
		bool showPixelData = true;
		int displayed_layer = 1;
		bool display_multiple = true;
		int BVH_height = 32; // the height of the BVH tree (the size of the traversal stack in the shader)

		unsigned int viewport_mouseX;
		unsigned int viewport_mouseY;
		unsigned int prev_viewport_mouseX = 0;
		unsigned int prev_viewport_mouseY = 0;
		unsigned int inverted_viewport_mouseY;

		// for averaging the number of intersects
		unsigned int AABB_intersect_count_sum = 0;
		unsigned int TRI_intersect_count_sum = 0;
		unsigned int same_mouse_pos_count = 0;

		while (!glfwWindowShouldClose(window)) {
			deltaTime.update();
			totalFrames += 1;
			was_ImGui_Input = false;

			GLCall(glClear(GL_COLOR_BUFFER_BIT));
			GLCall(glClearColor(0.0f, 0.0f, 0.0f, 1.0f));

			ImGui_ImplOpenGL3_NewFrame();
			ImGui_ImplGlfw_NewFrame();
			ImGui::NewFrame();
			
			// Widget windows.
			//
			// Most widgets only change parameters used by the next dispatch. The
			// Inspector is special: it edits actual scene data, so application.cpp
			// syncs ActiveScene and asks Renderer to upload the changed buffers.
			genDockspace();
			scene_selector_gui(sceneNames.data(), static_cast<int>(sceneNames.size()), selectedSceneIndex, was_ImGui_Input, cameraHandler.CameraControllMode);
			if (genInspector(activeScene, cameraHandler.CameraControllMode)) {
				// Inspector edits affect material parameters, sphere data and
				// transforms. Those changes only require buffer uploads, not a
				// full BVH rebuild.
				activeScene.syncSceneData();
				renderer->updateSceneBuffers(activeScene.preset.models);
				was_ImGui_Input = true;
			}
			camera_render_settings_gui(
				camera,
				was_ImGui_Input,
				cameraHandler.CameraControllMode,
				shouldAccumulate,
				shouldPostProcess,
				useACESToneMapping,
				exposure,
				displayGamma,
				bloomEnabled,
				bloomThreshold,
				bloomIntensity,
				denoiseEnabled,
				denoiseStrength,
				raysPerPixel,
				bouncesPerRay);
			genSkyboxGUI(SkyGroundColor, SkyColorHorizon, SkyColorZenith, show_skybox, was_ImGui_Input, cameraHandler.CameraControllMode);
			BVH_settings_GUI(display_BVH, display_TRI_heatmap, active_heuristic, BVH_height, sceneGeometry.metrics, bvh_heatmap_color_limit, triangle_heatmap_color_limit, showPixelData, was_ImGui_Input, cameraHandler.CameraControllMode);

			static int appliedSceneIndex = selectedSceneIndex;
			static BVH::Heuristic appliedHeuristic = active_heuristic;
			if (appliedSceneIndex != selectedSceneIndex || appliedHeuristic != active_heuristic)
			{
				// Scene changes and BVH heuristic changes alter the triangle/BVH
				// buffers, so the geometry is reconstructed and the renderer is
				// recreated with fresh GPU resources.
				//
				// This is the most expensive kind of update in the app. Simple
				// material/camera/post-processing edits do not come through this path.
				appliedSceneIndex = selectedSceneIndex;
				appliedHeuristic = active_heuristic;
				activeScene = makeActiveScene(scenePresets[selectedSceneIndex]);
				activeScene.syncSceneData();
				sceneGeometry = BVH::constructScene(activeScene.preset.models, active_heuristic);
				activeScene.syncPBRTextureLayers(sceneGeometry.PBR_TEXTURES);
				renderer.reset();
				renderer = std::make_unique<Renderer>(activeScene.sceneData, sceneGeometry);
				if (viewportSize.x > 0.0f && viewportSize.y > 0.0f) {
					renderer->setViewportSize(glm::vec2(viewportSize.x, viewportSize.y));
				}

				SkyGroundColor = activeScene.preset.sky.groundColor;
				SkyColorHorizon = activeScene.preset.sky.horizonColor;
				SkyColorZenith = activeScene.preset.sky.zenithColor;
				show_skybox = activeScene.preset.sky.enabled;

				camera.posVec = activeScene.preset.camera.position;
				camera.rotAroundX = activeScene.preset.camera.rotationDeg.x;
				camera.rotAroundY = activeScene.preset.camera.rotationDeg.y;
				camera.rotAroundZ = activeScene.preset.camera.rotationDeg.z;
				camera.updateRotation();
				was_ImGui_Input = true;
			}



			ImGui::ShowDemoWindow(&show_demo_window);
			if (camera.cameraKeybinds.camera_keybind_window_active) { genCameraChangeKeybindSplashScreen(); }
			
			
			// viewport
			// The viewport is an ImGui window that displays the post-processed
			// compute texture. The actual render resolution follows the available
			// content area while preserving the camera aspect ratio.
			ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{ 0, 0 });
			ImGui::Begin("Viewport", nullptr, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoTitleBar| ImGuiWindowFlags_NoCollapse);

			ImGui::SetNextWindowDockID(ImGui::GetID("MainDockspace"));
			
			ImVec2 viewportWindowSize = ImGui::GetContentRegionAvail();

			// check if the viewport size or aspect ratio changed
			if (viewportWindowSize.x != prevViewportWindowSize.x || viewportWindowSize.y != prevViewportWindowSize.y || camera.getAspect() != prevCamAspect)
			{
				prevViewportWindowSize = viewportWindowSize;
				prevCamAspect = camera.getAspect();
			
				double parentAspect = double(viewportWindowSize.x) / double(viewportWindowSize.y);
				// Fit the render texture inside the ImGui window without
				// stretching. The unused area becomes letterboxing/pillarboxing.
				// check if the parent aspect is smaller or equal to camera aspect (width is the limiting factor so the window will span the entire width)
				if (parentAspect <=  camera.getAspect())
				{
					viewportSize.x = viewportWindowSize.x; // width stays the same
					viewportSize.y = ceil(viewportWindowSize.x / camera.getAspect()); // calculate the height based on the aspect ratio
				}
				// (height is the limiting factor so the window will span the entire height)
				else if (parentAspect > camera.getAspect())
				{
					viewportSize.y = viewportWindowSize.y; // height stays the same
					viewportSize.x = ceil(viewportWindowSize.y * camera.getAspect()); // calculate the width based on the aspect ratio
				}
				
				renderer->setViewportSize(glm::vec2(viewportSize.x, viewportSize.y)); // sets the texture size to the viewport size
			
				// The camera stores screen dimensions only to update projection
				// values used by the ray generation shader.
				camera.setScreenDimentions(int(viewportSize.x), int(viewportSize.y));
				was_ImGui_Input = true;
			
				topLeftTextureCoords.x = (viewportWindowSize.x - viewportSize.x) / 2.0f;
				topLeftTextureCoords.y = (viewportWindowSize.y - viewportSize.y) / 2.0f;
				// viewport offset
				topLeftTextureCoords.x += ImGui::GetWindowPos().x;
				topLeftTextureCoords.y += ImGui::GetWindowPos().y;
				
				bottomLeftTextureCoords.x = topLeftTextureCoords.x + viewportSize.x;
				bottomLeftTextureCoords.y = topLeftTextureCoords.y + viewportSize.y;
			}
			

			// check if the viewport window position changed
			ImVec2 viewportWindowPos = ImGui::GetWindowPos();
			if (viewportWindowPos.x != prevViewportWindowPos.x || viewportWindowPos.y != prevViewportWindowPos.y)
			{
				prevViewportWindowPos = viewportWindowPos;
			
				topLeftTextureCoords.x = (viewportWindowSize.x - viewportSize.x) / 2.0f;
				topLeftTextureCoords.y = (viewportWindowSize.y - viewportSize.y) / 2.0f;
				// viewport offset
				topLeftTextureCoords.x += viewportWindowPos.x;
				topLeftTextureCoords.y += viewportWindowPos.y;
			
				bottomLeftTextureCoords.x = topLeftTextureCoords.x + viewportSize.x;
				bottomLeftTextureCoords.y = topLeftTextureCoords.y + viewportSize.y;
			}
			
			camera.Update();
			
			static bool wasGlobalInput; // input from the camera, ImGui or any other source (which should reset accumulation)
			wasGlobalInput = (camera.wasCameraInput || was_ImGui_Input || !shouldAccumulate);
			static unsigned int m_NumAccumulatedFrames;
			// Progressive path tracing relies on accumulating many noisy samples.
			// Any camera/scene/UI change invalidates previous samples, so the
			// accumulation counter is reset to restart the running average.
			if (wasGlobalInput) { m_NumAccumulatedFrames = 0; }
			else { m_NumAccumulatedFrames++; }

			viewport_mouseX = ImGui::GetMousePos().x - topLeftTextureCoords.x;
			viewport_mouseY = ImGui::GetMousePos().y - topLeftTextureCoords.y;
			inverted_viewport_mouseY = viewportSize.y - viewport_mouseY;
			// render the scene
			renderer->BeginComputeRtxStage();
			// Fill the ray tracing uniform struct immediately before dispatch.
			// Renderer::RenderComputeRtxStage() uploads these values to binding 0,
			// where ComputeRayTracing.comp reads them as uniformParameters.
			renderer->rtx_uniform_parameters.numAccumulatedFrames = m_NumAccumulatedFrames;
			renderer->rtx_uniform_parameters.raysPerPixel = raysPerPixel;
			renderer->rtx_uniform_parameters.bouncesPerRay = bouncesPerRay;
			renderer->rtx_uniform_parameters.FocalLength = camera.focalLength;
			renderer->rtx_uniform_parameters.skyboxGroundColor = SkyGroundColor;
			renderer->rtx_uniform_parameters.skyboxHorizonColor = SkyColorHorizon;
			renderer->rtx_uniform_parameters.skyboxZenithColor = SkyColorZenith;
			renderer->rtx_uniform_parameters.CameraPos = camera.posVec;
			renderer->rtx_uniform_parameters.ModelMatrix = glm::mat4(camera.getModelMatrix());
			renderer->rtx_uniform_parameters.WasInput = wasGlobalInput;

			renderer->rtx_uniform_parameters.display_BVH = display_BVH;
			renderer->rtx_uniform_parameters.display_TRI_heatmap = display_TRI_heatmap;
			renderer->rtx_uniform_parameters.displayed_layer = displayed_layer;
			renderer->rtx_uniform_parameters.display_multiple = display_multiple;
			renderer->rtx_uniform_parameters.BVH_tree_depth = BVH_height;
			renderer->rtx_uniform_parameters.show_skybox = show_skybox;
			renderer->rtx_uniform_parameters.bvh_heatmap_color_limit = bvh_heatmap_color_limit;
			renderer->rtx_uniform_parameters.triangle_heatmap_color_limit = triangle_heatmap_color_limit;
			renderer->rtx_uniform_parameters.pixelGlobalInvocationID = glm::vec3(viewport_mouseX, inverted_viewport_mouseY, 1.0f); // invocations start from bottom left
			renderer->enablePixelDataReadback = showPixelData;


			renderer->RenderComputeRtxStage();
			
			renderer->BeginComputePostProcStage();
			// Post-processing reads the HDR ray tracing image and writes the
			// viewport image. These parameters control optional denoise/bloom and
			// tone mapping without retracing the scene geometry.
			renderer->postProcessing_uniform_parameters.numAccumulatedFrames = m_NumAccumulatedFrames;
			renderer->postProcessing_uniform_parameters.enabled = shouldPostProcess;
			renderer->postProcessing_uniform_parameters.useACES = useACESToneMapping;
			renderer->postProcessing_uniform_parameters.exposure = exposure;
			renderer->postProcessing_uniform_parameters.gamma = displayGamma;
			renderer->postProcessing_uniform_parameters.bloomEnabled = bloomEnabled;
			renderer->postProcessing_uniform_parameters.bloomThreshold = bloomThreshold;
			renderer->postProcessing_uniform_parameters.bloomIntensity = bloomIntensity;
			renderer->postProcessing_uniform_parameters.denoiseEnabled = denoiseEnabled;
			renderer->postProcessing_uniform_parameters.denoiseStrength = denoiseStrength;
			ComputeTexture* postProcOutput = renderer->RenderComputePostProcStage();
			
			ImDrawList* drawList = ImGui::GetWindowDrawList();
			// ImGui displays the OpenGL texture directly. UVs are flipped because
			// OpenGL image coordinates and ImGui texture coordinates use different
			// vertical origins.
			drawList->AddImage(reinterpret_cast<ImTextureID>(static_cast<intptr_t>(postProcOutput->ID())), topLeftTextureCoords, bottomLeftTextureCoords, {0, 1}, {1, 0});

			if (ImGui::IsWindowHovered() && showPixelData && !cameraHandler.CameraControllMode) {
				// Debug tooltip: the shader writes data for exactly one pixel, the
				// one under the mouse. Averaging while the mouse stays still makes
				// the reported intersection counts less noisy.
				if (viewport_mouseX >= 0 && viewport_mouseX < viewportSize.x && inverted_viewport_mouseY >= 0 && inverted_viewport_mouseY < viewportSize.y) {
					ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f, 10.0f));
						ImGui::BeginTooltip();
							ImGui::BeginGroup();
								ImGui::Spacing();
								ImGui::Text("Pixel Data");
								ImGui::Separator();
								ImGui::Text("X: %d, Y: %d", viewport_mouseX, inverted_viewport_mouseY);
								if (wasGlobalInput || viewport_mouseX != prev_viewport_mouseX || viewport_mouseY != prev_viewport_mouseY) {
									AABB_intersect_count_sum = 0;
									TRI_intersect_count_sum = 0;
									same_mouse_pos_count = 0;
								}
								prev_viewport_mouseX = viewport_mouseX;
								prev_viewport_mouseY = viewport_mouseY;

								AABB_intersect_count_sum += renderer->pixelData.AABB_intersect_count;
								TRI_intersect_count_sum += renderer->pixelData.pixelColor.w;
								same_mouse_pos_count++;


								glm::vec4 pclr = renderer->pixelData.pixelColor;
								
								ImGui::ColorButton("MyColor##3c", *(ImVec4*)&pclr, 0, ImVec2(20, 20));
								ImGui::SameLine();
								ImGui::Text("RGB: (%f, %f, %f)", pclr.r, pclr.g, pclr.b);
								ImGui::Text("ray-AABB intersects: %u", AABB_intersect_count_sum / same_mouse_pos_count);
								ImGui::Text("ray-triangle intersects: %u", TRI_intersect_count_sum / same_mouse_pos_count);
							ImGui::EndGroup();
						ImGui::EndTooltip();
					ImGui::PopStyleVar();
				}
			}

			ImGui::PopStyleVar();
			ImGui::End();
			
			genPerformanceCounter();
			camera.ResetFlags();
			
			ImGui::Render();
			ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
			glfwSwapBuffers(window);
			glfwPollEvents();
		}
	}
	ImGui_ImplOpenGL3_Shutdown();
	ImGui_ImplGlfw_Shutdown();
	ImGui::DestroyContext();

	glfwTerminate();
	return 0;
}
