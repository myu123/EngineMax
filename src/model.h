#pragma once

#include "mesh.h"
#include "shader.h"

#include <glm/glm.hpp>
#include <vector>
#include <string>

class Model {
public:
    Model() = default;

    // Load a 3D model file (OBJ, FBX, glTF, etc.) via Assimp.
    // Returns true on success.
    bool loadFromFile(const std::string& path);

    // Draw all meshes in this model.
    void draw(Shader& shader) const;

    bool isLoaded() const { return !meshes.empty(); }

private:
    std::vector<Mesh> meshes;
    std::string directory;  // base directory for texture paths

    // Assimp processing (implemented in model.cpp)
    void processNode(void* node, void* scene);
    Mesh processMesh(void* mesh, void* scene);
    GLuint loadMaterialTexture(void* material, int type);
};
