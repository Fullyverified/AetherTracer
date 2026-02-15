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
#include "ComputeStage.h"
#include "RayTracingStage.h"
#include "ResourceManager.h"

class DX12Renderer {
public:

	struct UploadDefaultBufferPair {
		ID3D12Resource* HEAP_UPLOAD_BUFFER; // cpu
		ID3D12Resource* HEAP_DEFAULT_BUFFER; // gpu
	};
	DX12Renderer(EntityManager* entityManager, MeshManager* meshManager, MaterialManager* materialManager);

	~DX12Renderer() {}

	void run();

	static LRESULT WINAPI WndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);

	void init(HWND hwnd);
	void initDevice();
	void flush();
	void initSurfaces(HWND hwnd);
	void resize(HWND hwnd);
	void initCommand();

	void accumulationReset();
	
	void render();
	void present();

	void quit();

	void checkHR(HRESULT hr, ID3DBlob* errorblob, std::string context);

	UploadDefaultBufferPair createBuffers(const void* data, size_t byteSize, D3D12_RESOURCE_STATES finalState);
	
	bool reset = false;

	// managers
	MeshManager* meshManager;
	MaterialManager* materialManager;
	EntityManager* entityManager;
	ResourceManager* rm;

	ComputeStage* computeStage;
	RayTracingStage* raytracingStage;

};
