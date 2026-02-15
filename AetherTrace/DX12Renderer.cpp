#include "DX12Renderer.h"
#include <iostream>
#include <random>


bool debug = true;

DX12Renderer::DX12Renderer(EntityManager* entityManager, MeshManager* meshManager, MaterialManager* materialManager) : entityManager(entityManager), meshManager(meshManager), materialManager(materialManager) {

	rm = new ResourceManager();

}


LRESULT WINAPI DX12Renderer::WndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
	// Store 'this' pointer during WM_NCCREATE
	if (msg == WM_NCCREATE) {
		auto* cs = reinterpret_cast<CREATESTRUCT*>(lparam);
		SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
		return TRUE;
	}

	// Retrieve 'this'
	DX12Renderer* self = reinterpret_cast<DX12Renderer*>(
		GetWindowLongPtr(hwnd, GWLP_USERDATA));

	switch (msg) {
	case WM_SIZE:
	case WM_SIZING:
		if (self)
			self->resize(hwnd);
		return 0;

	case WM_CLOSE:
		std::cout << "WM_CLOSE received\n";
		PostQuitMessage(0);
		return 0;

	case WM_DESTROY:
		std::cout << "WM_DESTROY received\n";
		PostQuitMessage(0);
		return 0;

	case WM_CREATE:
		std::cout << "WM_CREATE received\n";
		return 0;

	default:
		return DefWindowProcW(hwnd, msg, wparam, lparam);
	}
}

void DX12Renderer::run() {

	std::cout << "making window" << std::endl;

	SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
	// make a window
	WNDCLASSW wcw = { .lpfnWndProc = &WndProc,
				 .hCursor = LoadCursor(nullptr, IDC_ARROW),
				 .lpszClassName = L"DxrTutorialClass" };
	RegisterClassW(&wcw);
	HWND hwnd = CreateWindowExW(0, L"DxrTutorialClass", L"DXR tutorial", WS_VISIBLE | WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
		/*width=*/1200, /*height=*/1200, nullptr, nullptr, nullptr, this);

	if (!hwnd) {
		std::cerr << "CreateWindowExW failed: " << GetLastError() << std::endl;
		return;
	}

	std::cout << "Window created successfully, HWND = " << hwnd << std::endl;


	std::cout << "init(hwnd)" << std::endl;

	// initialize DirectX
	init(hwnd);

	if (debug) std::cout << "render loop" << std::endl;

	// run a msg loop until quit msg is received
	for (MSG msg;;) {
		while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
			if (msg.message == WM_QUIT)
				return;
			TranslateMessage(&msg);
			DispatchMessageW(&msg);
		}

		// start of cmdList
		render();
		present();
		// end of cmdList
	}


}

void DX12Renderer::init(HWND hwnd) {

	initDevice();
	initSurfaces(hwnd);
	initCommand();

	computeStage = new ComputeStage(rm, meshManager, materialManager, entityManager);
	raytracingStage = new RayTracingStage(rm, meshManager, materialManager, entityManager);

	rm->dx12Camera = new ResourceManager::DX12Camera{};

	resize(hwnd);

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
	if (ID3D12Debug* debug;
		SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug))))
		debug->EnableDebugLayer(), debug->Release();

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

void DX12Renderer::initSurfaces(HWND hwnd) {

	// 8-bit SRGB
	// alternative: R16G16B16A16_FLOAT for HDR
	DXGI_SWAP_CHAIN_DESC1 scDesc = {
	   .Format = DXGI_FORMAT_R8G8B8A8_UNORM,
	   .SampleDesc = rm->NO_AA,
	   .BufferCount = 2,
	   .SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD,
	};
	IDXGISwapChain1* swapChain1;

	HRESULT hr = rm->factory->CreateSwapChainForHwnd(rm->cmdQueue, hwnd, &scDesc, nullptr, nullptr, &swapChain1);
	checkHR(hr, nullptr, "Create swap chain: ");


	swapChain1->QueryInterface(&rm->swapChain);
	swapChain1->Release();

	// early factory release
	rm->factory->Release();

}

// render target
void DX12Renderer::resize(HWND hwnd) {

	std::cout << "Resize called" << std::endl;
	if (!rm->swapChain) {
		std::cout << "Resize: swapChain is null - skipping" << std::endl;
		return;
	}

	RECT rect;
	GetClientRect(hwnd, &rect);
	rm->width = std::max<UINT>(rect.right - rect.left, 1);
	rm->height = std::max<UINT>(rect.bottom - rect.top, 1);

	flush();

	rm->swapChain->ResizeBuffers(0, rm->width, rm->height, DXGI_FORMAT_UNKNOWN, 0);

	// Update render target and accumulation texture
	
	if (rm->renderTarget) {
		rm->randPattern.resize(rm->width * rm->height);
	}
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

	rm->num_frames++;
	raytracingStage->updateCamera();
	computeStage->updateToneParams();
	raytracingStage->traceRays();
	computeStage->postProcess();

}

void DX12Renderer::present() {

	// copy image onto swap chain's current buffer

	ID3D12Resource* backBuffer;
	rm->swapChain->GetBuffer(rm->swapChain->GetCurrentBackBufferIndex(), IID_PPV_ARGS(&backBuffer));
	backBuffer->SetName(L"Back Buffer");

	auto barrier = [this](auto* resource, auto before, auto after) {

		D3D12_RESOURCE_BARRIER rb = {
		.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION,
		.Transition = {.pResource = resource,
						.StateBefore = before,
						.StateAfter = after},
		};

		rm->cmdList->ResourceBarrier(1, &rb);

		};


	// transition render target and back buffer to copy src/dst states
	barrier(rm->renderTarget, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
	barrier(backBuffer, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_COPY_DEST);

	rm->cmdList->CopyResource(backBuffer, rm->renderTarget);

	barrier(backBuffer, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PRESENT);
	barrier(rm->renderTarget, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

	// release refernce to backbuffer
	backBuffer->Release();

	rm->cmdList->Close();
	ID3D12CommandList* lists[] = { rm->cmdList };
	rm->cmdQueue->ExecuteCommandLists(1, lists);

	flush();
	rm->swapChain->Present(1, 0);

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