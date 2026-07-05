#pragma once
#include <GL/glew.h>

/**
 * @file OpenGLdebugFuncs.h
 * @brief Error-checking utilities wrapped around OpenGL calls.
 * @details OpenGL failures are otherwise easy to miss because most functions do
 * not throw exceptions or return errors directly. GLCall clears previous errors,
 * executes one GL statement, then checks whether that statement produced an
 * error and breaks in the debugger if it did.
 */

#if defined(_MSC_VER)
#define DEBUG_BREAK() __debugbreak()
#else
#include <csignal>
#define DEBUG_BREAK() raise(SIGTRAP)
#endif

/** @brief Debug assertion used by GLCall. */
#define ASSERT(x) if (!(x)) DEBUG_BREAK();
/**
 * @brief Executes an OpenGL command and validates it immediately.
 * @details Used throughout Renderer and GL utility classes to catch wrong
 * buffer bindings, texture formats, shader dispatch errors, and invalid states
 * near the source line that caused them.
 */
#define GLCall(x) GLClearError();\
	x;\
	ASSERT(GLLogCall(#x, __FILE__, __LINE__))

/** @brief Removes all pending OpenGL errors before a checked call is executed. */
void GLClearError();
/**
 * @brief Logs the first OpenGL error found after a checked call.
 * @return true when no error was detected; false when GLCall should assert.
 */
bool GLLogCall(const char* function, const char* file, int line);
