#pragma once

#include <DirectXMath.h> // For XMMATRIX
#include <d3d12.h>
#include <vector>
#include <iostream>

class MeshManager {
public:

	MeshManager(ID3D12Device5* d3dDevice, ID3D12GraphicsCommandList4* cmdList) : d3dDevice(d3dDevice), cmdList(cmdList) {}
	~MeshManager() {}

	struct alignas(16) Vertex {
		DirectX::XMFLOAT3 position;
		DirectX::XMFLOAT3 normal;
		DirectX::XMFLOAT2 texcoord;
		uint32_t materialIndex;
	};

	struct Mesh {
		std::vector<Vertex> vertices;
		std::vector<uint32_t> indices;
		std::string name;
		DirectX::XMFLOAT3A boundsMin, boundsMax;
		uint32_t materialIndex;
	};

	struct LoadedModel {
		std::vector<Mesh> meshes;
		std::vector<ID3D12Resource*> vertexUploadBuffers;
		std::vector<ID3D12Resource*> indexUploadBuffers;
		std::vector<ID3D12Resource*> vertexDefaultBuffers;
		std::vector<ID3D12Resource*> indexDefaultBuffers;
	};

	struct UploadDefaultBufferPair {
		ID3D12Resource* HEAP_UPLOAD_BUFFER; // cpu
		ID3D12Resource* HEAP_DEFAULT_BUFFER; // gpu
	};

	LoadedModel loadFromObject(const std::string& fileName, bool forceOpaque = true, bool computeNormalsIfMissing = true);

	UploadDefaultBufferPair createBuffers(const void* data, size_t byteSize, D3D12_RESOURCE_STATES finalState);

	ID3D12Device5* d3dDevice;
	ID3D12GraphicsCommandList4* cmdList;

	

};

