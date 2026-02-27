#include "camera.h"

#include <cmath>

Camera::Camera(glm::vec3 pos, glm::vec3 up, float yaw, float pitch)
    : position(pos), worldUp(up), yaw(yaw), pitch(pitch),
      speed(3.5f), sensitivity(0.1f), fov(70.0f)
{
    updateVectors();
}

glm::mat4 Camera::getViewMatrix() const {
    return glm::lookAt(position, position + front, up);
}

void Camera::processKeyboard(CameraMovement direction, float deltaTime) {
    float velocity = speed * deltaTime;
    // Move on XZ plane only (grounded, no flying)
    glm::vec3 flatFront = glm::normalize(glm::vec3(front.x, 0.0f, front.z));

    switch (direction) {
        case CameraMovement::Forward:  position += flatFront * velocity; break;
        case CameraMovement::Backward: position -= flatFront * velocity; break;
        case CameraMovement::Left:     position -= right * velocity;     break;
        case CameraMovement::Right:    position += right * velocity;     break;
    }
}

void Camera::processMouseMovement(float xoffset, float yoffset) {
    xoffset *= sensitivity;
    yoffset *= sensitivity;

    yaw   += xoffset;
    pitch += yoffset;

    // Clamp pitch so you can't flip upside down
    if (pitch >  89.0f) pitch =  89.0f;
    if (pitch < -89.0f) pitch = -89.0f;

    updateVectors();
}

void Camera::updateVectors() {
    glm::vec3 f;
    f.x = cosf(glm::radians(yaw)) * cosf(glm::radians(pitch));
    f.y = sinf(glm::radians(pitch));
    f.z = sinf(glm::radians(yaw)) * cosf(glm::radians(pitch));
    front = glm::normalize(f);
    right = glm::normalize(glm::cross(front, worldUp));
    up    = glm::normalize(glm::cross(right, front));
}
