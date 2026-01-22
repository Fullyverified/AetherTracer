#define TINYOBJLOADER_IMPLEMENTATION
#include "tiny_obj_loader.h"

#include "MeshManager.h"

#include <algorithm>     // For std::size, typed std::max, etc.
#include <DirectXMath.h> // For XMMATRIX
#include <d3d12.h>       // The star of our show :)

#include <iostream>
#include <fstream>
#include <unordered_map>


MeshManager::LoadedModel MeshManager::loadFromObject(const std::string& fileName, bool forceOpaque, bool computeNormalsIfMissing) {

	tinyobj::attrib_t attrib;
	std::vector<tinyobj::shape_t> shapes;
	std::vector<tinyobj::material_t> materials;
	std::string warn, err;

    std::string path = "test";

    bool load = tinyobj::LoadObj(
        &attrib,
        &shapes,
        &materials,
        &warn,
        &err,
        fileName.c_str(),           // filename
        nullptr,                    // mtl_basedir = nullptr (no separate .mtl folder)
        true,                       // triangulate = true (strongly recommended!)
        true                        // default_vcols_fallback = true
    );

    if (!warn.empty()) std::cerr << "tinyobj warning: " << warn << std::endl;
    if (!err.empty()) std::cerr << "tinyobj error: " << err << std::endl;

    if (!load) std::cerr << "failed to load OBJ" << std::endl;

    LoadedModel model;

    for (const auto& shape : shapes) {
    
        Mesh mesh;
        mesh.name = shape.name;

        std::unordered_map<int, uint32_t> uniqueVerticies; // pos+normal+uv -> index

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

            // deduplication
            // later

            mesh.vertices.push_back(vertex);
            mesh.indices.push_back(static_cast<uint32_t>(mesh.indices.size()));

        }
    
        // compute normals if missing
        // later

        model.meshes.push_back(std::move(mesh));

    }

    // upload to GPU
    for (auto& mesh : model.meshes) {
    
        size_t vbSize = mesh.vertices.size() * sizeof(Vertex);
        size_t ibSize = mesh.indices.size() * sizeof(uint32_t);

        auto vbUpload = uploadBuffer(mesh.vertices.data(), vbSize, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
        auto ibUpload = uploadBuffer(mesh.indices.data(), ibSize, D3D12_RESOURCE_STATE_INDEX_BUFFER);


        model.vertexBuffers.push_back(vbUpload);
        model.indexBuffers.push_back(ibUpload);
    }

    return model;
}

// create default heap buffer
ID3D12Resource* MeshManager::uploadBuffer(const void* data, size_t byteSize, D3D12_RESOURCE_STATES finalState) {

    DXGI_SAMPLE_DESC NO_AA = { .Count = 1, .Quality = 0 };
    D3D12_RESOURCE_DESC DESC = {
        .Dimension = D3D12_RESOURCE_DIMENSION_BUFFER,
        .Width = 0, // will be changed in copies
        .Height = 1,
        .DepthOrArraySize = byteSize,
        .MipLevels = 1,
        .SampleDesc = NO_AA,
        .Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR,
    };
    DESC.Width = byteSize;
    DESC.Flags = D3D12_RESOURCE_FLAG_NONE;
    D3D12_HEAP_PROPERTIES UPLOAD_HEAP = { .Type = D3D12_HEAP_TYPE_UPLOAD };

    ID3D12Resource* upload = nullptr;
    d3dDevice->CreateCommittedResource(
        &UPLOAD_HEAP,
        D3D12_HEAP_FLAG_NONE,
        &DESC,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&upload)
    );

    void* mapped = nullptr;
    upload->Map(0, nullptr, &mapped);
    memcpy(mapped, data, byteSize);
    upload->Unmap(0, nullptr);

    D3D12_HEAP_PROPERTIES DEFAULT_HEAP = { .Type = D3D12_HEAP_TYPE_DEFAULT };

    // Create target buffer in DEFAULT heap
    DESC.Flags = D3D12_RESOURCE_FLAG_NONE; // or ALLOW_UNORDERED_ACCESS if needed later
    ID3D12Resource* target = nullptr;
    d3dDevice->CreateCommittedResource(
        &DEFAULT_HEAP,
        D3D12_HEAP_FLAG_NONE,
        &DESC,
        D3D12_RESOURCE_STATE_COPY_DEST,
        nullptr,
        IID_PPV_ARGS(&target)
    );

    return upload;
}