#pragma once

#include <glad/gl.h>
#include <glm/glm.hpp>

struct Portal {
    glm::vec3 position;     // center of portal surface
    glm::vec3 normal;       // outward-facing normal (into the room)
    glm::vec3 up;           // up direction
    float     width  = 1.8f;
    float     height = 2.8f;
    glm::vec3 color;        // border color (orange / blue)

    GLuint fbo      = 0;
    GLuint colorTex = 0;
    GLuint depthRbo = 0;

    // Model matrix: transforms a unit quad to the portal's world position (includes width/height scale)
    glm::mat4 getModelMatrix() const;

    // Transform matrix: position + orientation only (NO width/height scale).
    // Used for virtual camera calculations.
    glm::mat4 getTransformMatrix() const;

    // Initialize the FBO + texture + depth renderbuffer
    void initFBO(int texWidth, int texHeight);

    // Cleanup
    void destroyFBO();
};

// Compute the virtual camera's view matrix for rendering through a portal.
// src = the portal the player is looking at
// dst = the portal the view comes out of
glm::mat4 getPortalView(const glm::mat4& playerView, const Portal& src, const Portal& dst);

// Modify a projection matrix so the near plane aligns with the portal surface.
// clipPlane is in VIEW space (of the virtual camera).
glm::mat4 obliqueProjection(const glm::mat4& projection, const glm::vec4& clipPlane);

// Get the portal's surface plane in world space (as vec4: normal.xyz, d)
glm::vec4 getPortalPlane(const Portal& p);
