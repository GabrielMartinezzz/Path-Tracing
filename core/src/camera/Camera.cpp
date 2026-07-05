#include "core/camera/Camera.hpp"

/*

   | y+ (up)
   |
   |
   |cam -> __________ z+ (fwd)
   \.
    \.
     \ -x (rt)

*/

Camera::Camera( float FOV_deg, float Aspect, DeltaTime& deltaTime)
    :sWidth(-1), sHeight(-1), FOV_deg(FOV_deg), Aspect(Aspect), deltaTime(deltaTime)
{
    // Store FOV in radians because tan() expects radians. focalLength is later
    // uploaded to the ray tracing shader and controls how wide the primary ray
    // cone is: lower focal length means wider field of view.
    FOV_rad = glm::radians(FOV_deg);
    
    fwdVec = glm::vec3(0.0f, 0.0f, 1.0f);
    upVec = glm::vec3(0.0f, 1.0f, 0.0f);
    rtVec = glm::cross(fwdVec, upVec);  // (-1.0f, 0.0f, 0.0f) 

    posVec = glm::vec3(0.0f, 0.0f, 0.0f);

    rotAroundX = 0;
    rotAroundY = 0;
    rotAroundZ = 0;

    // since we transform the size of the screen in the compute shader to "normalized device coordinates" or NDC for short (-1, 1) 
    // half of the screen width is 1. Therefore (screen width / 2) / tan(FOV in radians / 2) can be simplified to 1 / tan(FOV_rad / 2)
    focalLength = 1 / tan(FOV_rad / 2);   
    u_ModelMatrix = glm::mat3(rtVec, upVec, fwdVec);
}

void Camera::updatePosition()
{
    glm::vec3 tempFwdDir;
    glm::vec3 tempUpDir;
    glm::vec3 tempRtDir;
    glm::vec3 offsetVector(0.0f);

    if (movementType == CAMERA_RELATIVE){
        // Camera-relative movement follows the current camera basis. Pressing W
        // moves toward the direction currently visible on screen.
        tempFwdDir = fwdVec;
        tempUpDir = upVec;
        tempRtDir = rtVec;
    }
    else if (movementType == WORLD_RELATIVE){
        // World-relative movement keeps vertical movement aligned with world Y.
        // This is easier for inspecting scenes because looking up/down does not
        // make W/S move diagonally into the sky/floor.
        tempRtDir = rtVec;
        tempUpDir = glm::vec3(0.0f, 1.0f, 0.0f);
        tempFwdDir = glm::cross(tempRtDir, tempUpDir);
        tempFwdDir *= -1; // invert the direction
    }

    if (FORWARD_KEY_ACTIVE)  { offsetVector += tempFwdDir; }
    if (BACKWARD_KEY_ACTIVE) { offsetVector -= tempFwdDir; }
    if (RIGHT_KEY_ACTIVE)    { offsetVector += tempRtDir;  }
    if (LEFT_KEY_ACTIVE)     { offsetVector -= tempRtDir;  }
    if (UP_KEY_ACTIVE)       { offsetVector += tempUpDir;  }
    if (DOWN_KEY_ACTIVE)     { offsetVector -= tempUpDir;  }
    
    if (offsetVector.x != 0 && offsetVector.y != 0 && offsetVector.z != 0)
    {
        // Normalization prevents diagonal movement from being faster than
        // single-axis movement. The final displacement still scales by speed and
        // delta time for frame-rate-independent controls.
        offsetVector = glm::normalize(offsetVector); 
    }
    wasCameraInput = true;
    posVec += (static_cast<float>(deltaTime.getDeltaTime()) * offsetVector * speed); // s = v*t where s = offset * speed * deltaTime
    
}

void Camera::updateRotation()
{
    // Rebuild the camera basis from default axes every time instead of applying
    // incremental rotations to the previous basis. This avoids accumulating
    // numerical drift over long sessions.
    glm::mat4 temp;
    
    glm::vec3 tempFwd = defaultFwdVec;
    glm::vec3 tempRt = defaultRtVec;
    glm::vec3 tempUp = defaultUpVec;

    if (movementType == CAMERA_RELATIVE) {
        if (ROLL_RIGHT_KEY_ACTIVE) { rotAroundZ -= (static_cast<float>(deltaTime.getDeltaTime()) * 3 * sensitivity); }
        if (ROLL_LEFT_KEY_ACTIVE) { rotAroundZ += (static_cast<float>(deltaTime.getDeltaTime()) * 3 * sensitivity); }
    }


    // yaw
    // Yaw rotates around the current up vector and changes where the camera
    // looks horizontally.
    temp = glm::rotate(glm::mat4(1.0f), glm::radians(rotAroundY), tempUp);
    glm::mat3 yRotMat = glm::mat3(temp); // Convert the 4x4 matrix to a 3x3 matrix
    tempFwd = tempFwd * yRotMat;
    tempRt = tempRt * yRotMat;
    tempUp = tempUp * yRotMat;

    // pitch
    // Pitch rotates around the right vector and changes vertical aim.
    temp = glm::rotate(glm::mat4(1.0f), glm::radians(rotAroundX), tempRt);
    glm::mat3 xRotMat = glm::mat3(temp); // Convert the 4x4 matrix to a 3x3 matrix
    tempFwd = tempFwd * xRotMat;
    tempRt = tempRt * xRotMat;
    tempUp = tempUp * xRotMat;

    // roll
    // Roll rotates around the forward vector. It is only actively controlled in
    // camera-relative mode.
    temp = glm::rotate(glm::mat4(1.0f), glm::radians(rotAroundZ), tempFwd);
    glm::mat3 zRotMat = glm::mat3(temp); // Convert the 4x4 matrix to a 3x3 matrix
    fwdVec = tempFwd * zRotMat;
    rtVec = tempRt * zRotMat;
    upVec = tempUp * zRotMat;

    fwdVec = glm::normalize(fwdVec);
    rtVec = glm::normalize(rtVec);
    upVec = glm::normalize(upVec);
    
    wasCameraInput = true;
    // u_ModelMatrix is consumed by ComputeRayTracing.comp to transform the
    // screen-space ray direction into world space for every pixel.
    u_ModelMatrix = glm::mat3(rtVec, upVec, fwdVec);
}

void Camera::setScreenDimentions(int newWidth, int newHeight)
{
    sWidth = newWidth;
    sHeight = newHeight;

    updateFOV();
}

void Camera::updateFOV()
{
    FOV_rad = glm::radians(FOV_deg);
    focalLength = 1 / tan(FOV_rad / 2);
}

void Camera::Update() 
{
    // Called once per frame by application.cpp. Input callbacks only update
    // flags/angles; this method applies those scheduled changes in one place so
    // the renderer can know if accumulation must be reset.
    if (FORWARD_KEY_ACTIVE || BACKWARD_KEY_ACTIVE || LEFT_KEY_ACTIVE || RIGHT_KEY_ACTIVE || UP_KEY_ACTIVE || DOWN_KEY_ACTIVE) // dont need the flag because imgui changes the position directly
    {
        updatePosition(); 
    }
    if (flags.scheduleRotUpdate || ROLL_LEFT_KEY_ACTIVE || ROLL_RIGHT_KEY_ACTIVE)
    {
        updateRotation();
    }
}

void Camera::ResetFlags()
{
    flags.schedulePosUpdate = false;
    flags.scheduleRotUpdate = false;
    wasCameraInput = false;
}
