#include "ComputeStage.h"
#include <d3dcompiler.h> // for compiling shaders
#include <random>


ComputeStage::ComputeStage(ResourceManager* resourceManager, MeshManager* meshManager, MaterialManager* materialManager, EntityManager* entityManager, ID3D12GraphicsCommandList4* cmdList, ID3D12Device5* d3dDevice)
	: rm(resourceManager), meshManager(meshManager), materialManager(materialManager), entityManager(entityManager), cmdList(cmdList), d3dDevice(d3dDevice) {
}

void ComputeStage::init() {

	initRenderTarget();
	updateRand();
	updateToneParams();

	initComputeRootSignature();
	initComputePipeline();
	initComputeDescriptors();
}

void ComputeStage::updateRand() {

	std::random_device rd;
	std::mt19937 gen(rd());
	std::uniform_int_distribution<UINT> dist;

	for (UINT x = 0; x < rm->renderTarget->GetDesc().Width; x++) {

		for (UINT y = 0; y < rm->renderTarget->GetDesc().Height; y++) {

			UINT state = ((y << 16u) | x) ^ (rm->num_frames * 1664525u * static_cast<UINT>(GetTickCount64())) ^ 0xdeadbeefu;

			// PCG hash function

			UINT word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
			word = (word >> 22u) ^ word;

			UINT rot = state >> 28u;

			word = (word >> rot) | (word << (32u - rot));

			//rm->randPattern[x + y * rm->renderTarget->GetDesc().Width] = word;
			//rm->randPattern[x + y * rm->renderTarget->GetDesc().Width] = dist(gen);
			rm->randPattern[x + y * rm->renderTarget->GetDesc().Width] = x + y * rm->renderTarget->GetDesc().Width;
		}
	}

	size_t randSize = rm->randPattern.size() * sizeof(UINT);
	rm->randBuffer = createBuffers(rm->randPattern.data(), randSize, D3D12_RESOURCE_STATE_COMMON);

	pushBuffer(rm->randBuffer, randSize, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);


}

void ComputeStage::initRenderTarget() {

	D3D12_RESOURCE_DESC rtDesc = {
	   .Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D,
	   .Width = rm->width,
	   .Height = rm->height,
	   .DepthOrArraySize = 1,
	   .MipLevels = 1,
	   .Format = DXGI_FORMAT_R8G8B8A8_UNORM,
	   .SampleDesc = {},
	   .Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS };

	HRESULT hr = d3dDevice->CreateCommittedResource(&DEFAULT_HEAP, D3D12_HEAP_FLAG_NONE, &rtDesc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(&rm->renderTarget));
	checkHR(hr, nullptr, "Create render target");

}

void ComputeStage::updateToneParams() {

	rm->toneMappingParams->numIts = rm->num_frames;

	if (!rm->toneMappingConstantBuffer) {

		std::cout << "Creating tone mapping buffer" << std::endl;
		createCBV(rm->toneMappingConstantBuffer, sizeof(ResourceManager::ToneMappingParams));

	}

	void* mapped = nullptr;
	rm->toneMappingConstantBuffer->defaultBuffers->Map(0, nullptr, &mapped);
	memcpy(mapped, rm->toneMappingParams, sizeof(ResourceManager::ToneMappingParams));
	rm->toneMappingConstantBuffer->defaultBuffers->Unmap(0, nullptr);

}

void ComputeStage::initComputeRootSignature() {

	std::cout << "initComputeRootSignature" << std::endl;

	// t0, accumulation texture
	D3D12_DESCRIPTOR_RANGE srvRange = {
		.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
		.NumDescriptors = 1,
		.BaseShaderRegister = 0,
		.RegisterSpace = 0,
		.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND

	};

	// u0, render target
	D3D12_DESCRIPTOR_RANGE uavRange = {
		.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV,
		.NumDescriptors = 1,
		.BaseShaderRegister = 0,
		.RegisterSpace = 0,
		.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND
	};

	D3D12_ROOT_PARAMETER toneParam = {};
	toneParam.ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
	toneParam.Descriptor.ShaderRegister = 0;
	toneParam.Descriptor.RegisterSpace = 0;
	toneParam.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

	D3D12_ROOT_PARAMETER params[3] = {
		{.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE, .DescriptorTable = {1, &srvRange}},
		{.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE, .DescriptorTable = {1, &uavRange}},
	};

	params[2] = toneParam;

	D3D12_ROOT_SIGNATURE_DESC desc = {
		.NumParameters = 3,
		.pParameters = params,
		.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE
	};

	ID3DBlob* blob;
	ID3DBlob* errorBlob;
	HRESULT hr = D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1_0, &blob, &errorBlob);
	checkHR(hr, errorBlob, "Serialize Compute root signature");

	hr = d3dDevice->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&computeRootSignature));
	checkHR(hr, errorBlob, "Serialize Compute root signature");
	blob->Release();

}

void ComputeStage::initComputePipeline() {

	std::cout << "initComputePipeline" << std::endl;

	D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc = {};
	psoDesc.pRootSignature = computeRootSignature;
	psoDesc.CS = { csBlob->GetBufferPointer(), csBlob->GetBufferSize() };
	HRESULT hr = d3dDevice->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(&computePSO));
	checkHR(hr, nullptr, "Create compute pipeline state");

}

void ComputeStage::initComputeDescriptors() {

	std::cout << "initComputeDescriptors" << std::endl;

	UINT numDescriptors = 3; // SRV accumulationTexture, UAV renderTarget, CBV params
	descriptorIncrementSize = d3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {
	.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
	.NumDescriptors = 3,
	.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE
	};

	HRESULT hr = d3dDevice->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&computeDescHeap));
	checkHR(hr, nullptr, "CreateDescriptorHeap");

	D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle = computeDescHeap->GetCPUDescriptorHandleForHeapStart();
	descriptorIncrementSize = d3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	// slot 0 SRV for accumulationTexture
	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MipLevels = 1;
	srvDesc.Texture2D.MostDetailedMip = 0;
	d3dDevice->CreateShaderResourceView(rm->accumulationTexture, &srvDesc, cpuHandle);
	cpuHandle.ptr += descriptorIncrementSize;

	// slot 1 UAV for renderTarget
	D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
	uavDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
	d3dDevice->CreateUnorderedAccessView(rm->renderTarget, nullptr, &uavDesc, cpuHandle);
	cpuHandle.ptr += descriptorIncrementSize;

	// CBV post processing params
	D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {};
	cbvDesc.BufferLocation = rm->toneMappingConstantBuffer->defaultBuffers->GetGPUVirtualAddress();
	cbvDesc.SizeInBytes = sizeof(ResourceManager::ToneMappingParams);
	d3dDevice->CreateConstantBufferView(&cbvDesc, cpuHandle);
	cpuHandle.ptr += descriptorIncrementSize;

}

void ComputeStage::postProcess() {

	ID3D12DescriptorHeap* heaps[] = { computeDescHeap };
	cmdList->SetDescriptorHeaps(1, heaps);

	updateToneParams();

	// tone mapping

	cmdList->SetPipelineState(computePSO);
	cmdList->SetComputeRootSignature(computeRootSignature);

	D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle = computeDescHeap->GetGPUDescriptorHandleForHeapStart();
	descriptorIncrementSize = d3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	cmdList->SetComputeRootDescriptorTable(0, gpuHandle); // t0
	gpuHandle.ptr += descriptorIncrementSize;
	cmdList->SetComputeRootDescriptorTable(1, gpuHandle); // u1
	gpuHandle.ptr += descriptorIncrementSize;
	cmdList->SetComputeRootConstantBufferView(2, rm->toneMappingConstantBuffer->defaultBuffers->GetGPUVirtualAddress()); // maxLum, etc

	// start compute shader

	UINT groupsX = (rm->renderTarget->GetDesc().Width + 15) / 16;
	UINT groupsY = (rm->renderTarget->GetDesc().Height + 15) / 16;
	cmdList->Dispatch(groupsX, groupsY, 1);

	// transition accumulation texture from SRV TO UAV for next frame
	D3D12_RESOURCE_BARRIER barrier = {};
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrier.Transition.pResource = rm->accumulationTexture;
	barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
	barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
	cmdList->ResourceBarrier(1, &barrier);

}


ResourceManager::Buffer* ComputeStage::createBuffers(const void* data, size_t byteSize, D3D12_RESOURCE_STATES finalState) {
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
	cmdList->ResourceBarrier(1, &barrier);

	// Copy to GPU
	cmdList->CopyBufferRegion(buffer->defaultBuffers, 0, buffer->uploadBuffers, 0, dataSize);

	// Barrier - transition to final state
	barrier.Transition.pResource = buffer->defaultBuffers;
	barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
	barrier.Transition.StateAfter = state_after;
	cmdList->ResourceBarrier(1, &barrier);
}

void ComputeStage::createCBV(ResourceManager::Buffer* buffer, size_t byteSize) {

	D3D12_RESOURCE_DESC cbDesc = {};
	cbDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	cbDesc.Width = byteSize;
	cbDesc.Height = 1;
	cbDesc.DepthOrArraySize = 1;
	cbDesc.MipLevels = 1;
	cbDesc.SampleDesc.Count = 1;
	cbDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
	cbDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

	D3D12_HEAP_PROPERTIES uploadHeapProps = { D3D12_HEAP_TYPE_UPLOAD };

	HRESULT hr = d3dDevice->CreateCommittedResource(
		&uploadHeapProps,
		D3D12_HEAP_FLAG_NONE,
		&cbDesc,
		D3D12_RESOURCE_STATE_GENERIC_READ, // cbv
		nullptr,
		IID_PPV_ARGS(&buffer->defaultBuffers)
	);
	checkHR(hr, nullptr, "Create camera CB");

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