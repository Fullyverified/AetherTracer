#pragma once

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <algorithm>
#include <DirectXMath.h> // For XMMATRIX
#include <Windows.h>     // To make a window, of course
#include <d3d12.h>
#include <dxgi1_4.h>

#include <d3dcompiler.h> // for compiling shaders

#pragma comment(lib, "user32") // For DefWindowProcW, etc.
#pragma comment(lib, "d3d12")
#pragma comment(lib, "dxgi")

#include "MeshManager.h"
#include "MaterialManager.h"
#include "EntityManager.h"
#include "Vector.h"

class DX12PathTracer {
public:

	struct VertexIndexBuffers {
		std::vector<ID3D12Resource*> vertexUploadBuffers;
		std::vector<ID3D12Resource*> indexUploadBuffers;
		std::vector<ID3D12Resource*> vertexDefaultBuffers;
		std::vector<ID3D12Resource*> indexDefaultBuffers;
	};

	struct UploadDefaultBufferPair {
		ID3D12Resource* HEAP_UPLOAD_BUFFER; // cpu
		ID3D12Resource* HEAP_DEFAULT_BUFFER; // gpu
	};

	struct DX12Model {
		DX12Model() : loadedModel(nullptr), BLAS(nullptr), modelBuffers(nullptr) {};

		MeshManager::LoadedModel* loadedModel; // mesh and raw vertex data
		ID3D12Resource* BLAS;
		VertexIndexBuffers* modelBuffers; // DX12 buffers - Upload refers to CPU, Default refers to GPU
	};

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
		float pad1;

	};

	DX12PathTracer(EntityManager* entityManager, MeshManager* meshManager, MaterialManager* materialManager);

	~DX12PathTracer() {}

	void run();

	static LRESULT WINAPI WndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);

	void loadShaders();

	void init(HWND hwnd);
	void initDevice();
	void flush();
	void initSurfaces(HWND hwnd);
	void resize(HWND hwnd);
	void initCommand();
	void initModelBuffers();
	void initModelBLAS();
	void updateTransforms();
	void initScene();
	void initMaterialBuffer();
	void initTopLevel();

	void initRTDescriptors();
	void initRTRootSignature();
	void initRTPipeline();
	void initRTShaderTables();

	void initComputeRootSignature();
	void initComputePipeline();
	void initComputeDescriptors();

	void updateCamera();
	void updateToneParams();
	void updateScene();
	void render();

	void postProcess();
	void present();

	void quit();

	void checkHR(HRESULT hr, ID3DBlob* errorblob, std::string context);

	UploadDefaultBufferPair createBuffers(const void* data, size_t byteSize, D3D12_RESOURCE_STATES finalState);
	ID3D12Resource* makeAccelerationStructure(const D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS& inputs, UINT64* updateScratchSize = nullptr);
	ID3D12Resource* makeTLAS(ID3D12Resource* instances, UINT numInstances, UINT64* updateScratchSize);

	DXGI_SAMPLE_DESC NO_AA = { .Count = 1, .Quality = 0 };
	D3D12_HEAP_PROPERTIES UPLOAD_HEAP = { .Type = D3D12_HEAP_TYPE_UPLOAD };
	D3D12_HEAP_PROPERTIES DEFAULT_HEAP = { .Type = D3D12_HEAP_TYPE_DEFAULT };
	D3D12_RESOURCE_DESC BASIC_BUFFER_DESC = {
		.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER,
		.Width = 0, // will be changed in copies
		.Height = 1,
		.DepthOrArraySize = 1,
		.MipLevels = 1,
		.SampleDesc = NO_AA,
		.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR };

	// device init
	IDXGIFactory4* factory;
	ID3D12Device5* d3dDevice;
	ID3D12CommandQueue* cmdQueue;
	ID3D12Fence* fence;

	// swap chain and uav
	IDXGISwapChain3* swapChain;

	// render target
	ID3D12Resource* renderTarget;

	// accumulation texture
	ID3D12Resource* accumulationTexture;
	UINT numFrames = 0;
	bool cameraMoved = false;

	// Command list and allocator

	ID3D12CommandAllocator* cmdAlloc; // block of memory
	ID3D12GraphicsCommandList4* cmdList;

	// vertex, index, material buffers for SRVs
	UINT descriptorIncrementSize;
	std::vector<ID3D12Resource*> allVertexBuffers;
	std::vector<ID3D12Resource*> allIndexBuffers;

	ID3D12Resource* materialDefaultBuffer;
	std::vector<DX12Material> dx12Materials;
	ID3D12Resource* materialIndexDefaultBuffer;
	std::vector<uint32_t> materialIndices;
	ID3D12Resource* cameraConstantBuffer;

	// scene object resources - including per model blas
	std::vector<DX12Entity*> dx12Entitys;
	std::unordered_map<std::string, DX12Model*> dx12Models;
	std::unordered_map<std::string, DX12Material*> materials;
	std::vector<uint32_t> materialIndex;
	DX12Camera* dx12Camera;

	// acceleration structure
	ID3D12Resource* tlas;
	ID3D12Resource* tlasUpdateScratch;

	// instances
	UINT NUM_INSTANCES = 0;
	ID3D12Resource* instances;
	D3D12_RAYTRACING_INSTANCE_DESC* instanceData;
	std::unordered_map<std::string, uint32_t> uniqueInstancesID;
	std::unordered_set<std::string> uniqueInstances;

	// RT root signature and ray tracing pso
	ID3D12RootSignature* raytracingRootSignature;
	ID3D12StateObject* raytracingPSO;
	ID3D12DescriptorHeap* raytracingDescHeap;

	// shader tables
	UINT64 NUM_SHADER_IDS = 3;
	ID3D12Resource* shaderIDs;

	// Compute root signature and ray tracing pso
	ID3D12RootSignature* computeRootSignature;
	ID3D12PipelineState* computePSO;
	ID3D12DescriptorHeap* computeDescHeap;

	struct alignas(256)ToneMappingParams {
		ToneMappingParams() : maxLum(20.0f), numIts(1.0f), exposure(1.0f) {};
		float maxLum;
		float exposure;
		UINT numIts;
	};

	ToneMappingParams* toneMappingParams;
	ID3D12Resource* toneMappingConstantBuffer;

	// shader compilation
	ID3DBlob* rsBlob;
	ID3DBlob* csBlob;

	// managers
	MeshManager* meshManager;
	MaterialManager* materialManager;
	EntityManager* entityManager;

};
