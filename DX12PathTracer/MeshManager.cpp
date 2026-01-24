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

        //std::vector<Vertex> uniqueVertices;

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
            //mesh.indices.push_back(mesh.vertices.size()-1);

            // deduplication

            if (mesh.vertices.empty()) {
                mesh.vertices.push_back(vertex);
                mesh.indices.push_back(0);
                continue;
            }

            for (int i = 0; i < mesh.vertices.size(); i++) {
                auto uniqueVertex = mesh.vertices[i];

                if (vertex.position.x == uniqueVertex.position.x && vertex.position.y == uniqueVertex.position.y && vertex.position.z == uniqueVertex.position.z) {
                    // vertex already exists
                    mesh.indices.push_back(i);
                    break;
                }
                if (i == mesh.vertices.size() - 1) {
                    // end of list - vertice doesnt exist
                    mesh.vertices.push_back(vertex);
                    mesh.indices.push_back(mesh.vertices.size()-1);
                    break;
                }
            } 
            

        }
    
        // compute normals if missing
        // later


        model.meshes.push_back(std::move(mesh));

    }

    std::cout << "Creating upload and default buffers" << std::endl;

    // create upload and default buffers and store them
    for (auto& mesh : model.meshes) {
    
        

        size_t vbSize = mesh.vertices.size() * sizeof(MeshManager::Vertex);
        size_t ibSize = mesh.indices.size() * sizeof(uint32_t);
        std::cout << ": vbSize=" << vbSize << " bytes, ibSize=" << ibSize << " bytes\n";


        auto vbUpload = createBuffers(mesh.vertices.data(), vbSize, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
        auto ibUpload = createBuffers(mesh.indices.data(), ibSize, D3D12_RESOURCE_STATE_INDEX_BUFFER);

        // CPU memory buffers
        model.vertexUploadBuffers.push_back(vbUpload.HEAP_UPLOAD_BUFFER);
        model.indexUploadBuffers.push_back(ibUpload.HEAP_UPLOAD_BUFFER);
        // GPU memory buffers
        model.vertexDefaultBuffers.push_back(vbUpload.HEAP_DEFAULT_BUFFER);
        model.indexDefaultBuffers.push_back(ibUpload.HEAP_DEFAULT_BUFFER);

    }

    return model;
}

// create default heap buffer
MeshManager::UploadDefaultBufferPair MeshManager::createBuffers(const void* data, size_t byteSize, D3D12_RESOURCE_STATES finalState) {
    std::cout << "byteSize: " << byteSize << std::endl;


    // CPU Buffer (upload buffer)
    DXGI_SAMPLE_DESC NO_AA = { .Count = 1, .Quality = 0 };
    D3D12_RESOURCE_DESC DESC = {
        .Dimension = D3D12_RESOURCE_DIMENSION_BUFFER,
        .Width = byteSize,
        .Height = 1,
        .DepthOrArraySize = 1,
        .MipLevels = 1,
        .SampleDesc = NO_AA,
        .Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR,
        .Flags = D3D12_RESOURCE_FLAG_NONE,
    };
    D3D12_HEAP_PROPERTIES UPLOAD_HEAP = { .Type = D3D12_HEAP_TYPE_UPLOAD };


    ID3D12Resource* upload;
    d3dDevice->CreateCommittedResource(
        &UPLOAD_HEAP,
        D3D12_HEAP_FLAG_NONE,
        &DESC,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&upload)
    );

    void* mapped;
    upload->Map(0, nullptr, &mapped); // mapped now points to the upload buffer
    memcpy(mapped, data, byteSize); // copy data to upload buffer
    upload->Unmap(0, nullptr); // 7

    // VRAM Buffer

    D3D12_HEAP_PROPERTIES DEFAULT_HEAP = { .Type = D3D12_HEAP_TYPE_DEFAULT };

    // Create target buffer in DEFAULT heap
    DESC.Flags = D3D12_RESOURCE_FLAG_NONE; // or ALLOW_UNORDERED_ACCESS if needed later
    ID3D12Resource* target = nullptr;
    d3dDevice->CreateCommittedResource(
        &DEFAULT_HEAP,
        D3D12_HEAP_FLAG_NONE,
        &DESC,
        D3D12_RESOURCE_STATE_COMMON,
        nullptr,
        IID_PPV_ARGS(&target)
    );

    return {upload, target};
}