#include "DX12Renderer.h"
#include <iostream>
#include <random>


bool debug = true;

DX12Renderer::DX12Renderer(EntityManager* entityManager, MeshManager* meshManager, MaterialManager* materialManager) : entityManager(entityManager), meshManager(meshManager), materialManager(materialManager) {

	rm = new ResourceManager();
	computeStage = new ComputeStage(rm, meshManager, materialManager, entityManager, cmdList, d3dDevice);
	raytracingStage = new RayTracingStage(rm, meshManager, materialManager, entityManager, cmdList, d3dDevice);

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
		TranslateMessage(&msg);
		DispatchMessageW(&msg);

		rm->num_frames++;
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

	raytracingStage->init();
	computeStage->init();

}

// device

void DX12Renderer::initDevice() {

	if (FAILED(CreateDXGIFactory2(DXGI_CREATE_FACTORY_DEBUG, IID_PPV_ARGS(&factory))))
		CreateDXGIFactory2(0, IID_PPV_ARGS(&factory));


	// D3D12 debug layer
	if (ID3D12Debug* debug;
		SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug))))
		debug->EnableDebugLayer(), debug->Release();

	// feature level dx12_2
	IDXGIAdapter* adapter = nullptr;
	D3D12CreateDevice(adapter, D3D_FEATURE_LEVEL_12_2, IID_PPV_ARGS(&d3dDevice));

	// command queue
	D3D12_COMMAND_QUEUE_DESC cmdQueueDesc = { .Type = D3D12_COMMAND_LIST_TYPE_DIRECT, };
	d3dDevice->CreateCommandQueue(&cmdQueueDesc, IID_PPV_ARGS(&cmdQueue));
	// fence
	d3dDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence));


	if (d3dDevice == nullptr) {
		std::cout << "device nullptr" << std::endl;
	}
	else std::cout << "device exists" << std::endl;
}


// cpu gpu syncronization

void DX12Renderer::flush() {
	static UINT64 value = 1;
	cmdQueue->Signal(fence, value);
	fence->SetEventOnCompletion(value++, nullptr);
}

// swap chain and uav

void DX12Renderer::initSurfaces(HWND hwnd) {


	// 8-bit SRGB
	// alternative: R16G16B16A16_FLOAT for HDR
	DXGI_SWAP_CHAIN_DESC1 scDesc = {
	   .Format = DXGI_FORMAT_R8G8B8A8_UNORM,
	   .SampleDesc = {},
	   .BufferCount = 2,
	   .SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD,
	};
	IDXGISwapChain1* swapChain1;

	HRESULT hr = factory->CreateSwapChainForHwnd(cmdQueue, hwnd, &scDesc, nullptr, nullptr, &swapChain1);
	checkHR(hr, nullptr, "Create swap chain: ");


	swapChain1->QueryInterface(&swapChain);
	swapChain1->Release();

	// early factory release
	factory->Release();

	resize(hwnd);
}

// render target
void DX12Renderer::resize(HWND hwnd) {

	std::cout << "Resize called" << std::endl;
	if (!swapChain) {
		std::cout << "Resize: swapChain is null - skipping" << std::endl;
		return;
	}

	RECT rect;
	GetClientRect(hwnd, &rect);
	auto width = std::max<UINT>(rect.right - rect.left, 1);
	auto height = std::max<UINT>(rect.bottom - rect.top, 1);

	flush();

	swapChain->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0);

	if (rm->accumulationTexture) rm->accumulationTexture->Release();
	if (rm->renderTarget) rm->renderTarget->Release();

	// Update render target and accumulation texture
	// BROKEN
	if (false) {

		UINT descriptorIncrementSize = d3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

		D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {
			.Format = DXGI_FORMAT_R8G8B8A8_UNORM,
			.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D };
		d3dDevice->CreateUnorderedAccessView(rm->accumulationTexture, nullptr, &uavDesc, raytracingStage->raytracingDescHeap->GetCPUDescriptorHandleForHeapStart());

		D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle = computeStage->computeDescHeap->GetCPUDescriptorHandleForHeapStart();

		D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		srvDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
		srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		srvDesc.Texture2D.MipLevels = 1;
		srvDesc.Texture2D.MostDetailedMip = 0;
		d3dDevice->CreateShaderResourceView(rm->accumulationTexture, &srvDesc, cpuHandle);
		cpuHandle.ptr += descriptorIncrementSize;
		

		uavDesc = {
			.Format = DXGI_FORMAT_R8G8B8A8_UNORM,
			.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D };
		d3dDevice->CreateUnorderedAccessView(rm->renderTarget, nullptr, &uavDesc, computeStage->computeDescHeap->GetCPUDescriptorHandleForHeapStart());

		
	}

	rm->randPattern.resize(rm->renderTarget->GetDesc().Width * rm->renderTarget->GetDesc().Height);
}

// command list and allocator

void DX12Renderer::initCommand() {
	// only one
	d3dDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&cmdAlloc));
	d3dDevice->CreateCommandList1(0, D3D12_COMMAND_LIST_TYPE_DIRECT, D3D12_COMMAND_LIST_FLAG_NONE, IID_PPV_ARGS(&cmdList));
}

// create default heap buffer
DX12Renderer::UploadDefaultBufferPair DX12Renderer::createBuffers(const void* data, size_t byteSize, D3D12_RESOURCE_STATES finalState) {
	std::cout << "byteSize: " << byteSize << std::endl;

	// CPU Buffer (upload buffer)
	DXGI_SAMPLE_DESC NO_AA = { .Count = 1, .Quality = 0 };
	D3D12_RESOURCE_DESC DESC = {
		.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER,
		.Width = byteSize,
		.Height = 1,
		.DepthOrArraySize = 1,
		.MipLevels = 1,
		.SampleDesc = NO_AA,
		.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR,
		.Flags = D3D12_RESOURCE_FLAG_NONE,
	};
	D3D12_HEAP_PROPERTIES UPLOAD_HEAP = { .Type = D3D12_HEAP_TYPE_UPLOAD };


	ID3D12Resource* upload;
	d3dDevice->CreateCommittedResource(
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
	d3dDevice->CreateCommittedResource(
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

	raytracingStage->traceRays();
	computeStage->postProcess();

}

void DX12Renderer::present() {

	// copy image onto swap chain's current buffer

	ID3D12Resource* backBuffer;
	swapChain->GetBuffer(swapChain->GetCurrentBackBufferIndex(), IID_PPV_ARGS(&backBuffer));

	auto barrier = [this](auto* resource, auto before, auto after) {

		D3D12_RESOURCE_BARRIER rb = {
		.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION,
		.Transition = {.pResource = resource,
						.StateBefore = before,
						.StateAfter = after},
		};

		cmdList->ResourceBarrier(1, &rb);

		};


	// transition render target and back buffer to copy src/dst states
	barrier(rm->renderTarget, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
	barrier(backBuffer, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_COPY_DEST);

	cmdList->CopyResource(backBuffer, rm->renderTarget);

	barrier(backBuffer, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PRESENT);
	barrier(rm->renderTarget, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

	// release refernce to backbuffer
	backBuffer->Release();

	cmdList->Close();
	ID3D12CommandList* lists[] = { cmdList };
	cmdQueue->ExecuteCommandLists(1, lists);

	flush();
	swapChain->Present(1, 0);

	// To finish whole frame??
	cmdAlloc->Reset();
	cmdList->Reset(cmdAlloc, nullptr);
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