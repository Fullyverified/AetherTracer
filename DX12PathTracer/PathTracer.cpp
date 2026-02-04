#include "PathTracer.h"
#include <iostream>
bool debug = true;

LRESULT WINAPI PathTracer::WndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
	// Store 'this' pointer during WM_NCCREATE
	if (msg == WM_NCCREATE) {
		auto* cs = reinterpret_cast<CREATESTRUCT*>(lparam);
		SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
		return TRUE;
	}

	// Retrieve 'this'
	PathTracer* self = reinterpret_cast<PathTracer*>(
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

void PathTracer::run() {

	std::cout << "making window" << std::endl;

	SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
	// make a window
	WNDCLASSW wcw = { .lpfnWndProc = &WndProc,
				 .hCursor = LoadCursor(nullptr, IDC_ARROW),
				 .lpszClassName = L"DxrTutorialClass" };
	RegisterClassW(&wcw);
	HWND hwnd = CreateWindowExW(0, L"DxrTutorialClass", L"DXR tutorial", WS_VISIBLE | WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
		/*width=*/CW_USEDEFAULT, /*height=*/CW_USEDEFAULT, nullptr, nullptr, nullptr, this);

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

		render(); // Render the next frame
	}


}

void PathTracer::init(HWND hwnd) {
	initDevice();
	initSurfaces(hwnd);
	initCommand();

	meshManager = new MeshManager(d3dDevice, cmdList);

	initMeshes();
	initBottomLevel();
	initScene();
	initTopLevel();
	initDescriptors();
	initRootSignature();
	initPipeline();
	initShaderTables();
}

// device

void PathTracer::initDevice() {

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

void PathTracer::flush() {
	static UINT64 value = 1;
	cmdQueue->Signal(fence, value);
	fence->SetEventOnCompletion(value++, nullptr);
}

// swap chain and uav

void PathTracer::initSurfaces(HWND hwnd) {


	// 8-bit SRGB
	// alternative: R16G16B16A16_FLOAT for HDR
	DXGI_SWAP_CHAIN_DESC1 scDesc = {
	   .Format = DXGI_FORMAT_R8G8B8A8_UNORM,
	   .SampleDesc = NO_AA,
	   .BufferCount = 2,
	   .SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD,
	};
	IDXGISwapChain1* swapChain1;

	HRESULT hr = factory->CreateSwapChainForHwnd(cmdQueue, hwnd, &scDesc, nullptr, nullptr, &swapChain1);
	checkHR(hr, "Create swap chain: ");


	swapChain1->QueryInterface(&swapChain);
	swapChain1->Release();

	// early factory release
	factory->Release();

	resize(hwnd);
}

// render target
void PathTracer::resize(HWND hwnd) {

	std::cout << "Resize called" << std::endl;
	if (!swapChain) {
		std::cout << "Resize: swapChain is null - skipping" << std::endl;
		return;
	}

	if (!swapChain) [[unlikely]] return;


	RECT rect;
	GetClientRect(hwnd, &rect);
	auto width = std::max<UINT>(rect.right - rect.left, 1);
	auto height = std::max<UINT>(rect.bottom - rect.top, 1);

	flush();

	swapChain->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0);

	if (renderTarget) [[likely]] renderTarget->Release();

	D3D12_RESOURCE_DESC rtDesc = {
	   .Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D,
	   .Width = width,
	   .Height = height,
	   .DepthOrArraySize = 1,
	   .MipLevels = 1,
	   .Format = DXGI_FORMAT_R8G8B8A8_UNORM,
	   .SampleDesc = NO_AA,
	   .Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS };
	d3dDevice->CreateCommittedResource(&DEFAULT_HEAP, D3D12_HEAP_FLAG_NONE, &rtDesc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(&renderTarget));

	if (descHeap) {
	
		D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {
			.Format = DXGI_FORMAT_R8G8B8A8_UNORM,
			.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D };
		d3dDevice->CreateUnorderedAccessView(renderTarget, nullptr, &uavDesc, descHeap->GetCPUDescriptorHandleForHeapStart());

	}

}

// command list and allocator

void PathTracer::initCommand() {
	// only one
	d3dDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&cmdAlloc));
	d3dDevice->CreateCommandList1(0, D3D12_COMMAND_LIST_TYPE_DIRECT, D3D12_COMMAND_LIST_FLAG_NONE, IID_PPV_ARGS(&cmdList));
}

// meshes

void PathTracer::initMeshes() {

	loadedModels.emplace_back(meshManager->loadFromObject("assets/meshes/companionCubeLow.obj", Vector3{-1.5, 0, 0}, Vector3(0, 0, 0)));
	loadedModels.emplace_back(meshManager->loadFromObject("assets/meshes/companionCubeLow.obj", Vector3{0, 0, 0 }, Vector3(0, 0, 0)));
	loadedModels.emplace_back(meshManager->loadFromObject("assets/meshes/companionCubeLow.obj", Vector3{1.5, 0, 0 }, Vector3(0, 0, 0)));

	// Reset cmdList and cmdAllc
	cmdAlloc->Reset();
	cmdList->Reset(cmdAlloc, nullptr);

	std::cout << "Pushing buffers to VRAM" << std::endl;

	for (MeshManager::LoadedModel loadedModel : loadedModels) {

		for (int i = 0; i < loadedModel.meshes.size(); i++) {
			MeshManager::Mesh mesh = loadedModel.meshes[i];

			size_t vbSize = mesh.vertices.size() * sizeof(MeshManager::Vertex);
			size_t ibSize = mesh.indices.size() * sizeof(uint32_t);

			std::cout << "Mesh " << i << ": vbSize=" << vbSize << " bytes, ibSize=" << ibSize << " bytes\n";
			
			// Barrier - transition vertex target to COPY_DEST
			D3D12_RESOURCE_BARRIER barrier = {};
			barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
			barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
			barrier.Transition.pResource = loadedModel.vertexDefaultBuffers[i];
			barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
			barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
			barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
			cmdList->ResourceBarrier(1, &barrier);

			// transition index buffer
			barrier.Transition.pResource = loadedModel.indexDefaultBuffers[i];
			cmdList->ResourceBarrier(1, &barrier);

			// Copy to GPU
			cmdList->CopyBufferRegion(loadedModel.vertexDefaultBuffers[i], 0, loadedModel.vertexUploadBuffers[i], 0, vbSize);
			cmdList->CopyBufferRegion(loadedModel.indexDefaultBuffers[i], 0, loadedModel.indexUploadBuffers[i], 0, ibSize);

			// Barrier - transition to final state
			barrier.Transition.pResource = loadedModel.vertexDefaultBuffers[i];
			barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
			barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
			cmdList->ResourceBarrier(1, &barrier);

			barrier.Transition.pResource = loadedModel.indexDefaultBuffers[i];
			barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_INDEX_BUFFER;
			cmdList->ResourceBarrier(1, &barrier);
		}

	}

	// Execute upload of buffers
	cmdList->Close();
	ID3D12CommandList* lists[] = { cmdList };
	cmdQueue->ExecuteCommandLists(1, lists);
	flush();

}

// acceleration structures

ID3D12Resource* PathTracer::makeAccelerationStructure(const D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS& inputs, UINT64* updateScratchSize) {


	auto makeBuffer = [this](UINT64 size, auto initialState) {
		D3D12_RESOURCE_DESC desc = {};
		desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
		desc.Width = size;
		desc.Height = 1;
		desc.DepthOrArraySize = 1;
		desc.MipLevels = 1;
		desc.SampleDesc.Count = 1;
		desc.SampleDesc.Quality = 0;
		desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
		desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
		desc.Width = size;
		ID3D12Resource* buffer;
		//d3dDevice->CreateCommittedResource(&DEFAULT_HEAP, D3D12_HEAP_FLAG_NONE, &desc, initialState, nullptr, IID_PPV_ARGS(&buffer));

		HRESULT hr = d3dDevice->CreateCommittedResource(
			&DEFAULT_HEAP,
			D3D12_HEAP_FLAG_NONE,
			&desc,
			initialState,
			nullptr,
			IID_PPV_ARGS(&buffer)
		);

		checkHR(hr, "CreateCommittedResource for AS failed");

		return buffer;
		};

	// debug
	if (inputs.NumDescs == 0 || inputs.pGeometryDescs == nullptr) {
		std::cerr << "Invalid inputs: no geometry\n";
		return nullptr;
	}

	D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO prebuildInfo;
	d3dDevice->GetRaytracingAccelerationStructurePrebuildInfo(&inputs, &prebuildInfo);

	// debug
	std::cout << "Prebuild: scratch=" << prebuildInfo.ScratchDataSizeInBytes
		<< ", result=" << prebuildInfo.ResultDataMaxSizeInBytes << "\n";

	if (prebuildInfo.ResultDataMaxSizeInBytes == 0 || prebuildInfo.ScratchDataSizeInBytes == 0) {
		std::cerr << "Prebuild returned zero sizes — invalid geometry!\n";
		return nullptr;
	}

	if (updateScratchSize) *updateScratchSize = prebuildInfo.UpdateScratchDataSizeInBytes;

	auto* scratch = makeBuffer(prebuildInfo.ScratchDataSizeInBytes, D3D12_RESOURCE_STATE_COMMON);
	auto* as = makeBuffer(prebuildInfo.ResultDataMaxSizeInBytes, D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE);

	D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC buildDesc = {
	.DestAccelerationStructureData = as->GetGPUVirtualAddress(), .Inputs = inputs, .ScratchAccelerationStructureData = scratch->GetGPUVirtualAddress() };

	cmdAlloc->Reset();
	cmdList->Reset(cmdAlloc, nullptr);
	cmdList->BuildRaytracingAccelerationStructure(&buildDesc, 0, nullptr);
	cmdList->Close();
	cmdQueue->ExecuteCommandLists(1, reinterpret_cast<ID3D12CommandList**>(&cmdList));

	flush();
	scratch->Release();
	return as;

}

// BLAS

ID3D12Resource* PathTracer::makeBLAS(ID3D12Resource* vertexBuffer, UINT vertexSize, ID3D12Resource* indexBuffer, UINT indicesSize) {

	D3D12_RAYTRACING_GEOMETRY_DESC geometryDesc = {
		.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES,
		.Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE,
		.Triangles = {.Transform3x4 = 0,
			.IndexFormat = DXGI_FORMAT_R32_UINT,
			.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT,
			.IndexCount = indicesSize,
			.VertexCount = vertexSize,
			.IndexBuffer = indexBuffer->GetGPUVirtualAddress(),
			.VertexBuffer = {.StartAddress = vertexBuffer->GetGPUVirtualAddress(),
			.StrideInBytes = sizeof(MeshManager::Vertex)}
		}
	};

	if (!vertexBuffer || !indexBuffer) {
		std::cerr << "Null vertex or index buffer in makeBLAS\n";
		return nullptr;
	}

	D3D12_GPU_VIRTUAL_ADDRESS vbAddr = vertexBuffer->GetGPUVirtualAddress();
	D3D12_GPU_VIRTUAL_ADDRESS ibAddr = indexBuffer->GetGPUVirtualAddress();
	if (vbAddr == 0 || ibAddr == 0) {
		std::cerr << "Invalid GPU VA for vertex/index buffer\n";
		return nullptr;
	}

	std::cout << "BLAS inputs: verts=" << vertexSize << ", indices=" << indicesSize << "\n";
	if (vertexSize == 0 || indicesSize == 0 || indicesSize % 3 != 0) {
		std::cerr << "Invalid mesh counts: verts=" << vertexSize << ", indices=" << indicesSize << "\n";
		return nullptr;
	}

	D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs = {
		.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL,
		.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE,
		.NumDescs = 1,
		.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY,
		.pGeometryDescs = &geometryDesc };


	return makeAccelerationStructure(inputs);

}

void PathTracer::initBottomLevel() {

	if (debug) std::cout << "initBottomLevel()" << std::endl;

	BLAS.clear();

	// only the first mesh in a model for now
	// store all bottom level AS in a vector
	// instance IDs should be in the same order as BLAS creation

	NUM_INSTANCES = 0; // num meshes
	for (MeshManager::LoadedModel loadedModel : loadedModels) {

		for (int i = 0; i < loadedModel.meshes.size(); i++) {

			BLAS.emplace_back(makeBLAS(loadedModel.vertexDefaultBuffers[i], loadedModel.meshes[i].vertices.size(), loadedModel.indexDefaultBuffers[i], loadedModel.meshes[i].indices.size()));
			NUM_INSTANCES++;
		}
	}
}

// TLAS

ID3D12Resource* PathTracer::makeTLAS(ID3D12Resource* instances, UINT numInstances, UINT64* updateScratchSize) {

	D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs = {
	.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL,
	.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_UPDATE,
	.NumDescs = numInstances,
	.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY,
	.InstanceDescs = instances->GetGPUVirtualAddress() };

	return makeAccelerationStructure(inputs, updateScratchSize);
}

// scene update

void PathTracer::updateTransforms() {

	if (debug) std::cout << "Update Transforms" << std::endl;

	auto time = static_cast<float>(GetTickCount64()) / 1000;

	size_t currentInstance = 0;

	// apply meshes stored transform
	for (size_t i = 0; i < loadedModels.size(); i++) {

		auto vecRotation = loadedModels[i].rotation;
		auto vecPosition = loadedModels[i].position;

		auto transform = DirectX::XMMatrixRotationRollPitchYaw(vecRotation.x, vecRotation.y, vecRotation.z);
		transform = DirectX::XMMatrixRotationRollPitchYaw(time / 2, time / 3, time / 5);
		transform *= DirectX::XMMatrixTranslation(vecPosition.x, vecPosition.y, vecPosition.z);

		for (size_t j = 0; j < loadedModels[i].meshes.size(); j++) {
			auto* ptr = reinterpret_cast<DirectX::XMFLOAT3X4*>(&instanceData[currentInstance].Transform);
			XMStoreFloat3x4(ptr, transform);
			currentInstance++;
		}

	}
	

}

void PathTracer::initScene() {

	auto instancesDesc = BASIC_BUFFER_DESC;
	instancesDesc.Width = sizeof(D3D12_RAYTRACING_INSTANCE_DESC) * NUM_INSTANCES;
	HRESULT hr = d3dDevice->CreateCommittedResource(&UPLOAD_HEAP, D3D12_HEAP_FLAG_NONE, &instancesDesc, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&instances));
	checkHR(hr, "initScene, CreateComittedResource: ");
	
	instances->Map(0, nullptr, reinterpret_cast<void**>(&instanceData));

	if (debug) std::cout << "init scene" << std::endl;

	// if instance number and BLAS creation align
	for (UINT i = 0; i < NUM_INSTANCES; i++) {
		instanceData[i] = {
		.InstanceID = i,
		.InstanceMask = 1,
		.AccelerationStructure = BLAS[i]->GetGPUVirtualAddress(),
		};
	}
	updateTransforms();

}

void PathTracer::initTopLevel() {

	if (debug) std::cout << "initTopLevel()" << std::endl;

	UINT64 updateScratchSize;
	tlas = makeTLAS(instances, NUM_INSTANCES, &updateScratchSize);

	// create scratch space for TLAS updates in advance
	auto desc = BASIC_BUFFER_DESC;
	desc.Width = std::max(updateScratchSize, 8ULL);
	desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
	HRESULT hr = d3dDevice->CreateCommittedResource(&DEFAULT_HEAP, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&tlasUpdateScratch));
	checkHR(hr, "initTopLevel: CreateCommittedResource");
}

// bind 

void PathTracer::initDescriptors() {

	if (debug) std::cout << "initDescriptors()" << std::endl;

	descriptorIncrementSize = d3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	allVertexBuffers.clear();
	allIndexBuffers.clear();
	for (const auto& model : loadedModels) {
		for (size_t i = 0; i < model.meshes.size(); i++) {
			allVertexBuffers.push_back(model.vertexDefaultBuffers[i]);
			allIndexBuffers.push_back(model.indexDefaultBuffers[i]);
		}
	}

	// Heap size: 1 UAV (render target) + 1 SRV (scene), + NUM_INSTANCES * (vertex srvs, index srvs)
	UINT numDescriptors = 2 + 2 * NUM_INSTANCES;
	D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {
		.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
		.NumDescriptors = numDescriptors,
		.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE
	};

	HRESULT hr = d3dDevice->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&descHeap));
	checkHR(hr, "CreateDescriptorHeap");

	// Create views
	D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle = descHeap->GetCPUDescriptorHandleForHeapStart();

	// slot 0 UAV for render target
	D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {
		.Format = DXGI_FORMAT_R8G8B8A8_UNORM,
		.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D
	};
	d3dDevice->CreateUnorderedAccessView(renderTarget, nullptr, &uavDesc, cpuHandle);
	cpuHandle.ptr += descriptorIncrementSize;

	// slot 1 SRV for TLAS
	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Format = DXGI_FORMAT_UNKNOWN;
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_RAYTRACING_ACCELERATION_STRUCTURE;
	srvDesc.RaytracingAccelerationStructure.Location = tlas->GetGPUVirtualAddress();

	d3dDevice->CreateShaderResourceView(nullptr, &srvDesc, cpuHandle);
	cpuHandle.ptr += descriptorIncrementSize;

	// slot 2
	for (auto* vb : allVertexBuffers) {
		D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		srvDesc.Format = DXGI_FORMAT_UNKNOWN;
		srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
		srvDesc.Buffer.FirstElement = 0;
		srvDesc.Buffer.NumElements = static_cast<UINT>(vb->GetDesc().Width / sizeof(MeshManager::Vertex));
		srvDesc.Buffer.StructureByteStride = sizeof(MeshManager::Vertex);
		d3dDevice->CreateShaderResourceView(vb, &srvDesc, cpuHandle);
		cpuHandle.ptr += descriptorIncrementSize;
		std::cout << "srvNumElementsVertex: " << srvDesc.Buffer.NumElements << std::endl;
	}
	// slot 3
	for (auto* ib : allIndexBuffers) {
		D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		srvDesc.Format = DXGI_FORMAT_R32_UINT;
		srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
		srvDesc.Buffer.FirstElement = 0;
		srvDesc.Buffer.NumElements = static_cast<UINT>(ib->GetDesc().Width / sizeof(uint32_t));
		srvDesc.Buffer.StructureByteStride = 0;
		d3dDevice->CreateShaderResourceView(ib, &srvDesc, cpuHandle);
		cpuHandle.ptr += descriptorIncrementSize;
		std::cout << "srvNumElementsIndex: " << srvDesc.Buffer.NumElements << std::endl;
	}


}


// root signature

void PathTracer::initRootSignature() {

	if (debug) std::cout << "initRootSignature()" << std::endl;

	D3D12_DESCRIPTOR_RANGE uavRange = {
	.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV,
	.NumDescriptors = 1,
	.BaseShaderRegister = 0,
	.RegisterSpace = 0,
	.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND
	};

	D3D12_DESCRIPTOR_RANGE sceneRange = {
	.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
	.NumDescriptors = 1,
	.BaseShaderRegister = 0,
	.RegisterSpace = 0,
	.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND
	};

	D3D12_DESCRIPTOR_RANGE vertexRange = {
	.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
	.NumDescriptors = NUM_INSTANCES,
	.BaseShaderRegister = 1,
	.RegisterSpace = 1,
	.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND
	};

	D3D12_DESCRIPTOR_RANGE indexRange = {
	.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
	.NumDescriptors = NUM_INSTANCES,
	.BaseShaderRegister = 2,
	.RegisterSpace = 2,
	.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND
	};

	D3D12_ROOT_PARAMETER params[4] = {												// num desriptor ranges, descriptor range
		{.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE, .DescriptorTable = {1, &uavRange}},
		{.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE, .DescriptorTable = {1, &sceneRange}},
		{.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE, .DescriptorTable = {1, &vertexRange}},
		{.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE, .DescriptorTable = {1, &indexRange}},

	};

	D3D12_ROOT_SIGNATURE_DESC desc = {
		.NumParameters = 4,
		.pParameters = params,
		.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE
	};

	ID3DBlob* blob;
	HRESULT hr = D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1_0, &blob, nullptr);
	checkHR(hr, "D3D12SerializeRootSignature: ");


	hr = d3dDevice->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&rootSignature));
	checkHR(hr, "CreateRootSignature: ");

	blob->Release();

}

// ray tracing pso

void PathTracer::initPipeline() {

	if (debug) std::cout << "initPipeline()" << std::endl;

	D3D12_DXIL_LIBRARY_DESC lib = {
	.DXILLibrary = {.pShaderBytecode = compiledShader, .BytecodeLength = std::size(compiledShader)} };



	D3D12_HIT_GROUP_DESC hitGroup = { .HitGroupExport = L"HitGroup",
	.Type = D3D12_HIT_GROUP_TYPE_TRIANGLES,
	.ClosestHitShaderImport = L"ClosestHit" };


	D3D12_RAYTRACING_SHADER_CONFIG shaderCfg = {
	.MaxPayloadSizeInBytes = 32,
	.MaxAttributeSizeInBytes = 8,
	};


	D3D12_GLOBAL_ROOT_SIGNATURE globalSig = { rootSignature };

	D3D12_RAYTRACING_PIPELINE_CONFIG pipelineCfg = { .MaxTraceRecursionDepth = 3 };

	D3D12_STATE_SUBOBJECT subobjects[] = {
		{.Type = D3D12_STATE_SUBOBJECT_TYPE_DXIL_LIBRARY, .pDesc = &lib},
		{.Type = D3D12_STATE_SUBOBJECT_TYPE_HIT_GROUP, .pDesc = &hitGroup},
		{.Type = D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_SHADER_CONFIG, .pDesc = &shaderCfg},
		{.Type = D3D12_STATE_SUBOBJECT_TYPE_GLOBAL_ROOT_SIGNATURE, .pDesc = &globalSig},
		{.Type = D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_PIPELINE_CONFIG, .pDesc = &pipelineCfg}
	};

	D3D12_STATE_OBJECT_DESC desc = { .Type = D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE,
		.NumSubobjects = std::size(subobjects),
		.pSubobjects = subobjects };
	HRESULT hr = d3dDevice->CreateStateObject(&desc, IID_PPV_ARGS(&pso));
	checkHR(hr, "initPipeLine, CreateStateObject: ");

}

// shader tables

void PathTracer::initShaderTables() {

	if (debug) std::cout << "iniShaderTables()" << std::endl;

	auto idDesc = BASIC_BUFFER_DESC;
	idDesc.Width = NUM_SHADER_IDS * D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT;
	HRESULT hr = d3dDevice->CreateCommittedResource(&UPLOAD_HEAP, D3D12_HEAP_FLAG_NONE, &idDesc, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&shaderIDs));
	checkHR(hr, "initShaderTables, CreateCommittedResource: ");
	ID3D12StateObjectProperties* props;
	pso->QueryInterface(&props);

	void* data;
	auto writeId = [&](const wchar_t* name) {
		void* id = props->GetShaderIdentifier(name);
		memcpy(data, id, D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
		data = static_cast<char*>(data) + D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT;

		};

	shaderIDs->Map(0, nullptr, &data);
	writeId(L"RayGeneration");
	writeId(L"Miss");
	writeId(L"HitGroup");
	shaderIDs->Unmap(0, nullptr);

	props->Release();

}

void PathTracer::updateScene() {

	if (debug) std::cout << "updateScene()" << std::endl;

	updateTransforms();

	D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC desc = {
	.DestAccelerationStructureData = tlas->GetGPUVirtualAddress(),
	.Inputs = {
		.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL,
		.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PERFORM_UPDATE,
		.NumDescs = NUM_INSTANCES,
		.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY,
		.InstanceDescs = instances->GetGPUVirtualAddress()},
	.SourceAccelerationStructureData = tlas->GetGPUVirtualAddress(),
	.ScratchAccelerationStructureData = tlasUpdateScratch->GetGPUVirtualAddress(),
	};

	cmdList->BuildRaytracingAccelerationStructure(&desc, 0, nullptr);


	D3D12_RESOURCE_BARRIER barrier = { .Type = D3D12_RESOURCE_BARRIER_TYPE_UAV,
										.UAV = {.pResource = tlas} };

	cmdList->ResourceBarrier(1, &barrier);

}

// command submission

void PathTracer::render() {

	cmdAlloc->Reset();
	cmdList->Reset(cmdAlloc, nullptr);
	updateScene();

	cmdList->SetPipelineState1(pso);
	cmdList->SetComputeRootSignature(rootSignature);

	ID3D12DescriptorHeap* heaps[] = { descHeap };
	cmdList->SetDescriptorHeaps(1, heaps);

	D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle = descHeap->GetGPUDescriptorHandleForHeapStart();
	cmdList->SetComputeRootDescriptorTable(0, gpuHandle); // u0 UAV
	gpuHandle.ptr += descriptorIncrementSize;
	cmdList->SetComputeRootDescriptorTable(1, gpuHandle); // t0 TLAS
	gpuHandle.ptr += descriptorIncrementSize;
	cmdList->SetComputeRootDescriptorTable(2, gpuHandle); // u1 vertex buffer
	gpuHandle.ptr += descriptorIncrementSize * NUM_INSTANCES;
	cmdList->SetComputeRootDescriptorTable(3, gpuHandle); // u2 index buffer buffer



	// Dispatch rays

	auto rtDesc = renderTarget->GetDesc();

	D3D12_DISPATCH_RAYS_DESC dispatchDesc = {
		.RayGenerationShaderRecord = {
			.StartAddress = shaderIDs->GetGPUVirtualAddress(),
			.SizeInBytes = D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES},
		.MissShaderTable = {
			.StartAddress = shaderIDs->GetGPUVirtualAddress() + D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT,
			.SizeInBytes = D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES},
		.HitGroupTable = {
			.StartAddress = shaderIDs->GetGPUVirtualAddress() + 2 * D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT,
			.SizeInBytes = D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES},
		.Width = static_cast<UINT>(rtDesc.Width),
		.Height = rtDesc.Height,
		.Depth = 1 };
	cmdList->DispatchRays(&dispatchDesc);


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
	barrier(renderTarget, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
	barrier(backBuffer, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_COPY_DEST);

	cmdList->CopyResource(backBuffer, renderTarget);

	barrier(backBuffer, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PRESENT);
	barrier(renderTarget, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

	// release refernce to backbuffer
	backBuffer->Release();


	cmdList->Close();
	cmdQueue->ExecuteCommandLists(1, reinterpret_cast<ID3D12CommandList**>(&cmdList));

	flush();
	swapChain->Present(1, 0);

}

void PathTracer::quit() {
	delete meshManager;
}

void PathTracer::checkHR(HRESULT hr, std::string context) {
	if (FAILED(hr)) {
		std::cerr << context << "HRESULT 0x" << std::hex << hr << std::endl;
		if (hr == E_OUTOFMEMORY) std::cerr << "(Out of memory?)\n";
		else if (hr == E_INVALIDARG) std::cerr << "(Invalid arg — check desc)\n";
	}
}