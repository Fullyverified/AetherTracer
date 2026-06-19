#include "DX12ResourceManager.h"
#include <random>

#include <d3dcompiler.h>

DX12ResourceManager::DX12ResourceManager(MeshManager* meshManager, MaterialManager* materialManager, EntityManager* entityManager, IDXGIFactory4* factory, ID3D12Device5* d3dDevice, ID3D12CommandQueue* cmdQueue, ID3D12CommandAllocator* cmdAlloc, ID3D12GraphicsCommandList4* cmdList)
	: meshManager(meshManager), materialManager(materialManager), entityManager(entityManager), factory(factory), d3dDevice(d3dDevice), cmdQueue(cmdQueue), cmdAlloc(cmdAlloc), cmdList(cmdList) {

	global_descriptor_heap_allocator = new DX12ResourceManager::DescriptorAllocator{};

	renderTarget = new DX12ResourceHandle;
	accumulationTexture = new DX12ResourceHandle;
};

void DX12ResourceManager::initDescriptorHeap(DX12ResourceManager::DescriptorAllocator* descriptor_allocator, UINT num_descriptors, bool is_shader_visible, std::string descriptor_name) {

	D3D12_DESCRIPTOR_HEAP_FLAGS flags = is_shader_visible ? D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE : D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

	D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {
		.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
		.NumDescriptors = num_descriptors,
		.Flags = flags
	};

	std::cout << "d3dDevice->CreateDescriptorHeap" << std::endl;
	HRESULT hr = d3dDevice->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&descriptor_allocator->desc_heap));

	checkHR(hr, nullptr, "Creating " + descriptor_name);

	std::cout << "Creating " + descriptor_name << std::endl;

	if (descriptor_allocator->desc_heap == nullptr) {
		std::cout << "heap desc null ptr" << std::endl;
	}

	descriptor_allocator->desc_increment_size = d3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	
	std::cout << "Filling free indices" << std::endl;

	descriptor_allocator->init(num_descriptors);

}

void DX12ResourceManager::initModelBuffers() {

	// for vertex and index buffer SRVs

	std::cout << "Creating upload and default buffers" << std::endl;

	// create upload and default buffers and store them
	for (auto& [name, loadedModel] : meshManager->loadedModels) {

		std::cout << "model name: " << name << std::endl;

		DX12Model* dx12Model = new DX12Model{};

		dx12Model->loadedModel = loadedModel;
		dx12Models_map[loadedModel->name] = dx12Model;

		for (size_t i = 0; i < loadedModel->meshes.size(); i++) {

			MeshManager::Mesh mesh = loadedModel->meshes[i];

			size_t vbSize = mesh.vertices.size() * sizeof(MeshManager::Vertex);
			size_t ibSize = mesh.indices.size() * sizeof(uint32_t);

			size_t vbSizeMax = config.maxInstances * sizeof(MeshManager::Vertex);
			size_t ibSizeMax = config.maxInstances * sizeof(uint32_t);

			std::cout << ": vbSize=" << vbSize << " bytes, ibSize=" << ibSize << " bytes" << std::endl;

			DX12ResourceHandle *vertexBuffer = createResourceHandle(mesh.vertices.data(), vbSize, vbSize, D3D12_RESOURCE_STATE_COMMON, false);
			DX12ResourceHandle* indexBuffer = createResourceHandle(mesh.indices.data(), ibSize, ibSize, D3D12_RESOURCE_STATE_COMMON, false);

			vertexBuffer->upload_buffer->SetName(L"Object Vertex Upload Buffer");
			indexBuffer->upload_buffer->SetName(L"Object Index Upload Buffer");
			vertexBuffer->default_buffer->SetName(L"Object Vertex Default Buffer");
			indexBuffer->default_buffer->SetName(L"Object Index Default Buffer");

			// CPU memory buffers = HEAP_UPLOAD
			// GPU memory buffers = HEAP_DEFAULT

			dx12Model->vertexBuffers.push_back(vertexBuffer);
			dx12Model->indexBuffers.push_back(indexBuffer);
		}

	}

	std::cout << "Pushing buffers to VRAM" << std::endl;

	for (auto& [name, dx12Model] : dx12Models_map) {

		std::cout << dx12Model->loadedModel->name << std::endl;

		std::vector<MeshManager::Mesh>& meshes = dx12Model->loadedModel->meshes;

		for (size_t i = 0; i < dx12Model->loadedModel->meshes.size(); i++) {

			MeshManager::Mesh& mesh = dx12Model->loadedModel->meshes[i];

			size_t vbSize = mesh.vertices.size() * sizeof(MeshManager::Vertex);
			size_t ibSize = mesh.indices.size() * sizeof(uint32_t);

			std::cout << "Mesh " << i << ": vbSize=" << vbSize << " bytes, ibSize=" << ibSize << " bytes" << std::endl;

			pushResourceHandle(dx12Model->vertexBuffers[i], vbSize, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
			pushResourceHandle(dx12Model->indexBuffers[i], ibSize, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

		}

	}

}

void DX12ResourceManager::initVertexIndexBuffers() {

	allVertexBuffers.clear();
	allIndexBuffers.clear();

	for (auto& [name, model] : dx12Models_map) {

		std::vector<DX12ResourceHandle*> indexBuffers = model->indexBuffers;
		std::vector<DX12ResourceHandle*> vertexBuffers = model->vertexBuffers;

		size_t bufferSize = indexBuffers.size();

		for (size_t i = 0; i < bufferSize; i++) {
			allIndexBuffers.push_back(indexBuffers[i]);
			allVertexBuffers.push_back(vertexBuffers[i]);
		}

		dx12Models_index_map[name] = static_cast<UINT>(allIndexBuffers.size() - 1);
		std::cout << name << ": " << "idx: " << dx12Models_index_map[name] << std::endl;

	}

}

void DX12ResourceManager::updateCamera() {

	entityManager->camera->update();

	using namespace DirectX;

	EntityManager::Camera* entityCamera = entityManager->camera;

	dx12Camera->position = { entityCamera->position.x, entityCamera->position.y, entityCamera->position.z };
	dx12Camera->seed = seed;
	dx12Camera->sky = config.sky == true ? 1u : 0u;
	dx12Camera->skyBrighness = config.skyBrightness;
	dx12Camera->minBounces = config.minBounces;
	dx12Camera->maxBounces = config.maxBounces;
	dx12Camera->jitter = config.jitter == true ? 1u : 0u;
	dx12Camera->next_event_estimation = config.NEE == true ? 1u : 0u;
	dx12Camera->NEE_samples = static_cast<UINT>(config.NEE_samples);

	PT::Vector3 position = entityCamera->position;
	PT::Vector3 right = entityCamera->right;
	PT::Vector3 up = entityCamera->up;
	PT::Vector3 forward = entityCamera->forward;
	float fovYDegrees = entityCamera->fovYDegrees;
	float fovYRad = XMConvertToRadians(fovYDegrees);
	float aspect = static_cast<float>(width) / static_cast<float>(height);


	XMFLOAT3 pos = { position.x, position.y, position.z };
	XMVECTOR eyePos = XMLoadFloat3(&pos);

	XMFLOAT3 forw = { forward.x, forward.y, forward.z };

	XMVECTOR forwardVec = XMLoadFloat3(&forw);
	XMVECTOR targetPos = XMVectorAdd(eyePos, forwardVec);
	XMVECTOR worldUp = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);

	XMMATRIX view = XMMatrixLookAtLH(eyePos, targetPos, worldUp);

	XMMATRIX proj = DirectX::XMMatrixPerspectiveFovLH(fovYRad, aspect, 0.01f, 1000.0f);

	XMMATRIX viewProj = XMMatrixMultiply(view, proj);
	XMVECTOR det;

	XMMATRIX invViewProj = DirectX::XMMatrixInverse(&det, viewProj);

	XMStoreFloat4x4(&dx12Camera->invViewProj, invViewProj);


	if (!cameraConstantBuffer) {
		std::cout << "test" << std::endl;
		cameraConstantBuffer = new DX12ResourceHandle{};
		createCBV(cameraConstantBuffer, sizeof(DX12Camera));
		cameraConstantBuffer->upload_buffer->SetName(L"Camera Default Buffer");
	}

	void* mapped = nullptr;
	HRESULT hr = cameraConstantBuffer->upload_buffer->Map(0, nullptr, &mapped);
	checkHR(hr, nullptr, "memcpy Camera CBV");
	memcpy(mapped, dx12Camera, sizeof(DX12Camera));
	cameraConstantBuffer->upload_buffer->Unmap(0, nullptr);

	// upload heap is always visible for cbv, no barriers


}

void DX12ResourceManager::initDX12Entites(bool is_update) {

	if (config.debug) std::cout << "initScene()" << std::endl;

	for (DX12Entity* dx12Entity : dx12entities) {
		delete dx12Entity;
	}

	dx12entities.clear();

	for (size_t i = 0; i < entityManager->entities.size(); i++) {

		EntityManager::Entity* entity = entityManager->entities[i];
		DX12Entity* dx12Entity = new DX12Entity{};
		dx12Entity->entity = entity;
		dx12Entity->model = dx12Models_map[entity->model];

		dx12entities.push_back(dx12Entity);
	}


	if (config.debug) std::cout << "creating instances" << std::endl;

	// create instances

	NUM_INSTANCES = static_cast<UINT>(dx12entities.size());

	D3D12_RESOURCE_DESC instancesDesc{};
	instancesDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	instancesDesc.Width = sizeof(D3D12_RAYTRACING_INSTANCE_DESC) * config.maxInstances;
	instancesDesc.Height = 1;
	instancesDesc.DepthOrArraySize = 1;
	instancesDesc.MipLevels = 1;
	instancesDesc.SampleDesc = NO_AA;
	instancesDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

	if (!is_update) {
		instances = new DX12ResourceHandle{};
		HRESULT hr = d3dDevice->CreateCommittedResource(&UPLOAD_HEAP, D3D12_HEAP_FLAG_NONE, &instancesDesc, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&instances->default_buffer));
		checkHR(hr, nullptr, "initScene, CreateComittedResource: ");
	}
	
	instances->default_buffer->Map(0, nullptr, reinterpret_cast<void**>(&instanceData));

	if (config.debug) std::cout << "init scene" << std::endl;

	uint32_t instanceID = -1; // user provided
	uint32_t instanceIndex = 0;

	for (DX12Entity* dx12Entity : dx12entities) {

		DX12ResourceHandle* objectBlas = dx12Entity->model->BLAS;
		std::cout << dx12Entity->entity->model << std::endl;
		std::cout << "idx: " << dx12Models_index_map[dx12Entity->entity->model] << std::endl;

		instanceData[instanceIndex] = {
			.InstanceID = static_cast<UINT>(dx12Models_index_map[dx12Entity->entity->model]),
			.InstanceMask = 1,
			.InstanceContributionToHitGroupIndex = 0,
			.Flags = 0,
			.AccelerationStructure = objectBlas->default_buffer->GetGPUVirtualAddress(),
		};

		instanceIndex++;
	}

	// fill in dummy instances
	// to allow easily rebuildable BLAS

	DX12Model* dummyModel = dx12Models_map["cube"];
	
	for (UINT i = dx12entities.size(); i < config.maxInstances; i++) {
		
		instanceData[instanceIndex] = {
			.InstanceID = static_cast<UINT>(instanceID),
			.InstanceMask = 0,
			.InstanceContributionToHitGroupIndex = 0,
			.Flags = 0,
			.AccelerationStructure = dummyModel->BLAS->default_buffer->GetGPUVirtualAddress(),
		};

		instanceIndex++;
	}

	updateTransforms();
}

void DX12ResourceManager::updateTransforms() {

	//if (debugstage) std::cout << "Update Transforms" << std::endl;

	auto time = static_cast<float>(GetTickCount64()) / 1000.0f;

	size_t currentInstance = 0;

	// apply meshes stored transform

	for (DX12Entity* dx12Entity : dx12entities) {

		auto vecRotation = dx12Entity->entity->rotation;
		auto vecPosition = dx12Entity->entity->position;
		auto vecScale = dx12Entity->entity->scale;

		DirectX::XMMATRIX worldTransform = DirectX::XMMatrixScaling(vecScale.x, vecScale.y, vecScale.z);
			
		worldTransform *= DirectX::XMMatrixRotationRollPitchYaw(vecRotation.x, vecRotation.y, vecRotation.z);

		worldTransform *= DirectX::XMMatrixTranslation(vecPosition.x, vecPosition.y, vecPosition.z);

		auto* ptr = reinterpret_cast<DirectX::XMFLOAT3X4*>(&instanceData[currentInstance].Transform);
		XMStoreFloat3x4(ptr, worldTransform);

		currentInstance++;

	}

}

void DX12ResourceManager::updateRand(bool is_resize) {

	std::cout << "update rand" << std::endl;

	std::random_device rd;
	std::mt19937 gen(rd());
	std::uniform_int_distribution<uint64_t> dist;

	randPattern.resize(width * height);

	for (uint64_t x = 0; x < width; x++) {

		for (uint64_t y = 0; y < height; y++) {

			uint64_t state = ((y << 16u) | x) ^ (seed * 1664525u * static_cast<uint64_t>(GetTickCount64())) ^ 0xdeadbeefu;

			// PCG hash function

			uint64_t word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
			word = (word >> 22u) ^ word;

			uint64_t rot = state >> 28u;

			word = (word >> rot) | (word << (32u - rot));

			//randPattern[x + y * width] = word;
			randPattern[x + y * width] = dist(gen);
		}
	}

	size_t randSize = randPattern.size() * sizeof(uint64_t);

	randBuffer = createResourceHandle(randPattern.data(), randSize, randSize, D3D12_RESOURCE_STATE_COMMON, true);
	randBuffer->default_buffer->SetName(L"rng Defaut Buffer");
	
	pushResourceHandle(randBuffer, randSize, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

	
}

void DX12ResourceManager::initMaxLumBuffer() {

	std::vector<UINT> lum;
	lum.resize(1);
	lum[0] = 1u;
	maxLumBuffer = createResourceHandle(lum.data(), sizeof(UINT), sizeof(UINT), D3D12_RESOURCE_STATE_COMMON, true);
	maxLumBuffer->default_buffer->SetName(L"Max Luminance Buffer");
	pushResourceHandle(maxLumBuffer, sizeof(UINT), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);


};

void DX12ResourceManager::updateToneParams() {

	//if (config.debug) std::cout << "updateTonePrams" << std::endl;

	if (!toneMappingParams) {
		toneMappingParams = new DX12ResourceManager::ToneMappingParams();
	}

	toneMappingParams->num_samples = samples;
	toneMappingParams->white_point = config.whitepoint;

	if (!toneMappingConstantBuffer) {

		std::cout << "Creating tone mapping buffer" << std::endl;
		toneMappingConstantBuffer = new DX12ResourceHandle();
		createCBV(toneMappingConstantBuffer, sizeof(DX12ResourceManager::ToneMappingParams));
		toneMappingConstantBuffer->upload_buffer->SetName(L"Tone Mapping Default Buffer");
	}

	void* mapped = nullptr;
	toneMappingConstantBuffer->upload_buffer->Map(0, nullptr, &mapped);
	memcpy(mapped, toneMappingParams, sizeof(DX12ResourceManager::ToneMappingParams));
	toneMappingConstantBuffer->upload_buffer->Unmap(0, nullptr);

}

void DX12ResourceManager::rebuildBLAS() {

	initModelBuffers(); // only if a new unique model is added
	//initModelBLAS(); // only if a new unique model is added

	initDX12Entites(true); // only if a new entity is added

}

// utility

void DX12ResourceManager::initRenderTexture(DX12ResourceHandle* resource_handle, DXGI_FORMAT format, std::string resource_name, bool is_resize) {

	if (is_resize) {
		resource_handle->default_buffer->Release();
	}

	D3D12_RESOURCE_DESC accumDesc = {};
	accumDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	accumDesc.Width = width;
	accumDesc.Height = height;
	accumDesc.DepthOrArraySize = 1;
	accumDesc.MipLevels = 1;
	accumDesc.Format = format;
	accumDesc.SampleDesc = NO_AA;
	accumDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

	HRESULT hr = d3dDevice->CreateCommittedResource(&DEFAULT_HEAP, D3D12_HEAP_FLAG_NONE, &accumDesc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(&resource_handle->default_buffer));
	checkHR(hr, nullptr, "Create accumulation texture");
	
	//resource_handle->default_buffer->SetName(resource_name);
}

DX12ResourceHandle* DX12ResourceManager::createResourceHandle(const void* data, size_t byte_size, size_t max_size, D3D12_RESOURCE_STATES finalState, bool UAV) {
	//std::cout << "byteSize: " << byteSize << std::endl;

	// CPU Buffer (upload buffer)
	D3D12_RESOURCE_DESC DESC = {
		.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER,
		.Width = max_size,
		.Height = 1,
		.DepthOrArraySize = 1,
		.MipLevels = 1,
		.SampleDesc = NO_AA,
		.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR,
		.Flags = D3D12_RESOURCE_FLAG_NONE,
	};


	ID3D12Resource* upload;
	d3dDevice->CreateCommittedResource(&UPLOAD_HEAP, D3D12_HEAP_FLAG_NONE, &DESC, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&upload));

	void* mapped;
	upload->Map(0, nullptr, &mapped); // mapped now points to the upload buffer
	memcpy(mapped, data, byte_size); // copy data to upload buffer
	upload->Unmap(0, nullptr); // 7

	// Create target buffer in DEFAULT heap (VRAM)

	if (UAV) {
		DESC.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
	}
	ID3D12Resource* target = nullptr;
	d3dDevice->CreateCommittedResource(&DEFAULT_HEAP, D3D12_HEAP_FLAG_NONE, &DESC, finalState, nullptr, IID_PPV_ARGS(&target));

	DX12ResourceHandle* resource_handle = new DX12ResourceHandle();
	resource_handle->upload_buffer = upload;
	resource_handle->default_buffer = target;
	return resource_handle;
}

void DX12ResourceManager::updateResourceHandle(DX12ResourceHandle* resource_handle, const void* data, size_t byte_size, size_t max_size) {
	//std::cout << "byteSize: " << byteSize << std::endl;

	void* mapped;
	resource_handle->upload_buffer->Map(0, nullptr, &mapped); // mapped now points to the upload buffer
	memcpy(mapped, data, byte_size); // copy data to upload buffer
	resource_handle->upload_buffer->Unmap(0, nullptr); //
}

void DX12ResourceManager::resizeResourceHandle(DX12ResourceHandle* resource_handle, const void* data, size_t byteSize, D3D12_RESOURCE_STATES finalState, bool UAV) {
	//std::cout << "byteSize: " << byteSize << std::endl;

	resource_handle->upload_buffer->Release();
	resource_handle->default_buffer->Release();

	// CPU Buffer (upload buffer)
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


	ID3D12Resource* upload;
	d3dDevice->CreateCommittedResource(&UPLOAD_HEAP, D3D12_HEAP_FLAG_NONE, &DESC, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&upload));

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
	d3dDevice->CreateCommittedResource(&DEFAULT_HEAP, D3D12_HEAP_FLAG_NONE, &DESC, finalState, nullptr, IID_PPV_ARGS(&target));

	resource_handle->upload_buffer = upload;
	resource_handle->default_buffer = target;
}

void DX12ResourceManager::createCBV(DX12ResourceHandle* resource_handle, size_t byte_size) {

	D3D12_RESOURCE_DESC cbDesc = {};
	cbDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	cbDesc.Width = byte_size;
	cbDesc.Height = 1;
	cbDesc.DepthOrArraySize = 1;
	cbDesc.MipLevels = 1;
	cbDesc.SampleDesc = NO_AA;;
	cbDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
	cbDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

	D3D12_HEAP_PROPERTIES uploadHeapProps = { D3D12_HEAP_TYPE_UPLOAD };

	HRESULT hr = d3dDevice->CreateCommittedResource(
		&uploadHeapProps,
		D3D12_HEAP_FLAG_NONE,
		&cbDesc,
		D3D12_RESOURCE_STATE_GENERIC_READ, // cbv
		nullptr,
		IID_PPV_ARGS(&resource_handle->upload_buffer)
	);
	checkHR(hr, nullptr, "Create camera CB");

}

void DX12ResourceManager::pushResourceHandle(DX12ResourceHandle* resource_handle, size_t data_size, D3D12_RESOURCE_STATES state_before, D3D12_RESOURCE_STATES state_after) {

	// Barrier - transition target to COPY_DEST
	D3D12_RESOURCE_BARRIER barrier = {};
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
	barrier.Transition.pResource = resource_handle->default_buffer;
	barrier.Transition.StateBefore = state_before;
	barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
	barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	cmdList->ResourceBarrier(1, &barrier);

	// Copy to GPU
	cmdList->CopyBufferRegion(resource_handle->default_buffer, 0, resource_handle->upload_buffer, 0, data_size);

	// Barrier - transition to final state
	barrier.Transition.pResource = resource_handle->default_buffer;
	barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
	barrier.Transition.StateAfter = state_after;
	cmdList->ResourceBarrier(1, &barrier);
}

// helper

void DX12ResourceManager::checkHR(HRESULT hr, ID3DBlob* errorblob, std::string context) {
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

void DX12ResourceManager::waitForGPU() {
	
	const UINT64 currentFence = fenceState;
	cmdQueue->Signal(fence, currentFence);
	if (fence->GetCompletedValue() < currentFence) {
		HANDLE event = CreateEventEx(nullptr, nullptr, 0, EVENT_ALL_ACCESS);
		fence->SetEventOnCompletion(currentFence, event);
		WaitForSingleObject(event, INFINITE);
		CloseHandle(event);
	}
	fenceState++;
}

void DX12ResourceManager::createFence(ID3D12Fence*& fence) {
	
	HRESULT hr = d3dDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence));
	checkHR(hr, nullptr, "Create fence");
}