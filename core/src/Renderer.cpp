/**
 * @file Renderer.cpp
 * @brief OpenGL resource creation, buffer uploads and compute shader dispatch.
 * @details
 * This file is the bridge between the C++ scene and the GLSL shaders. The scene
 * is built on the CPU, but the ray tracer runs on the GPU, so Renderer creates
 * the OpenGL buffers/textures, uploads the data, and launches the compute passes.
 *
 * Frame flow:
 * 1. application.cpp fills the public uniform structs.
 * 2. Renderer uploads those values to GPU buffers.
 * 3. ComputeRayTracing.comp writes an HDR image.
 * 4. ComputePostProcessing.comp turns that HDR image into the viewport texture.
 */

#include <algorithm>
#include <cmath>
#include <iostream>
#include <utility>
#include <vector>

#include "core/Renderer.h"
#include <stb_image/stb_image.h>

struct SceneData;

namespace {
// Creates one OpenGL 2D texture array for a PBR channel.
//
// References:
// - OpenGL texture array documentation:
//   https://registry.khronos.org/OpenGL-Refpages/gl4/html/glTexStorage3D.xhtml
//
// A texture array is like a stack of same-sized images. The shader samples it
// with vec3(uv, layer), where layer chooses which material texture to use.
// OpenGL requires every layer in one array to have the same width, height and
// format, so the first loaded texture defines the array size. Missing or
// mismatched layers are filled with neutral fallback values.

unsigned int createTextureArray(
	const std::vector<std::string>& paths,
	int desiredChannels,
	unsigned int internalFormat,
	unsigned int uploadFormat,
	const unsigned char* fallback)
{
	int width = 1;
	int height = 1;
	int channels = 0;
	const int layerCount = std::max(1, static_cast<int>(paths.size()));

	stbi_set_flip_vertically_on_load(1);
	unsigned char* firstImage = nullptr;
	if (!paths.empty()) {
		// The first valid image defines the resolution of the whole stack. Any
		// later image with a different size cannot be placed in this same array.
		firstImage = stbi_load(paths[0].c_str(), &width, &height, &channels, desiredChannels);
		if (!firstImage) {
			std::cerr << "Failed to load PBR texture: " << paths[0] << std::endl;
			width = 1;
			height = 1;
		}
	}

	unsigned int texture = 0;
	GLCall(glGenTextures(1, &texture));
	GLCall(glBindTexture(GL_TEXTURE_2D_ARRAY, texture));
	GLCall(glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR));
	GLCall(glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR));
	GLCall(glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_REPEAT));
	GLCall(glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_REPEAT));
	GLCall(glTexStorage3D(
		GL_TEXTURE_2D_ARRAY,
		// Allocate mipmaps too. Mipmaps are smaller versions of the texture used
		// when the surface is far away, reducing shimmering/noise in texture lookup.
		static_cast<int>(std::floor(std::log2(std::max(width, height)))) + 1,
		internalFormat,
		width,
		height,
		layerCount));

	for (int layer = 0; layer < layerCount; ++layer) {
		unsigned char* pixels = layer == 0 ? firstImage : nullptr;
		int imageWidth = width;
		int imageHeight = height;
		if (layer > 0 && layer < static_cast<int>(paths.size())) {
			pixels = stbi_load(paths[layer].c_str(), &imageWidth, &imageHeight, &channels, desiredChannels);
		}

		if (pixels && imageWidth == width && imageHeight == height) {
			// Upload this image into one layer of the stack. ModelInfo.textureLayer
			// later tells the shader which layer belongs to each model.
			GLCall(glTexSubImage3D(GL_TEXTURE_2D_ARRAY, 0, 0, 0, layer, width, height, 1, uploadFormat, GL_UNSIGNED_BYTE, pixels));
		}
		else {
			if (pixels) {
				std::cerr << "PBR texture dimensions do not match: " << paths[layer] << std::endl;
			}
			// Missing or mismatched textures receive a neutral fallback. That keeps
			// shader code simple: it can always sample a texture layer instead of
			// checking whether a file existed. Fill the whole layer, not just one
			// pixel, because uninitialized texture memory can produce random colors.
			std::vector<unsigned char> fallbackLayer(width * height * desiredChannels);
			for (int pixel = 0; pixel < width * height; ++pixel) {
				for (int channel = 0; channel < desiredChannels; ++channel) {
					fallbackLayer[pixel * desiredChannels + channel] = fallback[channel];
				}
			}
			GLCall(glTexSubImage3D(GL_TEXTURE_2D_ARRAY, 0, 0, 0, layer, width, height, 1, uploadFormat, GL_UNSIGNED_BYTE, fallbackLayer.data()));
		}

		if (pixels) {
			stbi_image_free(pixels);
		}
	}

	GLCall(glGenerateMipmap(GL_TEXTURE_2D_ARRAY));
	GLCall(glBindTexture(GL_TEXTURE_2D_ARRAY, 0));
	return texture;
}
}

Renderer::Renderer(SceneData& scene, BVH::SceneGeometryData sceneGeometry)
	: m_Scene(scene),

	computeRtxShader(nullptr),
	computeRtxTexture(nullptr),

	computePostProcShader(nullptr),
	computePostProcTexture(nullptr),

	m_SceneGeometry(std::move(sceneGeometry))
{
	// Create all GPU resources for the currently selected scene. If the user
	// changes scene preset or BVH heuristic, application.cpp replaces this whole
	// Renderer so mesh buffers, BVH buffers and texture arrays start fresh.
	initComputeRtxStage();
	initComputePostProcStage();
}

Renderer::~Renderer()
{
	// Release every OpenGL object owned by this renderer. This matters when
	// switching scenes because the app creates a new Renderer and destroys the old
	// one.
	delete computeRtxShader;
	delete computeRtxTexture;

	delete computePostProcShader;
	delete computePostProcTexture;

	glDeleteBuffers(1, &rtx_parameters_UBO_ID);
	glDeleteBuffers(1, &sphereBuffer_UBO_ID);
	glDeleteBuffers(1, &postProcessing_parameters_UBO_ID);
	glDeleteBuffers(1, &tris_SSBO_ID);
	glDeleteBuffers(1, &BVH_SSBO_ID);
	glDeleteBuffers(1, &pixelData_SSBO_ID);
	glDeleteBuffers(1, &modelInfo_SSBO_ID);
	glDeleteTextures(1, &albedoTextureArray_ID);
	glDeleteTextures(1, &normalTextureArray_ID);
	glDeleteTextures(1, &roughnessTextureArray_ID);
	glDeleteTextures(1, &metallicTextureArray_ID);
	glDeleteTextures(1, &aoTextureArray_ID);
	glDeleteTextures(1, &heightTextureArray_ID);
}

void Renderer::setViewportSize(glm::vec2 viewportSize)
{
	// Render resolution follows the ImGui viewport, not necessarily the OS window.
	// When the viewport changes size, recreate the ray tracing and post-processing
	// textures so every shader invocation maps to a valid pixel.
	GLCall(glViewport(0, 0, viewportSize.x, viewportSize.y));
	m_ViewportSize = viewportSize;

	delete computePostProcTexture;
	computePostProcTexture = new ComputeTexture(viewportSize.x, viewportSize.y, 1);

	delete computeRtxTexture;
	computeRtxTexture = new ComputeTexture(viewportSize.x, viewportSize.y, 0);
}

void Renderer::updateSceneBuffers(const std::vector<ModelInstance>& models)
{
	// Called after Inspector edits. Materials, transforms and PBR multipliers can
	// change without changing the mesh triangles, so this only re-uploads dynamic
	// scene data and does not rebuild the BVH.
	update_sphereBuffer_UBO_block();

	const size_t modelCount = std::min(m_SceneGeometry.MODELS.size(), models.size());
	for (size_t i = 0; i < modelCount; ++i) {
		// Keep per-model data synchronized with GUI edits. The expensive shared
		// triangle/BVH buffers remain unchanged.
		m_SceneGeometry.MODELS[i].localToWorldMatrix = models[i].localToWorld;
		m_SceneGeometry.MODELS[i].worldToLocalMatrix = glm::inverse(models[i].localToWorld);
		m_SceneGeometry.MODELS[i].material = models[i].material;
		m_SceneGeometry.MODELS[i].pbrParameters = glm::vec4(
			models[i].pbrTextures.uvScale,
			models[i].pbrTextures.normalStrength,
			models[i].pbrTextures.roughnessMultiplier,
			models[i].pbrTextures.metallicMultiplier);
		m_SceneGeometry.MODELS[i].pbrExtraParameters = glm::vec4(
			models[i].pbrTextures.parallaxStrength,
			models[i].pbrTextures.roughnessBias,
			models[i].pbrTextures.metallicBase,
			0.0f);
	}
	update_ModelInfo_SSBO_block();
}


void Renderer::initComputeRtxStage()
{	
	// Set up everything the ray tracing shader needs except the output texture.
	// The texture size depends on the ImGui viewport and is created later in
	// setViewportSize().
	//computeRtxUBO = new UniformBuffer(sizeof(ComputeRtxUniforms), 0);
	computeRtxShader = new ComputeShader(CORE_RESOURCES_PATH "shaders/ComputeRayTracing.comp");
	computeRtxShader->Bind();
	configure_rtx_parameters_UBO_block();
	configure_sphereBuffer_UBO_block();
	configure_TrisMesh_SSBO_block();
	configure_BVH_SSBO_block();
	configure_PixelData_SSBO_block();
	configure_ModelInfo_SSBO_block();
	configurePBRTextureArrays();
	
	// Static scene buffers are uploaded once here. Inspector edits can later call
	// updateSceneBuffers() for the smaller dynamic updates.
	update_sphereBuffer_UBO_block();
	update_TrisMesh_SSBO_block();
	update_BVH_SSBO_block();
	update_ModelInfo_SSBO_block();
}

void Renderer::BeginComputeRtxStage()
{
	computeRtxShader->Bind();
	// PBR samplers are regular texture units, so bind them before dispatching the
	// ray tracing compute shader.
	bindPBRTextureArrays();
}

ComputeTexture* Renderer::RenderComputeRtxStage()
{
	if (!computeRtxTexture || !computePostProcTexture || m_ViewportSize.x <= 0.0f || m_ViewportSize.y <= 0.0f) {
		setViewportSize(glm::vec2(1.0f, 1.0f));
	}

	// One dispatch traces the whole viewport. Each workgroup contains 8x4 shader
	// invocations, so divide the viewport by 8 and 4, rounding up, to cover every
	// pixel.
	update_rtx_parameters_UBO_block();
	computeRtxShader->DrawCall(ceil(m_ViewportSize.x / 8), ceil(m_ViewportSize.y / 4), 1); // work_groups size

	if (enablePixelDataReadback) {
		read_PixelData_SSBO_block();
	}

	return computeRtxTexture;
}


void Renderer::initComputePostProcStage()
{
	computePostProcShader = new ComputeShader(CORE_RESOURCES_PATH "shaders/ComputePostProcessing.comp");
	computePostProcShader->Bind();
	configure_postProcessing_parameters_UBO_block();
}

void Renderer::BeginComputePostProcStage()
{
	computePostProcShader->Bind();
}

ComputeTexture* Renderer::RenderComputePostProcStage()
{
	if (!computeRtxTexture || !computePostProcTexture || m_ViewportSize.x <= 0.0f || m_ViewportSize.y <= 0.0f) {
		setViewportSize(glm::vec2(1.0f, 1.0f));
	}

	// Second dispatch in the frame: read the HDR ray tracing texture, apply
	// display effects, and write the texture that ImGui shows in the viewport.
	update_postProcessing_parameters_UBO_block();
	computePostProcShader->DrawCall(ceil(m_ViewportSize.x / 8), ceil(m_ViewportSize.y / 4), 1);
	return computePostProcTexture;
}

// Binding point 0: small per-frame ray tracing settings.
// ComputeRayTracing.comp reads this as uniformParameters. It changes every frame
// because the camera, sample count, debug toggles or sky colors can change.
void Renderer::configure_rtx_parameters_UBO_block() {	
	GLCall(glGenBuffers(1, &rtx_parameters_UBO_ID));
	GLCall(glBindBuffer(GL_UNIFORM_BUFFER, rtx_parameters_UBO_ID));
	GLCall(glBufferData(GL_UNIFORM_BUFFER, 208, nullptr, GL_DYNAMIC_DRAW));
	GLCall(glBindBufferBase(GL_UNIFORM_BUFFER, 0, rtx_parameters_UBO_ID)); // binding the uniform buffer object to binding point 0
}

void Renderer::update_rtx_parameters_UBO_block() {
	// Upload booleans as unsigned ints. This avoids ambiguity between C++ bool
	// size and GLSL/std140 bool layout on different drivers.
	const unsigned int wasInput = rtx_uniform_parameters.WasInput ? 1u : 0u;
	const unsigned int displayBVH = rtx_uniform_parameters.display_BVH ? 1u : 0u;
	const unsigned int displayTriangleHeatmap = rtx_uniform_parameters.display_TRI_heatmap ? 1u : 0u;
	const unsigned int displayMultiple = rtx_uniform_parameters.display_multiple ? 1u : 0u;
	const unsigned int showSkybox = rtx_uniform_parameters.show_skybox ? 1u : 0u;

	GLCall(glBindBuffer(GL_UNIFORM_BUFFER, rtx_parameters_UBO_ID));
	GLCall(glBufferSubData(GL_UNIFORM_BUFFER, 0,   sizeof(unsigned int), &rtx_uniform_parameters.numAccumulatedFrames));
	GLCall(glBufferSubData(GL_UNIFORM_BUFFER, 4,   sizeof(unsigned int), &rtx_uniform_parameters.raysPerPixel));
	GLCall(glBufferSubData(GL_UNIFORM_BUFFER, 8,   sizeof(unsigned int), &rtx_uniform_parameters.bouncesPerRay));
	GLCall(glBufferSubData(GL_UNIFORM_BUFFER, 12,  sizeof(float), &rtx_uniform_parameters.FocalLength));
	
	GLCall(glBufferSubData(GL_UNIFORM_BUFFER, 16,  sizeof(glm::vec3), &rtx_uniform_parameters.skyboxGroundColor));
	GLCall(glBufferSubData(GL_UNIFORM_BUFFER, 32,  sizeof(glm::vec3), &rtx_uniform_parameters.skyboxHorizonColor));
	GLCall(glBufferSubData(GL_UNIFORM_BUFFER, 48,  sizeof(glm::vec3), &rtx_uniform_parameters.skyboxZenithColor));
	GLCall(glBufferSubData(GL_UNIFORM_BUFFER, 64,  sizeof(glm::vec3), &rtx_uniform_parameters.CameraPos));
	
	GLCall(glBufferSubData(GL_UNIFORM_BUFFER, 80, sizeof(glm::vec3), &rtx_uniform_parameters.pixelGlobalInvocationID));
	
	GLCall(glBufferSubData(GL_UNIFORM_BUFFER, 96,  sizeof(glm::mat4), &rtx_uniform_parameters.ModelMatrix));

	GLCall(glBufferSubData(GL_UNIFORM_BUFFER, 160, sizeof(unsigned int), &wasInput));
	GLCall(glBufferSubData(GL_UNIFORM_BUFFER, 164, sizeof(unsigned int), &displayBVH));
	GLCall(glBufferSubData(GL_UNIFORM_BUFFER, 168, sizeof(unsigned int), &displayTriangleHeatmap));
	GLCall(glBufferSubData(GL_UNIFORM_BUFFER, 172, sizeof(unsigned int), &displayMultiple));
	GLCall(glBufferSubData(GL_UNIFORM_BUFFER, 176, sizeof(unsigned int), &rtx_uniform_parameters.displayed_layer));
	GLCall(glBufferSubData(GL_UNIFORM_BUFFER, 180, sizeof(unsigned int), &rtx_uniform_parameters.BVH_tree_depth));
	GLCall(glBufferSubData(GL_UNIFORM_BUFFER, 184, sizeof(unsigned int), &showSkybox));
	GLCall(glBufferSubData(GL_UNIFORM_BUFFER, 188, sizeof(int), &rtx_uniform_parameters.bvh_heatmap_color_limit));
	GLCall(glBufferSubData(GL_UNIFORM_BUFFER, 192, sizeof(int), &rtx_uniform_parameters.triangle_heatmap_color_limit));
	
	GLCall(glBindBuffer(GL_UNIFORM_BUFFER, 0));
}

// Binding point 1: fixed-size analytic sphere buffer.
// ComputeRayTracing.comp reads this as u_Spheres. Some spheres are visible
// objects, others are emissive spheres used as lights.
void Renderer::configure_sphereBuffer_UBO_block() {
	GLCall(glGenBuffers(1, &sphereBuffer_UBO_ID));
	GLCall(glBindBuffer(GL_UNIFORM_BUFFER, sphereBuffer_UBO_ID));
	GLCall(glBufferData(GL_UNIFORM_BUFFER, m_Scene.size, nullptr, GL_STATIC_DRAW));
	GLCall(glBindBufferBase(GL_UNIFORM_BUFFER, 1, sphereBuffer_UBO_ID)); 
}

void Renderer::update_sphereBuffer_UBO_block() {
	GLCall(glBindBuffer(GL_UNIFORM_BUFFER, sphereBuffer_UBO_ID));
	GLCall(glBufferSubData(GL_UNIFORM_BUFFER, 0, m_Scene.size, m_Scene.sceneObjects)); // sceneObjects are already a const void* pointer
	GLCall(glBindBuffer(GL_UNIFORM_BUFFER, 0));
}

// Binding point 2: small post-processing settings.
// ComputePostProcessing.comp uses these to control tone mapping, bloom and
// denoise after the HDR ray traced image has been produced.
void Renderer::configure_postProcessing_parameters_UBO_block() {
	// std140 uniform blocks are aligned to 16-byte chunks, so the buffer is a bit
	// larger than the visible fields might suggest.
	unsigned int postProcessing_buffer_size_bytes = 48;
	GLCall(glGenBuffers(1, &postProcessing_parameters_UBO_ID));
	GLCall(glBindBuffer(GL_UNIFORM_BUFFER, postProcessing_parameters_UBO_ID));
	GLCall(glBufferData(GL_UNIFORM_BUFFER, postProcessing_buffer_size_bytes, nullptr, GL_DYNAMIC_DRAW));
	GLCall(glBindBufferBase(GL_UNIFORM_BUFFER, 2, postProcessing_parameters_UBO_ID)); // binding point 0
}

void Renderer::update_postProcessing_parameters_UBO_block() {
	const unsigned int enabled = postProcessing_uniform_parameters.enabled ? 1u : 0u;
	const unsigned int useACES = postProcessing_uniform_parameters.useACES ? 1u : 0u;
	const unsigned int bloomEnabled = postProcessing_uniform_parameters.bloomEnabled ? 1u : 0u;
	const unsigned int denoiseEnabled = postProcessing_uniform_parameters.denoiseEnabled ? 1u : 0u;

	GLCall(glBindBuffer(GL_UNIFORM_BUFFER, postProcessing_parameters_UBO_ID));
	GLCall(glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(unsigned int), &postProcessing_uniform_parameters.numAccumulatedFrames));
	GLCall(glBufferSubData(GL_UNIFORM_BUFFER, 4, sizeof(unsigned int), &enabled));
	GLCall(glBufferSubData(GL_UNIFORM_BUFFER, 8, sizeof(unsigned int), &useACES));
	GLCall(glBufferSubData(GL_UNIFORM_BUFFER, 12, sizeof(float), &postProcessing_uniform_parameters.exposure));
	GLCall(glBufferSubData(GL_UNIFORM_BUFFER, 16, sizeof(float), &postProcessing_uniform_parameters.gamma));
	GLCall(glBufferSubData(GL_UNIFORM_BUFFER, 20, sizeof(unsigned int), &bloomEnabled));
	GLCall(glBufferSubData(GL_UNIFORM_BUFFER, 24, sizeof(float), &postProcessing_uniform_parameters.bloomThreshold));
	GLCall(glBufferSubData(GL_UNIFORM_BUFFER, 28, sizeof(float), &postProcessing_uniform_parameters.bloomIntensity));
	GLCall(glBufferSubData(GL_UNIFORM_BUFFER, 32, sizeof(unsigned int), &denoiseEnabled));
	GLCall(glBufferSubData(GL_UNIFORM_BUFFER, 36, sizeof(float), &postProcessing_uniform_parameters.denoiseStrength));
	GLCall(glBindBuffer(GL_UNIFORM_BUFFER, 0));
}

void Renderer::configure_TrisMesh_SSBO_block()
{
	// Binding point 3: big triangle list used by RayTriangleIntersection().
	// This is static for a scene. Editing a material or transform does not change
	// the actual triangle geometry.
	GLCall(glGenBuffers(1, &tris_SSBO_ID));
	GLCall(glBindBuffer(GL_SHADER_STORAGE_BUFFER, tris_SSBO_ID));
	GLCall(glBufferData(GL_SHADER_STORAGE_BUFFER, std::max<size_t>(sizeof(Triangle), sizeof(Triangle) * m_SceneGeometry.TRIANGLES_size), nullptr, GL_STATIC_DRAW));
	GLCall(glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, tris_SSBO_ID));
}

void Renderer::update_TrisMesh_SSBO_block()
{
	GLCall(glBindBuffer(GL_SHADER_STORAGE_BUFFER, tris_SSBO_ID));
	if (!m_SceneGeometry.TRIANGLES.empty()) {
		GLCall(glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, sizeof(Triangle) * m_SceneGeometry.TRIANGLES_size, m_SceneGeometry.TRIANGLES.data()))
	}
	GLCall(glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0)); // unbind
}

void Renderer::configure_BVH_SSBO_block()
{
	// Binding point 4: flat list of BVH boxes used by BVH_traverse().
	// Each ModelInfo stores nodeOffset, which points to the root box for that
	// model inside this global list.
	GLCall(glGenBuffers(1, &BVH_SSBO_ID));
	GLCall(glBindBuffer(GL_SHADER_STORAGE_BUFFER, BVH_SSBO_ID));
	GLCall(glBufferData(GL_SHADER_STORAGE_BUFFER, std::max<size_t>(sizeof(BVH::Node), sizeof(BVH::Node) * m_SceneGeometry.BVH_size), nullptr, GL_STATIC_DRAW));
	GLCall(glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 4, BVH_SSBO_ID));
}

void Renderer::update_BVH_SSBO_block()
{
	GLCall(glBindBuffer(GL_SHADER_STORAGE_BUFFER, BVH_SSBO_ID));
	if (!m_SceneGeometry.BVH.empty()) {
		GLCall(glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, sizeof(BVH::Node) * m_SceneGeometry.BVH_size, m_SceneGeometry.BVH.data()));
	}
	GLCall(glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0)); // unbind
}

void Renderer::configure_PixelData_SSBO_block()
{
	// Binding point 5: tiny debug buffer. The shader writes information for the
	// pixel under the mouse, and application.cpp shows it in the viewport tooltip.
	GLCall(glGenBuffers(1, &pixelData_SSBO_ID));
	GLCall(glBindBuffer(GL_SHADER_STORAGE_BUFFER, pixelData_SSBO_ID));
	GLCall(glBufferData(GL_SHADER_STORAGE_BUFFER, 20, nullptr, GL_DYNAMIC_READ));
	GLCall(glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 5, pixelData_SSBO_ID));
}

void Renderer::read_PixelData_SSBO_block() {
    GLCall(glBindBuffer(GL_SHADER_STORAGE_BUFFER, pixelData_SSBO_ID));

	glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

	// Copy the small debug buffer back to CPU memory. GPU readback can pause the
	// pipeline, so this only happens when the GUI asks for pixel data.
    PixelData* SSBO_pixelData_ptr = (PixelData*)glMapBufferRange(
										GL_SHADER_STORAGE_BUFFER, 0,  20,
										GL_MAP_READ_BIT);

    if (SSBO_pixelData_ptr != nullptr) {
		pixelData.pixelColor = SSBO_pixelData_ptr->pixelColor;
		pixelData.AABB_intersect_count = SSBO_pixelData_ptr->AABB_intersect_count;
		

		GLCall(glUnmapBuffer(GL_SHADER_STORAGE_BUFFER));
    } 
	else { std::cout << "Error mapping buffer" << std::endl; }

    GLCall(glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0));
}

void Renderer::configure_ModelInfo_SSBO_block()
{
	// Binding point 6: one record per model instance.
	// The shader uses this to find a model's BVH root, triangle offset, transform,
	// material and PBR texture layer.
	GLCall(glGenBuffers(1, &modelInfo_SSBO_ID));
	GLCall(glBindBuffer(GL_SHADER_STORAGE_BUFFER, modelInfo_SSBO_ID));
	GLCall(glBufferData(GL_SHADER_STORAGE_BUFFER, std::max<size_t>(sizeof(ModelInfo), sizeof(ModelInfo) * m_SceneGeometry.MODEL_count), nullptr, GL_STATIC_DRAW));
	GLCall(glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 6, modelInfo_SSBO_ID));
}

void Renderer::update_ModelInfo_SSBO_block()
{
	GLCall(glBindBuffer(GL_SHADER_STORAGE_BUFFER, modelInfo_SSBO_ID));
	if (!m_SceneGeometry.MODELS.empty()) {
		GLCall(glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, sizeof(ModelInfo) * m_SceneGeometry.MODEL_count, m_SceneGeometry.MODELS.data()));
	}
	else {
		// Some scenes contain only analytic spheres and no mesh ModelInstances.
		// The buffer still gets one dummy ModelInfo because an empty shader storage
		// buffer is not a safe/portable OpenGL path.
		//
		// Important: MODELS.length() in the shader comes from the buffer byte size.
		// If this dummy slot were uninitialized, the shader could treat random data
		// as a real model and read outside the BVH/triangle buffers. Negative offsets
		// clearly mark the slot as invalid.
		ModelInfo dummyModel;
		dummyModel.nodeOffset = -1;
		dummyModel.triangleOffset = -1;
		dummyModel.textureLayer = -1;
		dummyModel.usePBRTextures = 0;
		GLCall(glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, sizeof(ModelInfo), &dummyModel));
	}
	GLCall(glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0));
}

void Renderer::configurePBRTextureArrays()
{
	// PBR maps are uploaded as texture arrays, one array per material channel:
	// - albedo/base color: surface color, stored as sRGB;
	// - normal: small surface direction changes, stored as RGB;
	// - roughness: 0 = sharp reflection, 1 = diffuse/blurry reflection;
	// - metallic: 0 = dielectric/non-metal, 1 = metal;
	// - ambient occlusion: darkens creases/cavities;
	// - height: used for parallax UV offset, not real geometry displacement.
	//
	// All arrays must keep the same layer order. If a model uses textureLayer N,
	// the shader samples layer N from every channel array.
	std::vector<std::string> albedoPaths;
	std::vector<std::string> normalPaths;
	std::vector<std::string> roughnessPaths;
	std::vector<std::string> metallicPaths;
	std::vector<std::string> aoPaths;
	std::vector<std::string> heightPaths;

	auto appendTextureSet = [&](const PBRTextureSet& textures) {
		albedoPaths.push_back(textures.albedoPath);
		normalPaths.push_back(textures.normalPath);
		roughnessPaths.push_back(textures.roughnessPath);
		metallicPaths.push_back(textures.metallicPath);
		aoPaths.push_back(textures.aoPath);
		heightPaths.push_back(textures.heightPath);
	};

	for (const PBRTextureSet& textures : m_SceneGeometry.PBR_TEXTURES) {
		appendTextureSet(textures);
	}

	if (m_Scene.pbrTextureSets) {
		for (const PBRTextureSet& textures : *m_Scene.pbrTextureSets) {
			appendTextureSet(textures);
		}
	}

	const unsigned char whiteRGB[3] = {255, 255, 255};
	const unsigned char flatNormalRGB[3] = {128, 128, 255};
	const unsigned char white = 255;
	const unsigned char black = 0;
	const unsigned char neutralHeight = 128;

	// Neutral fallbacks:
	// - white albedo keeps the material's base color unchanged;
	// - flat normal (128,128,255) means "no normal-map bump";
	// - white roughness/AO means fully rough and unoccluded;
	// - black metallic means non-metal;
	// - neutral height means no parallax offset.
	albedoTextureArray_ID = createTextureArray(albedoPaths, STBI_rgb, GL_SRGB8, GL_RGB, whiteRGB);
	normalTextureArray_ID = createTextureArray(normalPaths, STBI_rgb, GL_RGB8, GL_RGB, flatNormalRGB);
	roughnessTextureArray_ID = createTextureArray(roughnessPaths, STBI_grey, GL_R8, GL_RED, &white);
	metallicTextureArray_ID = createTextureArray(metallicPaths, STBI_grey, GL_R8, GL_RED, &black);
	aoTextureArray_ID = createTextureArray(aoPaths, STBI_grey, GL_R8, GL_RED, &white);
	heightTextureArray_ID = createTextureArray(heightPaths, STBI_grey, GL_R8, GL_RED, &neutralHeight);
	bindPBRTextureArrays();
}

void Renderer::bindPBRTextureArrays() const
{
	// Fixed texture units match the sampler2DArray bindings declared in
	// ComputeRayTracing.comp:
	// 0 = albedo, 1 = normal, 2 = roughness, 3 = metallic, 4 = AO, 5 = height.
	const unsigned int textures[] = {
		albedoTextureArray_ID,
		normalTextureArray_ID,
		roughnessTextureArray_ID,
		metallicTextureArray_ID,
		aoTextureArray_ID,
		heightTextureArray_ID
	};

	for (unsigned int unit = 0; unit < 6; ++unit) {
		GLCall(glActiveTexture(GL_TEXTURE0 + unit));
		GLCall(glBindTexture(GL_TEXTURE_2D_ARRAY, textures[unit]));
	}
}
