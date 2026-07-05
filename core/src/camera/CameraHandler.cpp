#include "core/camera/CameraHandler.hpp"
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/string_cast.hpp>
#include <map>


CameraHandler::CameraHandler(Camera& camera)
    : camera(camera) {
}

void CameraHandler::mouseCursorPositionCallback(GLFWwindow* window, double xpos, double ypos) {
    if (CameraControllMode) // check if we can handle the camera
        {
        // Mouse input is converted to angular deltas. The actual camera basis is
        // rebuilt later in Camera::Update(), which keeps input handling separate
        // from camera math.
        static double prev_xpos = xpos;
        static double prev_ypos = xpos;

        double delta_xpos = xpos - prev_xpos;
        double delta_ypos = ypos - prev_ypos;

        prev_xpos = xpos;
        prev_ypos = ypos;

        if (!camera.freeze) // check if the camera isnt frozen for this frame we dont
        { 
            if (delta_xpos != 0 || delta_ypos != 0) {
                // Delta time makes rotation speed independent from mouse event
                // frequency. scheduleRotUpdate tells the camera to rebuild its
                // orientation matrix before rendering the next frame.
                camera.rotAroundY += delta_xpos * static_cast<float>(camera.deltaTime.getDeltaTime()) * camera.sensitivity;
                camera.rotAroundX += delta_ypos * static_cast<float>(camera.deltaTime.getDeltaTime()) * camera.sensitivity;
                camera.flags.scheduleRotUpdate = true;
            }
        } 
        else
        {
            // When toggling cursor capture, GLFW may report a large artificial
            // mouse delta. freeze discards that first delta to prevent a sudden
            // camera jump.
            camera.freeze = false;
        }
        
    }
}

void CameraHandler::keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods)
{
    int keyCode = key;
    if (!camera.cameraKeybinds.camera_keybind_window_active) 
    {
        if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS) 
        {
            // Escape switches between UI mode and camera-control mode. In
            // camera mode, the cursor is hidden/locked so mouse deltas rotate
            // the camera instead of interacting with ImGui widgets.
            CameraControllMode = !CameraControllMode;

            if (CameraControllMode) 
            {
                camera.freeze = true;
                glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
            }
            else 
            {
                camera.freeze = true;
                glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
            }
        }
        if (CameraControllMode) 
        {
            // Key state is stored as booleans rather than moving immediately in
            // the callback. Camera::Update() reads these booleans every frame,
            // which produces smooth continuous movement while a key is held.
            if (action == GLFW_PRESS || action == GLFW_REPEAT) 
            {
                if (keyCode == camera.cameraKeybinds.FORWARD)    { camera.FORWARD_KEY_ACTIVE    = true; }
                if (keyCode == camera.cameraKeybinds.BACKWARD)   { camera.BACKWARD_KEY_ACTIVE   = true; }
                if (keyCode == camera.cameraKeybinds.LEFT)       { camera.LEFT_KEY_ACTIVE       = true; }
                if (keyCode == camera.cameraKeybinds.RIGHT)      { camera.RIGHT_KEY_ACTIVE      = true; }
                if (keyCode == camera.cameraKeybinds.UP)         { camera.UP_KEY_ACTIVE         = true; }
                if (keyCode == camera.cameraKeybinds.DOWN)       { camera.DOWN_KEY_ACTIVE       = true; }
                if (keyCode == camera.cameraKeybinds.ROLL_RIGHT) { camera.ROLL_RIGHT_KEY_ACTIVE = true; }
                if (keyCode == camera.cameraKeybinds.ROLL_LEFT)  { camera.ROLL_LEFT_KEY_ACTIVE  = true; }
            }
            if (action == GLFW_RELEASE) 
            {
                if (keyCode == camera.cameraKeybinds.FORWARD)    { camera.FORWARD_KEY_ACTIVE    = false; }
                if (keyCode == camera.cameraKeybinds.BACKWARD)   { camera.BACKWARD_KEY_ACTIVE   = false; }
                if (keyCode == camera.cameraKeybinds.LEFT)       { camera.LEFT_KEY_ACTIVE       = false; }
                if (keyCode == camera.cameraKeybinds.RIGHT)      { camera.RIGHT_KEY_ACTIVE      = false; }
                if (keyCode == camera.cameraKeybinds.UP)         { camera.UP_KEY_ACTIVE         = false; }
                if (keyCode == camera.cameraKeybinds.DOWN)       { camera.DOWN_KEY_ACTIVE       = false; }
                if (keyCode == camera.cameraKeybinds.ROLL_RIGHT) { camera.ROLL_RIGHT_KEY_ACTIVE = false; }
                if (keyCode == camera.cameraKeybinds.ROLL_LEFT)  { camera.ROLL_LEFT_KEY_ACTIVE  = false; }
            }
        } 
        else
        {
            // if somehow the controll mode changes while holding a button
            // to prevent unwanted active buttons we set the keybinds to false
            camera.FORWARD_KEY_ACTIVE = false;
            camera.BACKWARD_KEY_ACTIVE = false;
            camera.LEFT_KEY_ACTIVE = false;
            camera.RIGHT_KEY_ACTIVE = false;
            camera.UP_KEY_ACTIVE = false;
            camera.DOWN_KEY_ACTIVE = false;
            camera.ROLL_RIGHT_KEY_ACTIVE = false;
            camera.ROLL_LEFT_KEY_ACTIVE = false;
        }
    }
    else if (camera.cameraKeybinds.camera_keybind_window_active)
    // executed when the change keybind button has been called
    {
        // In keybind-edit mode the next valid key press is written into the
        // selected keybind pointer. Normal movement input is ignored here so the
        // user can safely reconfigure controls from the GUI.
        if (keyCode == GLFW_KEY_ESCAPE) { // close the window when pressing escape
            camera.cameraKeybinds.camera_keybind_window_active = false;
        }
        // bind the button only if the same key is not already bound however, you can rebind the same key
        else if (
            (keyCode != camera.cameraKeybinds.FORWARD    || camera.cameraKeybinds.keybind_to_be_changed == &camera.cameraKeybinds.FORWARD)   &&
            (keyCode != camera.cameraKeybinds.BACKWARD   || camera.cameraKeybinds.keybind_to_be_changed == &camera.cameraKeybinds.BACKWARD)  &&
            (keyCode != camera.cameraKeybinds.LEFT       || camera.cameraKeybinds.keybind_to_be_changed == &camera.cameraKeybinds.LEFT)      &&
            (keyCode != camera.cameraKeybinds.RIGHT      || camera.cameraKeybinds.keybind_to_be_changed == &camera.cameraKeybinds.RIGHT)     &&
            (keyCode != camera.cameraKeybinds.UP         || camera.cameraKeybinds.keybind_to_be_changed == &camera.cameraKeybinds.UP)        &&
            (keyCode != camera.cameraKeybinds.DOWN       || camera.cameraKeybinds.keybind_to_be_changed == &camera.cameraKeybinds.DOWN)      &&
            (keyCode != camera.cameraKeybinds.ROLL_LEFT  || camera.cameraKeybinds.keybind_to_be_changed == &camera.cameraKeybinds.ROLL_LEFT) &&
            (keyCode != camera.cameraKeybinds.ROLL_RIGHT || camera.cameraKeybinds.keybind_to_be_changed == &camera.cameraKeybinds.ROLL_RIGHT)
            )
        {
            *camera.cameraKeybinds.keybind_to_be_changed = keyCode;
            camera.cameraKeybinds.camera_keybind_window_active = false;
        }

    }

}