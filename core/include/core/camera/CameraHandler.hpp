#pragma once
#include "core/camera/Camera.hpp"
#include <string>
#include <GLFW/glfw3.h>

/**
 * @class CameraHandler
 * @brief Translates GLFW keyboard/mouse callbacks into Camera state.
 * @details GLFW requires static callback functions, but the camera data lives in
 * a C++ object. This class retrieves the CameraHandler stored in the window user
 * pointer and forwards events to instance methods that update movement flags,
 * keybinds, cursor capture mode, and scheduled rotation updates.
 */
class CameraHandler 
{
public:
    /** @brief Stores the camera reference that will be modified by input events. */
    CameraHandler(Camera& camera);
    
    /**
     * @brief Static GLFW bridge for keyboard events.
     * @details Registered with glfwSetKeyCallback. It forwards the event to the
     * CameraHandler associated with the window.
     */
    static void GLFWKeyCallback(GLFWwindow* window, int key, int scancode, int action, int mods) {
        CameraHandler* cameraHandler = reinterpret_cast<CameraHandler*>(glfwGetWindowUserPointer(window));
        if (cameraHandler) {
            cameraHandler->keyCallback(window, key, scancode, action, mods);
        }
    }

    /**
     * @brief Static GLFW bridge for mouse-position events.
     * @details Converts GLFW's C-style callback into an instance call so the
     * handler can modify its referenced Camera object.
     */
    static void GLFWMousePositionCallback(GLFWwindow* window, double xpos, double ypos) {
        CameraHandler* cameraHandler = reinterpret_cast<CameraHandler*>(glfwGetWindowUserPointer(window));
        if (cameraHandler) {
            cameraHandler->mouseCursorPositionCallback(window, xpos, ypos);
        }
    }

    /** @brief Processes mouse movement and schedules camera rotation updates. */
    void mouseCursorPositionCallback(GLFWwindow* window, double xpos, double ypos);
	/** @brief Processes movement keys, cursor capture toggle, and key rebinding. */
	void keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods);

private:
    Camera& camera; ///< Camera modified by this input handler.

public:
    /**
     * @brief True when mouse/keyboard input controls the camera instead of ImGui.
     * @details Toggled with Escape. application.cpp uses this to avoid showing
     * pixel debug tooltips while the user is navigating the scene.
     */
    bool CameraControllMode = false;
};
