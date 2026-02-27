#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

enum class CameraMovement {
    Forward,
    Backward,
    Left,
    Right
};

class Camera {
public:
    glm::vec3 position;
    glm::vec3 front;
    glm::vec3 up;
    glm::vec3 right;
    glm::vec3 worldUp;

    float yaw;
    float pitch;
    float speed;
    float sensitivity;
    float fov;

    Camera(glm::vec3 pos   = glm::vec3(0.0f, 1.7f, 4.0f),
           glm::vec3 up    = glm::vec3(0.0f, 1.0f, 0.0f),
           float     yaw   = -90.0f,
           float     pitch = 0.0f);

    glm::mat4 getViewMatrix() const;
    void processKeyboard(CameraMovement direction, float deltaTime);
    void processMouseMovement(float xoffset, float yoffset);

    // Recompute front/right/up from current yaw and pitch.
    // Public so portal teleportation can set yaw/pitch then refresh.
    void updateVectors();
};
