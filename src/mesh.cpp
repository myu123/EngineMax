#include "mesh.h"

Mesh::Mesh(const std::vector<MeshVertex>& vertices,
           const std::vector<unsigned int>& indices,
           const std::vector<MeshTexture>& textures)
    : textures(textures)
{
    setup(vertices, indices);
}

Mesh::Mesh(Mesh&& other) noexcept
    : vao(other.vao), vbo(other.vbo), ebo(other.ebo),
      indexCount(other.indexCount), textures(std::move(other.textures))
{
    other.vao = other.vbo = other.ebo = 0;
    other.indexCount = 0;
}

Mesh& Mesh::operator=(Mesh&& other) noexcept {
    if (this != &other) {
        cleanup();
        vao = other.vao;
        vbo = other.vbo;
        ebo = other.ebo;
        indexCount = other.indexCount;
        textures = std::move(other.textures);
        other.vao = other.vbo = other.ebo = 0;
        other.indexCount = 0;
    }
    return *this;
}

Mesh::~Mesh() {
    cleanup();
}

void Mesh::setup(const std::vector<MeshVertex>& vertices,
                 const std::vector<unsigned int>& indices)
{
    indexCount = static_cast<int>(indices.size());

    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &vbo);
    glGenBuffers(1, &ebo);

    glBindVertexArray(vao);

    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER,
                 vertices.size() * sizeof(MeshVertex),
                 vertices.data(), GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                 indices.size() * sizeof(unsigned int),
                 indices.data(), GL_STATIC_DRAW);

    // position
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE,
                          sizeof(MeshVertex),
                          (void*)offsetof(MeshVertex, position));
    // normal
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE,
                          sizeof(MeshVertex),
                          (void*)offsetof(MeshVertex, normal));
    // texcoords
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE,
                          sizeof(MeshVertex),
                          (void*)offsetof(MeshVertex, texCoords));

    glBindVertexArray(0);
}

void Mesh::draw(Shader& shader) const {
    // Bind textures if any
    for (unsigned int i = 0; i < textures.size(); i++) {
        glActiveTexture(GL_TEXTURE0 + i);
        glBindTexture(GL_TEXTURE_2D, textures[i].id);
    }

    glBindVertexArray(vao);
    glDrawElements(GL_TRIANGLES, indexCount, GL_UNSIGNED_INT, 0);
    glBindVertexArray(0);

    glActiveTexture(GL_TEXTURE0);
}

void Mesh::cleanup() {
    if (vao) glDeleteVertexArrays(1, &vao);
    if (vbo) glDeleteBuffers(1, &vbo);
    if (ebo) glDeleteBuffers(1, &ebo);
    vao = vbo = ebo = 0;
}
