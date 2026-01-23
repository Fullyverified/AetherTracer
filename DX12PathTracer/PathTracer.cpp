#include "PathTracer.h"
#include <iostream>

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

	std::cout << "render loop" << std::endl;

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
	
	factory->CreateSwapChainForHwnd(cmdQueue, hwnd, &scDesc, nullptr, nullptr, &swapChain1);
	
	swapChain1->QueryInterface(&swapChain);
	swapChain1->Release();

	// early factory release
	factory->Release();

	// uav descriptor heap
	// points to render target texture
	D3D12_DESCRIPTOR_HEAP_DESC uavHeapDesc = {
		.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
		.NumDescriptors = 1,
		.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE };
	d3dDevice->CreateDescriptorHeap(&uavHeapDesc, IID_PPV_ARGS(&uavHeap));

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

	D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {
   .Format = DXGI_FORMAT_R8G8B8A8_UNORM,
   .ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D };
	d3dDevice->CreateUnorderedAccessView(renderTarget, nullptr, &uavDesc, uavHeap->GetCPUDescriptorHandleForHeapStart());
}

// command list and allocator

void PathTracer::initCommand() {
	// only one
	d3dDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&cmdAlloc));
	d3dDevice->CreateCommandList1(0, D3D12_COMMAND_LIST_TYPE_DIRECT, D3D12_COMMAND_LIST_FLAG_NONE, IID_PPV_ARGS(&cmdList));
}

// meshes

void PathTracer::initMeshes() {


	loadedModels.emplace_back(meshManager->loadFromObject("assets/meshes/cube.obj"));

}

// acceleration structures

ID3D12Resource* PathTracer::makeAccelerationStructure(const D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS& inputs, UINT64* updateScratchSize) {


	auto makeBuffer = [this](UINT64 size, auto initialState) {
		auto desc = BASIC_BUFFER_DESC;
		desc.Width = size;
		desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
		ID3D12Resource* buffer;
		d3dDevice->CreateCommittedResource(&DEFAULT_HEAP, D3D12_HEAP_FLAG_NONE, &desc, initialState, nullptr, IID_PPV_ARGS(&buffer));

		return buffer;
		};

	D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO prebuildInfo;
	d3dDevice->GetRaytracingAccelerationStructurePrebuildInfo(&inputs, &prebuildInfo);

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


	D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs = {
		.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL,
		.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE,
		.NumDescs = 1,
		.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY,
		.pGeometryDescs = &geometryDesc };


	return makeAccelerationStructure(inputs);

}

void PathTracer::initBottomLevel() {

	BLAS.clear();

	// only the first mesh in a model for now
	// store all bottom level AS in a vector
	// instance IDs should be in the same order as BLAS creation
	for (MeshManager::LoadedModel loadedModel : loadedModels) {
		BLAS.emplace_back(makeBLAS(loadedModel.vertexBuffers[0], std::size(loadedModel.meshes[0].vertices), loadedModel.indexBuffers[0], std::size(loadedModel.meshes[0].indices)));
	}
	NUM_INSTANCES = loadedModels.size(); // this is a guess
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

	auto defaultTransform = DirectX::XMMatrixRotationRollPitchYaw(0, 0, 0);
	defaultTransform *= DirectX::XMMatrixTranslation(0, 1, 0);

	// for each instance apply the default transform
	for (int i = 0; i < NUM_INSTANCES; i++) {

		auto* ptr = reinterpret_cast<DirectX::XMFLOAT3X4*>(&instanceData[i].Transform);
		XMStoreFloat3x4(ptr, defaultTransform);
		
	}

}

void PathTracer::initScene() {

	auto instancesDesc = BASIC_BUFFER_DESC;
	instancesDesc.Width = sizeof(D3D12_RAYTRACING_INSTANCE_DESC) * NUM_INSTANCES;
	d3dDevice->CreateCommittedResource(&UPLOAD_HEAP, D3D12_HEAP_FLAG_NONE, &instancesDesc, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&instances));
	instances->Map(0, nullptr, reinterpret_cast<void**>(&instanceData));

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

	UINT64 updateScratchSize;
	tlas = makeTLAS(instances, NUM_INSTANCES, &updateScratchSize);

	// create scratch space for TLAS updates in advance
	auto desc = BASIC_BUFFER_DESC;
	desc.Width = std::max(updateScratchSize, 8ULL);
	desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
	d3dDevice->CreateCommittedResource(&DEFAULT_HEAP, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&tlasUpdateScratch));

}

// root signature

void PathTracer::initRootSignature() {

	D3D12_DESCRIPTOR_RANGE uavRange = {
	.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV,
	.NumDescriptors = 1,
	};

	D3D12_ROOT_PARAMETER params[] = {
		{.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE,
		.DescriptorTable = {.NumDescriptorRanges = 1,
		.pDescriptorRanges = &uavRange}},
		{.ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV, .Descriptor = {.ShaderRegister = 0, .RegisterSpace = 0 }}

	};

	D3D12_ROOT_SIGNATURE_DESC desc = { .NumParameters = std::size(params), .pParameters = params };

	ID3DBlob* blob;
	D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1_0, &blob, nullptr);

	d3dDevice->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&rootSignature));
	blob->Release();

}

// ray tracing pso

void PathTracer::initPipeline() {

	D3D12_DXIL_LIBRARY_DESC lib = {
	.DXILLibrary = {.pShaderBytecode = compiledShader, .BytecodeLength = std::size(compiledShader)} };



	D3D12_HIT_GROUP_DESC hitGroup = { .HitGroupExport = L"HitGroup",
	.Type = D3D12_HIT_GROUP_TYPE_TRIANGLES,
	.ClosestHitShaderImport = L"ClosestHit" };


	D3D12_RAYTRACING_SHADER_CONFIG shaderCfg = {
	.MaxPayloadSizeInBytes = 20,
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
	d3dDevice->CreateStateObject(&desc, IID_PPV_ARGS(&pso));

}

// shader tables

void PathTracer::initShaderTables() {


	auto idDesc = BASIC_BUFFER_DESC;
	idDesc.Width = NUM_SHADER_IDS * D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT;
	d3dDevice->CreateCommittedResource(&UPLOAD_HEAP, D3D12_HEAP_FLAG_NONE, &idDesc, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&shaderIDs));

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
	cmdList->SetDescriptorHeaps(1, &uavHeap);
	cmdList->SetComputeRootDescriptorTable(0, uavHeap->GetGPUDescriptorHandleForHeapStart()); // ←u0 ↓t0
	cmdList->SetComputeRootShaderResourceView(1, tlas->GetGPUVirtualAddress());

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