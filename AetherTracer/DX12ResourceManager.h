#pragma once

#define NOMINMAX

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

#include "DX12Resource.h"

class DX12ResourceManager {
public:

	struct DescriptorAllocator {
		ID3D12DescriptorHeap* desc_heap;
		std::vector<UINT> free_indices;
		UINT desc_increment_size;

		UINT alloc() {
			UINT idx = free_indices.back();
			free_indices.pop_back();
			return idx;
		}

		void free(UINT idx) {
			free_indices.push_back(idx);
		}

		void init(UINT num_descriptors) {
			free_indices.clear();
			for (int i = num_descriptors - 1; i >= 0; i--) {
				free_indices.push_back(i);
			}

		}

	};

	DX12ResourceManager(MeshManager* meshManager, MaterialManager* materialManager, EntityManager* entityManager, IDXGIFactory4* factory, ID3D12Device5* d3dDevice, ID3D12CommandQueue* cmdQueue, ID3D12CommandAllocator* cmdAlloc, ID3D12GraphicsCommandList4* cmdList);
	~DX12ResourceManager() {};

	DX12ResourceHandle* createResourceHandle(const void* data, size_t byte_size, size_t max_size, D3D12_RESOURCE_STATES final_state, bool is_UAV);
	void updateResourceHandle(DX12ResourceHandle* resource_handle, const void* data, size_t byte_size, size_t max_size);
	void pushResourceHandle(DX12ResourceHandle* resource_handle, size_t data_size, D3D12_RESOURCE_STATES state_before, D3D12_RESOURCE_STATES state_after);
	void resizeResourceHandle(DX12ResourceHandle* resource_handle, const void* data, size_t byte_size, D3D12_RESOURCE_STATES final_state, bool is_UAV);
	void createCBV(DX12ResourceHandle* resource_handle, size_t byte_size);

	// INIT Resource
	void createFence(ID3D12Fence*& fence);

	void initRenderTexture(DX12ResourceHandle* resource_handle, DXGI_FORMAT format, std::string resource_name, bool is_resize);

	void initModelBuffers();
	void initDX12Entites(bool is_update);

	void initVertexIndexBuffers();
	void initMaxLumBuffer();

	void updateToneParams();
	void updateCamera();
	void updateTransforms();
	void updateRand(bool is_resize);

	void initDescriptorHeap(DX12ResourceManager::DescriptorAllocator* descriptorAllocator, UINT num_descriptors, bool is_shader_visible, std::string descriptor_name);

	void rebuildBLAS();

	// Utility
	void checkHR(HRESULT hr, ID3DBlob* errorblob, std::string context);
	void waitForGPU();

	// RAY TRACING STAGE

	// MODEL
	std::vector<DX12ResourceHandle*> allVertexBuffers;
	std::vector<DX12ResourceHandle*> allIndexBuffers;

	std::vector<DX12ResourceHandle*> emissiveEntitiesIndexBuffer; // index for each emissive object
	std::vector<DX12ResourceHandle*> emissiveTrianglesIndexBuffer; // for each emissive object, indices only for its emissive triangles



	DX12ResourceHandle* cameraConstantBuffer;

	// ENTITY / BLAS
	std::unordered_map<std::string, DX12Model*> dx12Models_map;
	std::unordered_map<std::string, UINT> dx12Models_index_map;

	std::vector<DX12Entity*> dx12entities;
	DX12Camera* dx12Camera;

	UINT NUM_INSTANCES = 0;
	DX12ResourceHandle* instances;
	D3D12_RAYTRACING_INSTANCE_DESC* instanceData;

	// COMPUTE STAGE

	DX12ResourceHandle* renderTarget;

	struct alignas(256)ToneMappingParams {
		ToneMappingParams() : stage(0), num_samples(1), white_point(config.whitepoint) {};
		UINT stage;
		float white_point;
		UINT num_samples;
	};
	ToneMappingParams* toneMappingParams;
	DX12ResourceHandle* toneMappingConstantBuffer;
	DX12ResourceHandle* maxLumBuffer;

	// SHARED

	DX12ResourceHandle* accumulationTexture;

	DX12ResourceHandle* randBuffer;
	std::vector<uint64_t> randPattern;


	UINT samples = 0;
	UINT frames = 0;
	UINT seed = 1;

	UINT frameIndexInFlight = 2;


	UINT width;
	UINT height;

	DXGI_SAMPLE_DESC NO_AA = { .Count = 1, .Quality = 0 };

	// inherited from the DX12Renderer -> DX12PathTracerPipeLine -> this
	HWND hwnd;
	IDXGIFactory4* factory;
	ID3D12Device5* d3dDevice;
	ID3D12CommandQueue* cmdQueue;

	// fence
	ID3D12Fence* fence;
	UINT64 fenceState = 1;

	// swap chain and uav
	IDXGISwapChain3* swapChain;

	// Command list and allocator

	ID3D12CommandAllocator* cmdAlloc; // block of memory
	ID3D12GraphicsCommandList4* cmdList;

	ID3D12RootSignature* rootSignature;

	ID3D12StateObject* raytracingPSO;
	ID3D12PipelineState* computePSO;

	// ray tracing shader tables
	ID3D12Resource* shaderIDs;

	// Heap Management
	DescriptorAllocator* global_descriptor_heap_allocator;
	//DescriptorAllocator* UAVClear_descriptor_heap_allocator;


	D3D12_HEAP_PROPERTIES UPLOAD_HEAP = { .Type = D3D12_HEAP_TYPE_UPLOAD };
	D3D12_HEAP_PROPERTIES DEFAULT_HEAP = { .Type = D3D12_HEAP_TYPE_DEFAULT };

	// imgui
	ID3D12DescriptorHeap* rtvHeap = nullptr;
	UINT rtvDescriptorSize = 0;


	float clearColor[4] = { 0.0f, 0.0f, 0.0f, 1.0f };


	// cpu non shader visible descriptor heap
	ID3D12DescriptorHeap* cpuDescHeap;

	// shaders
	ID3DBlob* rsBlob;
	ID3DBlob* csBlob;

	MeshManager* meshManager;
	MaterialManager* materialManager;
	EntityManager* entityManager;
};