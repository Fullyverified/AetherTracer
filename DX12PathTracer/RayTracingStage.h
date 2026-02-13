#pragma once

#include "DirectXMath.h"
#include <d3d12.h>

#include "ResourceManager.h"
#include "EntityManager.h"
#include "MaterialManager.h"
#include "MeshManager.h"

class RayTracingStage {

public:

	RayTracingStage(ResourceManager* resourceManager, MeshManager* meshManager, MaterialManager* materialManager, EntityManager* entityManager, ID3D12GraphicsCommandList4* cmdList, ID3D12Device5* d3dDevice);
	~RayTracingStage() {};

	void init();
	void initAccumulationTexture();
	void initModelBuffers();
	void initModelBLAS();
	void updateTransforms();
	void updateCamera();
	void initScene();
	void initMaterialBuffer();
	void initTopLevel();
	void initVertexIndexBuffers();

	void loadShaders();

	void initRTDescriptors();
	void initRTRootSignature();
	void initRTPipeline();
	void initRTShaderTables();

	void traceRays();

	void checkHR(HRESULT hr, ID3DBlob* errorblob, std::string context);

	ResourceManager::Buffer* createBuffers(const void* data, size_t byteSize, D3D12_RESOURCE_STATES finalState);
	void createCBV(ResourceManager::Buffer* buffer, size_t byteSize);
	void pushBuffer(ResourceManager::Buffer* buffer, size_t dataSize, D3D12_RESOURCE_STATES state_before, D3D12_RESOURCE_STATES state_after);
	

	ID3D12Resource* makeAccelerationStructure(const D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS& inputs, UINT64* updateScratchSize = nullptr);
	ID3D12Resource* makeTLAS(ID3D12Resource* instances, UINT numInstances, UINT64* updateScratchSize);

	UINT NUM_INSTANCES = 0;

	// RT root signature and ray tracing pso
	ID3D12RootSignature* raytracingRootSignature;
	ID3D12StateObject* raytracingPSO;
	ID3D12DescriptorHeap* raytracingDescHeap;

	UINT descriptorIncrementSize;

	// shared
	ID3D12GraphicsCommandList4* cmdList;
	ID3D12Device5* d3dDevice;

	// shader tables
	UINT64 NUM_SHADER_IDS = 3;
	ID3D12Resource* shaderIDs;

	// shader loading
	ID3DBlob* rsBlob;

	ResourceManager* rm;
	MeshManager* meshManager;
	MaterialManager* materialManager;
	EntityManager* entityManager;

	// heap management
	D3D12_HEAP_PROPERTIES UPLOAD_HEAP = { .Type = D3D12_HEAP_TYPE_UPLOAD };
	D3D12_HEAP_PROPERTIES DEFAULT_HEAP = { .Type = D3D12_HEAP_TYPE_DEFAULT };
};

