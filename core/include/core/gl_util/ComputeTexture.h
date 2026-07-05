#pragma once
#include "core/gl_util/OpenGLdebugFuncs.h"

/**
 * @class ComputeTexture
 * @brief Owns a 2D OpenGL texture used as image storage by compute shaders.
 * @details The ray tracing pass writes HDR radiance to one ComputeTexture and
 * the post-processing pass writes the final display color to another one. The
 * texture id is also passed to ImGui so the result can be drawn in the viewport.
 */
class ComputeTexture
{
private:
	unsigned int m_RendererID; ///< OpenGL texture object handle.
	int m_Width, m_Height; ///< Texture resolution in pixels.
	unsigned int m_binding_point; ///< image2D binding index used in GLSL.
public:
	/**
	 * @brief Creates an RGBA32F image texture and binds it to a compute image slot.
	 * @param width Texture width in pixels.
	 * @param height Texture height in pixels.
	 * @param binding_point GLSL image binding used by imageLoad/imageStore.
	 */
	ComputeTexture(int width, int height, unsigned int binding_point);
	~ComputeTexture();

	/** @brief Binds the texture as GL_READ_WRITE image storage. */
	void Bind() const;
	/** @brief Clears the image binding used by this wrapper. */
	void Unbind() const;
	/** @brief Changes the GLSL image binding where this texture is exposed. */
	void changeBindingPoint(unsigned int binding_point);

	/** @brief Returns the current texture width. */
	inline int GetWidth() const { return m_Width; }
	/** @brief Returns the current texture height. */
	inline int GetHeight() const { return m_Height; }
	/** @brief Returns the OpenGL texture id used by renderer and ImGui. */
	inline int ID() { return m_RendererID; }
};