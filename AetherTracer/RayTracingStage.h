#pragma once

#include "DirectXMath.h"
#include <d3d12.h>

#include "ResourceManager.h"
#include "EntityManager.h"
#include "MaterialManager.h"
#include "MeshManager.h"

class RayTracingStage {

public:

	RayTracingStage(ResourceManager* resourceManager, MeshManager* meshManager, MaterialManager* materialManager, EntityManager* entityManager);
	~RayTracingStage() {};

	void loadShaders();
	void initAccumulationTexture();
	void initModelBuffers();
	void initModelBLAS();
	void updateTransforms();
	void updateCamera();
	void initScene();
	void initMaterialBuffer();
	void initTopLevelAS();
	void initVertexIndexBuffers();


	void initStage();
	void initRTDescriptors();
	void initRTRootSignature();
	void initRTPipeline();
	void initRTShaderTables();

	void initCPUDescriptor();

	void initClearDescriptorHeap();

	void traceRays();

	void checkHR(HRESULT hr, ID3DBlob* errorblob, std::string context);
	void flush();

	ResourceManager::Buffer* createBuffers(const void* data, size_t byteSize, D3D12_RESOURCE_STATES finalState, bool UAV);
	void createCBV(ResourceManager::Buffer* buffer, size_t byteSize);
	void pushBuffer(ResourceManager::Buffer* buffer, size_t dataSize, D3D12_RESOURCE_STATES state_before, D3D12_RESOURCE_STATES state_after);
	

	ID3D12Resource* makeAccelerationStructure(const D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS& inputs, UINT64* updateScratchSize = nullptr);

	// RT root signature and ray tracing pso
	ID3D12RootSignature* raytracingRootSignature;
	ID3D12StateObject* raytracingPSO;
	ID3D12DescriptorHeap* raytracingDescHeap;
	ID3D12DescriptorHeap* cpuDescHeap;


	UINT descriptorIncrementSize;

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

