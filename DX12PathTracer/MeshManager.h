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
		std::vector<ID3D12Resource*> vertexBuffers;
		std::vector<ID3D12Resource*> indexBuffers;
	};

	struct UploadBufferTarget {
		ID3D12Resource* uploadBuffer; // cpu
		ID3D12Resource* targetBuffer; // gpu
	};

	LoadedModel loadFromObject(const std::string& fileName, bool forceOpaque = true, bool computeNormalsIfMissing = true);

	UploadBufferTarget createBuffers(const void* data, UINT16 byteSize, D3D12_RESOURCE_STATES finalState);

	ID3D12Device5* d3dDevice;
	ID3D12GraphicsCommandList4* cmdList;

	

};

