#include "core/gl_util/ComputeTexture.h"
#include <iostream>

/**
 * @file ComputeTexture.cpp
 * @brief Texture wrapper used as read/write image storage by compute shaders.
 */

ComputeTexture::ComputeTexture(int width, int height, unsigned int binding_point)
: m_Width(width), m_Height(height), m_binding_point(binding_point), m_RendererID(0) {

	// RGBA32F is used because the ray tracer accumulates HDR radiance values.
	// A regular 8-bit color texture would clamp bright light and destroy
	// progressive accumulation before tone mapping.
	GLCall(glCreateTextures(GL_TEXTURE_2D, 1, &m_RendererID));
	GLCall(glTextureParameteri(m_RendererID, GL_TEXTURE_MIN_FILTER, GL_NEAREST));
	GLCall(glTextureParameteri(m_RendererID, GL_TEXTURE_MAG_FILTER, GL_NEAREST));
	GLCall(glTextureParameteri(m_RendererID, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE));
	GLCall(glTextureParameteri(m_RendererID, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE));
	GLCall(glTextureStorage2D(m_RendererID, 1, GL_RGBA32F, m_Width, m_Height));
	// Bind as an image, not just as a sampled texture. imageLoad/imageStore in
	// ComputeRayTracing.comp and ComputePostProcessing.comp use this binding.
	GLCall(glBindImageTexture(m_binding_point, m_RendererID, 0, GL_FALSE, 0, GL_READ_WRITE, GL_RGBA32F));
}

ComputeTexture::~ComputeTexture()
{
	GLCall(glDeleteTextures(1, &m_RendererID));
}

void ComputeTexture::changeBindingPoint(unsigned int binding_point)
{
	// Used when the same texture object needs to be exposed to a different image
	// binding. Current renderer usually creates separate textures for bindings 0
	// and 1, but this keeps the wrapper reusable.
	m_binding_point = binding_point;
	Bind();
}

void ComputeTexture::Bind() const
{
	GLCall(glBindImageTexture(m_binding_point, m_RendererID, 0, GL_FALSE, 0, GL_READ_WRITE, GL_RGBA32F));
}

void ComputeTexture::Unbind() const
{
	// Clear image binding 0. This is mostly a defensive cleanup helper; the
	// renderer normally binds the next texture explicitly before dispatch.
	GLCall(glBindImageTexture(0, 0, 0, GL_FALSE, 0, GL_READ_WRITE, GL_RGBA32F));
}