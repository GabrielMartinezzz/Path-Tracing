#include "core/gl_util/OpenGLdebugFuncs.h"
#include <iostream>

/**
 * @file OpenGLdebugFuncs.cpp
 * @brief Small helpers used by GLCall to report OpenGL errors with context.
 */

void GLClearError() {
	// OpenGL stores errors in a queue. Clear all previous errors before a call so
	// GLLogCall can report whether the call just executed caused a new error.
	while (glGetError() != GL_NO_ERROR) {}
}

bool GLLogCall(const char* function, const char* file, int line) {
	if (GLenum error = glGetError())
	{
		// Printed information points back to the wrapped GLCall location, which
		// is essential when debugging buffer layouts or texture bindings.
		std::cout << "[OpenGL error] " << error << " on line " << line << " in " << function << " " << file << std::endl;
		return false;
	}
	return true;
}