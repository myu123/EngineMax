#include "text_renderer.h"

#include <stb_truetype.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <fstream>
#include <iostream>
#include <vector>

// ---- 2D shaders ----

static const char* textVertSrc = R"(
#version 330 core
layout (location = 0) in vec2 aPos;
layout (location = 1) in vec2 aUV;
uniform mat4 projection;
out vec2 uv;
void main() {
    gl_Position = projection * vec4(aPos, 0.0, 1.0);
    uv = aUV;
}
)";

static const char* textFragSrc = R"(
#version 330 core
in vec2 uv;
out vec4 FragColor;
uniform sampler2D tex;
uniform vec4 color;
void main() {
    float a = texture(tex, uv).r;
    FragColor = vec4(color.rgb, color.a * a);
}
)";

// ---- helpers ----

static GLuint compileShader(GLenum type, const char* src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    int ok; glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) { char log[512]; glGetShaderInfoLog(s, 512, nullptr, log); std::cerr << log << "\n"; }
    return s;
}

// ---- TextRenderer impl ----

TextRenderer::TextRenderer() {}

TextRenderer::~TextRenderer() {
    if (fontTexture)    glDeleteTextures(1, &fontTexture);
    if (whiteTexture)   glDeleteTextures(1, &whiteTexture);
    if (vao)            glDeleteVertexArrays(1, &vao);
    if (vbo)            glDeleteBuffers(1, &vbo);
    if (shaderProgram)  glDeleteProgram(shaderProgram);
}

bool TextRenderer::init(const char* fontPath, float fontSize) {
    // ---- Load font file ----
    std::ifstream file(fontPath, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        std::cerr << "TextRenderer: cannot open font " << fontPath << "\n";
        return false;
    }
    auto size = file.tellg();
    std::vector<unsigned char> fontData(size);
    file.seekg(0);
    file.read(reinterpret_cast<char*>(fontData.data()), size);
    file.close();

    // ---- Bake atlas ----
    const int atlasW = 512, atlasH = 512;
    std::vector<unsigned char> atlasBitmap(atlasW * atlasH);

    stbtt_bakedchar bakedChars[128];
    int result = stbtt_BakeFontBitmap(
        fontData.data(), 0, fontSize,
        atlasBitmap.data(), atlasW, atlasH,
        0, 128, bakedChars
    );
    if (result <= 0) {
        std::cerr << "TextRenderer: stbtt_BakeFontBitmap failed (only fit " << result << " chars)\n";
    }

    // ---- Fill CharInfo ----
    for (int i = 0; i < 128; ++i) {
        const auto& bc = bakedChars[i];
        chars[i].s0 = bc.x0 / (float)atlasW;
        chars[i].t0 = bc.y0 / (float)atlasH;
        chars[i].s1 = bc.x1 / (float)atlasW;
        chars[i].t1 = bc.y1 / (float)atlasH;
        chars[i].x0 = bc.xoff;
        chars[i].y0 = bc.yoff;
        chars[i].x1 = bc.xoff + (bc.x1 - bc.x0);
        chars[i].y1 = bc.yoff + (bc.y1 - bc.y0);
        chars[i].advance = bc.xadvance;
    }
    charW = chars['M'].advance;
    charH = fontSize;

    // ---- Upload font texture ----
    glGenTextures(1, &fontTexture);
    glBindTexture(GL_TEXTURE_2D, fontTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, atlasW, atlasH, 0, GL_RED, GL_UNSIGNED_BYTE, atlasBitmap.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    // ---- 1x1 white texture for drawRect ----
    unsigned char white = 255;
    glGenTextures(1, &whiteTexture);
    glBindTexture(GL_TEXTURE_2D, whiteTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, 1, 1, 0, GL_RED, GL_UNSIGNED_BYTE, &white);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    // ---- Shader ----
    GLuint vs = compileShader(GL_VERTEX_SHADER, textVertSrc);
    GLuint fs = compileShader(GL_FRAGMENT_SHADER, textFragSrc);
    shaderProgram = glCreateProgram();
    glAttachShader(shaderProgram, vs);
    glAttachShader(shaderProgram, fs);
    glLinkProgram(shaderProgram);
    glDeleteShader(vs);
    glDeleteShader(fs);

    projLoc  = glGetUniformLocation(shaderProgram, "projection");
    colorLoc = glGetUniformLocation(shaderProgram, "color");
    texLoc   = glGetUniformLocation(shaderProgram, "tex");

    // ---- VAO / VBO ----
    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &vbo);
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    // Dynamic buffer, enough for ~1000 quads
    glBufferData(GL_ARRAY_BUFFER, sizeof(float) * 6 * 4 * 1000, nullptr, GL_DYNAMIC_DRAW);
    // pos (2 floats) + uv (2 floats) = 4 floats per vertex
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glBindVertexArray(0);

    std::cout << "TextRenderer initialized (" << fontPath << " @ " << fontSize << "px)\n";
    return true;
}

void TextRenderer::begin(int screenWidth, int screenHeight) {
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_DEPTH_TEST);

    glUseProgram(shaderProgram);
    glm::mat4 proj = glm::ortho(0.0f, (float)screenWidth, (float)screenHeight, 0.0f);
    glUniformMatrix4fv(projLoc, 1, GL_FALSE, glm::value_ptr(proj));
    glUniform1i(texLoc, 0);
    glActiveTexture(GL_TEXTURE0);
    glBindVertexArray(vao);
}

void TextRenderer::drawText(const std::string& text, float x, float y, const glm::vec4& color) {
    glBindTexture(GL_TEXTURE_2D, fontTexture);
    glUniform4fv(colorLoc, 1, glm::value_ptr(color));

    std::vector<float> verts;
    verts.reserve(text.size() * 6 * 4);

    float cursorX = x;
    float cursorY = y + charH; // baseline offset

    for (char ch : text) {
        if (ch < 0 || ch >= 128) ch = '?';
        const auto& ci = chars[(int)ch];

        float x0 = cursorX + ci.x0;
        float y0 = cursorY + ci.y0;
        float x1 = cursorX + ci.x1;
        float y1 = cursorY + ci.y1;

        // Two triangles per character
        float quad[] = {
            x0, y0, ci.s0, ci.t0,
            x1, y0, ci.s1, ci.t0,
            x1, y1, ci.s1, ci.t1,

            x0, y0, ci.s0, ci.t0,
            x1, y1, ci.s1, ci.t1,
            x0, y1, ci.s0, ci.t1,
        };
        verts.insert(verts.end(), std::begin(quad), std::end(quad));
        cursorX += ci.advance;
    }

    if (!verts.empty()) {
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferSubData(GL_ARRAY_BUFFER, 0, verts.size() * sizeof(float), verts.data());
        glDrawArrays(GL_TRIANGLES, 0, (GLsizei)(verts.size() / 4));
    }
}

void TextRenderer::drawRect(float x, float y, float w, float h, const glm::vec4& color) {
    glBindTexture(GL_TEXTURE_2D, whiteTexture);
    glUniform4fv(colorLoc, 1, glm::value_ptr(color));

    float verts[] = {
        x,     y,     0, 0,
        x + w, y,     1, 0,
        x + w, y + h, 1, 1,

        x,     y,     0, 0,
        x + w, y + h, 1, 1,
        x,     y + h, 0, 1,
    };

    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(verts), verts);
    glDrawArrays(GL_TRIANGLES, 0, 6);
}

void TextRenderer::end() {
    glBindVertexArray(0);
    glEnable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
}
