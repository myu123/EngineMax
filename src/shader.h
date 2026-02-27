#pragma once

#include <glad/gl.h>
#include <glm/glm.hpp>

class Shader {
public:
    GLuint id;

    Shader(const char* vertexSource, const char* fragmentSource);
    ~Shader();

    void use() const;
    void setInt(const char* name, int value) const;
    void setFloat(const char* name, float value) const;
    void setVec2(const char* name, const glm::vec2& value) const;
    void setVec3(const char* name, const glm::vec3& value) const;
    void setMat4(const char* name, const glm::mat4& value) const;

private:
    GLuint compileShader(GLenum type, const char* source);
};
