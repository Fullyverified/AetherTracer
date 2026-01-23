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

class PathTracer {
public:

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
	void initMeshes();
	void initBottomLevel();
	void updateTransforms();
	void initScene();
	void initTopLevel();
	void initRootSignature();
	void initPipeline();
	void initShaderTables();
	void updateScene();
	void render();

	void quit();

	ID3D12Resource* makeAccelerationStructure(const D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS& inputs, UINT64* updateScratchSize = nullptr);
	ID3D12Resource* makeBLAS(ID3D12Resource* vertexBuffer, UINT vertexSize, ID3D12Resource* indexBuffer, UINT indicesSize);
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
	ID3D12DescriptorHeap* uavHeap;

	// render target
	ID3D12Resource* renderTarget;

	// Command list and allocator

	ID3D12CommandAllocator* cmdAlloc; // block of memory
	ID3D12GraphicsCommandList4* cmdList;

	// meshes

	std::vector<MeshManager::LoadedModel> loadedModels;

	// accel structures
	ID3D12Resource* quadBlas;
	ID3D12Resource* cubeBlas;
	std::vector<ID3D12Resource*> BLAS;

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

};

