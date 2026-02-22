#define TINYOBJLOADER_IMPLEMENTATION
#include "tiny_obj_loader.h"

#include "MeshManager.h"

#include <algorithm>     // For std::size, typed std::max, etc.
#include <DirectXMath.h> // For XMMATRIX
#include <d3d12.h>       // The star of our show :)

#include <iostream>
#include <fstream>

void MeshManager::initMeshes() { 

    //std::vector<std::string> models = { "weirdTriangle", "cube", "sphere", "cornell" };
    std::vector<std::string> models = { "weirdTriangle", "cube", "sphere", "cornell", "TheStanfordDragon", "lucyScaled", "diamondFlat", "diamond", "portalGun", "portalButton", "CompanionCube", "floor"};

    for (std::string name : models) {
        loadFromObject(name, false, false);
    }

}

void MeshManager::loadFromObject(const std::string& fileName, bool forceOpaque, bool computeNormalsIfMissing) {

    std::cout << "starting load" << std::endl;

	tinyobj::attrib_t attrib;
	std::vector<tinyobj::shape_t> shapes;
	std::vector<tinyobj::material_t> materials;
	std::string warn, err;

    std::string filepath = "assets/meshes/" + fileName + ".obj";

    bool load = tinyobj::LoadObj(
        &attrib,
        &shapes,
        &materials,
        &warn,
        &err,
        filepath.c_str(),           // filename
        nullptr,                    // mtl_basedir = nullptr (no separate .mtl folder)
        false,                       // triangulate = true (strongly recommended!)
        false                        // default_vcols_fallback = true
    );

    if (!warn.empty()) std::cerr << "tinyobj warning: " << warn << std::endl;
    if (!err.empty()) std::cerr << "tinyobj error: " << err << std::endl;

    if (!load) std::cerr << "failed to load OBJ" << std::endl;

    LoadedModel* model = new LoadedModel(fileName);

    std::cout << "reading shapes" << std::endl;

    for (const auto& shape : shapes) {
    
        Mesh mesh;
        mesh.name = shape.name;

        std::unordered_map<VertexKey, uint32_t> uniqueVertices;

        std::cout << "for index of mesh indices" << std::endl;

        for (const auto& index : shape.mesh.indices) {
            Vertex vertex{};

            // position
            int vi = 3 * index.vertex_index;
            vertex.position = {
                attrib.vertices[vi + 0],
                attrib.vertices[vi + 1],
                attrib.vertices[vi + 2]
            };
        
            // normal (if present)
            if (index.normal_index >= 0) {
                int ni = 3 * index.normal_index;
                vertex.normal = {
                    attrib.normals[ni + 0],
                    attrib.normals[ni + 1],
                    attrib.normals[ni + 2]
                };            
            }
            
            // texcoord
            if (index.texcoord_index >= 0) {
                int ti = 2 * index.texcoord_index;
                vertex.texcoord = {
                    attrib.texcoords[ti + 0],
                    1.0f - attrib.texcoords[ti + 1] // flip v
                };
            }


            //mesh.vertices.push_back(vertex);
            //mesh.indices.push_back(static_cast<uint32_t>(mesh.vertices.size()) - 1);


            // deduplication
            VertexKey vertexKey{vertex.position.x, vertex.position.y, vertex.position.z, vertex.normal.x, vertex.normal.y, vertex.normal.z, vertex.texcoord.x, vertex.texcoord.y };
            
            auto it = uniqueVertices.find(vertexKey);
            if (it == uniqueVertices.end()) {
                uint32_t newIdx = static_cast<uint32_t>(mesh.vertices.size());
                mesh.vertices.push_back(vertex);
                uniqueVertices[vertexKey] = newIdx;
                mesh.indices.push_back(newIdx);
            }
            else {
                mesh.indices.push_back(it->second);
            }

        }
    
        std::cout << "Mesh has " << mesh.vertices.size() << " verts, " << mesh.indices.size() << " indices" << std::endl;
      
        // compute normals if missing
        // later
        model->meshes.push_back(std::move(mesh));

    }

    
    loadedModels[fileName] = model;
}

void MeshManager::cleanUp() {
    
    for (auto const& [name, model] : loadedModels) {
        loadedModels.erase(name);
        delete model;
    }

}