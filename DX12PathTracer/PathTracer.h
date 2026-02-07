#pragma once

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <algorithm>     // For std::size, typed std::max, etc.
#include <DirectXMath.h> // For XMMATRIX
#include <Windows.h>     // To make a window, of course
#include <d3d12.h>       // The star of our show :)
#include <dxgi1_4.h>     // Needed to make the former two talk to each other
#include "shader.fxh"    // The compiled shader binary, ready to go

#pragma comment(lib, "user32") // For DefWindowProcW, etc.
#pragma comment(lib, "d3d12")  // You'll never guess this one
#pragma comment(lib, "dxgi")   // Another enigma

#include "MeshManager.h"
#include "MaterialManager.h"
#include "SceneManager.h"
#include "Vector.h"

class PathTracer {
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

	struct DX12SceneObject {
		DX12SceneObject() : sceneObject(nullptr), model(nullptr) {};

		SceneManager::SceneObject* sceneObject; // name, position, rotation
		DX12Model* model;
	};

	struct DX12Material {
		DirectX::XMFLOAT3A color;
		float roughness;
		float metallic;
		float ior;
		float transmission;
		float emission;
	};

	PathTracer() {}

	~PathTracer() {}

	void run();

	static LRESULT WINAPI WndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);

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
	void initTopLevel();
	void initDescriptors();
	void initRootSignature();
	void initPipeline();
	void initShaderTables();
	void updateScene();
	void render();

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
	//ID3D12DescriptorHeap* uavHeap;

	// render target
	ID3D12Resource* renderTarget;

	// Command list and allocator

	ID3D12CommandAllocator* cmdAlloc; // block of memory
	ID3D12GraphicsCommandList4* cmdList;

	// vertex, index buffers and SRVs
	ID3D12DescriptorHeap* descHeap;
	UINT descriptorIncrementSize;
	std::vector<ID3D12Resource*> allVertexBuffers;
	std::vector<ID3D12Resource*> allIndexBuffers;

	// scene object resources - including per model blas
	std::vector<DX12SceneObject*> dx12SceneObjects;
	std::unordered_map<std::string, DX12Model*> dx12Models;

	// acceleration structure
	ID3D12Resource* tlas;
	ID3D12Resource* tlasUpdateScratch;

	// instances
	UINT NUM_INSTANCES = 0;
	ID3D12Resource* instances;
	D3D12_RAYTRACING_INSTANCE_DESC* instanceData;

	// root signature
	ID3D12RootSignature* rootSignature;

	// ray tracing pso
	ID3D12StateObject* pso;

	// shader tables
	UINT64 NUM_SHADER_IDS = 3;
	ID3D12Resource* shaderIDs;

	// managers
	MeshManager* meshManager;
	MaterialManager* materialManager;
	SceneManager* sceneManager;

};
