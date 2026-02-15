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
#include "Window.h"

#include <imgui.h>
#include "imgui_impl_sdl3.h"
#include "imgui_impl_dx12.h"


class DX12Renderer {
public:

	struct UploadDefaultBufferPair {
		ID3D12Resource* HEAP_UPLOAD_BUFFER; // cpu
		ID3D12Resource* HEAP_DEFAULT_BUFFER; // gpu
	};
	DX12Renderer(EntityManager* entityManager, MeshManager* meshManager, MaterialManager* materialManager, Window* window);

	~DX12Renderer() {
		// imgui
		ImGui_ImplDX12_Shutdown();
		ImGui_ImplSDL3_Shutdown();
		ImGui::DestroyContext();

		if (rm->imguiSrvHeap) rm->imguiSrvHeap->Release();
		if (rm->rtvHeap) rm->rtvHeap->Release();
	}

	void init();
	void initDevice();
	void flush();
	void initSurfaces();
	void resize();
	void initCommand();

	void accumulationReset();
	
	void render();
	void present();

	void quit();

	void initImgui();
	void resizeImgui();
	void imguiRender();


	void checkHR(HRESULT hr, ID3DBlob* errorblob, std::string context);

	UploadDefaultBufferPair createBuffers(const void* data, size_t byteSize, D3D12_RESOURCE_STATES finalState);
	
	bool reset = false;

	// managers
	MeshManager* meshManager;
	MaterialManager* materialManager;
	EntityManager* entityManager;
	ResourceManager* rm;
	Window* window;

	ComputeStage* computeStage;
	RayTracingStage* raytracingStage;

};
