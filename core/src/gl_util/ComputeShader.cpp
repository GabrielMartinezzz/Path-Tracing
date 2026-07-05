/**
 * @file ComputeShader.cpp
 * @brief Utility wrapper around OpenGL compute shader compilation and dispatch.
 */

#include "core/gl_util/ComputeShader.h"

#include <iostream>
#include <fstream>
#include <string>
#include <sstream>

#include "core/gl_util/OpenGLdebugFuncs.h"

// Minimal OpenGL compute shader wrapper.
//
// Reference:
// - OpenGL compute shader API:
//   https://registry.khronos.org/OpenGL-Refpages/gl4/html/glDispatchCompute.xhtml
//
// The wrapper reads a .comp file, compiles it as GL_COMPUTE_SHADER, links a
// program and dispatches workgroups. It is intentionally small so shader errors
// are easy to trace back to the generated compiler log.
ComputeShader::ComputeShader(const std::string& filepath)
 : m_Filepath(filepath), m_RendererID(0)
{
	m_RendererID = CreateShader();
}

ComputeShader::~ComputeShader()
{
	GLCall(glDeleteProgram(m_RendererID));
}


void ComputeShader::Bind()
{
	GLCall(glUseProgram(m_RendererID));
}

void ComputeShader::Unbind()
{
	GLCall(glUseProgram(0));
}

void ComputeShader::DrawCall(unsigned int workGroups_x, unsigned int workGroups_y, unsigned int workGroups_z)
{
	// Dispatch starts GPU execution. The memory barrier makes shader writes
	// visible to later image/SSBO reads in the post-processing pass or GUI.
	this->workGroups_x = workGroups_x;
	this->workGroups_y = workGroups_y;
	this->workGroups_z = workGroups_z;
	glDispatchCompute(workGroups_x, workGroups_y, workGroups_z);
	GLCall(glMemoryBarrier(GL_ALL_BARRIER_BITS))
}

std::string ComputeShader::ParseShader(const std::string& filepath)
{
	std::ifstream stream(filepath);

	std::stringstream ss;
	std::string line;
	// The project keeps GLSL compute shaders as external resources so they can be
	// edited without recompiling C++. At startup, the source is read into one
	// string and passed directly to glShaderSource().
	while (getline(stream, line)) // loop through every line and write it to a string stream
	{
		ss << line << '\n';
	}

	std::string source = ss.str();
	return source; // converting to c style string
}

unsigned int ComputeShader::CreateShader()
{
	// Compile first, then attach to a program. Compute shaders do not use vertex
	// or fragment stages, but still need a linked program object.
	std::string computeShaderSource = ParseShader(m_Filepath);
	const char* src = &computeShaderSource[0];
	GLuint computeShaderID = glCreateShader(GL_COMPUTE_SHADER);
	
	GLCall(glShaderSource(computeShaderID, 1, &src, NULL));
	GLCall(glCompileShader(computeShaderID));

	// checking for errors for debugging
	// GLSL compilation errors are printed here. This is usually where mistakes in
	// ComputeRayTracing.comp or ComputePostProcessing.comp are reported.
	int result;
	GLCall(glGetShaderiv(computeShaderID, GL_COMPILE_STATUS, &result));
	if (result == GL_FALSE)
	{
		int length;
		GLCall(glGetShaderiv(computeShaderID, GL_INFO_LOG_LENGTH, &length));
		char* message = (char*)alloca(length * sizeof(char));
		GLCall(glGetShaderInfoLog(computeShaderID, length, &length, message));
		std::cout << "Failed to compile the compute shader" << std::endl;
		std::cout << message << std::endl;
		GLCall(glDeleteShader(computeShaderID));
		return 0;
	}

	GLuint computeProgram = glCreateProgram(); // is m_RendererID
	GLCall(glAttachShader(computeProgram, computeShaderID));
	// Linking creates the program object that Renderer::BeginCompute*Stage()
	// binds before uploading uniforms and dispatching workgroups.
	GLCall(glLinkProgram(computeProgram));
	// cleanup
	GLCall(glValidateProgram(computeProgram));
	GLCall(glDeleteShader(computeShaderID));

	return computeProgram;
}
