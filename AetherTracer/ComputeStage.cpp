#include "ComputeStage.h"

#include <d3dcompiler.h>
#include <random>
#include <random>


ComputeStage::ComputeStage(ResourceManager* resourceManager, MeshManager* meshManager, MaterialManager* materialManager, EntityManager* entityManager)
	: rm(resourceManager), meshManager(meshManager), materialManager(materialManager), entityManager(entityManager) {
}

void ComputeStage::initStage() {
	initComputeRootSignature();
	initComputePipeline();
	initComputeDescriptors();
}

void ComputeStage::updateRand() {

	std::cout << "update rand" << std::endl;

	std::random_device rd;
	std::mt19937 gen(rd());
	std::uniform_int_distribution<uint64_t> dist;

	std::cout << "width: " << rm->width << ", height: " << rm->height << std::endl;

	rm->randPattern.resize(rm->width * rm->height);

	for (uint64_t x = 0; x < rm->width; x++) {
		
		for (uint64_t y = 0; y < rm->height; y++) {

			uint64_t state = ((y << 16u) | x) ^ (rm->seed * 1664525u * static_cast<uint64_t>(GetTickCount64())) ^ 0xdeadbeefu;

			// PCG hash function

			uint64_t word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
			word = (word >> 22u) ^ word;

			uint64_t rot = state >> 28u;

			word = (word >> rot) | (word << (32u - rot));

			rm->randPattern[x + y * rm->width] = word;
			//rm->randPattern[x + y * rm->width] = dist(gen);
		}
	}

	size_t randSize = rm->randPattern.size() * sizeof(uint64_t);
	rm->randBuffer = createBuffers(rm->randPattern.data(), randSize, D3D12_RESOURCE_STATE_COMMON, true);
	rm->randBuffer->defaultBuffers->SetName(L"rng Defaut Buffer");
	pushBuffer(rm->randBuffer, randSize, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

}

void ComputeStage::initMaxLumBuffer() {

	std::vector<UINT> lum;
	lum.resize(1);
	lum[0] = 1u;
	rm->maxLumBuffer = createBuffers(lum.data(), sizeof(UINT), D3D12_RESOURCE_STATE_COMMON, true);
	rm->maxLumBuffer->defaultBuffers->SetName(L"Max Luminance Buffer");
	pushBuffer(rm->maxLumBuffer, sizeof(UINT), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);


};

void ComputeStage::initRenderTarget() {

	std::cout << "initRenderTarget"<<std::endl;

	D3D12_RESOURCE_DESC rtDesc = {
	   .Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D,
	   .Width = rm->width,
	   .Height = rm->height,
	   .DepthOrArraySize = 1,
	   .MipLevels = 1,
	   .Format = DXGI_FORMAT_R8G8B8A8_UNORM,
	   .SampleDesc = rm->NO_AA,
	   .Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS };

	HRESULT hr = rm->d3dDevice->CreateCommittedResource(&DEFAULT_HEAP, D3D12_HEAP_FLAG_NONE, &rtDesc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(&rm->renderTarget));
	checkHR(hr, nullptr, "Create render target");
	rm->renderTarget->SetName(L"Render Target");

	rm->randPattern.resize(rm->width * rm->height);

}

void ComputeStage::updateToneParams() {

	//std::cout << "updateTonePrams" << std::endl;

	if (!rm->toneMappingParams) {
		rm->toneMappingParams = new ResourceManager::ToneMappingParams();
	}

	rm->toneMappingParams->numIts = rm->iterations;

	if (!rm->toneMappingConstantBuffer) {

		std::cout << "Creating tone mapping buffer" << std::endl;
		rm->toneMappingConstantBuffer = new ResourceManager::Buffer();
		createCBV(rm->toneMappingConstantBuffer, sizeof(ResourceManager::ToneMappingParams));
		rm->toneMappingConstantBuffer->defaultBuffers->SetName(L"Tone Mapping Default Buffer");
	}

	void* mapped = nullptr;
	rm->toneMappingConstantBuffer->defaultBuffers->Map(0, nullptr, &mapped);
	memcpy(mapped, rm->toneMappingParams, sizeof(ResourceManager::ToneMappingParams));
	rm->toneMappingConstantBuffer->defaultBuffers->Unmap(0, nullptr);

}

void ComputeStage::initComputeRootSignature() {

	std::cout << "initComputeRootSignature" << std::endl;
	// t0, accumulation texture
	D3D12_DESCRIPTOR_RANGE accumRange = {
		.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
		.NumDescriptors = 1,
		.BaseShaderRegister = 0,
		.RegisterSpace = 0,
		.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND
	};

	// u0, render target
	D3D12_DESCRIPTOR_RANGE rtRange = {
		.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV,
		.NumDescriptors = 1,
		.BaseShaderRegister = 0,
		.RegisterSpace = 0,
		.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND
	};

	// u1, maxLum
	D3D12_DESCRIPTOR_RANGE maxLumRange = {
		.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV,
		.NumDescriptors = 1,
		.BaseShaderRegister = 1,
		.RegisterSpace = 0,
		.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND
	};

	// tone map params, cbv
	D3D12_ROOT_PARAMETER toneParam = {};
	toneParam.ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
	toneParam.Descriptor.ShaderRegister = 0;
	toneParam.Descriptor.RegisterSpace = 0;
	toneParam.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

	D3D12_ROOT_PARAMETER params[4] = {
		{.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE, .DescriptorTable = {1, &accumRange}},
		{.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE, .DescriptorTable = {1, &rtRange}},
		{.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE, .DescriptorTable = {1, &maxLumRange}},

	};

	params[3] = toneParam;

	D3D12_ROOT_SIGNATURE_DESC desc = {
		.NumParameters = 4,
		.pParameters = params,
		.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE
	};

	ID3DBlob* blob;
	ID3DBlob* errorBlob;
	HRESULT hr = D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1_0, &blob, &errorBlob);
	checkHR(hr, errorBlob, "Serialize Compute root signature");

	hr = rm->d3dDevice->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&computeRootSignature));
	checkHR(hr, errorBlob, "Serialize Compute root signature");
	blob->Release();

}

void ComputeStage::initComputePipeline() {

	std::cout << "initComputePipeline" << std::endl;

	D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc = {};
	psoDesc.pRootSignature = computeRootSignature;
	psoDesc.CS = { csBlob->GetBufferPointer(), csBlob->GetBufferSize() };
	HRESULT hr = rm->d3dDevice->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(&computePSO));
	checkHR(hr, nullptr, "Create compute pipeline state");

}

void ComputeStage::initComputeDescriptors() {

	std::cout << "initComputeDescriptors" << std::endl;

	UINT numDescriptors = 4; // SRV accumulationTexture, UAV renderTarget, UAV maxLum, CBV params
	descriptorIncrementSize = rm->d3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {
	.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
	.NumDescriptors = 4,
	.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE
	};

	HRESULT hr = rm->d3dDevice->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&computeDescHeap));
	checkHR(hr, nullptr, "CreateDescriptorHeap");

	D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle = computeDescHeap->GetCPUDescriptorHandleForHeapStart();
	descriptorIncrementSize = rm->d3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	// slot 0 SRV for accumulationTexture
	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MipLevels = 1;
	srvDesc.Texture2D.MostDetailedMip = 0;
	rm->d3dDevice->CreateShaderResourceView(rm->accumulationTexture, &srvDesc, cpuHandle);
	cpuHandle.ptr += descriptorIncrementSize;

	// slot 1 UAV for renderTarget
	D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
	uavDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
	rm->d3dDevice->CreateUnorderedAccessView(rm->renderTarget, nullptr, &uavDesc, cpuHandle);
	cpuHandle.ptr += descriptorIncrementSize;

	// slot 2 UAV for maxLumBuffer
	uavDesc = {};
	uavDesc.Format = DXGI_FORMAT_UNKNOWN,
	uavDesc.Buffer.StructureByteStride = sizeof(UINT),
	uavDesc.Buffer.NumElements = 1;
	uavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER,
	rm->d3dDevice->CreateUnorderedAccessView(rm->maxLumBuffer->defaultBuffers, nullptr, &uavDesc, cpuHandle);
	cpuHandle.ptr += descriptorIncrementSize;

	// CBV post processing params
	D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {};
	cbvDesc.BufferLocation = rm->toneMappingConstantBuffer->defaultBuffers->GetGPUVirtualAddress();
	cbvDesc.SizeInBytes = sizeof(ResourceManager::ToneMappingParams);
	rm->d3dDevice->CreateConstantBufferView(&cbvDesc, cpuHandle);
	cpuHandle.ptr += descriptorIncrementSize;

}


void ComputeStage::postProcess() {

	ID3D12DescriptorHeap* heaps[] = { computeDescHeap };
	rm->cmdList->SetDescriptorHeaps(1, heaps);

	//updateToneParams();

	// tone mapping

	rm->cmdList->SetPipelineState(computePSO);
	rm->cmdList->SetComputeRootSignature(computeRootSignature);

	D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle = computeDescHeap->GetGPUDescriptorHandleForHeapStart();
	descriptorIncrementSize = rm->d3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	rm->cmdList->SetComputeRootDescriptorTable(0, gpuHandle); // t0
	gpuHandle.ptr += descriptorIncrementSize;
	rm->cmdList->SetComputeRootDescriptorTable(1, gpuHandle); // u1
	gpuHandle.ptr += descriptorIncrementSize;
	rm->cmdList->SetComputeRootDescriptorTable(2, gpuHandle); // u1

	rm->cmdList->SetComputeRootConstantBufferView(3, rm->toneMappingConstantBuffer->defaultBuffers->GetGPUVirtualAddress()); // maxLum, etc

	// start compute shader

	UINT groupsX = (rm->renderTarget->GetDesc().Width + 15) / 16;
	UINT groupsY = (rm->renderTarget->GetDesc().Height + 15) / 16;

	rm->toneMappingParams->stage = 0; // max Luminance
	updateToneParams();

	rm->cmdList->Dispatch(groupsX, groupsY, 1);

	rm->toneMappingParams->stage  = 1; // tone Map
	updateToneParams();

	rm->cmdList->Dispatch(groupsX, groupsY, 1);

	// transition accumulation texture from SRV TO UAV for next frame
	D3D12_RESOURCE_BARRIER barrier = {};
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrier.Transition.pResource = rm->accumulationTexture;
	barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
	barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
	rm->cmdList->ResourceBarrier(1, &barrier);

}


ResourceManager::Buffer* ComputeStage::createBuffers(const void* data, size_t byteSize, D3D12_RESOURCE_STATES finalState, bool UAV) {
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

	if (UAV) {
		DESC.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
	}
	ID3D12Resource* target = nullptr;
	rm->d3dDevice->CreateCommittedResource(
		&DEFAULT_HEAP,
		D3D12_HEAP_FLAG_NONE,
		&DESC,
		finalState,
		nullptr,
		IID_PPV_ARGS(&target)
	);

	ResourceManager::Buffer* buffer = new ResourceManager::Buffer();
	buffer->uploadBuffers = upload;
	buffer->defaultBuffers = target;
	return buffer;
}

void ComputeStage::pushBuffer(ResourceManager::Buffer* buffer, size_t dataSize, D3D12_RESOURCE_STATES state_before, D3D12_RESOURCE_STATES state_after) {

	// Barrier - transition vertex target to COPY_DEST
	D3D12_RESOURCE_BARRIER barrier = {};
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
	barrier.Transition.pResource = buffer->defaultBuffers;
	barrier.Transition.StateBefore = state_before;
	barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
	barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	rm->cmdList->ResourceBarrier(1, &barrier);

	// Copy to GPU
	rm->cmdList->CopyBufferRegion(buffer->defaultBuffers, 0, buffer->uploadBuffers, 0, dataSize);

	// Barrier - transition to final state
	barrier.Transition.pResource = buffer->defaultBuffers;
	barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
	barrier.Transition.StateAfter = state_after;
	rm->cmdList->ResourceBarrier(1, &barrier);
}

void ComputeStage::createCBV(ResourceManager::Buffer* buffer, size_t byteSize) {

	D3D12_RESOURCE_DESC cbDesc = {};
	cbDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	cbDesc.Width = byteSize;
	cbDesc.Height = 1;
	cbDesc.DepthOrArraySize = 1;
	cbDesc.MipLevels = 1;
	cbDesc.SampleDesc = rm->NO_AA;
	cbDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
	cbDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

	D3D12_HEAP_PROPERTIES uploadHeapProps = { D3D12_HEAP_TYPE_UPLOAD };

	HRESULT hr = rm->d3dDevice->CreateCommittedResource(
		&uploadHeapProps,
		D3D12_HEAP_FLAG_NONE,
		&cbDesc,
		D3D12_RESOURCE_STATE_GENERIC_READ, // cbv
		nullptr,
		IID_PPV_ARGS(&buffer->defaultBuffers)
	);
	checkHR(hr, nullptr, "Creating CBV");

}

void ComputeStage::checkHR(HRESULT hr, ID3DBlob* errorblob, std::string context) {
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

void ComputeStage::loadShaders() {

	HRESULT hr = D3DReadFileToBlob(L"postprocessingshader.cso", &csBlob);
	checkHR(hr, nullptr, "Loading postprocessingshader");

}

void ComputeStage::flush() {
	rm->cmdQueue->Signal(rm->fence, rm->fenceState);
	rm->fence->SetEventOnCompletion(rm->fenceState++, nullptr);
}