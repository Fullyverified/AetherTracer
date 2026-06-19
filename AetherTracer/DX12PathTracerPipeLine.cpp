#include "DX12PathTracerPipeLine.h"

#include <iostream>
#include <random>

#include <d3dcompiler.h> // for compiling shaders

#include "Config.h"
#include "UI.h"


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

DX12PathTracerPipeLine::DX12PathTracerPipeLine(EntityManager* entityManager, MeshManager* meshManager, MaterialManager* materialManager, Window* window, IDXGIFactory4* factory, ID3D12Device5* d3dDevice, ID3D12CommandQueue* cmdQueue)
	: entityManager(entityManager), meshManager(meshManager), materialManager(materialManager), window(window), factory(factory), d3dDevice(d3dDevice), cmdQueue(cmdQueue) {
}

void DX12PathTracerPipeLine::init() {

	hwnd = static_cast<HWND>(window->getNativeHandle());

	std::cout << "init command allocator" << std::endl;
	initCommand();

	dx12ResourceManager = new DX12ResourceManager(meshManager, materialManager, entityManager, factory, d3dDevice, cmdQueue, cmdAlloc, cmdList);

	dx12TLASManager = new DX12TLASManager(meshManager, materialManager, entityManager, factory, d3dDevice, cmdQueue, cmdAlloc, cmdList);

	dx12BLASManager = new DX12BLASManager(meshManager, materialManager, entityManager, factory, d3dDevice, cmdQueue, cmdAlloc, cmdList);

	dx12MaterialManager = new DX12MaterialManager(meshManager, materialManager, entityManager, factory, d3dDevice, cmdQueue, cmdAlloc, cmdList);

	RECT rect;
	GetClientRect(hwnd, &rect);
	dx12ResourceManager->width = std::max<UINT>(rect.right - rect.left, 1);
	dx12ResourceManager->height = std::max<UINT>(rect.bottom - rect.top, 1);

	createFence(dx12ResourceManager->fence);
	createFence(dx12TLASManager->fence);
	createFence(dx12BLASManager->fence);
	createFence(dx12MaterialManager->fence);
	createFence(fence);

	loadShaders();

	initSwapchain();
	
	initImgui();

	dx12ResourceManager->dx12Camera = new DX12Camera{};

	std::cout << "init Descriptor Heaps" << std::endl;

	dx12ResourceManager->initDescriptorHeap(dx12ResourceManager->global_descriptor_heap_allocator, 1000, true, "Global Descriptor Heap");

	std::cout << "init RTResources" << std::endl;

	dx12ResourceManager->initRenderTexture(dx12ResourceManager->renderTarget, DXGI_FORMAT_R8G8B8A8_UNORM, "Render Target", false); //
	dx12ResourceManager->initRenderTexture(dx12ResourceManager->accumulationTexture, DXGI_FORMAT_R32G32B32A32_FLOAT, "Accumulation Texture", false); //

	dx12ResourceManager->initModelBuffers(); // create each vertex and index buffer for each model

	dx12BLASManager->createModelBLAS(dx12ResourceManager->dx12Models_map);

	dx12ResourceManager->initVertexIndexBuffers(); // instanced buffers

	dx12ResourceManager->initDX12Entites(false);

	dx12TLASManager->initTopLevelAS(dx12ResourceManager->instances);

	dx12ResourceManager->updateRand(false);
	dx12ResourceManager->updateToneParams();
	dx12ResourceManager->initMaxLumBuffer();

	dx12MaterialManager->initMaterials(true, true, true);
	dx12ResourceManager->cmdList->Close();
	cmdQueue->ExecuteCommandLists(1, reinterpret_cast<ID3D12CommandList**>(&dx12ResourceManager->cmdList));
	dx12ResourceManager->waitForGPU();
	dx12ResourceManager->cmdAlloc->Reset();
	dx12ResourceManager->cmdList->Reset(dx12ResourceManager->cmdAlloc, nullptr);
	dx12MaterialManager->initMaterialBuffers(false);
	dx12MaterialManager->initEmissiveIndexBuffer(false);
	dx12ResourceManager->updateCamera();


	initGlobalDescriptors();
	initRootSignature();
	initComputePipeline();

	initRayTracingPipeline();
	initRTShaderTables();

	bindDescriptors();

	dx12ResourceManager->cmdList->Close();
	cmdQueue->ExecuteCommandLists(1, reinterpret_cast<ID3D12CommandList**>(&dx12ResourceManager->cmdList));
	dx12ResourceManager->waitForGPU();
	dx12ResourceManager->cmdAlloc->Reset();
	dx12ResourceManager->cmdList->Reset(dx12ResourceManager->cmdAlloc, nullptr);

}

void DX12PathTracerPipeLine::createFence(ID3D12Fence*& fence) {
	
	d3dDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence));

}

// swap chain

void DX12PathTracerPipeLine::initSwapchain() {

	// 8-bit SRGB
	// alternative: R16G16B16A16_FLOAT for HDR
	DXGI_SWAP_CHAIN_DESC1 scDesc = {
	   .Format = DXGI_FORMAT_R8G8B8A8_UNORM,
	   .SampleDesc = dx12ResourceManager->NO_AA,
	   .BufferCount = 2,
	   .SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD,
	   .Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING,
	};
	IDXGISwapChain1* swapChain1;

	HRESULT hr = factory->CreateSwapChainForHwnd(cmdQueue, hwnd, &scDesc, nullptr, nullptr, &swapChain1);
	checkHR(hr, nullptr, "Create swap chain: ");

	swapChain1->QueryInterface(&dx12ResourceManager->swapChain);
	swapChain1->Release();

	// early factory release
	factory->Release();

}

// render target
void DX12PathTracerPipeLine::resize() {
	std::cout << "resize" << std::endl;

	RECT rect;
	GetClientRect(hwnd, &rect);
	dx12ResourceManager->width = std::max<UINT>(rect.right - rect.left, 1);
	dx12ResourceManager->height = std::max<UINT>(rect.bottom - rect.top, 1);

	dx12ResourceManager->waitForGPU();

	dx12ResourceManager->swapChain->ResizeBuffers(0, dx12ResourceManager->width, dx12ResourceManager->height, DXGI_FORMAT_UNKNOWN, 0);

	// Update render target and accumulation texture
	//dx12ResourceManager->initRenderTexture(dx12ResourceManager->renderTarget, DXGI_FORMAT_R8G8B8A8_UNORM, "Render Target", true);
	//dx12ResourceManager->initRenderTexture(dx12ResourceManager->accumulationTexture, DXGI_FORMAT_R32G32B32A32_FLOAT, "Accumulation Texture", true);
	//initGlobalDescriptors();
	//bindDescriptors();
	

	createBackBufferRTVs();

	//dx12ResourceManager->updateRand(true);

}

// command list and allocator

void DX12PathTracerPipeLine::initCommand() {
	// only one
	d3dDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&cmdAlloc));
	d3dDevice->CreateCommandList1(0, D3D12_COMMAND_LIST_TYPE_DIRECT, D3D12_COMMAND_LIST_FLAG_NONE, IID_PPV_ARGS(&cmdList));

	cmdAlloc->SetName(L"Default cmdAlloc");
	cmdList->SetName(L"Default cmdList");

	cmdAlloc->Reset();
	cmdList->Reset(cmdAlloc, nullptr);
}

void DX12PathTracerPipeLine::loadShaders() {
	
	HRESULT hr = D3DReadFileToBlob(L"raytracingshader.cso", &dx12ResourceManager->rsBlob);
	checkHR(hr, nullptr, "Loading postprocessingshader");

	hr = D3DReadFileToBlob(L"postprocessingshader.cso", &dx12ResourceManager->csBlob);
	checkHR(hr, nullptr, "Loading postprocessingshader");

}

void DX12PathTracerPipeLine::initRootSignature() {

	if (config.debug) std::cout << "initRTRootSignature()" << std::endl;

	UINT NUM_VERTEX_BUFFERS = static_cast<UINT>(dx12ResourceManager->allVertexBuffers.size());

	D3D12_DESCRIPTOR_RANGE accumRangeUAV = {
	.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV,
	.NumDescriptors = 1,
	.BaseShaderRegister = 0,
	.RegisterSpace = 0,
	.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND
	};

	D3D12_DESCRIPTOR_RANGE randRange = {
	.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV,
	.NumDescriptors = 1,
	.BaseShaderRegister = 1,
	.RegisterSpace = 0,
	.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND
	};

	// u0, render target
	D3D12_DESCRIPTOR_RANGE rtRange = {
	.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV,
	.NumDescriptors = 1,
	.BaseShaderRegister = 2,
	.RegisterSpace = 0,
	.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND
	};

	// u1, maxLum
	D3D12_DESCRIPTOR_RANGE maxLumRange = {
	.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV,
	.NumDescriptors = 1,
	.BaseShaderRegister = 3,
	.RegisterSpace = 0,
	.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND
	};

	// t0, accumulation texture
	D3D12_DESCRIPTOR_RANGE accumRangeSRV = {
	.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
	.NumDescriptors = 1,
	.BaseShaderRegister = 0,
	.RegisterSpace = 0,
	.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND
	};

	D3D12_DESCRIPTOR_RANGE sceneRange = {
	.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
	.NumDescriptors = 1,
	.BaseShaderRegister = 1,
	.RegisterSpace = 1,
	.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND
	};

	D3D12_DESCRIPTOR_RANGE vertexRange = {
	.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
	.NumDescriptors = NUM_VERTEX_BUFFERS,
	.BaseShaderRegister = 2,
	.RegisterSpace = 2,
	.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND
	};

	D3D12_DESCRIPTOR_RANGE indexRange = {
	.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
	.NumDescriptors = NUM_VERTEX_BUFFERS,
	.BaseShaderRegister = 3,
	.RegisterSpace = 3,
	.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND
	};

	D3D12_DESCRIPTOR_RANGE materialRange = {
	.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
	.NumDescriptors = 1,
	.BaseShaderRegister = 4,
	.RegisterSpace = 4,
	.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND
	};

	D3D12_DESCRIPTOR_RANGE materialIndexRange = {
	.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
	.NumDescriptors = 1,
	.BaseShaderRegister = 5,
	.RegisterSpace = 5,
	.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND
	};

	D3D12_DESCRIPTOR_RANGE emissiveEntitiesIndicesRange = {
	.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
	.NumDescriptors = 1,
	.BaseShaderRegister = 6,
	.RegisterSpace = 6,
	.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND
	};

	UINT tex_maps_size = dx12MaterialManager->texture_maps.size();
	D3D12_DESCRIPTOR_RANGE materialTexturesRange = {
	.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
	.NumDescriptors = tex_maps_size,
	.BaseShaderRegister = 7,
	.RegisterSpace = 7,
	.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND
	};

	// CBV for Camera
	D3D12_ROOT_PARAMETER cameraParam = {};
	cameraParam.ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
	cameraParam.Descriptor.ShaderRegister = 0;
	cameraParam.Descriptor.RegisterSpace = 0;
	cameraParam.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

	// CBV for tone mapping parameters
	D3D12_ROOT_PARAMETER toneParam = {};
	toneParam.ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
	toneParam.Descriptor.ShaderRegister = 1;
	toneParam.Descriptor.RegisterSpace = 0;
	toneParam.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

	D3D12_ROOT_PARAMETER params[14] = {												// num desriptor ranges, descriptor range
		{.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE, .DescriptorTable = {1, &accumRangeUAV}},
		{.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE, .DescriptorTable = {1, &randRange}},
		{.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE, .DescriptorTable = {1, &rtRange}},
		{.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE, .DescriptorTable = {1, &maxLumRange}},
		{.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE, .DescriptorTable = {1, &accumRangeSRV}},
		{.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE, .DescriptorTable = {1, &sceneRange}},
		{.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE, .DescriptorTable = {1, &vertexRange}},
		{.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE, .DescriptorTable = {1, &indexRange}},
		{.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE, .DescriptorTable = {1, &materialRange}},
		{.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE, .DescriptorTable = {1, &materialIndexRange}},
		{.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE, .DescriptorTable = { 1, &emissiveEntitiesIndicesRange } },
		{.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE, .DescriptorTable = {1, &materialTexturesRange}},
		cameraParam,
		toneParam,
	};


	D3D12_STATIC_SAMPLER_DESC sampler = {};
	sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
	sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	sampler.MipLODBias = 0;
	sampler.MaxAnisotropy = 16;
	sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
	sampler.BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
	sampler.MinLOD = 0.0f;
	sampler.MaxLOD = D3D12_FLOAT32_MAX;
	sampler.ShaderRegister = 0;
	sampler.RegisterSpace = 0;
	sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

	D3D12_ROOT_SIGNATURE_DESC rootSigdesc = {};
	rootSigdesc.NumParameters = static_cast<UINT>(std::size(params)),
	rootSigdesc.pParameters = params,
	rootSigdesc.NumStaticSamplers = 1;
	rootSigdesc.pStaticSamplers = &sampler;
	rootSigdesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

	ID3DBlob* blob;
	ID3DBlob* errorblob;
	HRESULT hr = D3D12SerializeRootSignature(&rootSigdesc, D3D_ROOT_SIGNATURE_VERSION_1_0, &blob, &errorblob);
	checkHR(hr, nullptr, "D3D12SerializeRootSignature: ");


	hr = d3dDevice->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&dx12ResourceManager->rootSignature));
	checkHR(hr, errorblob, "CreateRootSignature: ");

	blob->Release();

	dx12ResourceManager->cmdList->SetComputeRootSignature(dx12ResourceManager->rootSignature);

}

void DX12PathTracerPipeLine::initRTShaderTables() {

	if (config.debug) std::cout << "iniRTShaderTables()" << std::endl;

	D3D12_RESOURCE_DESC shaderIDDesc{};
	shaderIDDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	shaderIDDesc.Width = 5 * D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT;;
	shaderIDDesc.Height = 1;
	shaderIDDesc.DepthOrArraySize = 1;
	shaderIDDesc.MipLevels = 1;
	shaderIDDesc.SampleDesc = dx12ResourceManager->NO_AA;
	shaderIDDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

	HRESULT hr = d3dDevice->CreateCommittedResource(&dx12ResourceManager->UPLOAD_HEAP, D3D12_HEAP_FLAG_NONE, &shaderIDDesc, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&dx12ResourceManager->shaderIDs));
	checkHR(hr, nullptr, "initShaderTables, CreateCommittedResource: ");
	ID3D12StateObjectProperties* props;
	dx12ResourceManager->raytracingPSO->QueryInterface(&props);

	void* data;
	auto writeId = [&](const wchar_t* name) {
		void* id = props->GetShaderIdentifier(name);
		memcpy(data, id, D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
		data = static_cast<char*>(data) + D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT;

		};

	dx12ResourceManager->shaderIDs->Map(0, nullptr, &data);
	writeId(L"RayGeneration");
	writeId(L"MissRadiance");
	writeId(L"MissShadow");
	writeId(L"HitGroupRadiance");
	writeId(L"HitGroupShadow");
	dx12ResourceManager->shaderIDs->Unmap(0, nullptr);

	props->Release();


}

void DX12PathTracerPipeLine::initRayTracingPipeline() {
	
	if (config.debug) std::cout << "initRTPipeline()" << std::endl;

	D3D12_DXIL_LIBRARY_DESC lib = {
	.DXILLibrary = { dx12ResourceManager->rsBlob->GetBufferPointer(), dx12ResourceManager->rsBlob->GetBufferSize()} };

	D3D12_HIT_GROUP_DESC hitGroupRadiance = {
	.HitGroupExport = L"HitGroupRadiance",
	.Type = D3D12_HIT_GROUP_TYPE_TRIANGLES,
	.AnyHitShaderImport = nullptr,
	.ClosestHitShaderImport = L"ClosestHitRadiance",
	.IntersectionShaderImport = nullptr
	};

	D3D12_HIT_GROUP_DESC hitGroupShadow = {
	.HitGroupExport = L"HitGroupShadow",
	.Type = D3D12_HIT_GROUP_TYPE_TRIANGLES,
	.AnyHitShaderImport = nullptr,
	.ClosestHitShaderImport = L"ClosestHitShadow",
	.IntersectionShaderImport = nullptr
	};

	D3D12_RAYTRACING_SHADER_CONFIG shaderCfg = {
	.MaxPayloadSizeInBytes = 64,
	.MaxAttributeSizeInBytes = 8, // triangle attribs
	};


	D3D12_GLOBAL_ROOT_SIGNATURE globalSig = { dx12ResourceManager->rootSignature };

	D3D12_RAYTRACING_PIPELINE_CONFIG pipelineCfg = { .MaxTraceRecursionDepth = 4 };

	D3D12_STATE_SUBOBJECT subobjects[] = {
		{.Type = D3D12_STATE_SUBOBJECT_TYPE_DXIL_LIBRARY, .pDesc = &lib},
		{.Type = D3D12_STATE_SUBOBJECT_TYPE_HIT_GROUP, .pDesc = &hitGroupRadiance},
		{.Type = D3D12_STATE_SUBOBJECT_TYPE_HIT_GROUP, .pDesc = &hitGroupShadow},
		{.Type = D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_SHADER_CONFIG, .pDesc = &shaderCfg},
		{.Type = D3D12_STATE_SUBOBJECT_TYPE_GLOBAL_ROOT_SIGNATURE, .pDesc = &globalSig},
		{.Type = D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_PIPELINE_CONFIG, .pDesc = &pipelineCfg},
	};

	D3D12_STATE_OBJECT_DESC psoDesc = {
		.Type = D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE,
		.NumSubobjects = std::size(subobjects),
		.pSubobjects = subobjects };

	HRESULT hr = d3dDevice->CreateStateObject(&psoDesc, IID_PPV_ARGS(&dx12ResourceManager->raytracingPSO));
	checkHR(hr, nullptr, "initPipeLine, CreateStateObject: ");

}

void DX12PathTracerPipeLine::initComputePipeline() {

	std::cout << "initComputePipeline" << std::endl;

	D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc = {};
	psoDesc.pRootSignature = dx12ResourceManager->rootSignature;
	psoDesc.CS = { dx12ResourceManager->csBlob->GetBufferPointer(), dx12ResourceManager->csBlob->GetBufferSize() };
	HRESULT hr = d3dDevice->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(&dx12ResourceManager->computePSO));
	checkHR(hr, nullptr, "Create compute pipeline state");

}

void DX12PathTracerPipeLine::initGlobalDescriptors() {

	//if (config.debug) std::cout << "creating SRVs" << std::endl;

	dx12ResourceManager->global_descriptor_heap_allocator->init(1000);

	// Create views
	D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle = dx12ResourceManager->global_descriptor_heap_allocator->desc_heap->GetCPUDescriptorHandleForHeapStart();

	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
	D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {};

	// UAV for accumulation texture
	uavDesc = {
		.Format = DXGI_FORMAT_R32G32B32A32_FLOAT,
		.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D
	};
	d3dDevice->CreateUnorderedAccessView(dx12ResourceManager->accumulationTexture->default_buffer, nullptr, &uavDesc, cpuHandle);
	cpuHandle.ptr += dx12ResourceManager->global_descriptor_heap_allocator->desc_increment_size;
	dx12ResourceManager->accumulationTexture->heap_index_uav = dx12ResourceManager->global_descriptor_heap_allocator->alloc();

	//if (config.debug) std::cout << "accumulationTexture->heap_index_uav: " << accumulationTexture->heap_index_uav << std::endl;


	// UAV for RNG Buffer
	uavDesc = {};
	uavDesc.Format = DXGI_FORMAT_UNKNOWN,
	uavDesc.Buffer.StructureByteStride = sizeof(uint64_t),
	uavDesc.Buffer.NumElements = dx12ResourceManager->randPattern.size();
	uavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER,

		d3dDevice->CreateUnorderedAccessView(dx12ResourceManager->randBuffer->default_buffer, nullptr, &uavDesc, cpuHandle);
	cpuHandle.ptr += dx12ResourceManager->global_descriptor_heap_allocator->desc_increment_size;
	dx12ResourceManager->randBuffer->heap_index_uav = dx12ResourceManager->global_descriptor_heap_allocator->alloc();

	//if (config.debug) std::cout << "randBuffer->heap_index_uav: " << randBuffer->heap_index_uav << std::endl;

	// SRV for TLAS
	srvDesc = {};
	srvDesc.Format = DXGI_FORMAT_UNKNOWN;
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_RAYTRACING_ACCELERATION_STRUCTURE;
	srvDesc.RaytracingAccelerationStructure.Location = dx12TLASManager->tlas->default_buffer->GetGPUVirtualAddress();

	d3dDevice->CreateShaderResourceView(nullptr, &srvDesc, cpuHandle);
	cpuHandle.ptr += dx12ResourceManager->global_descriptor_heap_allocator->desc_increment_size;
	dx12TLASManager->tlas->heap_index_srv = dx12ResourceManager->global_descriptor_heap_allocator->alloc();

	//if (config.debug) std::cout << "tlas->heap_index_srv: " << tlas->heap_index_srv << std::endl;

	// SRV Vertex Buffer
	for (DX12ResourceHandle* vertexBuffer : dx12ResourceManager->allVertexBuffers) {
		srvDesc = {};
		srvDesc.Format = DXGI_FORMAT_UNKNOWN;
		srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
		srvDesc.Buffer.FirstElement = 0;
		srvDesc.Buffer.NumElements = static_cast<UINT>(vertexBuffer->default_buffer->GetDesc().Width / sizeof(MeshManager::Vertex));
		srvDesc.Buffer.StructureByteStride = sizeof(MeshManager::Vertex);
		d3dDevice->CreateShaderResourceView(vertexBuffer->default_buffer, &srvDesc, cpuHandle);

		cpuHandle.ptr += dx12ResourceManager->global_descriptor_heap_allocator->desc_increment_size;
		vertexBuffer->heap_index_srv = dx12ResourceManager->global_descriptor_heap_allocator->alloc();

		//if (config.debug) std::cout << "srvNumElementsVertex: " << srvDesc.Buffer.NumElements << std::endl;
	}
	// SRV Vertex Index Buffer
	for (DX12ResourceHandle* indexBuffer : dx12ResourceManager->allIndexBuffers) {
		srvDesc = {};
		srvDesc.Format = DXGI_FORMAT_R32_UINT;
		srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
		srvDesc.Buffer.FirstElement = 0;
		srvDesc.Buffer.NumElements = static_cast<UINT>(indexBuffer->default_buffer->GetDesc().Width / sizeof(uint32_t));
		srvDesc.Buffer.StructureByteStride = 0;
		d3dDevice->CreateShaderResourceView(indexBuffer->default_buffer, &srvDesc, cpuHandle);

		cpuHandle.ptr += dx12ResourceManager->global_descriptor_heap_allocator->desc_increment_size;
		indexBuffer->heap_index_srv = dx12ResourceManager->global_descriptor_heap_allocator->alloc();
		//if (config.debug) std::cout << "srvNumElementsIndex: " << srvDesc.Buffer.NumElements << std::endl;
	}

	// SRV Material Buffer
	srvDesc = {};
	srvDesc.Format = DXGI_FORMAT_UNKNOWN;
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
	srvDesc.Buffer.FirstElement = 0;
	srvDesc.Buffer.NumElements = static_cast<UINT>(dx12MaterialManager->dx12materials.size());
	srvDesc.Buffer.StructureByteStride = sizeof(DX12MaterialManager::DX12Material);
	d3dDevice->CreateShaderResourceView(dx12MaterialManager->materialsBuffer->default_buffer, &srvDesc, cpuHandle);

	cpuHandle.ptr += dx12ResourceManager->global_descriptor_heap_allocator->desc_increment_size;
	dx12MaterialManager->materialsBuffer->heap_index_srv = dx12ResourceManager->global_descriptor_heap_allocator->alloc();

	// SRV Material Index Buffer
	srvDesc = {};
	srvDesc.Format = DXGI_FORMAT_R32_UINT;
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
	srvDesc.Buffer.FirstElement = 0;
	srvDesc.Buffer.NumElements = static_cast<UINT>((dx12MaterialManager->materialIndexBuffer->default_buffer->GetDesc().Width) / sizeof(uint32_t));
	srvDesc.Buffer.StructureByteStride = 0;
	d3dDevice->CreateShaderResourceView(dx12MaterialManager->materialIndexBuffer->default_buffer, &srvDesc, cpuHandle);

	cpuHandle.ptr += dx12ResourceManager->global_descriptor_heap_allocator->desc_increment_size;
	dx12MaterialManager->materialIndexBuffer->heap_index_srv = dx12ResourceManager->global_descriptor_heap_allocator->alloc();

	// SRV Emissive Entities Index Buffer SRV
	srvDesc = {};
	srvDesc.Format = DXGI_FORMAT_R32_UINT;
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
	srvDesc.Buffer.FirstElement = 0;
	srvDesc.Buffer.NumElements = static_cast<UINT>((dx12MaterialManager->emissive_entities_indices_buffer->default_buffer->GetDesc().Width) / sizeof(UINT));
	srvDesc.Buffer.StructureByteStride = 0;
	d3dDevice->CreateShaderResourceView(dx12MaterialManager->emissive_entities_indices_buffer->default_buffer, &srvDesc, cpuHandle);

	cpuHandle.ptr += dx12ResourceManager->global_descriptor_heap_allocator->desc_increment_size;
	dx12MaterialManager->emissive_entities_indices_buffer->heap_index_srv = dx12ResourceManager->global_descriptor_heap_allocator->alloc();

	// SRV Material Textures
	for (DX12ResourceHandle* texture : dx12MaterialManager->texture_maps) {
		srvDesc = {};
		srvDesc.Format = texture->default_buffer->GetDesc().Format;
		srvDesc.Texture2D.MipLevels = texture->default_buffer->GetDesc().MipLevels;
		srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		d3dDevice->CreateShaderResourceView(texture->default_buffer, &srvDesc, cpuHandle);

		cpuHandle.ptr += dx12ResourceManager->global_descriptor_heap_allocator->desc_increment_size;
		texture->heap_index_srv = dx12ResourceManager->global_descriptor_heap_allocator->alloc();
		//if (config.debug) std::cout << "srvNumElementsIndex: " << srvDesc.Buffer.NumElements << std::endl;
	}

	// Camera CBV
	cbvDesc = {};
	cbvDesc.BufferLocation = dx12ResourceManager->cameraConstantBuffer->upload_buffer->GetGPUVirtualAddress();
	cbvDesc.SizeInBytes = sizeof(DX12Camera);

	d3dDevice->CreateConstantBufferView(&cbvDesc, cpuHandle);
	cpuHandle.ptr += dx12ResourceManager->global_descriptor_heap_allocator->desc_increment_size;
	dx12ResourceManager->cameraConstantBuffer->heap_index_srv = dx12ResourceManager->global_descriptor_heap_allocator->alloc();

	// SRV for accumulationTexture
	srvDesc = {};
	srvDesc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MipLevels = 1;
	srvDesc.Texture2D.MostDetailedMip = 0;
	d3dDevice->CreateShaderResourceView(dx12ResourceManager->accumulationTexture->default_buffer, &srvDesc, cpuHandle);

	cpuHandle.ptr += dx12ResourceManager->global_descriptor_heap_allocator->desc_increment_size;
	dx12ResourceManager->accumulationTexture->heap_index_srv = dx12ResourceManager->global_descriptor_heap_allocator->alloc();

	// UAV for renderTarget
	uavDesc = {};
	uavDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
	d3dDevice->CreateUnorderedAccessView(dx12ResourceManager->renderTarget->default_buffer, nullptr, &uavDesc, cpuHandle);

	cpuHandle.ptr += dx12ResourceManager->global_descriptor_heap_allocator->desc_increment_size;
	dx12ResourceManager->renderTarget->heap_index_uav = dx12ResourceManager->global_descriptor_heap_allocator->alloc();

	// UAV for maxLumBuffer
	uavDesc = {};
	uavDesc.Format = DXGI_FORMAT_UNKNOWN,
		uavDesc.Buffer.StructureByteStride = sizeof(UINT),
		uavDesc.Buffer.NumElements = 1;
	uavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER,

		d3dDevice->CreateUnorderedAccessView(dx12ResourceManager->maxLumBuffer->default_buffer, nullptr, &uavDesc, cpuHandle);

	cpuHandle.ptr += dx12ResourceManager->global_descriptor_heap_allocator->desc_increment_size;
	dx12ResourceManager->maxLumBuffer->heap_index_uav = dx12ResourceManager->global_descriptor_heap_allocator->alloc();


	// CBV post processing params
	cbvDesc = {};
	cbvDesc.BufferLocation = dx12ResourceManager->toneMappingConstantBuffer->upload_buffer->GetGPUVirtualAddress();
	cbvDesc.SizeInBytes = sizeof(DX12ResourceManager::ToneMappingParams);
	d3dDevice->CreateConstantBufferView(&cbvDesc, cpuHandle);

	cpuHandle.ptr += dx12ResourceManager->global_descriptor_heap_allocator->desc_increment_size;
	dx12ResourceManager->toneMappingConstantBuffer->heap_index_srv = dx12ResourceManager->global_descriptor_heap_allocator->alloc();

}

void DX12PathTracerPipeLine::bindDescriptors() {

	ID3D12DescriptorHeap* heaps[] = { dx12ResourceManager->global_descriptor_heap_allocator->desc_heap };
	dx12ResourceManager->cmdList->SetDescriptorHeaps(1, heaps);

	dx12ResourceManager->cmdList->SetComputeRootSignature(dx12ResourceManager->rootSignature);

	D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle = dx12ResourceManager->global_descriptor_heap_allocator->desc_heap->GetGPUDescriptorHandleForHeapStart();
	UINT64 heap_start = gpuHandle.ptr;

	UINT pos = 0;

	gpuHandle.ptr = heap_start + dx12ResourceManager->accumulationTexture->heap_index_uav * dx12ResourceManager->global_descriptor_heap_allocator->desc_increment_size;
	dx12ResourceManager->cmdList->SetComputeRootDescriptorTable(pos++, gpuHandle); // u0 accum UAV
	
	gpuHandle.ptr = heap_start + dx12ResourceManager->randBuffer->heap_index_uav * dx12ResourceManager->global_descriptor_heap_allocator->desc_increment_size;
	dx12ResourceManager->cmdList->SetComputeRootDescriptorTable(pos++, gpuHandle); // u1 rand UAV

	gpuHandle.ptr = heap_start + dx12ResourceManager->renderTarget->heap_index_uav * dx12ResourceManager->global_descriptor_heap_allocator->desc_increment_size;
	dx12ResourceManager->cmdList->SetComputeRootDescriptorTable(pos++, gpuHandle); // u2 render target

	gpuHandle.ptr = heap_start + dx12ResourceManager->maxLumBuffer->heap_index_uav * dx12ResourceManager->global_descriptor_heap_allocator->desc_increment_size;
	dx12ResourceManager->cmdList->SetComputeRootDescriptorTable(pos++, gpuHandle); // u3 uav



	gpuHandle.ptr = heap_start + dx12ResourceManager->accumulationTexture->heap_index_srv * dx12ResourceManager->global_descriptor_heap_allocator->desc_increment_size;
	dx12ResourceManager->cmdList->SetComputeRootDescriptorTable(pos++, gpuHandle); // accum range SRV

	gpuHandle.ptr = heap_start + dx12TLASManager->tlas->heap_index_srv * dx12ResourceManager->global_descriptor_heap_allocator->desc_increment_size;
	dx12ResourceManager->cmdList->SetComputeRootDescriptorTable(pos++, gpuHandle); // t0 TLAS SRV

	gpuHandle.ptr = heap_start + dx12ResourceManager->allVertexBuffers[0]->heap_index_srv * dx12ResourceManager->global_descriptor_heap_allocator->desc_increment_size;
	dx12ResourceManager->cmdList->SetComputeRootDescriptorTable(pos++, gpuHandle); // t1 vertex buffer SRV

	gpuHandle.ptr = heap_start + dx12ResourceManager->allIndexBuffers[0]->heap_index_srv * dx12ResourceManager->global_descriptor_heap_allocator->desc_increment_size;
	dx12ResourceManager->cmdList->SetComputeRootDescriptorTable(pos++, gpuHandle); // t2 index buffer SRV

	gpuHandle.ptr = heap_start + dx12MaterialManager->materialsBuffer->heap_index_srv * dx12ResourceManager->global_descriptor_heap_allocator->desc_increment_size;
	dx12ResourceManager->cmdList->SetComputeRootDescriptorTable(pos++, gpuHandle); // t3 material buffer SRV

	gpuHandle.ptr = heap_start + dx12MaterialManager->materialIndexBuffer->heap_index_srv * dx12ResourceManager->global_descriptor_heap_allocator->desc_increment_size;
	dx12ResourceManager->cmdList->SetComputeRootDescriptorTable(pos++, gpuHandle); // t3 material index buffer SRV

	gpuHandle.ptr = heap_start + dx12MaterialManager->emissive_entities_indices_buffer->heap_index_srv * dx12ResourceManager->global_descriptor_heap_allocator->desc_increment_size;
	dx12ResourceManager->cmdList->SetComputeRootDescriptorTable(pos++, gpuHandle); // emissive entities indices

	gpuHandle.ptr = heap_start + dx12MaterialManager->texture_maps[0]->heap_index_srv * dx12ResourceManager->global_descriptor_heap_allocator->desc_increment_size;
	dx12ResourceManager->cmdList->SetComputeRootDescriptorTable(pos++, gpuHandle); // t3 material index buffer SRV

	dx12ResourceManager->cmdList->SetComputeRootConstantBufferView(pos++, dx12ResourceManager->cameraConstantBuffer->upload_buffer->GetGPUVirtualAddress()); // b0 camera cbv

	dx12ResourceManager->cmdList->SetComputeRootConstantBufferView(pos++, dx12ResourceManager->toneMappingConstantBuffer->upload_buffer->GetGPUVirtualAddress()); // maxLum, etc


}

// command submission

void DX12PathTracerPipeLine::render() {

	dx12ResourceManager->frames = (config.accumulate && !entityManager->camera->camMoved && !UI::accumulationUpdate) ? dx12ResourceManager->frames + 1 : 1;
	dx12ResourceManager->samples = dx12ResourceManager->frames * config.raysPerPixel;
	UI::numRays = dx12ResourceManager->samples;
	dx12ResourceManager->seed++;

	if (UI::accelUpdate) {

		if (entityManager->entities.size() > config.maxInstances) { // full rebuild
			dx12TLASManager->rebuildTLAS(dx12ResourceManager->instances, dx12ResourceManager->NUM_INSTANCES);
		}
		else { // update in place
			dx12ResourceManager->initDX12Entites(true);
			dx12ResourceManager->updateTransforms();
			dx12TLASManager->updateTLAS(dx12ResourceManager->instances, dx12ResourceManager->NUM_INSTANCES);
			dx12MaterialManager->initEmissiveIndexBuffer(true);
		}

	}

	if (UI::materialUpdate) {
		dx12MaterialManager->initMaterials(true, true, true);
		dx12MaterialManager->initMaterialBuffers(true);
		dx12MaterialManager->initEmissiveIndexBuffer(true);
	}

	//dx12ResourceManager->initGlobalDescriptors(dx12AccelerationStructureManager->tlas); // dynamic buffer resizing
	bindDescriptors();

	dx12ResourceManager->updateCamera();
	traceRays();
	postProcess();

	ImGui::Render();

}

void DX12PathTracerPipeLine::traceRays() {

	dx12ResourceManager->cmdList->SetPipelineState1(dx12ResourceManager->raytracingPSO);

	// clear accumulation texture
	if (!config.accumulate || entityManager->camera->camMoved || UI::accumulationUpdate || UI::accelUpdate) {
		// slot 0 UAV for accumulation texture
		D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle = dx12ResourceManager->global_descriptor_heap_allocator->desc_heap->GetCPUDescriptorHandleForHeapStart();
		UINT64 heap_start = cpuHandle.ptr;
		cpuHandle.ptr = heap_start + dx12ResourceManager->accumulationTexture->heap_index_uav * dx12ResourceManager->global_descriptor_heap_allocator->desc_increment_size;

		D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle = dx12ResourceManager->global_descriptor_heap_allocator->desc_heap->GetGPUDescriptorHandleForHeapStart();
		heap_start = gpuHandle.ptr;
		gpuHandle.ptr = heap_start + dx12ResourceManager->accumulationTexture->heap_index_uav * dx12ResourceManager->global_descriptor_heap_allocator->desc_increment_size;

		dx12ResourceManager->cmdList->ClearUnorderedAccessViewFloat(gpuHandle, cpuHandle, dx12ResourceManager->accumulationTexture->default_buffer, dx12ResourceManager->clearColor, 0, nullptr);

		UI::accelUpdate = false;
		UI::accumulationUpdate = false;
		UI::materialUpdate = false;
	}

	// Dispatch rays

	auto rtDesc = dx12ResourceManager->renderTarget->default_buffer->GetDesc();

	UINT recordSize = D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT;
	D3D12_GPU_VIRTUAL_ADDRESS base = dx12ResourceManager->shaderIDs->GetGPUVirtualAddress();

	D3D12_DISPATCH_RAYS_DESC dispatchDesc = {};

	dispatchDesc.RayGenerationShaderRecord.StartAddress = base + 0 * recordSize;
	dispatchDesc.RayGenerationShaderRecord.SizeInBytes = recordSize;

	dispatchDesc.MissShaderTable.StartAddress = base + 1 * recordSize;
	dispatchDesc.MissShaderTable.SizeInBytes = 2 * recordSize;
	dispatchDesc.MissShaderTable.StrideInBytes = recordSize;

	dispatchDesc.HitGroupTable.StartAddress = base + 3 * recordSize;
	dispatchDesc.HitGroupTable.SizeInBytes = 2 * recordSize;
	dispatchDesc.HitGroupTable.StrideInBytes = recordSize;

	dispatchDesc.Width = rtDesc.Width;
	dispatchDesc.Height = rtDesc.Height;
	dispatchDesc.Depth = 1;

	for (size_t i = 0; i < config.raysPerPixel; i++) {
		dx12ResourceManager->cmdList->DispatchRays(&dispatchDesc);
	}

	// transition accumulation texture from SRV TO UAV for tone mapping
	D3D12_RESOURCE_BARRIER barrier = {};
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrier.Transition.pResource = dx12ResourceManager->accumulationTexture->default_buffer;
	barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
	barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
	dx12ResourceManager->cmdList->ResourceBarrier(1, &barrier);


}

void DX12PathTracerPipeLine::postProcess() {

	// tone mapping

	dx12ResourceManager->cmdList->SetPipelineState(dx12ResourceManager->computePSO);

	// start compute shader

	UINT groupsX = static_cast<UINT>((dx12ResourceManager->renderTarget->default_buffer->GetDesc().Width + 15) / 16);
	UINT groupsY = static_cast<UINT>((dx12ResourceManager->renderTarget->default_buffer->GetDesc().Height + 15) / 16);

	dx12ResourceManager->toneMappingParams->stage = 0; // max Luminance
	dx12ResourceManager->updateToneParams();

	dx12ResourceManager->cmdList->Dispatch(groupsX, groupsY, 1);

	 // tone Map
	if (config.tone_mapper == reinhard_extended) dx12ResourceManager->toneMappingParams->stage = 1;
	if (config.tone_mapper == hable_filmic) dx12ResourceManager->toneMappingParams->stage = 2;
	if (config.tone_mapper == aces_filmic) dx12ResourceManager->toneMappingParams->stage = 3;
	dx12ResourceManager->updateToneParams();

	dx12ResourceManager->cmdList->Dispatch(groupsX, groupsY, 1);

	// transition accumulation texture from SRV TO UAV for next frame
	D3D12_RESOURCE_BARRIER barrier = {};
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrier.Transition.pResource = dx12ResourceManager->accumulationTexture->default_buffer;
	barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
	barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
	dx12ResourceManager->cmdList->ResourceBarrier(1, &barrier);

}

void DX12PathTracerPipeLine::imguiPresent(ID3D12Resource* backBuffer) {
	// set descriptor heap for imgui
	ID3D12DescriptorHeap* heaps[] = { ImGuiDescAlloc->heap };
	dx12ResourceManager->cmdList->SetDescriptorHeaps(ARRAYSIZE(heaps), heaps);

	// Get current backbuffer RTV
	UINT bbIndex = dx12ResourceManager->swapChain->GetCurrentBackBufferIndex();
	D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = dx12ResourceManager->rtvHeap->GetCPUDescriptorHandleForHeapStart();
	rtvHandle.ptr += bbIndex * dx12ResourceManager->rtvDescriptorSize;

	dx12ResourceManager->cmdList->OMSetRenderTargets(1, &rtvHandle, FALSE, nullptr);

	ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), dx12ResourceManager->cmdList);
}

void DX12PathTracerPipeLine::present() {

	// copy image onto swap chain's current buffer
	ID3D12Resource* backBuffer;
	dx12ResourceManager->swapChain->GetBuffer(dx12ResourceManager->swapChain->GetCurrentBackBufferIndex(), IID_PPV_ARGS(&backBuffer));
	backBuffer->SetName(L"Back Buffer");

	// transition render target and back buffer to copy src/dst states
	barrier(dx12ResourceManager->renderTarget->default_buffer, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
	barrier(backBuffer, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_COPY_DEST);

	dx12ResourceManager->cmdList->CopyResource(backBuffer, dx12ResourceManager->renderTarget->default_buffer);

	barrier(backBuffer, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_RENDER_TARGET);

	imguiPresent(backBuffer);

	barrier(backBuffer, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);

	// transition for present

	// transition for next frame
	barrier(dx12ResourceManager->renderTarget->default_buffer, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

	// release refernce to backbuffer
	backBuffer->Release();

	dx12ResourceManager->cmdList->Close();
	ID3D12CommandList* lists[] = { dx12ResourceManager->cmdList };
	cmdQueue->ExecuteCommandLists(1, lists);

	dx12ResourceManager->waitForGPU();
	dx12ResourceManager->swapChain->Present(0, DXGI_PRESENT_ALLOW_TEARING);

	// To finish whole frame??
	dx12ResourceManager->cmdAlloc->Reset();
	dx12ResourceManager->cmdList->Reset(dx12ResourceManager->cmdAlloc, nullptr);
}

void DX12PathTracerPipeLine::quit() {
	delete meshManager;
}

void DX12PathTracerPipeLine::checkHR(HRESULT hr, ID3DBlob* errorblob, std::string context) {
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

void DX12PathTracerPipeLine::barrier(ID3D12Resource* resource, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after) {

	D3D12_RESOURCE_BARRIER rb = {
		.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION,
		.Transition = {.pResource = resource,
						.StateBefore = before,
						.StateAfter = after},
	};

	dx12ResourceManager->cmdList->ResourceBarrier(1, &rb);

}

void DX12PathTracerPipeLine::initImgui() {

	// thanks grok

	ImGuiDescAlloc = new ImGuiDescriptorAllocator{};
	ImGuiDescAlloc->init(d3dDevice, 64);

	// for swap chain
	D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
	rtvHeapDesc.NumDescriptors = dx12ResourceManager->frameIndexInFlight;
	rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
	rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	d3dDevice->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&dx12ResourceManager->rtvHeap));
	dx12ResourceManager->rtvHeap->SetName(L"ImGui RTV Heap");
	dx12ResourceManager->rtvDescriptorSize = d3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

	// ImGui init
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO();
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
	ImGui_ImplSDL3_InitForD3D(window->getSDLHandle());

	ImGui_ImplDX12_InitInfo init_info = {};
	init_info.Device = d3dDevice;
	init_info.CommandQueue = cmdQueue;
	init_info.NumFramesInFlight = dx12ResourceManager->frameIndexInFlight;
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
void DX12PathTracerPipeLine::createBackBufferRTVs() {

	D3D12_CPU_DESCRIPTOR_HANDLE rtvStart = dx12ResourceManager->rtvHeap->GetCPUDescriptorHandleForHeapStart();

	for (UINT i = 0; i < dx12ResourceManager->frameIndexInFlight; i++) {
		ID3D12Resource* backBuffer = nullptr;
		HRESULT hr = dx12ResourceManager->swapChain->GetBuffer(i, IID_PPV_ARGS(&backBuffer));
		checkHR(hr, nullptr, "ImGui Backbuffer SRV");

		D3D12_CPU_DESCRIPTOR_HANDLE handle = rtvStart;
		handle.ptr += i * dx12ResourceManager->rtvDescriptorSize;

		d3dDevice->CreateRenderTargetView(backBuffer, nullptr, handle);

		backBuffer->Release();
	}

	ImGui_ImplDX12_InvalidateDeviceObjects();
	ImGui_ImplDX12_CreateDeviceObjects();

}