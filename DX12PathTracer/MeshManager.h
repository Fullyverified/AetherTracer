#pragma once

#include <DirectXMath.h> // For XMMATRIX
#include <d3d12.h>
#include <vector>
#include <iostream>

struct VertexKey {
	float px, py, pz;
	float nx, ny, nz;
	float tu, tv;

	bool operator==(const VertexKey& other) const {
		constexpr float eps = 1e-6f;
		return std::abs(px - other.px) < eps && std::abs(py - other.py) < eps && std::abs(pz - other.pz) < eps &&
			std::abs(nx - other.nx) < eps && std::abs(ny - other.ny) < eps && std::abs(nz - other.nz) < eps &&
			std::abs(tu - other.tu) < eps && std::abs(tv - other.tv) < eps;
	}
};

// weird hashmap magic thanks grok
namespace std {
	template<> struct hash<VertexKey> {
		size_t operator()(const VertexKey& k) const {
			size_t h1 = std::hash<float>()(k.px);
			size_t h2 = std::hash<float>()(k.py);
			size_t h3 = std::hash<float>()(k.pz);
			size_t h4 = std::hash<float>()(k.nx);
			size_t h5 = std::hash<float>()(k.ny);
			size_t h6 = std::hash<float>()(k.nz);
			size_t h7 = std::hash<float>()(k.tu);
			size_t h8 = std::hash<float>()(k.tv);
			return h1 ^ (h2 << 1) ^ (h3 << 2) ^ (h4 << 3) ^ (h5 << 4) ^ (h6 << 5) ^ (h7 << 6) ^ (h8 << 7);
		}
	};
}



struct Vector3 {

	Vector3(float x, float y, float z) : x(x), y(y), z(z) {};
	Vector3() : x(0), y(0), z(0) {};

	float x, y, z;

};

class MeshManager {
public:

	MeshManager(ID3D12Device5* d3dDevice, ID3D12GraphicsCommandList4* cmdList) : d3dDevice(d3dDevice), cmdList(cmdList) {}
	~MeshManager() {}

	struct Vertex {
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
		Vector3 position;
		Vector3 rotation;

	};

	struct UploadDefaultBufferPair {
		ID3D12Resource* HEAP_UPLOAD_BUFFER; // cpu
		ID3D12Resource* HEAP_DEFAULT_BUFFER; // gpu
	};

	
	LoadedModel loadFromObject(const std::string& fileName, Vector3 position = { 0, 0, 0 }, Vector3 rotation = {0, 0, 0}, bool forceOpaque = false, bool computeNormalsIfMissing = false);

	UploadDefaultBufferPair createBuffers(const void* data, size_t byteSize, D3D12_RESOURCE_STATES finalState);

	ID3D12Device5* d3dDevice;
	ID3D12GraphicsCommandList4* cmdList;

	

};

