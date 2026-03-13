#include <glad/gl.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "shader.h"
#include "camera.h"
#include "physics.h"
#include "portal.h"
#include "text_renderer.h"
#include "console.h"

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <vector>
#include <string>

// ---------------------------------------------------------------
// Shader sources (3D scene)
// ---------------------------------------------------------------

static const char* vertSrc = R"glsl(
#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec2 aTexCoord;

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;

out vec2 TexCoord;

void main() {
    gl_Position = projection * view * model * vec4(aPos, 1.0);
    TexCoord = aTexCoord;
}
)glsl";

static const char* fragSrc = R"glsl(
#version 330 core
in vec2 TexCoord;
out vec4 FragColor;

uniform sampler2D tex0;
uniform vec3 tint;

void main() {
    FragColor = texture(tex0, TexCoord) * vec4(tint, 1.0);
}
)glsl";

// Portal surface shader: samples FBO using screen-space coordinates.
// This is the key trick -- the portal acts like a window, not a painting.
// Each pixel on the portal quad samples the FBO at its own screen position.
static const char* portalFragSrc = R"glsl(
#version 330 core
out vec4 FragColor;

uniform sampler2D portalTex;
uniform vec2 screenSize;

void main() {
    vec2 uv = gl_FragCoord.xy / screenSize;
    FragColor = texture(portalTex, uv);
}
)glsl";


// ---------------------------------------------------------------
// Globals
// ---------------------------------------------------------------

static Camera camera(glm::vec3(0.0f, 1.7f, 4.0f));
static float  lastX       = 960.0f;
static float  lastY       = 540.0f;
static bool   firstMouse  = true;
static float  deltaTime   = 0.0f;
static float  lastFrame   = 0.0f;

// Player physics
static float playerYVelocity = 0.0f;
static bool  playerGrounded  = true;

static const float GRAVITY        = 14.0f;
static const float JUMP_SPEED     = 5.5f;
static const float EYE_HEIGHT     = 1.7f;
static const float PLAYER_HEIGHT  = 1.8f;
static const float PLAYER_RADIUS  = 0.3f;

// Room bounds
static const float ROOM_HALF      = 6.0f;
static const float ROOM_CEILING   = 4.0f;

// Portal teleportation
static float prevDistA      = 1.0f;  // signed distance to portal A last frame
static float prevDistB      = 1.0f;  // signed distance to portal B last frame
static float teleportCooldown = 0.0f; // seconds remaining before next teleport allowed

// Console system (initialized in main, referenced in callbacks)
static Console* gConsole = nullptr;

// Forward-declared portal pointers (set in main, used by helpers)
static Portal* gPortalA = nullptr;
static Portal* gPortalB = nullptr;

// FPS counter
static float fpsTimer     = 0.0f;
static int   fpsFrameCount = 0;
static int   fpsDisplay    = 0;

// ---------------------------------------------------------------
// Portal helpers
// ---------------------------------------------------------------

// Check if position is within a portal's wall-plane footprint (for wall exemption).
// Uses expanded bounds (player radius) so the player body can pass through.
static bool isPlayerInPortalZone(const glm::vec3& pos, const Portal& portal) {
    glm::vec3 right = glm::normalize(glm::cross(portal.up, portal.normal));
    glm::vec3 diff  = pos - portal.position;

    float projRight = glm::dot(diff, right);
    float halfW     = portal.width * 0.5f + PLAYER_RADIUS;

    float feetY        = pos.y - EYE_HEIGHT;
    float headY        = feetY + PLAYER_HEIGHT;
    float portalBottom = portal.position.y - portal.height * 0.5f;
    float portalTop    = portal.position.y + portal.height * 0.5f;

    return fabsf(projRight) < halfW &&
           feetY < portalTop && headY > portalBottom;
}

// Check if position is within portal bounds (for teleport trigger).
static bool isInPortalBounds(const glm::vec3& pos, const Portal& portal) {
    glm::vec3 right = glm::normalize(glm::cross(portal.up, portal.normal));
    glm::vec3 diff  = pos - portal.position;

    float projRight = glm::dot(diff, right);
    float projUp    = glm::dot(diff, portal.up);

    return fabsf(projRight) < (portal.width  * 0.5f + PLAYER_RADIUS) &&
           fabsf(projUp)    < (portal.height * 0.5f);
}

// Signed distance from a point to a portal's surface plane.
// Positive = room side (in front of portal), negative = behind the wall.
static float portalSignedDist(const glm::vec3& pos, const Portal& portal) {
    return glm::dot(portal.normal, pos - portal.position);
}

// Teleport the player from src portal to dst portal.
// Transforms position, view direction, and preserves vertical velocity.
static void teleportPlayer(const Portal& src, const Portal& dst) {
    glm::mat4 rot180 = glm::rotate(glm::mat4(1.0f), glm::pi<float>(), glm::vec3(0.0f, 1.0f, 0.0f));
    glm::mat4 M = dst.getTransformMatrix() * rot180 * glm::inverse(src.getTransformMatrix());

    // Transform position
    glm::vec4 newPos = M * glm::vec4(camera.position, 1.0f);
    camera.position = glm::vec3(newPos);

    // Transform view direction
    glm::mat3 M3(M);
    glm::vec3 newFront = glm::normalize(M3 * camera.front);

    // Extract yaw and pitch from transformed direction
    camera.pitch = glm::degrees(asinf(glm::clamp(newFront.y, -1.0f, 1.0f)));
    camera.yaw   = glm::degrees(atan2f(newFront.z, newFront.x));
    camera.updateVectors();
}

// ---------------------------------------------------------------
// Callbacks
// ---------------------------------------------------------------

static void framebufferSizeCallback(GLFWwindow*, int w, int h) {
    glViewport(0, 0, w, h);
}

static void mouseCallback(GLFWwindow*, double xposD, double yposD) {
    if (gConsole && gConsole->isOpen()) return; // no mouse look when console is open

    float xpos = static_cast<float>(xposD);
    float ypos = static_cast<float>(yposD);

    if (firstMouse) {
        lastX = xpos;
        lastY = ypos;
        firstMouse = false;
    }

    float xoff = xpos - lastX;
    float yoff = lastY - ypos;
    lastX = xpos;
    lastY = ypos;

    camera.processMouseMovement(xoff, yoff);
}

static void keyCallback(GLFWwindow* window, int key, int /*scancode*/, int action, int mods) {
    // Toggle console with grave accent / tilde
    if (key == GLFW_KEY_GRAVE_ACCENT && action == GLFW_PRESS) {
        if (gConsole) gConsole->toggle();
        return;
    }

    // Forward to console when open
    if (gConsole && gConsole->isOpen()) {
        gConsole->handleKey(key, action, mods);

        // ESC also closes console
        if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS)
            gConsole->toggle();
        return;
    }

    // Normal game keys
    if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS)
        glfwSetWindowShouldClose(window, true);
}

static void charCallback(GLFWwindow*, unsigned int codepoint) {
    if (gConsole) gConsole->handleChar(codepoint);
}

static void processInput(GLFWwindow* window) {
    if (gConsole && gConsole->isOpen()) return; // no movement when console open

    if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS)
        camera.processKeyboard(CameraMovement::Forward, deltaTime);
    if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS)
        camera.processKeyboard(CameraMovement::Backward, deltaTime);
    if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS)
        camera.processKeyboard(CameraMovement::Left, deltaTime);
    if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS)
        camera.processKeyboard(CameraMovement::Right, deltaTime);

    if (glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS && playerGrounded) {
        playerYVelocity = JUMP_SPEED;
        playerGrounded  = false;
    }
}

// ---------------------------------------------------------------
// Procedural texture generation
// ---------------------------------------------------------------

static GLuint createCheckerTexture(int size, int checks,
                                   unsigned char r1, unsigned char g1, unsigned char b1,
                                   unsigned char r2, unsigned char g2, unsigned char b2) {
    std::vector<unsigned char> pixels(size * size * 3);
    int cellSize = size / checks;
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            bool white = ((x / cellSize) + (y / cellSize)) % 2 == 0;
            int idx = (y * size + x) * 3;
            pixels[idx + 0] = white ? r1 : r2;
            pixels[idx + 1] = white ? g1 : g2;
            pixels[idx + 2] = white ? b1 : b2;
        }
    }
    GLuint tex;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, size, size, 0, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    return tex;
}

static GLuint createPanelTexture(int size, int panels,
                                 unsigned char rBg, unsigned char gBg, unsigned char bBg,
                                 unsigned char rLn, unsigned char gLn, unsigned char bLn) {
    std::vector<unsigned char> pixels(size * size * 3);
    int cellSize = size / panels;
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            bool isLine = (x % cellSize < 2) || (y % cellSize < 2);
            int idx = (y * size + x) * 3;
            pixels[idx + 0] = isLine ? rLn : rBg;
            pixels[idx + 1] = isLine ? gLn : gBg;
            pixels[idx + 2] = isLine ? bLn : bBg;
        }
    }
    GLuint tex;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, size, size, 0, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    return tex;
}

static GLuint createSolidTexture(unsigned char r, unsigned char g, unsigned char b) {
    unsigned char pixels[3] = { r, g, b };
    GLuint tex;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, 1, 1, 0, GL_RGB, GL_UNSIGNED_BYTE, pixels);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    return tex;
}

// ---------------------------------------------------------------
// Geometry
// ---------------------------------------------------------------

struct Vertex {
    float x, y, z;
    float u, v;
};

static const Vertex cubeVerts[] = {
    // Back face  (z = -0.5)
    {-0.5f, -0.5f, -0.5f,  0.0f, 0.0f},
    { 0.5f,  0.5f, -0.5f,  1.0f, 1.0f},
    { 0.5f, -0.5f, -0.5f,  1.0f, 0.0f},
    { 0.5f,  0.5f, -0.5f,  1.0f, 1.0f},
    {-0.5f, -0.5f, -0.5f,  0.0f, 0.0f},
    {-0.5f,  0.5f, -0.5f,  0.0f, 1.0f},
    // Front face (z = +0.5)
    {-0.5f, -0.5f,  0.5f,  0.0f, 0.0f},
    { 0.5f, -0.5f,  0.5f,  1.0f, 0.0f},
    { 0.5f,  0.5f,  0.5f,  1.0f, 1.0f},
    { 0.5f,  0.5f,  0.5f,  1.0f, 1.0f},
    {-0.5f,  0.5f,  0.5f,  0.0f, 1.0f},
    {-0.5f, -0.5f,  0.5f,  0.0f, 0.0f},
    // Left face  (x = -0.5)
    {-0.5f,  0.5f,  0.5f,  1.0f, 1.0f},
    {-0.5f,  0.5f, -0.5f,  0.0f, 1.0f},
    {-0.5f, -0.5f, -0.5f,  0.0f, 0.0f},
    {-0.5f, -0.5f, -0.5f,  0.0f, 0.0f},
    {-0.5f, -0.5f,  0.5f,  1.0f, 0.0f},
    {-0.5f,  0.5f,  0.5f,  1.0f, 1.0f},
    // Right face (x = +0.5)
    { 0.5f,  0.5f,  0.5f,  1.0f, 1.0f},
    { 0.5f, -0.5f, -0.5f,  0.0f, 0.0f},
    { 0.5f,  0.5f, -0.5f,  0.0f, 1.0f},
    { 0.5f, -0.5f, -0.5f,  0.0f, 0.0f},
    { 0.5f,  0.5f,  0.5f,  1.0f, 1.0f},
    { 0.5f, -0.5f,  0.5f,  1.0f, 0.0f},
    // Bottom face (y = -0.5)
    {-0.5f, -0.5f, -0.5f,  0.0f, 0.0f},
    { 0.5f, -0.5f, -0.5f,  1.0f, 0.0f},
    { 0.5f, -0.5f,  0.5f,  1.0f, 1.0f},
    { 0.5f, -0.5f,  0.5f,  1.0f, 1.0f},
    {-0.5f, -0.5f,  0.5f,  0.0f, 1.0f},
    {-0.5f, -0.5f, -0.5f,  0.0f, 0.0f},
    // Top face    (y = +0.5)
    {-0.5f,  0.5f, -0.5f,  0.0f, 0.0f},
    { 0.5f,  0.5f,  0.5f,  1.0f, 1.0f},
    { 0.5f,  0.5f, -0.5f,  1.0f, 0.0f},
    { 0.5f,  0.5f,  0.5f,  1.0f, 1.0f},
    {-0.5f,  0.5f, -0.5f,  0.0f, 0.0f},
    {-0.5f,  0.5f,  0.5f,  0.0f, 1.0f},
};
static const int cubeVertCount = sizeof(cubeVerts) / sizeof(cubeVerts[0]);

static const Vertex roomVerts[] = {
    // Floor
    {-6.0f, 0.0f, -6.0f, 0.0f, 0.0f}, { 6.0f, 0.0f, -6.0f, 6.0f, 0.0f},
    { 6.0f, 0.0f,  6.0f, 6.0f, 6.0f}, { 6.0f, 0.0f,  6.0f, 6.0f, 6.0f},
    {-6.0f, 0.0f,  6.0f, 0.0f, 6.0f}, {-6.0f, 0.0f, -6.0f, 0.0f, 0.0f},
    // Ceiling
    {-6.0f, 4.0f,  6.0f, 0.0f, 6.0f}, { 6.0f, 4.0f,  6.0f, 6.0f, 6.0f},
    { 6.0f, 4.0f, -6.0f, 6.0f, 0.0f}, { 6.0f, 4.0f, -6.0f, 6.0f, 0.0f},
    {-6.0f, 4.0f, -6.0f, 0.0f, 0.0f}, {-6.0f, 4.0f,  6.0f, 0.0f, 6.0f},
    // North wall (z = -6) — with hole for portal A
    // Portal A opening: x ∈ [-2.9, -1.1], y ∈ [0.0, 2.8]
    // UV mapping: u = (x+6)/2, v = y/2
    // Left piece: x ∈ [-6, -2.9]
    {-6.0f, 0.0f, -6.0f,  0.0f,  0.0f}, {-2.9f, 4.0f, -6.0f,  1.55f, 2.0f},
    {-2.9f, 0.0f, -6.0f,  1.55f, 0.0f}, {-2.9f, 4.0f, -6.0f,  1.55f, 2.0f},
    {-6.0f, 0.0f, -6.0f,  0.0f,  0.0f}, {-6.0f, 4.0f, -6.0f,  0.0f,  2.0f},
    // Right piece: x ∈ [-1.1, 6]
    {-1.1f, 0.0f, -6.0f,  2.45f, 0.0f}, { 6.0f, 4.0f, -6.0f,  6.0f,  2.0f},
    { 6.0f, 0.0f, -6.0f,  6.0f,  0.0f}, { 6.0f, 4.0f, -6.0f,  6.0f,  2.0f},
    {-1.1f, 0.0f, -6.0f,  2.45f, 0.0f}, {-1.1f, 4.0f, -6.0f,  2.45f, 2.0f},
    // Top piece: x ∈ [-2.9, -1.1], y ∈ [2.8, 4]
    {-2.9f, 2.8f, -6.0f,  1.55f, 1.4f}, {-1.1f, 4.0f, -6.0f,  2.45f, 2.0f},
    {-1.1f, 2.8f, -6.0f,  2.45f, 1.4f}, {-1.1f, 4.0f, -6.0f,  2.45f, 2.0f},
    {-2.9f, 2.8f, -6.0f,  1.55f, 1.4f}, {-2.9f, 4.0f, -6.0f,  1.55f, 2.0f},
    // South wall (z = +6)
    { 6.0f, 0.0f,  6.0f, 0.0f, 0.0f}, {-6.0f, 4.0f,  6.0f, 6.0f, 2.0f},
    {-6.0f, 0.0f,  6.0f, 6.0f, 0.0f}, {-6.0f, 4.0f,  6.0f, 6.0f, 2.0f},
    { 6.0f, 0.0f,  6.0f, 0.0f, 0.0f}, { 6.0f, 4.0f,  6.0f, 0.0f, 2.0f},
    // West wall (x = -6)
    {-6.0f, 0.0f,  6.0f, 0.0f, 0.0f}, {-6.0f, 4.0f, -6.0f, 6.0f, 2.0f},
    {-6.0f, 0.0f, -6.0f, 6.0f, 0.0f}, {-6.0f, 4.0f, -6.0f, 6.0f, 2.0f},
    {-6.0f, 0.0f,  6.0f, 0.0f, 0.0f}, {-6.0f, 4.0f,  6.0f, 0.0f, 2.0f},
    // East wall (x = +6) — with hole for portal B
    // Portal B opening: z ∈ [-2.9, -1.1], y ∈ [0.0, 2.8]
    // UV mapping: u = (z+6)/2, v = y/2
    // Left piece: z ∈ [-6, -2.9]
    { 6.0f, 0.0f, -6.0f,  0.0f,  0.0f}, { 6.0f, 4.0f, -2.9f,  1.55f, 2.0f},
    { 6.0f, 0.0f, -2.9f,  1.55f, 0.0f}, { 6.0f, 4.0f, -2.9f,  1.55f, 2.0f},
    { 6.0f, 0.0f, -6.0f,  0.0f,  0.0f}, { 6.0f, 4.0f, -6.0f,  0.0f,  2.0f},
    // Right piece: z ∈ [-1.1, 6]
    { 6.0f, 0.0f, -1.1f,  2.45f, 0.0f}, { 6.0f, 4.0f,  6.0f,  6.0f,  2.0f},
    { 6.0f, 0.0f,  6.0f,  6.0f,  0.0f}, { 6.0f, 4.0f,  6.0f,  6.0f,  2.0f},
    { 6.0f, 0.0f, -1.1f,  2.45f, 0.0f}, { 6.0f, 4.0f, -1.1f,  2.45f, 2.0f},
    // Top piece: z ∈ [-2.9, -1.1], y ∈ [2.8, 4]
    { 6.0f, 2.8f, -2.9f,  1.55f, 1.4f}, { 6.0f, 4.0f, -1.1f,  2.45f, 2.0f},
    { 6.0f, 2.8f, -1.1f,  2.45f, 1.4f}, { 6.0f, 4.0f, -1.1f,  2.45f, 2.0f},
    { 6.0f, 2.8f, -2.9f,  1.55f, 1.4f}, { 6.0f, 4.0f, -2.9f,  1.55f, 2.0f},
};
static const int floorVertOffset   = 0;
static const int ceilingVertOffset = 6;
static const int wallVertOffset    = 12;
static const int roomVertCount     = sizeof(roomVerts) / sizeof(roomVerts[0]);

// Portal quad (unit quad on XY plane)
static const Vertex portalQuadVerts[] = {
    {-0.5f, -0.5f, 0.0f,  0.0f, 0.0f},
    { 0.5f, -0.5f, 0.0f,  1.0f, 0.0f},
    { 0.5f,  0.5f, 0.0f,  1.0f, 1.0f},
    { 0.5f,  0.5f, 0.0f,  1.0f, 1.0f},
    {-0.5f,  0.5f, 0.0f,  0.0f, 1.0f},
    {-0.5f, -0.5f, 0.0f,  0.0f, 0.0f},
};
static const int portalQuadVertCount = 6;

// Fullscreen quad in NDC (for stencil-based portal fill)
static const float fsQuadVerts[] = {
    // x      y     z     u     v
    -1.0f, -1.0f, 0.0f, 0.0f, 0.0f,
     1.0f, -1.0f, 0.0f, 1.0f, 0.0f,
     1.0f,  1.0f, 0.0f, 1.0f, 1.0f,
     1.0f,  1.0f, 0.0f, 1.0f, 1.0f,
    -1.0f,  1.0f, 0.0f, 0.0f, 1.0f,
    -1.0f, -1.0f, 0.0f, 0.0f, 0.0f,
};

// ---------------------------------------------------------------
// Helper: create VAO
// ---------------------------------------------------------------

static GLuint createVAO(const Vertex* verts, int count) {
    GLuint vao, vbo;
    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &vbo);
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, count * sizeof(Vertex), verts, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glBindVertexArray(0);
    return vao;
}

// ---------------------------------------------------------------
// Scene objects
// ---------------------------------------------------------------

struct SceneObject {
    glm::vec3 position;
    glm::vec3 scale;
    glm::vec3 tint;
};

static SceneObject sceneCubes[] = {
    { glm::vec3(-2.0f, 0.5f, -2.0f), glm::vec3(1.0f),  glm::vec3(0.3f, 0.6f, 1.0f)  },
    { glm::vec3( 3.0f, 0.75f, 1.0f), glm::vec3(1.5f),  glm::vec3(1.0f, 0.5f, 0.2f)  },
    { glm::vec3( 0.0f, 0.35f,-4.0f), glm::vec3(0.7f),  glm::vec3(0.4f, 1.0f, 0.5f)  },
};
static const int sceneCubeCount = sizeof(sceneCubes) / sizeof(sceneCubes[0]);

// ---------------------------------------------------------------
// Draw the scene (room + cubes) -- called for main pass and portal passes
// ---------------------------------------------------------------

static void renderScene(Shader& shader, const glm::mat4& view, const glm::mat4& projection,
                         GLuint roomVAO, GLuint cubeVAO,
                         GLuint floorTex, GLuint wallTex, GLuint ceilTex, GLuint cubeTex) {
    shader.use();
    shader.setMat4("view", view);
    shader.setMat4("projection", projection);

    glm::mat4 identity(1.0f);

    // Room
    glBindVertexArray(roomVAO);
    shader.setMat4("model", identity);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, floorTex);
    shader.setVec3("tint", glm::vec3(1.0f));
    glDrawArrays(GL_TRIANGLES, floorVertOffset, 6);

    glBindTexture(GL_TEXTURE_2D, ceilTex);
    glDrawArrays(GL_TRIANGLES, ceilingVertOffset, 6);

    glBindTexture(GL_TEXTURE_2D, wallTex);
    glDrawArrays(GL_TRIANGLES, wallVertOffset, 48);

    // Cubes
    glBindVertexArray(cubeVAO);
    glBindTexture(GL_TEXTURE_2D, cubeTex);
    for (int i = 0; i < sceneCubeCount; ++i) {
        const auto& obj = sceneCubes[i];
        glm::mat4 model = glm::translate(identity, obj.position);
        model = glm::scale(model, obj.scale);
        shader.setMat4("model", model);
        shader.setVec3("tint", obj.tint);
        glDrawArrays(GL_TRIANGLES, 0, cubeVertCount);
    }
}

// ---------------------------------------------------------------
// Cube-man player model (placeholder until a real model is loaded)
// ---------------------------------------------------------------

struct BodyPart {
    glm::vec3 offset;  // relative to feet position
    glm::vec3 scale;   // cube scale
    glm::vec3 color;
};

static const BodyPart playerBodyParts[] = {
    // Head
    { {0.0f, 1.575f, 0.0f}, {0.30f, 0.30f, 0.30f}, {0.87f, 0.72f, 0.58f} },
    // Torso
    { {0.0f, 1.10f, 0.0f},  {0.40f, 0.60f, 0.22f}, {0.20f, 0.38f, 0.65f} },
    // Left arm
    { {-0.31f, 1.10f, 0.0f},{0.14f, 0.58f, 0.14f}, {0.20f, 0.38f, 0.65f} },
    // Right arm
    { {0.31f, 1.10f, 0.0f}, {0.14f, 0.58f, 0.14f}, {0.20f, 0.38f, 0.65f} },
    // Left leg
    { {-0.10f, 0.40f, 0.0f},{0.16f, 0.78f, 0.16f}, {0.28f, 0.28f, 0.32f} },
    // Right leg
    { {0.10f, 0.40f, 0.0f}, {0.16f, 0.78f, 0.16f}, {0.28f, 0.28f, 0.32f} },
};
static const int playerPartCount = sizeof(playerBodyParts) / sizeof(playerBodyParts[0]);

// Draw the cube-man at the player's position, rotated to face camera yaw.
// Only called during portal FBO passes (never in main pass -- first-person).
static void drawPlayerModel(Shader& shader, const glm::mat4& view,
                            const glm::mat4& projection,
                            GLuint cubeVAO, int cubeVertCount, GLuint whiteTex,
                            const glm::vec3& playerPos, float yawDegrees)
{
    // Skip rendering if the player is too close to the virtual camera
    // (prevents seeing your own model from your own viewpoint).
    glm::mat4 invView = glm::inverse(view);
    glm::vec3 camPos  = glm::vec3(invView[3]);
    if (glm::length(playerPos - camPos) < 1.5f)
        return;

    shader.use();
    shader.setMat4("view", view);
    shader.setMat4("projection", projection);

    glBindVertexArray(cubeVAO);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, whiteTex);

    // Root transform: translate to feet, rotate by yaw
    glm::vec3 feetPos = playerPos - glm::vec3(0.0f, EYE_HEIGHT, 0.0f);
    glm::mat4 root = glm::translate(glm::mat4(1.0f), feetPos);
    root = glm::rotate(root, glm::radians(yawDegrees + 90.0f), glm::vec3(0.0f, 1.0f, 0.0f));

    for (int i = 0; i < playerPartCount; ++i) {
        const auto& part = playerBodyParts[i];
        glm::mat4 model = root;
        model = glm::translate(model, part.offset);
        model = glm::scale(model, part.scale);
        shader.setMat4("model", model);
        shader.setVec3("tint", part.color);
        glDrawArrays(GL_TRIANGLES, 0, cubeVertCount);
    }
}

// ---------------------------------------------------------------
// Main
// ---------------------------------------------------------------

int main() {
    // ----- GLFW init -----
    if (!glfwInit()) {
        std::cerr << "Failed to initialize GLFW\n";
        return EXIT_FAILURE;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif

    GLFWwindow* window = glfwCreateWindow(1920, 1080, "EngineMax", nullptr, nullptr);
    if (!window) {
        std::cerr << "Failed to create GLFW window\n";
        glfwTerminate();
        return EXIT_FAILURE;
    }
    glfwMakeContextCurrent(window);
    glfwSetFramebufferSizeCallback(window, framebufferSizeCallback);
    glfwSetCursorPosCallback(window, mouseCallback);
    glfwSetKeyCallback(window, keyCallback);
    glfwSetCharCallback(window, charCallback);
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);

    // ----- GLAD -----
    int version = gladLoadGL(glfwGetProcAddress);
    if (!version) {
        std::cerr << "Failed to initialize GLAD\n";
        return EXIT_FAILURE;
    }
    std::cout << "OpenGL " << GLAD_VERSION_MAJOR(version)
              << "." << GLAD_VERSION_MINOR(version) << " loaded\n";
    std::cout << "Controls: WASD move, Mouse look, Space jump, ~ console, ESC quit\n";

    glEnable(GL_DEPTH_TEST);
    glClearColor(0.05f, 0.05f, 0.07f, 1.0f);

    // ----- Text renderer & Console -----
    TextRenderer textRenderer;
    // Try Consolas first, then Courier New as fallback
    if (!textRenderer.init("C:/Windows/Fonts/consola.ttf", 18.0f)) {
        if (!textRenderer.init("C:/Windows/Fonts/cour.ttf", 18.0f)) {
            std::cerr << "Warning: Could not load any system font for console\n";
        }
    }

    Console console(textRenderer);
    gConsole = &console;

    // ----- 3D Shader -----
    Shader shader(vertSrc, fragSrc);
    shader.use();
    shader.setInt("tex0", 0);

    // Portal surface shader (screen-space UVs)
    Shader portalShader(vertSrc, portalFragSrc);
    portalShader.use();
    portalShader.setInt("portalTex", 0);

    // ----- Geometry -----
    GLuint roomVAO      = createVAO(roomVerts, roomVertCount);
    GLuint cubeVAO      = createVAO(cubeVerts, cubeVertCount);
    GLuint portalQuadVAO = createVAO(portalQuadVerts, portalQuadVertCount);

    // Fullscreen quad VAO (for stencil-based portal rendering)
    GLuint fsQuadVAO;
    {
        GLuint vao, vbo;
        glGenVertexArrays(1, &vao);
        glGenBuffers(1, &vbo);
        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER, sizeof(fsQuadVerts), fsQuadVerts, GL_STATIC_DRAW);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(3 * sizeof(float)));
        glEnableVertexAttribArray(1);
        glBindVertexArray(0);
        fsQuadVAO = vao;
    }

    // ----- Textures -----
    GLuint floorTex = createCheckerTexture(256, 8, 200, 200, 200, 140, 140, 140);
    GLuint wallTex  = createPanelTexture(256, 4, 220, 220, 215, 170, 170, 165);
    GLuint ceilTex  = createPanelTexture(256, 8, 235, 235, 230, 200, 200, 195);
    GLuint cubeTex  = createCheckerTexture(64, 4, 255, 255, 255, 200, 200, 200);
    GLuint whiteTex = createSolidTexture(255, 255, 255);

    // ----- Portals -----
    Portal portalA, portalB;

    // Portal A: on north wall (z = -6), facing into the room (+z)
    // height=2.8 → center y=1.4 → bottom edge at y=0 (ground)
    portalA.position = glm::vec3(-2.0f, 1.4f, -5.95f);
    portalA.normal   = glm::vec3(0.0f, 0.0f, 1.0f);
    portalA.up       = glm::vec3(0.0f, 1.0f, 0.0f);
    portalA.color    = glm::vec3(0.2f, 0.5f, 1.0f);  // blue

    // Portal B: on east wall (x = +6), facing into the room (-x)
    portalB.position = glm::vec3(5.95f, 1.4f, -2.0f);
    portalB.normal   = glm::vec3(-1.0f, 0.0f, 0.0f);
    portalB.up       = glm::vec3(0.0f, 1.0f, 0.0f);
    portalB.color    = glm::vec3(1.0f, 0.5f, 0.1f);  // orange

    gPortalA = &portalA;
    gPortalB = &portalB;

    int fbW, fbH;
    glfwGetFramebufferSize(window, &fbW, &fbH);
    portalA.initFBO(fbW, fbH);
    portalB.initFBO(fbW, fbH);

    // ----- Render loop -----
    while (!glfwWindowShouldClose(window)) {
        float currentFrame = static_cast<float>(glfwGetTime());
        deltaTime = currentFrame - lastFrame;
        lastFrame = currentFrame;

        // FPS counting
        fpsTimer += deltaTime;
        fpsFrameCount++;
        if (fpsTimer >= 1.0f) {
            fpsDisplay = fpsFrameCount;
            fpsFrameCount = 0;
            fpsTimer -= 1.0f;
        }

        processInput(window);

        // ----- Physics -----
        float dt = (deltaTime > 0.05f) ? 0.05f : deltaTime;

        if (!(gConsole && gConsole->isOpen())) {
            playerYVelocity -= GRAVITY * dt;
            camera.position.y += playerYVelocity * dt;

            float feetY = camera.position.y - EYE_HEIGHT;
            if (feetY < 0.0f) {
                camera.position.y = EYE_HEIGHT;
                playerYVelocity = 0.0f;
                playerGrounded = true;
            }
            float headY = feetY + PLAYER_HEIGHT;
            if (headY > ROOM_CEILING) {
                camera.position.y = ROOM_CEILING - PLAYER_HEIGHT + EYE_HEIGHT;
                if (playerYVelocity > 0.0f) playerYVelocity = 0.0f;
            }

            // Wall clamping -- exempt walls where portals sit
            float minX = -ROOM_HALF + PLAYER_RADIUS;
            float maxX =  ROOM_HALF - PLAYER_RADIUS;
            float minZ = -ROOM_HALF + PLAYER_RADIUS;
            float maxZ =  ROOM_HALF - PLAYER_RADIUS;

            bool nearPortalA = gPortalA && isPlayerInPortalZone(camera.position, *gPortalA);
            bool nearPortalB = gPortalB && isPlayerInPortalZone(camera.position, *gPortalB);

            // Portal A is on north wall (z = -6): skip minZ clamp
            if (camera.position.x < minX) camera.position.x = minX;
            if (camera.position.x > maxX && !nearPortalB) camera.position.x = maxX;  // east wall (portal B)
            if (camera.position.z < minZ && !nearPortalA) camera.position.z = minZ;  // north wall (portal A)
            if (camera.position.z > maxZ) camera.position.z = maxZ;

            // Even in the portal zone, don't let the player drift past the
            // actual wall surface.  Without this, rapid strafing at an angle
            // gradually pushes them behind the wall, breaking rendering.
            if (nearPortalA && camera.position.z < -ROOM_HALF)
                camera.position.z = -ROOM_HALF;
            if (nearPortalB && camera.position.x > ROOM_HALF)
                camera.position.x = ROOM_HALF;

            // Cube collisions
            for (int i = 0; i < sceneCubeCount; ++i) {
                const auto& obj = sceneCubes[i];
                glm::vec3 halfScale = obj.scale * 0.5f;
                AABB cubeBox  = { obj.position - halfScale, obj.position + halfScale };
                float pFeet = camera.position.y - EYE_HEIGHT;
                AABB playerBox = {
                    glm::vec3(camera.position.x - PLAYER_RADIUS, pFeet, camera.position.z - PLAYER_RADIUS),
                    glm::vec3(camera.position.x + PLAYER_RADIUS, pFeet + PLAYER_HEIGHT, camera.position.z + PLAYER_RADIUS)
                };
                glm::vec3 push = resolveCollision(playerBox, cubeBox);
                if (push.x != 0.0f || push.y != 0.0f || push.z != 0.0f) {
                    camera.position += push;
                    if (push.y > 0.0f) { playerYVelocity = 0.0f; playerGrounded = true; }
                    if (push.y < 0.0f && playerYVelocity > 0.0f) playerYVelocity = 0.0f;
                }
            }

            // ----- Portal teleportation -----
            teleportCooldown -= dt;
            if (gPortalA && gPortalB) {
                float distA = portalSignedDist(camera.position, *gPortalA);
                float distB = portalSignedDist(camera.position, *gPortalB);

                if (teleportCooldown <= 0.0f) {
                    bool didTeleport = false;
                    // Crossed portal A? (positive → negative = walked through from room side)
                    if (prevDistA > 0.0f && distA <= 0.0f && isInPortalBounds(camera.position, *gPortalA)) {
                        teleportPlayer(*gPortalA, *gPortalB);
                        teleportCooldown = 0.15f;
                        didTeleport = true;
                        // Re-sample after teleport so prevDist is fresh at new location
                        distA = portalSignedDist(camera.position, *gPortalA);
                        distB = portalSignedDist(camera.position, *gPortalB);
                    }
                    // Crossed portal B?
                    else if (prevDistB > 0.0f && distB <= 0.0f && isInPortalBounds(camera.position, *gPortalB)) {
                        teleportPlayer(*gPortalB, *gPortalA);
                        teleportCooldown = 0.15f;
                        didTeleport = true;
                        distA = portalSignedDist(camera.position, *gPortalA);
                        distB = portalSignedDist(camera.position, *gPortalB);
                    }

                    // After teleportation, the player is at the destination
                    // portal.  Wall clamping already ran with stale nearPortal
                    // flags (computed from the pre-teleport position), so
                    // re-run it with fresh flags to avoid a one-frame jolt.
                    if (didTeleport) {
                        bool npA = isPlayerInPortalZone(camera.position, *gPortalA);
                        bool npB = isPlayerInPortalZone(camera.position, *gPortalB);

                        if (camera.position.x < minX) camera.position.x = minX;
                        if (camera.position.x > maxX && !npB) camera.position.x = maxX;
                        if (camera.position.z < minZ && !npA) camera.position.z = minZ;
                        if (camera.position.z > maxZ) camera.position.z = maxZ;

                        if (npA && camera.position.z < -ROOM_HALF)
                            camera.position.z = -ROOM_HALF;
                        if (npB && camera.position.x > ROOM_HALF)
                            camera.position.x = ROOM_HALF;
                    }
                }

                // ALWAYS update tracking, even during cooldown, so distances
                // are never stale when cooldown expires.
                prevDistA = distA;
                prevDistB = distB;
            }
        }

        // ----- Get framebuffer size -----
        glfwGetFramebufferSize(window, &fbW, &fbH);
        float aspect = (fbH > 0) ? (float)fbW / fbH : 1.0f;
        glm::mat4 projection = glm::perspective(glm::radians(camera.fov), aspect, 0.05f, 100.0f);
        glm::mat4 view = camera.getViewMatrix();

        // Pre-compute both virtual cameras and projections (needed for inner portal UVs)
        glm::mat4 portalViewA = getPortalView(view, portalA, portalB);
        glm::mat4 portalViewB = getPortalView(view, portalB, portalA);

        auto computeObliqueProj = [&](const glm::mat4& pv, const Portal& clipPortal) {
            glm::vec4 wp = getPortalPlane(clipPortal);
            glm::vec4 vp = glm::transpose(glm::inverse(pv)) * wp;
            float nLen = glm::length(glm::vec3(vp));
            float dist = glm::abs(vp.w) / nLen;
            if (dist < 0.5f) {
                // Virtual camera is very close to the clip portal (always the
                // case in our setup).  Oblique clipping this close distorts
                // the projection and clips nearby geometry (floor, walls),
                // causing black artifacts.  Since the wall behind the portal
                // is behind the camera at this range, regular projection is
                // correct and artifact-free.
                return projection;
            }
            return obliqueProjection(projection, vp);
        };

        glm::mat4 portalProjA = computeObliqueProj(portalViewA, portalB);
        glm::mat4 portalProjB = computeObliqueProj(portalViewB, portalA);

        // Second-level virtual cameras for inner portal rendering.
        // In FBO Pass A (cam at B), we see portal A.  Looking through A
        // again means another portal transform → second-level camera.
        glm::mat4 secondViewA = getPortalView(portalViewA, portalA, portalB);
        glm::mat4 secondProjA = computeObliqueProj(secondViewA, portalB);
        glm::mat4 secondViewB = getPortalView(portalViewB, portalB, portalA);
        glm::mat4 secondProjB = computeObliqueProj(secondViewB, portalA);

        // ===== PORTAL PASS A: render what you see through portal A =====
        // Virtual camera at portal B, looking out.  Portal A is visible and
        // should show a second-level recursive view (true "window" effect).
        {
            glBindFramebuffer(GL_FRAMEBUFFER, portalA.fbo);
            glViewport(0, 0, fbW, fbH);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

            // Step 1: Stencil-mark inner portal A + write its depth
            glEnable(GL_STENCIL_TEST);
            glEnable(GL_DEPTH_CLAMP);
            glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
            glDepthMask(GL_TRUE);
            glStencilFunc(GL_ALWAYS, 1, 0xFF);
            glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);

            glBindVertexArray(portalQuadVAO);
            shader.use();
            shader.setMat4("view", portalViewA);
            shader.setMat4("projection", portalProjA);
            shader.setMat4("model", portalA.getModelMatrix());
            shader.setVec3("tint", glm::vec3(1.0f));
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, whiteTex);
            glDrawArrays(GL_TRIANGLES, 0, portalQuadVertCount);

            glDisable(GL_DEPTH_CLAMP);
            glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);

            // Step 2: Render first-level scene everywhere.
            // Objects closer than the portal clear stencil → 0.
            glStencilFunc(GL_ALWAYS, 0, 0xFF);
            glStencilOp(GL_KEEP, GL_KEEP, GL_ZERO);

            renderScene(shader, portalViewA, portalProjA, roomVAO, cubeVAO,
                        floorTex, wallTex, ceilTex, cubeTex);
            drawPlayerModel(shader, portalViewA, portalProjA,
                            cubeVAO, cubeVertCount, whiteTex,
                            camera.position, camera.yaw);

            // Step 3: Clear depth where stencil = 1 (inner portal area)
            // so the second-level scene can render there with correct depth.
            glStencilFunc(GL_EQUAL, 1, 0xFF);
            glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
            glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
            glDepthMask(GL_TRUE);
            glDepthFunc(GL_ALWAYS);
            glDepthRange(1.0, 1.0);  // force far depth

            glBindVertexArray(fsQuadVAO);
            shader.setMat4("model", glm::mat4(1.0f));
            shader.setMat4("view", glm::mat4(1.0f));
            shader.setMat4("projection", glm::mat4(1.0f));
            glDrawArrays(GL_TRIANGLES, 0, 6);

            glDepthRange(0.0, 1.0);
            glDepthFunc(GL_LESS);
            glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);

            // Step 4: Render second-level scene into inner portal area
            renderScene(shader, secondViewA, secondProjA, roomVAO, cubeVAO,
                        floorTex, wallTex, ceilTex, cubeTex);
            drawPlayerModel(shader, secondViewA, secondProjA,
                            cubeVAO, cubeVertCount, whiteTex,
                            camera.position, camera.yaw);

            // Step 5: Draw source portal's border.
            // Use stencil to prevent border from drawing INSIDE the inner
            // portal area (stencil=1 there), only the ring around it.
            glStencilFunc(GL_NOTEQUAL, 1, 0xFF);
            glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
            glEnable(GL_DEPTH_CLAMP);
            glBindVertexArray(portalQuadVAO);
            shader.use();
            shader.setMat4("view", portalViewA);
            shader.setMat4("projection", portalProjA);
            glBindTexture(GL_TEXTURE_2D, whiteTex);
            {
                glm::mat4 bm = portalA.getModelMatrix();
                bm = bm * glm::scale(glm::mat4(1.0f), glm::vec3(1.12f, 1.06f, 1.0f));
                bm[3] -= glm::vec4(portalA.normal * 0.005f, 0.0f);
                shader.setMat4("model", bm);
                shader.setVec3("tint", portalA.color);
                glDrawArrays(GL_TRIANGLES, 0, portalQuadVertCount);
            }
            glDisable(GL_DEPTH_CLAMP);
            glDisable(GL_STENCIL_TEST);
        }

        // ===== PORTAL PASS B: render what you see through portal B =====
        // Virtual camera at portal A, looking out.  Portal B is visible and
        // should show a second-level recursive view (true "window" effect).
        {
            glBindFramebuffer(GL_FRAMEBUFFER, portalB.fbo);
            glViewport(0, 0, fbW, fbH);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

            // Step 1: Stencil-mark inner portal B + write its depth
            glEnable(GL_STENCIL_TEST);
            glEnable(GL_DEPTH_CLAMP);
            glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
            glDepthMask(GL_TRUE);
            glStencilFunc(GL_ALWAYS, 1, 0xFF);
            glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);

            glBindVertexArray(portalQuadVAO);
            shader.use();
            shader.setMat4("view", portalViewB);
            shader.setMat4("projection", portalProjB);
            shader.setMat4("model", portalB.getModelMatrix());
            shader.setVec3("tint", glm::vec3(1.0f));
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, whiteTex);
            glDrawArrays(GL_TRIANGLES, 0, portalQuadVertCount);

            glDisable(GL_DEPTH_CLAMP);
            glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);

            // Step 2: Render first-level scene everywhere.
            glStencilFunc(GL_ALWAYS, 0, 0xFF);
            glStencilOp(GL_KEEP, GL_KEEP, GL_ZERO);

            renderScene(shader, portalViewB, portalProjB, roomVAO, cubeVAO,
                        floorTex, wallTex, ceilTex, cubeTex);
            drawPlayerModel(shader, portalViewB, portalProjB,
                            cubeVAO, cubeVertCount, whiteTex,
                            camera.position, camera.yaw);

            // Step 3: Clear depth where stencil = 1
            glStencilFunc(GL_EQUAL, 1, 0xFF);
            glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
            glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
            glDepthMask(GL_TRUE);
            glDepthFunc(GL_ALWAYS);
            glDepthRange(1.0, 1.0);

            glBindVertexArray(fsQuadVAO);
            shader.setMat4("model", glm::mat4(1.0f));
            shader.setMat4("view", glm::mat4(1.0f));
            shader.setMat4("projection", glm::mat4(1.0f));
            glDrawArrays(GL_TRIANGLES, 0, 6);

            glDepthRange(0.0, 1.0);
            glDepthFunc(GL_LESS);
            glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);

            // Step 4: Render second-level scene into inner portal area
            renderScene(shader, secondViewB, secondProjB, roomVAO, cubeVAO,
                        floorTex, wallTex, ceilTex, cubeTex);
            drawPlayerModel(shader, secondViewB, secondProjB,
                            cubeVAO, cubeVertCount, whiteTex,
                            camera.position, camera.yaw);

            // Step 5: Draw source portal's border.
            // Use stencil to prevent border from drawing INSIDE the inner
            // portal area (stencil=1 there), only the ring around it.
            glStencilFunc(GL_NOTEQUAL, 1, 0xFF);
            glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
            glEnable(GL_DEPTH_CLAMP);
            glBindVertexArray(portalQuadVAO);
            shader.use();
            shader.setMat4("view", portalViewB);
            shader.setMat4("projection", portalProjB);
            glBindTexture(GL_TEXTURE_2D, whiteTex);
            {
                glm::mat4 bm = portalB.getModelMatrix();
                bm = bm * glm::scale(glm::mat4(1.0f), glm::vec3(1.12f, 1.06f, 1.0f));
                bm[3] -= glm::vec4(portalB.normal * 0.005f, 0.0f);
                shader.setMat4("model", bm);
                shader.setVec3("tint", portalB.color);
                glDrawArrays(GL_TRIANGLES, 0, portalQuadVertCount);
            }
            glDisable(GL_DEPTH_CLAMP);
            glDisable(GL_STENCIL_TEST);
        }

        // ===== MAIN PASS (stencil + depth portal rendering) =====
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, fbW, fbH);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

        // --- Step 1: Mark portal stencil AND write portal depth ---
        // Flat quad marks stencil at the exact portal boundary.
        // Depth clamp ensures behind-camera fragments still mark stencil
        // when the camera is at or past the portal surface.
        // The walls have holes cut where portals are, so even if stencil
        // isn't perfectly marked edge-on, no wall shows through.
        glEnable(GL_STENCIL_TEST);
        glEnable(GL_DEPTH_CLAMP);
        glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
        glDepthMask(GL_TRUE);
        glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);

        glBindVertexArray(portalQuadVAO);
        shader.use();
        shader.setMat4("view", view);
        shader.setMat4("projection", projection);
        shader.setVec3("tint", glm::vec3(1.0f));
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, whiteTex);

        // Portal A → stencil 1
        glStencilFunc(GL_ALWAYS, 1, 0xFF);
        shader.setMat4("model", portalA.getModelMatrix());
        glDrawArrays(GL_TRIANGLES, 0, portalQuadVertCount);

        // Portal B → stencil 2
        glStencilFunc(GL_ALWAYS, 2, 0xFF);
        shader.setMat4("model", portalB.getModelMatrix());
        glDrawArrays(GL_TRIANGLES, 0, portalQuadVertCount);

        glDisable(GL_DEPTH_CLAMP);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);

        // --- Step 2: Draw scene everywhere (no stencil restriction) ---
        // At portal pixels: objects closer than portal pass depth test,
        // draw, AND clear stencil → 0 (so FBO fill skips those pixels).
        // Objects further (wall) fail depth test, stencil stays 1/2.
        glStencilFunc(GL_ALWAYS, 0, 0xFF);
        glStencilOp(GL_KEEP, GL_KEEP, GL_ZERO);

        renderScene(shader, view, projection, roomVAO, cubeVAO,
                    floorTex, wallTex, ceilTex, cubeTex);

        // --- Step 3: Fill portal areas with FBO content ---
        // Only pixels where stencil is still 1/2 (nothing closer drew).
        glDisable(GL_DEPTH_TEST);
        glDepthMask(GL_FALSE);

        glm::mat4 identity(1.0f);
        glBindVertexArray(fsQuadVAO);
        portalShader.use();
        portalShader.setMat4("view", identity);
        portalShader.setMat4("projection", identity);
        portalShader.setMat4("model", identity);
        portalShader.setVec2("screenSize", glm::vec2((float)fbW, (float)fbH));

        glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);

        // Portal A view where stencil = 1
        glStencilFunc(GL_EQUAL, 1, 0xFF);
        glBindTexture(GL_TEXTURE_2D, portalA.colorTex);
        glDrawArrays(GL_TRIANGLES, 0, 6);

        // Portal B view where stencil = 2
        glStencilFunc(GL_EQUAL, 2, 0xFF);
        glBindTexture(GL_TEXTURE_2D, portalB.colorTex);
        glDrawArrays(GL_TRIANGLES, 0, 6);

        glEnable(GL_DEPTH_TEST);
        glDepthMask(GL_TRUE);

        // --- Step 4: Draw portal borders on top ---
        // Drawn after FBO fill. Use GL_NOTEQUAL so the border only
        // appears at the frame edges (where stencil was zeroed by the
        // scene or was never marked by the flat quad).
        glBindVertexArray(portalQuadVAO);
        shader.use();
        shader.setMat4("view", view);
        shader.setMat4("projection", projection);
        glBindTexture(GL_TEXTURE_2D, whiteTex);

        glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);

        for (auto* p : { &portalA, &portalB }) {
            int stencilVal = (p == &portalA) ? 1 : 2;
            glStencilFunc(GL_NOTEQUAL, stencilVal, 0xFF);
            glm::mat4 bm = p->getModelMatrix();
            bm = bm * glm::scale(glm::mat4(1.0f), glm::vec3(1.12f, 1.06f, 1.0f));
            bm[3] -= glm::vec4(p->normal * 0.005f, 0.0f);
            shader.setMat4("model", bm);
            shader.setVec3("tint", p->color);
            glDrawArrays(GL_TRIANGLES, 0, portalQuadVertCount);
        }

        glDisable(GL_STENCIL_TEST);

        // ===== 2D OVERLAY (FPS + Crosshair + Console) =====
        textRenderer.begin(fbW, fbH);

        // Crosshair (only when console is closed)
        if (!console.isOpen()) {
            float cx = fbW * 0.5f;
            float cy = fbH * 0.5f;
            float gap   = 3.0f;
            float len   = 10.0f;
            float thick = 2.0f;
            glm::vec4 chColor(1.0f, 1.0f, 1.0f, 0.85f);

            // Left
            textRenderer.drawRect(cx - gap - len, cy - thick * 0.5f,
                                  len, thick, chColor);
            // Right
            textRenderer.drawRect(cx + gap, cy - thick * 0.5f,
                                  len, thick, chColor);
            // Top
            textRenderer.drawRect(cx - thick * 0.5f, cy - gap - len,
                                  thick, len, chColor);
            // Bottom
            textRenderer.drawRect(cx - thick * 0.5f, cy + gap,
                                  thick, len, chColor);
        }

        // FPS counter
        if (console.showFps()) {
            std::string fpsText = "FPS: " + std::to_string(fpsDisplay);
            // Background pill
            textRenderer.drawRect(8, 8, fpsText.size() * textRenderer.charWidth() + 16, textRenderer.charHeight() + 8, glm::vec4(0, 0, 0, 0.6f));
            textRenderer.drawText(fpsText, 16, 12, glm::vec4(0.0f, 1.0f, 0.4f, 1.0f));
        }

        // Console overlay
        console.draw(fbW, fbH);

        textRenderer.end();

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    // ----- Cleanup -----
    portalA.destroyFBO();
    portalB.destroyFBO();
    glDeleteTextures(1, &floorTex);
    glDeleteTextures(1, &wallTex);
    glDeleteTextures(1, &ceilTex);
    glDeleteTextures(1, &cubeTex);
    glDeleteTextures(1, &whiteTex);
    gConsole = nullptr;
    gPortalA = nullptr;
    gPortalB = nullptr;
    glfwDestroyWindow(window);
    glfwTerminate();
    return EXIT_SUCCESS;
}
