#pragma once

#include "DirectXMath.h"
#include <d3d12.h>

#include "ResourceManager.h"
#include "EntityManager.h"
#include "MaterialManager.h"
#include "MeshManager.h"

class ComputeStage {

public:

	ComputeStage(ResourceManager* resourceManager, MeshManager* meshManager, MaterialManager* materialManager, EntityManager* entityManager, ID3D12GraphicsCommandList4* cmdList, ID3D12Device5* d3dDevice);
	~ComputeStage() {};

	void init();
	void initComputeRootSignature();
	void initComputePipeline();
	void initComputeDescriptors();

	void loadShaders();

	void initRenderTarget();
	void updateRand();
	void updateToneParams();

	void postProcess();

	void checkHR(HRESULT hr, ID3DBlob* errorblob, std::string context);

	ResourceManager::Buffer* createBuffers(const void* data, size_t byteSize, D3D12_RESOURCE_STATES finalState);
	void createCBV(ResourceManager::Buffer* buffer, size_t byteSize);
	void pushBuffer(ResourceManager::Buffer* buffer, size_t dataSize, D3D12_RESOURCE_STATES state_before, D3D12_RESOURCE_STATES state_after);

	// Compute root signature and ray tracing pso
	ID3D12RootSignature* computeRootSignature;
	ID3D12PipelineState* computePSO;
	ID3D12DescriptorHeap* computeDescHeap;

	UINT descriptorIncrementSize;

	// shared
	ID3D12GraphicsCommandList4* cmdList;
	ID3D12Device5* d3dDevice;

	// shader loading
	ID3DBlob* csBlob;

	ResourceManager* rm;
	MeshManager* meshManager;
	MaterialManager* materialManager;
	EntityManager* entityManager;

	// heap management
	D3D12_HEAP_PROPERTIES UPLOAD_HEAP = { .Type = D3D12_HEAP_TYPE_UPLOAD };
	D3D12_HEAP_PROPERTIES DEFAULT_HEAP = { .Type = D3D12_HEAP_TYPE_DEFAULT };
};

