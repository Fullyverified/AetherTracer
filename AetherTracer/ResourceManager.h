#pragma once

#include <vector>
#include "DirectXMath.h"
#include <d3d12.h>
#include <dxgi1_4.h>
#include <d3dcompiler.h> // for compiling shaders
#pragma comment(lib, "d3d12")
#pragma comment(lib, "dxgi")

#include "MaterialManager.h"
#include "EntityManager.h"
#include "MeshManager.h"



class ResourceManager {
public:

	struct Buffer {
		ID3D12Resource* uploadBuffers;
		ID3D12Resource* defaultBuffers;
	};


	ResourceManager() {};
	~ResourceManager() {};

	void initClearDescriptors();


	Buffer* createBuffers(const void* data, size_t byteSize, D3D12_RESOURCE_STATES finalState, bool UAV);




	// Utility

	

	// RAY TRACING STAGE

	struct DX12Material {
		DirectX::XMFLOAT3 color;
		float roughness;
		float metallic;
		float ior;
		float transmission;
		float emission;

		DX12Material(MaterialManager::Material* material) {
			color.x = material->color.x;
			color.y = material->color.y;
			color.z = material->color.z;
			roughness = material->roughness;
			metallic = material->metallic;
			ior = material->ior;
			transmission = material->transmission;
			emission = material->emission;
		}
	};

	struct DX12Model {
		DX12Model() : loadedModel(nullptr), BLAS(nullptr) {};

		MeshManager::LoadedModel* loadedModel; // mesh and raw vertex data
		ID3D12Resource* BLAS;

		std::vector<Buffer*> vertexBuffers;
		std::vector<Buffer*> indexBuffers;;
	};

	struct DX12Entity {
		DX12Entity() : entity(nullptr), model(nullptr) {};

		EntityManager::Entity* entity; // name, position, rotation
		DX12Model* model;
		DX12Material* material;
	};

	struct alignas(256)DX12Camera {

		DX12Camera() : position{} {};
		DX12Camera(PT::Vector3 position) : position{ position.x, position.y, position.z } {};

		DirectX::XMFLOAT3 position;
		float pad0;

		DirectX::XMFLOAT4X4 invViewProj;
		UINT seed;
		UINT sky;
		float skyBrighness;
		UINT minBounces;
		UINT maxBounces;
		UINT jitter;
	};

	// MODEL
	std::vector<Buffer*> allVertexBuffers;
	std::vector<Buffer*> allIndexBuffers;

	Buffer* materialsBuffer;
	std::vector<DX12Material> dx12Materials;
	Buffer* materialIndexBuffer;
	std::vector<uint32_t> materialIndices;

	Buffer* cameraConstantBuffer;

	// ENTITY / BLAS
	std::unordered_map<std::string, DX12Material*> materials;
	std::unordered_map<std::string, DX12Model*> dx12Models;

	std::vector<DX12Entity*> dx12Entitys;
	DX12Camera* dx12Camera;

	// TLAS
	ID3D12Resource* tlas;
	ID3D12Resource* tlasscratch;

	UINT NUM_INSTANCES = 0;
	ID3D12Resource* instances;
	D3D12_RAYTRACING_INSTANCE_DESC* instanceData;
	std::unordered_map<std::string, uint32_t> uniqueInstancesID;
	std::unordered_set<std::string> uniqueInstances;

	// COMPUTE STAGE

	ID3D12Resource* renderTarget;

	struct alignas(256)ToneMappingParams {
		ToneMappingParams() : stage(0), numIts(1), exposure(1.0f) {};
		UINT stage;
		float exposure;
		UINT numIts;
	};
	ToneMappingParams* toneMappingParams;
	Buffer* toneMappingConstantBuffer;
	Buffer* maxLumBuffer;

	// SHARED

	ID3D12Resource* accumulationTexture;

	Buffer* randBuffer;
	std::vector<uint64_t> randPattern;


	UINT num_frames = 1;
	UINT seed = 1;

	UINT frameIndexInFlight = 2;


	UINT width;
	UINT height;

	DXGI_SAMPLE_DESC NO_AA = { .Count = 1, .Quality = 0 };


	// device init
	HWND hwnd;
	IDXGIFactory4* factory;
	ID3D12Device5* d3dDevice;
	ID3D12CommandQueue* cmdQueue;
	ID3D12Fence* fence;
	UINT64 fenceState = 1;

	// swap chain and uav
	IDXGISwapChain3* swapChain;

	// Command list and allocator

	ID3D12CommandAllocator* cmdAlloc; // block of memory
	ID3D12GraphicsCommandList4* cmdList;

	// imgui
	ID3D12DescriptorHeap* rtvHeap = nullptr;
	UINT rtvDescriptorSize = 0;


	float clearColor[4] = { 0.0f, 0.0f, 0.0f, 1.0f };


	// cpu non shader visible descriptor heap
	ID3D12DescriptorHeap* cpuDescHeap;


};