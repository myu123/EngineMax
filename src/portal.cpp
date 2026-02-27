#include "portal.h"

#include <glm/gtc/matrix_transform.hpp>
#include <iostream>

glm::mat4 Portal::getModelMatrix() const {
    glm::vec3 right = glm::normalize(glm::cross(up, normal));
    glm::mat4 m(1.0f);
    m[0] = glm::vec4(right  * width,  0.0f);
    m[1] = glm::vec4(up     * height, 0.0f);
    m[2] = glm::vec4(normal,          0.0f);
    m[3] = glm::vec4(position,        1.0f);
    return m;
}

glm::mat4 Portal::getTransformMatrix() const {
    glm::vec3 right = glm::normalize(glm::cross(up, normal));
    glm::mat4 m(1.0f);
    m[0] = glm::vec4(right,    0.0f);   // no width scaling
    m[1] = glm::vec4(up,       0.0f);   // no height scaling
    m[2] = glm::vec4(normal,   0.0f);
    m[3] = glm::vec4(position, 1.0f);
    return m;
}

void Portal::initFBO(int texWidth, int texHeight) {
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);

    // Color texture
    glGenTextures(1, &colorTex);
    glBindTexture(GL_TEXTURE_2D, colorTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, texWidth, texHeight, 0, GL_RGB, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, colorTex, 0);

    // Depth+stencil renderbuffer (stencil needed for inner portal rendering)
    glGenRenderbuffers(1, &depthRbo);
    glBindRenderbuffer(GL_RENDERBUFFER, depthRbo);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, texWidth, texHeight);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, depthRbo);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        std::cerr << "Portal FBO is not complete!\n";

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void Portal::destroyFBO() {
    if (colorTex) glDeleteTextures(1, &colorTex);
    if (depthRbo) glDeleteRenderbuffers(1, &depthRbo);
    if (fbo)      glDeleteFramebuffers(1, &fbo);
    colorTex = depthRbo = fbo = 0;
}

glm::mat4 getPortalView(const glm::mat4& playerView, const Portal& src, const Portal& dst) {
    // Transform chain: player view → src portal space → rotate 180° → dst portal space
    // Uses unscaled transform matrices so width/height don't corrupt the view
    glm::mat4 rot180 = glm::rotate(glm::mat4(1.0f), glm::pi<float>(), glm::vec3(0.0f, 1.0f, 0.0f));
    return playerView * src.getTransformMatrix() * rot180 * glm::inverse(dst.getTransformMatrix());
}

glm::vec4 getPortalPlane(const Portal& p) {
    // Plane equation: dot(normal, point) = d, stored as (nx, ny, nz, -d)
    float d = glm::dot(p.normal, p.position);
    return glm::vec4(p.normal, -d);
}

glm::mat4 obliqueProjection(const glm::mat4& projection, const glm::vec4& clipPlane) {
    // Eric Lengyel's oblique near-plane clipping
    glm::vec4 q;
    q.x = (glm::sign(clipPlane.x) + projection[2][0]) / projection[0][0];
    q.y = (glm::sign(clipPlane.y) + projection[2][1]) / projection[1][1];
    q.z = -1.0f;
    q.w = (1.0f + projection[2][2]) / projection[3][2];

    glm::vec4 c = clipPlane * (2.0f / glm::dot(clipPlane, q));

    glm::mat4 result = projection;
    result[0][2] = c.x;
    result[1][2] = c.y;
    result[2][2] = c.z + 1.0f;
    result[3][2] = c.w;
    return result;
}
