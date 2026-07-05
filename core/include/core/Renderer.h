#pragma once
/**
 * @file Renderer.h
 * @brief OpenGL renderer interface and GPU uniform structures.
 * @details
 * Renderer is the hand-off point between CPU code and GLSL. application.cpp
 * fills the public uniform structs, ActiveScene/BVH provide geometry data, and
 * Renderer owns the OpenGL objects that expose that data to the compute shaders.
 */

#include <GL/glew.h>

#include "core/gl_util/ComputeShader.h"
#include "core/gl_util/ComputeTexture.h"

#include "imgui.h"

#include "core/gl_util/OpenGLdebugFuncs.h"
#include "core/camera/Camera.hpp"
#include "core/ObjParser/ObjParser.h"


/**
 * @struct SceneData
 * @brief CPU pointer block used to upload analytic sphere data to the renderer.
 * @details
 * sceneObjects points to the contiguous ActiveScene::spheres array. The renderer
 * uploads that memory directly to the sphere UBO, so size and numberOfObjects
 * must match the CPU and GLSL Sphere layouts.
 */
struct SceneData {
	const void* sceneObjects; ///< Raw pointer to the sphere array.
	size_t size; ///< Total byte size of the sphere array.
	int numberOfObjects; ///< Number of active sphere slots sent to the shader.
	const std::vector<PBRTextureSet>* pbrTextureSets = nullptr; ///< Extra PBR textures used by analytic spheres.
};

/**
 * @struct PixelData
 * @brief Debug data read back from the compute shader for the GUI.
 */
struct PixelData {
	glm::vec4 pixelColor; // .xyz = color, .w = TRI_intersect_count
	unsigned int AABB_intersect_count; ///< Number of BVH box tests for the pixel under the mouse.
};

/**
 * @struct rtx_parameters_uniform_struct
 * @brief Uniform data uploaded to the ray tracing compute shader.
 * @details
 * The offset comments document the std140 layout expected by the shader. This is
 * important because a mismatch here can produce invalid camera data, wrong
 * toggles, or driver crashes when the compute shader reads the UBO.
 */
struct rtx_parameters_uniform_struct {
	unsigned int numAccumulatedFrames;      // offset 0  // alignment 4 // total 4 bytes
	unsigned int raysPerPixel;              // offset 4  // alignment 4 // total 8 bytes
	unsigned int bouncesPerRay;             // offset 8  // alignment 4 // total 12 bytes
	float FocalLength;						// offset 12 // alignment 4 // total 16 bytes

	glm::vec3 skyboxGroundColor;			// offset 16 // alignment 16 // total 32 bytes
	glm::vec3 skyboxHorizonColor;			// offset 32 // alignment 16 // total 48 bytes
	glm::vec3 skyboxZenithColor;			// offset 48 // alignment 16 // total 64 bytes                            
	glm::vec3 CameraPos;					// offset 64 // alignment 16 // total 80 bytes

	glm::vec3 pixelGlobalInvocationID;		// offset 80 // alignment 16 // total 96 bytes

	glm::mat4 ModelMatrix;					// offset 96 // alignment 16 // total 160 bytes

	bool WasInput;							// offset 160 // alignment 4 // total 164 bytes
	bool display_BVH = false;				// offset 164 // alignment 4 // total 168 bytes
	bool display_TRI_heatmap = false;		// offset 168 // alignment 4 // total 172 bytes
	bool display_multiple = false;			// offset 172 // alignment 4 // total 176 bytes
	unsigned int displayed_layer = 0;		// offset 176 // alignment 4 // total 180 bytes
	unsigned int BVH_tree_depth = 0;		// offset 180 // alignment 4 // total 184 bytes
	bool show_skybox = true;				// offset 184 // alignment 4 // total 188 bytes
	int bvh_heatmap_color_limit = 350;		// offset 188 // alignment 4 // total 192 bytes
	int triangle_heatmap_color_limit = 100;	// offset 192 // alignment 4 // total 196 bytes
};

/**
 * @struct postProcessing_parameters_uniform_struct
 * @brief Uniform data uploaded to the post-processing compute shader.
 * @details These values come mostly from CameraRenderSettingsGUI.h. They change the
 * display pass only: the ray traced HDR texture is reused, then tone mapping,
 * bloom and denoise are applied into the viewport texture.
 */
struct postProcessing_parameters_uniform_struct {
	unsigned int numAccumulatedFrames;
	bool enabled = false;
	bool useACES = true;
	float exposure = 0.0f;
	float gamma = 1.0f;
	bool bloomEnabled = false;
	float bloomThreshold = 1.0f;
	float bloomIntensity = 0.15f;
	bool denoiseEnabled = false;
	float denoiseStrength = 1.0f;
};

/**
 * @class Renderer
 * @brief Owns the OpenGL resources used by the compute rendering pipeline.
 * @details
 * Responsibilities:
 * - create and update UBO/SSBO buffers for scene data, BVH and materials
 * - load PBR texture arrays
 * - dispatch ray tracing and post-processing compute shaders
 * - read back optional per-pixel debug information for the GUI
 */
class Renderer
{
private:
	SceneData m_Scene;

	glm::vec2 m_ViewportSize;

	void initComputeRtxStage();
	void initComputePostProcStage();

	// GPU buffer object ids in binding-point order:
	// 0 = ray tracing uniforms, 1 = spheres, 2 = post-processing uniforms,
	// 3 = triangles, 4 = BVH, 5 = pixel debug, 6 = model info.
	unsigned int rtx_parameters_UBO_ID, sphereBuffer_UBO_ID, postProcessing_parameters_UBO_ID, tris_SSBO_ID, BVH_SSBO_ID, pixelData_SSBO_ID, modelInfo_SSBO_ID;
	unsigned int albedoTextureArray_ID = 0;
	unsigned int normalTextureArray_ID = 0;
	unsigned int roughnessTextureArray_ID = 0;
	unsigned int metallicTextureArray_ID = 0;
	unsigned int aoTextureArray_ID = 0;
	unsigned int heightTextureArray_ID = 0;

	void configure_rtx_parameters_UBO_block();
	void update_rtx_parameters_UBO_block();

	void configure_sphereBuffer_UBO_block();
	void update_sphereBuffer_UBO_block();

	void configure_postProcessing_parameters_UBO_block();
	void update_postProcessing_parameters_UBO_block();

	void configure_TrisMesh_SSBO_block();
	void update_TrisMesh_SSBO_block();

	void configure_BVH_SSBO_block();
	void update_BVH_SSBO_block();

	void configure_PixelData_SSBO_block();
	void read_PixelData_SSBO_block();

	void configure_ModelInfo_SSBO_block();
	void update_ModelInfo_SSBO_block();

	void configurePBRTextureArrays();
	void bindPBRTextureArrays() const;

public:
	/**
	 * @brief Creates renderer resources for one scene.
	 * @param scene Sphere scene data owned by ActiveScene.
	 * @param sceneGeometry Mesh/BVH/model data built on the CPU.
	 */
	Renderer(SceneData& scene, BVH::SceneGeometryData sceneGeometry);
	~Renderer();

	/**
	 * @brief Resizes compute output textures to match the viewport.
	 * @param viewportSize ImGui viewport dimensions in pixels.
	 */
	void setViewportSize(glm::vec2 viewportSize);

	/**
	 * @brief Uploads editable scene parameters without rebuilding geometry.
	 * @param models Current model instances from the active scene.
	 */
	void updateSceneBuffers(const std::vector<ModelInstance>& models);

	/** @brief Binds the ray tracing compute shader and PBR texture arrays. */
	void BeginComputeRtxStage();

	/**
	 * @brief Dispatches the ray tracing compute shader.
	 * @return HDR compute texture containing accumulated radiance.
	 */
	ComputeTexture* RenderComputeRtxStage();
	rtx_parameters_uniform_struct rtx_uniform_parameters;

	PixelData pixelData; // should be read after rendering the compute rtx stage
	bool enablePixelDataReadback = false;

	/** @brief Binds the post-processing compute shader. */
	void BeginComputePostProcStage();

	/**
	 * @brief Dispatches tone mapping/bloom/denoise post-processing.
	 * @return LDR compute texture displayed in the viewport.
	 */
	ComputeTexture* RenderComputePostProcStage();
	postProcessing_parameters_uniform_struct postProcessing_uniform_parameters;

private:
	// Ray tracing stage: writes HDR accumulated radiance into computeRtxTexture.
	ComputeTexture* computeRtxTexture;
	ComputeShader* computeRtxShader;

	// Post-processing stage: reads computeRtxTexture and writes the image shown by ImGui.
	ComputeTexture* computePostProcTexture;
	ComputeShader* computePostProcShader;

	// Combined mesh/BVH/model data built by ObjParser/BVH on the CPU.
	BVH::SceneGeometryData m_SceneGeometry;
};
