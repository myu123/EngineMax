#include "model.h"

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

#include <iostream>

bool Model::loadFromFile(const std::string& path) {
    Assimp::Importer importer;
    const aiScene* scene = importer.ReadFile(path,
        aiProcess_Triangulate |
        aiProcess_FlipUVs |
        aiProcess_GenNormals);

    if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode) {
        std::cerr << "Assimp error: " << importer.GetErrorString() << std::endl;
        return false;
    }

    // Extract directory for relative texture paths
    size_t lastSlash = path.find_last_of("/\\");
    directory = (lastSlash != std::string::npos) ? path.substr(0, lastSlash) : ".";

    meshes.clear();
    processNode(scene->mRootNode, const_cast<aiScene*>(scene));

    std::cout << "Loaded model: " << path
              << " (" << meshes.size() << " meshes)" << std::endl;
    return true;
}

void Model::draw(Shader& shader) const {
    for (const auto& mesh : meshes) {
        mesh.draw(shader);
    }
}

void Model::processNode(void* nodePtr, void* scenePtr) {
    auto* node  = static_cast<aiNode*>(nodePtr);
    auto* scene = static_cast<aiScene*>(scenePtr);

    for (unsigned int i = 0; i < node->mNumMeshes; i++) {
        aiMesh* mesh = scene->mMeshes[node->mMeshes[i]];
        meshes.push_back(processMesh(mesh, scene));
    }

    for (unsigned int i = 0; i < node->mNumChildren; i++) {
        processNode(node->mChildren[i], scene);
    }
}

Mesh Model::processMesh(void* meshPtr, void* scenePtr) {
    auto* mesh  = static_cast<aiMesh*>(meshPtr);

    std::vector<MeshVertex> vertices;
    std::vector<unsigned int> indices;

    // Vertices
    vertices.reserve(mesh->mNumVertices);
    for (unsigned int i = 0; i < mesh->mNumVertices; i++) {
        MeshVertex v;
        v.position = { mesh->mVertices[i].x,
                       mesh->mVertices[i].y,
                       mesh->mVertices[i].z };

        if (mesh->mNormals) {
            v.normal = { mesh->mNormals[i].x,
                         mesh->mNormals[i].y,
                         mesh->mNormals[i].z };
        }

        if (mesh->mTextureCoords[0]) {
            v.texCoords = { mesh->mTextureCoords[0][i].x,
                            mesh->mTextureCoords[0][i].y };
        }

        vertices.push_back(v);
    }

    // Indices
    for (unsigned int i = 0; i < mesh->mNumFaces; i++) {
        aiFace& face = mesh->mFaces[i];
        for (unsigned int j = 0; j < face.mNumIndices; j++) {
            indices.push_back(face.mIndices[j]);
        }
    }

    return Mesh(vertices, indices);
}

GLuint Model::loadMaterialTexture(void* material, int type) {
    // Placeholder -- texture loading can be extended later
    return 0;
}
