#pragma once

#include <glad/gl.h>
#include <glm/glm.hpp>
#include <string>

class TextRenderer {
public:
    TextRenderer();
    ~TextRenderer();

    // Must be called once after OpenGL context is ready.
    // Loads a TTF font from disk and bakes an atlas.
    bool init(const char* fontPath, float fontSize);

    // Set up orthographic state for 2D drawing.
    void begin(int screenWidth, int screenHeight);

    // Draw a string. (x, y) is top-left corner in screen pixels.
    void drawText(const std::string& text, float x, float y,
                  const glm::vec4& color = glm::vec4(1.0f));

    // Draw a solid colored rectangle.
    void drawRect(float x, float y, float w, float h,
                  const glm::vec4& color);

    // Done with 2D drawing (restores GL state).
    void end();

    float charWidth()  const { return charW; }
    float charHeight() const { return charH; }

private:
    GLuint shaderProgram = 0;
    GLuint vao = 0, vbo = 0;
    GLuint fontTexture = 0;
    GLuint whiteTexture = 0;  // 1x1 white pixel for solid rects

    // Baked character data from stb_truetype
    struct CharInfo {
        float x0, y0, x1, y1; // quad coords (relative to cursor)
        float s0, t0, s1, t1; // texture coords
        float advance;
    };
    CharInfo chars[128];
    float charW = 0.0f;  // approximate monospace width
    float charH = 0.0f;  // line height

    int projLoc   = -1;
    int colorLoc  = -1;
    int texLoc    = -1;
};
