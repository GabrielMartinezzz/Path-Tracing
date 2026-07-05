#pragma once
#include <string>

/**
 * @class ComputeShader
 * @brief Loads, compiles, links, and dispatches one OpenGL compute shader.
 * @details Renderer owns one instance for ray tracing and one for
 * post-processing. DrawCall() launches work groups; the shader then uses global
 * invocation ids to map GPU threads to viewport pixels.
 */
class ComputeShader {
public:
	/**
	 * @brief Creates a compute shader program from a GLSL file path.
	 * @param filepath Path to a `.comp` shader file.
	 */
	ComputeShader(const std::string& filepath);
	~ComputeShader();

	/** @brief Makes this compute shader the active OpenGL program. */
	void Bind();
	/** @brief Unbinds the active OpenGL program. */
	void Unbind();
	/**
	 * @brief Dispatches the compute shader with the requested work group count.
	 * @details The renderer chooses group counts from viewport size and shader
	 * local size. Each invocation is responsible for one pixel.
	 */
	void DrawCall(unsigned int workGroups_x, unsigned int workGroups_y, unsigned int workGroups_z);

	unsigned int m_RendererID; ///< OpenGL shader program id.
	
	unsigned int workGroups_x; ///< Last dispatched X work group count.
	unsigned int workGroups_y; ///< Last dispatched Y work group count.
	unsigned int workGroups_z; ///< Last dispatched Z work group count.

private:
	const std::string& m_Filepath; ///< Source path kept for shader reload/debug messages.

	

	/** @brief Compiles and links the parsed compute shader source. */
	unsigned int CreateShader();
	/** @brief Reads the GLSL source file from disk. */
	std::string ParseShader(const std::string& filepath);

};
