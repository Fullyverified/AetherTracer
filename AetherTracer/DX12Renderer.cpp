#include "DX12Renderer.h"
#include <iostream>
#include <random>
#include "Config.h"

bool debug = true;
ImGuiDescriptorAllocator* ImGuiDescAlloc;

// for imgui
void ImGuiDX12AllocateSRV(ImGui_ImplDX12_InitInfo* init_info_unused, D3D12_CPU_DESCRIPTOR_HANDLE* out_cpu, D3D12_GPU_DESCRIPTOR_HANDLE* out_gpu) {

	(void)init_info_unused;  // Not used in most cases

	return ImGuiDescAlloc->alloc(out_cpu, out_gpu);
}

// for imgui
void ImGuiDX12FreeSRV(ImGui_ImplDX12_InitInfo* init_info_unused, D3D12_CPU_DESCRIPTOR_HANDLE cpu, D3D12_GPU_DESCRIPTOR_HANDLE gpu) {
	(void)init_info_unused;
	ImGuiDescAlloc->free(cpu, gpu);
}

DX12Renderer::DX12Renderer(EntityManager* entityManager, MeshManager* meshManager, MaterialManager* materialManager, Window* window) : entityManager(entityManager), meshManager(meshManager), materialManager(materialManager), window(window) {

	rm = new ResourceManager();

}

void DX12Renderer::init() {

	rm->hwnd = static_cast<HWND>(window->getNativeHandle());

	initDevice();
	initSurfaces();
	initCommand();

	initImgui();

	computeStage = new ComputeStage(rm, meshManager, materialManager, entityManager);
	raytracingStage = new RayTracingStage(rm, meshManager, materialManager, entityManager);

	rm->dx12Camera = new ResourceManager::DX12Camera{};

	resize();

	std::cout << "init RTResources" << std::endl;

	raytracingStage->loadShaders();
	raytracingStage->updateCamera();
	raytracingStage->initAccumulationTexture();
	raytracingStage->initModelBuffers();

	rm->cmdList->Close();
	rm->cmdQueue->ExecuteCommandLists(1, reinterpret_cast<ID3D12CommandList**>(&rm->cmdList));
	flush();
	rm->cmdAlloc->Reset();
	rm->cmdList->Reset(rm->cmdAlloc, nullptr);

	raytracingStage->initModelBLAS();


	raytracingStage->initScene();
	raytracingStage->initTopLevelAS();
	raytracingStage->initMaterialBuffer();
	raytracingStage->initVertexIndexBuffers();
	raytracingStage->updateTransforms();

	std::cout << "init ComputeResources" << std::endl;
	computeStage->loadShaders();
	computeStage->initRenderTarget();
	computeStage->updateRand();
	computeStage->updateToneParams();
	computeStage->initMaxLumBuffer();

	std::cout << "raytracingStage->initStage();" << std::endl;
	raytracingStage->initStage();
	std::cout << "computeStage->initStage();" << std::endl;
	computeStage->initStage();

	rm->cmdList->Close();
	rm->cmdQueue->ExecuteCommandLists(1, reinterpret_cast<ID3D12CommandList**>(&rm->cmdList));
	flush();

	rm->cmdAlloc->Reset();
	rm->cmdList->Reset(rm->cmdAlloc, nullptr);

}

// device

void DX12Renderer::initDevice() {

	if (FAILED(CreateDXGIFactory2(DXGI_CREATE_FACTORY_DEBUG, IID_PPV_ARGS(&rm->factory))))
		CreateDXGIFactory2(0, IID_PPV_ARGS(&rm->factory));


	// D3D12 debug layer
	if (ID3D12Debug* debug; SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)))) {
		debug->EnableDebugLayer();
		debug->Release();
	}

	/*if (ID3D12Debug1* debug1; SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug1)))) {
		debug1->SetEnableGPUBasedValidation(true);
		debug1->Release();
	}*/

	// feature level dx12_2
	IDXGIAdapter* adapter = nullptr;
	D3D12CreateDevice(adapter, D3D_FEATURE_LEVEL_12_2, IID_PPV_ARGS(&rm->d3dDevice));

	// command queue
	D3D12_COMMAND_QUEUE_DESC cmdQueueDesc = { .Type = D3D12_COMMAND_LIST_TYPE_DIRECT, };
	rm->d3dDevice->CreateCommandQueue(&cmdQueueDesc, IID_PPV_ARGS(&rm->cmdQueue));
	// fence
	rm->d3dDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&rm->fence));


	if (rm->d3dDevice == nullptr) {
		std::cout << "device nullptr" << std::endl;
	}
	else std::cout << "device exists" << std::endl;
}


// cpu gpu syncronization

void DX12Renderer::flush() {
	rm->cmdQueue->Signal(rm->fence, rm->fenceState);
	rm->fence->SetEventOnCompletion(rm->fenceState++, nullptr);
}

// swap chain

void DX12Renderer::initSurfaces() {

	// 8-bit SRGB
	// alternative: R16G16B16A16_FLOAT for HDR
	DXGI_SWAP_CHAIN_DESC1 scDesc = {
	   .Format = DXGI_FORMAT_R8G8B8A8_UNORM,
	   .SampleDesc = rm->NO_AA,
	   .BufferCount = 2,
	   .SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD,
	   .Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING,
	};
	IDXGISwapChain1* swapChain1;

	HRESULT hr = rm->factory->CreateSwapChainForHwnd(rm->cmdQueue, rm->hwnd, &scDesc, nullptr, nullptr, &swapChain1);
	checkHR(hr, nullptr, "Create swap chain: ");


	swapChain1->QueryInterface(&rm->swapChain);
	swapChain1->Release();

	// early factory release
	rm->factory->Release();

}

// render target
void DX12Renderer::resize() {

	std::cout << "Resize called" << std::endl;
	if (!rm->swapChain) {
		std::cout << "Resize: swapChain is null - skipping" << std::endl;
		return;
	}

	RECT rect;
	GetClientRect(rm->hwnd, &rect);
	rm->width = std::max<UINT>(rect.right - rect.left, 1);
	rm->height = std::max<UINT>(rect.bottom - rect.top, 1);

	flush();

	rm->swapChain->ResizeBuffers(0, rm->width, rm->height, DXGI_FORMAT_UNKNOWN, 0);

	// Update render target and accumulation texture
	
	if (rm->renderTarget) {
		rm->randPattern.resize(rm->width * rm->height);
	}

	
	createBackBufferRTVs();

	if (rm->renderTarget) {
		rm->randPattern.resize(rm->width * rm->height);
	}

	std::cout << "resize" << std::endl;
}

// command list and allocator

void DX12Renderer::initCommand() {
	// only one
	rm->d3dDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&rm->cmdAlloc));
	rm->d3dDevice->CreateCommandList1(0, D3D12_COMMAND_LIST_TYPE_DIRECT, D3D12_COMMAND_LIST_FLAG_NONE, IID_PPV_ARGS(&rm->cmdList));

	rm->cmdAlloc->SetName(L"Default cmdAlloc");
	rm->cmdList->SetName(L"Default cmdList");

	rm->cmdAlloc->Reset();
	rm->cmdList->Reset(rm->cmdAlloc, nullptr);
}

// create default heap buffer
DX12Renderer::UploadDefaultBufferPair DX12Renderer::createBuffers(const void* data, size_t byteSize, D3D12_RESOURCE_STATES finalState) {
	std::cout << "byteSize: " << byteSize << std::endl;

	// CPU Buffer (upload buffer)
	D3D12_RESOURCE_DESC DESC = {
		.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER,
		.Width = byteSize,
		.Height = 1,
		.DepthOrArraySize = 1,
		.MipLevels = 1,
		.SampleDesc = rm->NO_AA,
		.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR,
		.Flags = D3D12_RESOURCE_FLAG_NONE,
	};
	D3D12_HEAP_PROPERTIES UPLOAD_HEAP = { .Type = D3D12_HEAP_TYPE_UPLOAD };


	ID3D12Resource* upload;
	rm->d3dDevice->CreateCommittedResource(
		&UPLOAD_HEAP,
		D3D12_HEAP_FLAG_NONE,
		&DESC,
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(&upload)
	);

	void* mapped;
	upload->Map(0, nullptr, &mapped); // mapped now points to the upload buffer
	memcpy(mapped, data, byteSize); // copy data to upload buffer
	upload->Unmap(0, nullptr); // 7

	// VRAM Buffer

	D3D12_HEAP_PROPERTIES DEFAULT_HEAP = { .Type = D3D12_HEAP_TYPE_DEFAULT };

	// Create target buffer in DEFAULT heap
	DESC.Flags = D3D12_RESOURCE_FLAG_NONE; // or ALLOW_UNORDERED_ACCESS if needed later
	ID3D12Resource* target = nullptr;
	rm->d3dDevice->CreateCommittedResource(
		&DEFAULT_HEAP,
		D3D12_HEAP_FLAG_NONE,
		&DESC,
		D3D12_RESOURCE_STATE_COMMON,
		nullptr,
		IID_PPV_ARGS(&target)
	);

	return { upload, target };
}


void DX12Renderer::accumulationReset() {

	if (reset) {
		//updateRand();

		// reset accumulationTexutre
	}
	
}

// command submission

void DX12Renderer::render() {

	raytracingStage->updateCamera();
	raytracingStage->traceRays();
	computeStage->postProcess();

	rm->num_frames = (config.accumulate & !entityManager->camera->camMoved) ? rm->num_frames + config.raysPerPixel : 1;
	rm->seed++;

	ImGui::Render();
}

void DX12Renderer::imguiPresent(ID3D12Resource* backBuffer) {
	// set descriptor heap for imgui
	ID3D12DescriptorHeap* heaps[] = { ImGuiDescAlloc->heap };
	rm->cmdList->SetDescriptorHeaps(ARRAYSIZE(heaps), heaps);

	// Get current backbuffer RTV
	UINT bbIndex = rm->swapChain->GetCurrentBackBufferIndex();
	D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = rm->rtvHeap->GetCPUDescriptorHandleForHeapStart();
	rtvHandle.ptr += bbIndex * rm->rtvDescriptorSize;

	rm->cmdList->OMSetRenderTargets(1, &rtvHandle, FALSE, nullptr);

	ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), rm->cmdList);
}

void DX12Renderer::present() {

	// copy image onto swap chain's current buffer
	ID3D12Resource* backBuffer;
	rm->swapChain->GetBuffer(rm->swapChain->GetCurrentBackBufferIndex(), IID_PPV_ARGS(&backBuffer));
	backBuffer->SetName(L"Back Buffer");

	// transition render target and back buffer to copy src/dst states
	barrier(rm->renderTarget, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
	barrier(backBuffer, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_COPY_DEST);

	rm->cmdList->CopyResource(backBuffer, rm->renderTarget);

	barrier(backBuffer, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_RENDER_TARGET);

	imguiPresent(backBuffer);

	barrier(backBuffer, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);

	// transition for present

	// transition for next frame
	barrier(rm->renderTarget, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

	// release refernce to backbuffer
	backBuffer->Release();

	rm->cmdList->Close();
	ID3D12CommandList* lists[] = { rm->cmdList };
	rm->cmdQueue->ExecuteCommandLists(1, lists);

	flush();
	rm->swapChain->Present(0, DXGI_PRESENT_ALLOW_TEARING);

	// To finish whole frame??
	rm->cmdAlloc->Reset();
	rm->cmdList->Reset(rm->cmdAlloc, nullptr);
}

void DX12Renderer::quit() {
	delete meshManager;
}

void DX12Renderer::checkHR(HRESULT hr, ID3DBlob* errorblob, std::string context) {
	if (FAILED(hr)) {
		std::cerr << context << "HRESULT 0x" << std::hex << hr << std::endl;
		if (hr == E_OUTOFMEMORY) std::cerr << "(Out of memory?)\n";
		else if (hr == E_INVALIDARG) std::cerr << "(Invalid arg — check desc)\n";
	}
	if (errorblob) {
		OutputDebugStringA((char*)errorblob->GetBufferPointer());
		errorblob->Release();
	}
}

void DX12Renderer::barrier(ID3D12Resource* resource, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after) {

	D3D12_RESOURCE_BARRIER rb = {
		.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION,
		.Transition = {.pResource = resource,
						.StateBefore = before,
						.StateAfter = after},
	};

	rm->cmdList->ResourceBarrier(1, &rb);

}

void DX12Renderer::initImgui() {

	// thanks grok

	ImGuiDescAlloc = new ImGuiDescriptorAllocator{};
	ImGuiDescAlloc->init(rm->d3dDevice, 64);

	// for swap chain
	D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
	rtvHeapDesc.NumDescriptors = rm->frameIndexInFlight;
	rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
	rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	rm->d3dDevice->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&rm->rtvHeap));
	rm->rtvHeap->SetName(L"ImGui RTV Heap");
	rm->rtvDescriptorSize = rm->d3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

	// ImGui init
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO();
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
	ImGui_ImplSDL3_InitForD3D(window->getSDLHandle());

	ImGui_ImplDX12_InitInfo init_info = {};
	init_info.Device = rm->d3dDevice;
	init_info.CommandQueue = rm->cmdQueue;
	init_info.NumFramesInFlight = rm->frameIndexInFlight;
	init_info.RTVFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
	init_info.DSVFormat = DXGI_FORMAT_UNKNOWN;
	init_info.SrvDescriptorHeap = ImGuiDescAlloc->heap;
	init_info.SrvDescriptorAllocFn = ImGuiDX12AllocateSRV;
	init_info.SrvDescriptorFreeFn = ImGuiDX12FreeSRV;

	ImGui_ImplDX12_Init(&init_info);
	ImGui::StyleColorsDark();
	io.Fonts->AddFontDefault();
	io.Fonts->Build();

	createBackBufferRTVs();
}

// for imgui
void DX12Renderer::createBackBufferRTVs() {

	D3D12_CPU_DESCRIPTOR_HANDLE rtvStart = rm->rtvHeap->GetCPUDescriptorHandleForHeapStart();

	for (UINT i = 0; i < rm->frameIndexInFlight; i++) {
		ID3D12Resource* backBuffer = nullptr;
		HRESULT hr = rm->swapChain->GetBuffer(i, IID_PPV_ARGS(&backBuffer));
		checkHR(hr, nullptr, "ImGui Backbuffer SRV");

		D3D12_CPU_DESCRIPTOR_HANDLE handle = rtvStart;
		handle.ptr += i * rm->rtvDescriptorSize;

		rm->d3dDevice->CreateRenderTargetView(backBuffer, nullptr, handle);

		backBuffer->Release();
	}

	ImGui_ImplDX12_InvalidateDeviceObjects();
	ImGui_ImplDX12_CreateDeviceObjects();

}