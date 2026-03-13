#pragma once

#include <glad/gl.h>
#include <glm/glm.hpp>
#include <vector>
#include <string>

#include "shader.h"

// Vertex format for loaded models (position + normal + UV)
struct MeshVertex {
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec2 texCoords;
};

struct MeshTexture {
    GLuint id;
    std::string type;  // "texture_diffuse", "texture_specular", etc.
};

class Mesh {
public:
    Mesh() = default;
    Mesh(const std::vector<MeshVertex>& vertices,
         const std::vector<unsigned int>& indices,
         const std::vector<MeshTexture>& textures = {});

    // Move-only (owns GPU resources)
    Mesh(Mesh&& other) noexcept;
    Mesh& operator=(Mesh&& other) noexcept;
    Mesh(const Mesh&) = delete;
    Mesh& operator=(const Mesh&) = delete;

    ~Mesh();

    void draw(Shader& shader) const;

    bool isValid() const { return vao != 0; }

private:
    GLuint vao = 0, vbo = 0, ebo = 0;
    int indexCount = 0;
    std::vector<MeshTexture> textures;

    void setup(const std::vector<MeshVertex>& vertices,
               const std::vector<unsigned int>& indices);
    void cleanup();
};
