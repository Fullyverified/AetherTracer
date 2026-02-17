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

struct ImGuiDescriptorAllocator {

	ID3D12DescriptorHeap* heap = nullptr;
	std::vector<int> freeIndices;
	UINT descriptorSize = 0;

	~ImGuiDescriptorAllocator() {
		if (heap) heap->Release();
	}


	void init(ID3D12Device* device, int capacity) {

		D3D12_DESCRIPTOR_HEAP_DESC desc = {};
		desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
		desc.NumDescriptors = capacity;
		desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
		device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&heap));

		descriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

		freeIndices.reserve(capacity);
		for (int n = 0; n < capacity; n++) {
			freeIndices.push_back(n);
		}

	}

	void alloc(D3D12_CPU_DESCRIPTOR_HANDLE* out_cpu, D3D12_GPU_DESCRIPTOR_HANDLE* out_gpu) {

		if (freeIndices.empty()) return;

		int idx = freeIndices.back();
		freeIndices.pop_back();

		D3D12_CPU_DESCRIPTOR_HANDLE cpu = heap->GetCPUDescriptorHandleForHeapStart();
		cpu.ptr += (SIZE_T)idx * descriptorSize;

		D3D12_GPU_DESCRIPTOR_HANDLE gpu = heap->GetGPUDescriptorHandleForHeapStart();
		gpu.ptr += (SIZE_T)idx * descriptorSize;


		*out_cpu = cpu;
		*out_gpu = gpu;
	}

	void free(D3D12_CPU_DESCRIPTOR_HANDLE cpu, D3D12_GPU_DESCRIPTOR_HANDLE gpu) {


		size_t offset = cpu.ptr - heap->GetCPUDescriptorHandleForHeapStart().ptr;
		int idx = (int)(offset / descriptorSize);
		freeIndices.push_back(idx);
	}
};

extern ImGuiDescriptorAllocator* ImGuiDescAlloc;

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
		
		if (rm->rtvHeap) rm->rtvHeap->Release();
		//delete ImGuiDescAlloc;
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

	void initImgui();
	void createBackBufferRTVs();
	void imguiPresent(ID3D12Resource* backBuffer);

	void quit();

	void checkHR(HRESULT hr, ID3DBlob* errorblob, std::string context);

	UploadDefaultBufferPair createBuffers(const void* data, size_t byteSize, D3D12_RESOURCE_STATES finalState);
	void barrier(ID3D12Resource* resource, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after);

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