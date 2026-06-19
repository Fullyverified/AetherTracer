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

#include "DX12ResourceManager.h"

#include "Vector.h"

class DX12MaterialManager {

public:

	struct DX12Material {

		// each UINT is an index to the texture
		UINT textureMap; // indices
		DirectX::XMFLOAT3 albedo;
		UINT roughnessMap;
		float roughness;
		UINT metallicMap;
		float metallic;
		float ior; // no map for IOR (unbounded value)
		float transmission;
		UINT emissionMap;
		float emission; // emission map value is [0, 1], multiply by actual emission value
		UINT normalMap;
		UINT displacementMap;

	};

	DX12MaterialManager(MeshManager* meshManager, MaterialManager* materialManager, EntityManager* entityManager, IDXGIFactory4* factory, ID3D12Device5* d3dDevice, ID3D12CommandQueue* cmdQueue, ID3D12CommandAllocator* cmdAlloc, ID3D12GraphicsCommandList4* cmdList);

	void initMaterials(bool new_material, bool changed_material, bool entity_material_changed);

	void initMaterialBuffers(bool is_update);

	void initEmissiveIndexBuffer(bool is_update);

	DX12ResourceHandle* createTextureFromVector(PT::Vector3 value);

	DX12ResourceHandle* createTextureFromImage(std::string name);

	void pushTexture(DX12ResourceHandle* texture_handle, D3D12_PLACED_SUBRESOURCE_FOOTPRINT tex_footprint, D3D12_RESOURCE_STATES state_before, D3D12_RESOURCE_STATES state_after, bool is_1x1);

	DX12ResourceHandle* createResourceHandle(const void* data, size_t byte_size, size_t max_size, D3D12_RESOURCE_STATES finalState, bool UAV);

	void pushResourceHandle(DX12ResourceHandle* resource_handle, size_t data_size, D3D12_RESOURCE_STATES state_before, D3D12_RESOURCE_STATES state_after);

	void updateResourceHandle(DX12ResourceHandle* resource_handle, const void* data, size_t byte_size, size_t max_size);


	// Utility
	void checkHR(HRESULT hr, ID3DBlob* errorblob, std::string context);
	void waitForGPU();

	DX12ResourceHandle* materialsBuffer;
	DX12ResourceHandle* materialIndexBuffer;

	std::vector<DX12Material> dx12materials;
	std::unordered_map<std::string, DX12Material*> dx12materials_map;
	std::unordered_map<std::string, UINT> dx12materials_indices_map;

	std::vector<UINT> material_indices;

	std::vector<DX12ResourceHandle*> texture_maps;
	std::unordered_map<std::string, UINT> texture_indices_map;

	std::vector<UINT> emissive_entities_indices;
	std::unordered_map<std::string, UINT> emissive_entities_map; // stores model name + material name as key

	std::vector<DX12ResourceHandle*> emissive_triangles_indices;
	DX12ResourceHandle* emissive_entities_indices_buffer;


	// inherited from the DX12Renderer -> DX12PathTracerPipeLine -> this
	IDXGIFactory4* factory;
	ID3D12Device5* d3dDevice;
	ID3D12CommandQueue* cmdQueue;
	ID3D12CommandAllocator* cmdAlloc; // block of memory
	ID3D12GraphicsCommandList4* cmdList;

	// fence
	ID3D12Fence* fence;
	UINT64 fenceState = 1;

	MeshManager* meshManager;
	MaterialManager* materialManager;
	EntityManager* entityManager;

	DXGI_SAMPLE_DESC NO_AA = { .Count = 1, .Quality = 0 };
	D3D12_HEAP_PROPERTIES UPLOAD_HEAP = { .Type = D3D12_HEAP_TYPE_UPLOAD };
	D3D12_HEAP_PROPERTIES DEFAULT_HEAP = { .Type = D3D12_HEAP_TYPE_DEFAULT };
};

