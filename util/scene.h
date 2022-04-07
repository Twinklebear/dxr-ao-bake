#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include "camera.h"
#include "lights.h"
#include "material.h"
#include "mesh.h"
#include "phmap.h"
#include <glm/glm.hpp>

struct Scene {
    std::vector<Mesh> meshes;
    std::vector<Instance> instances;
    std::vector<DisneyMaterial> materials;
    std::vector<Image> textures;
    std::vector<QuadLight> lights;
    std::vector<Camera> cameras;

    Scene(const std::string &fname);
    Scene() = default;

    // Compute the unique number of triangles in the scene
    size_t unique_tris() const;

    // Compute the total number of triangles in the scene (after instancing)
    size_t total_tris() const;

    size_t num_geometries() const;

private:
    void load_obj(const std::string &file);

    void load_gltf(const std::string &file);

    void load_crts(const std::string &file);

    void validate_materials();
};
