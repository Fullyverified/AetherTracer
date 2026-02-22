#include "RayTracingStage.h"
#include <d3dcompiler.h> // for compiling shaders
#include "Config.h"
#include "UI.h"

bool debugstage = true;

RayTracingStage::RayTracingStage(ResourceManager* resourceManager, MeshManager* meshManager, MaterialManager* materialManager, EntityManager* entityManager)
	: rm(resourceManager), meshManager(meshManager), materialManager(materialManager), entityManager(entityManager) { 

};

void RayTracingStage::initStage() {

	initCPUDescriptor();
	initRTDescriptors();
	initRTRootSignature();
	initRTPipeline();
	initRTShaderTables();
}

void RayTracingStage::loadShaders() {

	HRESULT hr = D3DReadFileToBlob(L"raytracingshader.cso", &rsBlob);
	checkHR(hr, nullptr, "Loading postprocessingshader");

}

void RayTracingStage::initAccumulationTexture() {

	D3D12_RESOURCE_DESC accumDesc = {};
	accumDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	accumDesc.Width = rm->width;
	accumDesc.Height = rm->height;
	accumDesc.DepthOrArraySize = 1;
	accumDesc.MipLevels = 1;
	accumDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
	accumDesc.SampleDesc = rm->NO_AA;
	accumDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

	HRESULT hr = rm->d3dDevice->CreateCommittedResource(&DEFAULT_HEAP, D3D12_HEAP_FLAG_NONE, &accumDesc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(&rm->accumulationTexture));
	checkHR(hr, nullptr, "Create accumulation texture");
	rm->accumulationTexture->SetName(L"Accumulation Texture");


}


void RayTracingStage::initModelBuffers() {

	// for vertex and index buffer SRVs

	std::cout << "Creating upload and default buffers" << std::endl;

	// create upload and default buffers and store them
	for (auto& [name, loadedModel] : meshManager->loadedModels) {

		ResourceManager::DX12Model* dx12Model = new ResourceManager::DX12Model{};

		dx12Model->loadedModel = loadedModel;
		rm->dx12Models[loadedModel->name] = dx12Model;

		for (size_t i = 0; i < loadedModel->meshes.size(); i++) {

			MeshManager::Mesh mesh = loadedModel->meshes[i];

			size_t vbSize = mesh.vertices.size() * sizeof(MeshManager::Vertex);
			size_t ibSize = mesh.indices.size() * sizeof(uint32_t);
			std::cout << ": vbSize=" << vbSize << " bytes, ibSize=" << ibSize << " bytes" << std::endl;

			ResourceManager::Buffer* vertexBuffer = createBuffers(mesh.vertices.data(), vbSize, D3D12_RESOURCE_STATE_COMMON, false);
			ResourceManager::Buffer* indexBuffer = createBuffers(mesh.indices.data(), ibSize, D3D12_RESOURCE_STATE_COMMON, false);

			vertexBuffer->uploadBuffers->SetName(L"Object Vertex Upload Buffer");
			indexBuffer->uploadBuffers->SetName(L"Object Index Upload Buffer");
			vertexBuffer->defaultBuffers->SetName(L"Object Vertex Default Buffer");
			indexBuffer->defaultBuffers->SetName(L"Object Index Default Buffer");

			// CPU memory buffers = HEAP_UPLOAD
			// GPU memory buffers = HEAP_DEFAULT

			dx12Model->vertexBuffers.push_back(vertexBuffer);
			dx12Model->indexBuffers.push_back(indexBuffer);
		}

	}


	// Reset cmdList and cmdAllc
	//cmdAlloc->Reset();
	//cmdList->Reset(cmdAlloc, nullptr); // NOT DOING THIS HERE ANYMORE

	std::cout << "Pushing buffers to VRAM" << std::endl;

	for (auto& [name, dx12Model] : rm->dx12Models) {

		std::cout << "first model: " << dx12Model->loadedModel->name << std::endl;

		std::vector<MeshManager::Mesh>& meshes = dx12Model->loadedModel->meshes;

		for (size_t i = 0; i < dx12Model->loadedModel->meshes.size(); i++) {

			MeshManager::Mesh& mesh = dx12Model->loadedModel->meshes[i];

			size_t vbSize = mesh.vertices.size() * sizeof(MeshManager::Vertex);
			size_t ibSize = mesh.indices.size() * sizeof(uint32_t);

			std::cout << "Mesh " << i << ": vbSize=" << vbSize << " bytes, ibSize=" << ibSize << " bytes\n" << std::endl;

			pushBuffer(dx12Model->vertexBuffers[i], vbSize, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
			pushBuffer(dx12Model->indexBuffers[i], ibSize, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

		}

	}

}

ID3D12Resource* RayTracingStage::makeAccelerationStructure(const D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS& inputs, UINT64* updateScratchSize) {


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

		HRESULT hr = rm->d3dDevice->CreateCommittedResource(
			&DEFAULT_HEAP,
			D3D12_HEAP_FLAG_NONE,
			&desc,
			initialState,
			nullptr,
			IID_PPV_ARGS(&buffer)
		);

		checkHR(hr, nullptr, "CreateCommittedResource for AS failed");

		return buffer;
		};

	// debug
	if (inputs.NumDescs == 0 || inputs.pGeometryDescs == nullptr) {
		std::cerr << "Invalid inputs: no geometry\n";
		return nullptr;
	}

	D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO prebuildInfo;
	rm->d3dDevice->GetRaytracingAccelerationStructurePrebuildInfo(&inputs, &prebuildInfo);

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

	scratch->SetName(L"scratch buffer");
	as->SetName(L"BLAS");

	if (scratch == nullptr) std::cout << "scratch nullptr" << std::endl;
	if (as == nullptr) std::cout << "BLAS nullptr" << std::endl;

	D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC buildDesc = {
	.DestAccelerationStructureData = as->GetGPUVirtualAddress(), .Inputs = inputs, .ScratchAccelerationStructureData = scratch->GetGPUVirtualAddress() };

	std::cout << "executing cmd list " << std::endl;

	rm->cmdList->BuildRaytracingAccelerationStructure(&buildDesc, 0, nullptr);
	rm->cmdList->Close();
	rm->cmdQueue->ExecuteCommandLists(1, reinterpret_cast<ID3D12CommandList**>(&rm->cmdList));
	flush();
	rm->cmdAlloc->Reset();
	rm->cmdList->Reset(rm->cmdAlloc, nullptr);


	scratch->Release();
	return as;

}

// BLAS

void RayTracingStage::initModelBLAS() {

	if (debugstage) std::cout << "initBottomLevel()" << std::endl;

	for (auto& [name, model] : rm->dx12Models) {

		delete model->BLAS;

		std::cout << "object name: " << name << std::endl;

		std::vector<D3D12_RAYTRACING_GEOMETRY_DESC> geomDescs;

		for (size_t i = 0; i < model->vertexBuffers.size(); i++) {

			D3D12_RAYTRACING_GEOMETRY_DESC geometryDesc = {};
			geometryDesc.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
			geometryDesc.Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;
			geometryDesc.Triangles.VertexBuffer.StartAddress = model->vertexBuffers[i]->defaultBuffers->GetGPUVirtualAddress();
			geometryDesc.Triangles.VertexBuffer.StrideInBytes = sizeof(MeshManager::Vertex);
			geometryDesc.Triangles.VertexCount = static_cast<UINT>(model->loadedModel->meshes[i].vertices.size());
			geometryDesc.Triangles.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT;
			geometryDesc.Triangles.IndexBuffer = model->indexBuffers[i]->defaultBuffers->GetGPUVirtualAddress();
			geometryDesc.Triangles.IndexCount = static_cast<UINT>(model->loadedModel->meshes[i].indices.size());
			geometryDesc.Triangles.IndexFormat = DXGI_FORMAT_R32_UINT;
			geomDescs.push_back(geometryDesc);
		}

		std::cout << "making accel struc inputs: " << std::endl;

		D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs = {
			.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL,
			.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE,
			.NumDescs = static_cast<UINT>(geomDescs.size()),
			.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY,
			.pGeometryDescs = geomDescs.data() };

		std::cout << "making accel struc " << std::endl;

		model->BLAS = makeAccelerationStructure(inputs);
		model->BLAS->SetName(L"Model BLAS");
	}

}


void RayTracingStage::updateCamera() {

	entityManager->camera->update();

	using namespace DirectX;

	EntityManager::Camera* entityCamera = entityManager->camera;

	rm->dx12Camera->position = { entityCamera->position.x, entityCamera->position.y, entityCamera->position.z };
	rm->dx12Camera->seed = rm->seed;
	rm->dx12Camera->sky = config.sky == true ? 1u : 0u;
	rm->dx12Camera->skyBrighness = config.skyBrightness;
	rm->dx12Camera->minBounces = config.minBounces;
	rm->dx12Camera->maxBounces = config.maxBounces;
	rm->dx12Camera->jitter = config.jitter == true ? 1u : 0u;

	PT::Vector3 position = entityCamera->position;
	PT::Vector3 right = entityCamera->right;
	PT::Vector3 up = entityCamera->up;
	PT::Vector3 forward = entityCamera->forward;
	float fovYDegrees = entityCamera->fovYDegrees;
	float fovYRad = XMConvertToRadians(fovYDegrees);
	float aspect = entityCamera->aspect;


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

	XMStoreFloat4x4(&rm->dx12Camera->invViewProj, invViewProj);


	if (!rm->cameraConstantBuffer) {
		rm->cameraConstantBuffer = new ResourceManager::Buffer{};
		rm->cameraConstantBuffer = createBuffers(rm->dx12Camera, sizeof(ResourceManager::DX12Camera), D3D12_RESOURCE_STATE_COMMON, false);
		rm->cameraConstantBuffer->defaultBuffers->SetName(L"Camera Default Buffer");
		createCBV(rm->cameraConstantBuffer, sizeof(ResourceManager::DX12Camera));
	}

	void* mapped = nullptr;
	rm->cameraConstantBuffer->defaultBuffers->Map(0, nullptr, &mapped);
	memcpy(mapped, rm->dx12Camera, sizeof(ResourceManager::DX12Camera));
	rm->cameraConstantBuffer->defaultBuffers->Unmap(0, nullptr);

	// upload heap is always visible for cbv, no barries


}

void RayTracingStage::initScene() {

	if (debugstage) std::cout << "initScene()" << std::endl;

	rm->materials.clear();

	for (size_t i = 0; i < entityManager->entitys.size(); i++) {

		EntityManager::Entity* entity = entityManager->entitys[i];
		ResourceManager::DX12Entity* dx12Entity = new ResourceManager::DX12Entity{};
		dx12Entity->entity = entity;
		dx12Entity->model = rm->dx12Models[entity->name];

		std::cout << "Material name: " << entity->material->name << std::endl;

		// create material if it doesn't already exist
		if (rm->materials.find(entity->material->name) == rm->materials.end()) {
			std::cout << "Material doesnt exist " << std::endl;

			ResourceManager::DX12Material* dx12Material = new ResourceManager::DX12Material{ entity->material };
			rm->materials[entity->material->name] = dx12Material;
			dx12Entity->material = dx12Material;
		}
		else {
			std::cout << "Material exists " << std::endl;
			dx12Entity->material = rm->materials[entity->material->name];
		}

		rm->dx12Entitys.push_back(dx12Entity);
	}

	if (debugstage) std::cout << "creating instances" << std::endl;

	// create instances

	rm->NUM_INSTANCES = static_cast<UINT>(rm->dx12Entitys.size());

	D3D12_RESOURCE_DESC instancesDesc{};
	instancesDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	instancesDesc.Width = sizeof(D3D12_RAYTRACING_INSTANCE_DESC) * rm->NUM_INSTANCES;
	instancesDesc.Height = 1;
	instancesDesc.DepthOrArraySize = 1;
	instancesDesc.MipLevels = 1;
	instancesDesc.SampleDesc = rm->NO_AA;
	instancesDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

	HRESULT hr = rm->d3dDevice->CreateCommittedResource(&UPLOAD_HEAP, D3D12_HEAP_FLAG_NONE, &instancesDesc, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&rm->instances));
	checkHR(hr, nullptr, "initScene, CreateComittedResource: ");

	rm->instances->Map(0, nullptr, reinterpret_cast<void**>(&rm->instanceData));

	if (debugstage) std::cout << "init scene" << std::endl;

	uint32_t instanceID = -1; // user provided
	uint32_t instanceIndex = 0;

	for (ResourceManager::DX12Entity* dx12Entity : rm->dx12Entitys) {

		if (dx12Entity->model->BLAS == nullptr) std::cout << "BLAS nullptr " << std::endl;

		ID3D12Resource* objectBlas = dx12Entity->model->BLAS;
		objectBlas->GetGPUVirtualAddress();

		if (rm->uniqueInstancesID.find(dx12Entity->entity->name) == rm->uniqueInstancesID.end()) {
			instanceID++;
			rm->uniqueInstancesID[dx12Entity->entity->name] = instanceID;
		}
		else {
			instanceID = rm->uniqueInstancesID[dx12Entity->entity->name];
		}

		std::cout << "InstanceIndex: " << instanceIndex << std::endl;
		std::cout << "InstanceID: " << instanceID << std::endl;

		rm->instanceData[instanceIndex] = {
			.InstanceID = static_cast<UINT>(instanceID),
			.InstanceMask = 1,
			.InstanceContributionToHitGroupIndex = 0,
			.Flags = 0,
			.AccelerationStructure = objectBlas->GetGPUVirtualAddress(),
		};

		instanceIndex++;
	}

	updateTransforms();

}

void RayTracingStage::updateTransforms() {

	//if (debugstage) std::cout << "Update Transforms" << std::endl;

	auto time = static_cast<float>(GetTickCount64()) / 1000.0f;

	size_t currentInstance = 0;

	// apply meshes stored transform

	for (ResourceManager::DX12Entity* dx12Entity : rm->dx12Entitys) {

		auto vecRotation = dx12Entity->entity->rotation;
		auto vecPosition = dx12Entity->entity->position;

		auto transform = DirectX::XMMatrixRotationRollPitchYaw(vecRotation.x, vecRotation.y, vecRotation.z);
		//transform = DirectX::XMMatrixRotationRollPitchYaw(time / 2, time / 3, time / 5); // alternative, scale by time
		transform *= DirectX::XMMatrixTranslation(vecPosition.x, vecPosition.y, vecPosition.z);

		auto* ptr = reinterpret_cast<DirectX::XMFLOAT3X4*>(&rm->instanceData[currentInstance].Transform);
		XMStoreFloat3x4(ptr, transform);

		currentInstance++;

	}

}

void RayTracingStage::initMaterialBuffer() {


	if (debugstage) std::cout << "initMaterialBuffer()" << std::endl;

	// similar to init top level
	// index buffer for materials
	// no material duplicates

	rm->dx12Materials.clear();
	rm->uniqueInstancesID.clear();
	rm->materialIndices.clear();

	uint32_t instanceIndex = -1;

	for (ResourceManager::DX12Entity* dx12Entity : rm->dx12Entitys) {

		std::cout << "Entity Name: " << dx12Entity->entity->name << std::endl;

		ResourceManager::DX12Material* dx12Mateiral = dx12Entity->material;
		std::cout << "Material name: " << dx12Entity->entity->material->name << std::endl;

		if (rm->uniqueInstancesID.find(dx12Entity->entity->material->name) == rm->uniqueInstancesID.end()) {
			instanceIndex = rm->dx12Materials.size();
			rm->uniqueInstancesID[dx12Entity->entity->material->name] = instanceIndex;
			rm->dx12Materials.push_back(*dx12Entity->material);
			std::cout << "Not in map " << std::endl;
			std::cout << "instanceIndex: " << instanceIndex << std::endl;
		}
		else {
			instanceIndex = rm->uniqueInstancesID[dx12Entity->entity->material->name];
			std::cout << "In map " << std::endl;
			std::cout << "instanceIndex: " << instanceIndex << std::endl;
		}

		rm->materialIndices.push_back(instanceIndex);
	}

	if (debugstage) std::cout << "DX12Materials.size(): " << rm->dx12Materials.size() << std::endl;
	if (debugstage) std::cout << "materialIndex.size(): " << rm->materialIndices.size() << std::endl;


	size_t materialsSize = rm->dx12Materials.size() * sizeof(ResourceManager::DX12Material);
	size_t indexSize = rm->materialIndices.size() * sizeof(uint32_t);

	rm->materialsBuffer = createBuffers(rm->dx12Materials.data(), materialsSize, D3D12_RESOURCE_STATE_COMMON, false);
	rm->materialIndexBuffer = createBuffers(rm->materialIndices.data(), indexSize, D3D12_RESOURCE_STATE_COMMON, false);

	rm->materialsBuffer->uploadBuffers->SetName(L"Materials Upload Buffer");
	rm->materialIndexBuffer->uploadBuffers->SetName(L"Materials Index Upload Default Buffer");
	rm->materialsBuffer->defaultBuffers->SetName(L"Materials Default Buffer");
	rm->materialIndexBuffer->defaultBuffers->SetName(L"Materials Index Default Buffer");

	pushBuffer(rm->materialsBuffer, materialsSize, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	pushBuffer(rm->materialIndexBuffer, indexSize, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_INDEX_BUFFER);


	if (debugstage) std::cout << "Finished material buffer" <<  std::endl;

}

// TLAS

void RayTracingStage::initTopLevelAS() {

	if (debugstage) std::cout << "initTopLevel()" << std::endl;

	UINT64 updateScratchSize;
	D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs = {
	.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL,
	.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_UPDATE,
	.NumDescs = rm->NUM_INSTANCES,
	.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY,
	.InstanceDescs = rm->instances->GetGPUVirtualAddress() };

	std::cout << "NUM_INSTANCES = "<<rm->NUM_INSTANCES << std::endl;

	D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO prebuildInfo;
	rm->d3dDevice->GetRaytracingAccelerationStructurePrebuildInfo(&inputs, &prebuildInfo);

	D3D12_RESOURCE_DESC desc = {};
	desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	desc.Width = prebuildInfo.ScratchDataSizeInBytes;
	desc.Height = 1;
	desc.DepthOrArraySize = 1;
	desc.MipLevels = 1;
	desc.SampleDesc = rm->NO_AA;
	desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
	desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

	HRESULT hr = rm->d3dDevice->CreateCommittedResource(&DEFAULT_HEAP, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&rm->tlasscratch));
	checkHR(hr, nullptr, "CreateCommittedResource for AS failed");

	desc.Width = prebuildInfo.ResultDataMaxSizeInBytes;

	hr = rm->d3dDevice->CreateCommittedResource(&DEFAULT_HEAP, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE, nullptr, IID_PPV_ARGS(&rm->tlas));
	checkHR(hr, nullptr, "CreateCommittedResource for AS failed");
		
	rm->tlasscratch->SetName(L"Scratch");
	rm->tlas->SetName(L"AS");

	if (rm->tlasscratch == nullptr) std::cout << "scratch nullptr" << std::endl;
	if (rm->tlas == nullptr) std::cout << "BLAS nullptr" << std::endl;

	D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC buildDesc = {
	.DestAccelerationStructureData = rm->tlas->GetGPUVirtualAddress(), .Inputs = inputs, .ScratchAccelerationStructureData = rm->tlasscratch->GetGPUVirtualAddress() };

	std::cout << "executing cmd list " << std::endl;

	rm->cmdList->BuildRaytracingAccelerationStructure(&buildDesc, 0, nullptr);

	rm->cmdList->Close();
	rm->cmdQueue->ExecuteCommandLists(1, reinterpret_cast<ID3D12CommandList**>(&rm->cmdList));
	flush();
	rm->cmdAlloc->Reset();
	rm->cmdList->Reset(rm->cmdAlloc, nullptr);

	rm->tlasscratch->Release();
}


void RayTracingStage::initVertexIndexBuffers() {

	rm->allVertexBuffers.clear();
	rm->allIndexBuffers.clear();

	rm->uniqueInstances.clear();
	for (ResourceManager::DX12Entity* dx12SceneObject : rm->dx12Entitys) {


		if (rm->uniqueInstances.count(dx12SceneObject->entity->name) == 0) {

			rm->uniqueInstances.insert(dx12SceneObject->entity->name);
			std::vector<ResourceManager::Buffer*> indexBuffers = dx12SceneObject->model->indexBuffers;
			std::vector<ResourceManager::Buffer*> vertexBuffers = dx12SceneObject->model->vertexBuffers;

			size_t bufferSize = indexBuffers.size();

			for (size_t i = 0; i < bufferSize; i++) {
				rm->allIndexBuffers.push_back(indexBuffers[i]);
				rm->allVertexBuffers.push_back(vertexBuffers[i]);
			}
		}

	}

}

void RayTracingStage::initCPUDescriptor() {

	// for the ClearUnorderedAccessViewFloat method
	D3D12_DESCRIPTOR_HEAP_DESC cpuDesc = {
	.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
	.NumDescriptors = 1,
	.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE, // not shader visible
	};

	HRESULT hr = rm->d3dDevice->CreateDescriptorHeap(&cpuDesc, IID_PPV_ARGS(&cpuDescHeap));
	checkHR(hr, nullptr, "Create CPU Descriptor Heap");

	// for the non shader visible descriptor heap
	D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {
			.Format = DXGI_FORMAT_R16G16B16A16_FLOAT,
			.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D
	};

	rm->d3dDevice->CreateUnorderedAccessView(rm->accumulationTexture, nullptr, &uavDesc, cpuDescHeap->GetCPUDescriptorHandleForHeapStart());
	//cpuHandle.ptr += descriptorIncrementSize; // unnessacary, only one
}

void RayTracingStage::initRTDescriptors() {

	// Heap size: 1 UAV (accumulation texture) + 1 UAV Rand Buffer + 1 SRV (scene), + NUM_INSTANCES * (vertex srvs, index srvs) + 1 Material SRV + MaterialIndex SRV + Camera CBV

	if (debugstage) std::cout << "creating SRVs" << std::endl;

	UINT num_modelBuffers = rm->allVertexBuffers.size();

	UINT numDescriptors = 3 + num_modelBuffers * 2 + 3;

	descriptorIncrementSize = rm->d3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {
		.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
		.NumDescriptors = numDescriptors,
		.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE
	};

	HRESULT hr = rm->d3dDevice->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&raytracingDescHeap));
	checkHR(hr, nullptr, "Create Ray Tracing Descriptor Heap");

	// Create views
	D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle = raytracingDescHeap->GetCPUDescriptorHandleForHeapStart();

	// slot 0 UAV for accumulation texture
	D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {
		.Format = DXGI_FORMAT_R16G16B16A16_FLOAT,
		.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D
	};
	rm->d3dDevice->CreateUnorderedAccessView(rm->accumulationTexture, nullptr, &uavDesc, cpuHandle);
	cpuHandle.ptr += descriptorIncrementSize;

	
	// slot 1 UAV for RNG Buffer
	uavDesc = {};
	uavDesc.Format = DXGI_FORMAT_UNKNOWN,
		uavDesc.Buffer.StructureByteStride = sizeof(uint64_t),
		uavDesc.Buffer.NumElements = rm->randPattern.size();
	uavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER,

		rm->d3dDevice->CreateUnorderedAccessView(rm->randBuffer->defaultBuffers, nullptr, &uavDesc, cpuHandle);
	cpuHandle.ptr += descriptorIncrementSize;

	// slot 2 SRV for TLAS
	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Format = DXGI_FORMAT_UNKNOWN;
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_RAYTRACING_ACCELERATION_STRUCTURE;
	srvDesc.RaytracingAccelerationStructure.Location = rm->tlas->GetGPUVirtualAddress();

	rm->d3dDevice->CreateShaderResourceView(nullptr, &srvDesc, cpuHandle);
	cpuHandle.ptr += descriptorIncrementSize;

	// slot 3
	for (ResourceManager::Buffer* vertexBuffer : rm->allVertexBuffers) {
		D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		srvDesc.Format = DXGI_FORMAT_UNKNOWN;
		srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
		srvDesc.Buffer.FirstElement = 0;
		srvDesc.Buffer.NumElements = static_cast<UINT>(vertexBuffer->defaultBuffers->GetDesc().Width / sizeof(MeshManager::Vertex));
		srvDesc.Buffer.StructureByteStride = sizeof(MeshManager::Vertex);
		rm->d3dDevice->CreateShaderResourceView(vertexBuffer->defaultBuffers, &srvDesc, cpuHandle);
		cpuHandle.ptr += descriptorIncrementSize;
		std::cout << "srvNumElementsVertex: " << srvDesc.Buffer.NumElements << std::endl;
	}
	// slot 4
	for (ResourceManager::Buffer* indexBuffer : rm->allIndexBuffers) {
		D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		srvDesc.Format = DXGI_FORMAT_R32_UINT;
		srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
		srvDesc.Buffer.FirstElement = 0;
		srvDesc.Buffer.NumElements = static_cast<UINT>(indexBuffer->defaultBuffers->GetDesc().Width / sizeof(uint32_t));
		srvDesc.Buffer.StructureByteStride = 0;
		rm->d3dDevice->CreateShaderResourceView(indexBuffer->defaultBuffers, &srvDesc, cpuHandle);
		cpuHandle.ptr += descriptorIncrementSize;
		std::cout << "srvNumElementsIndex: " << srvDesc.Buffer.NumElements << std::endl;
	}

	// slot 5 Material Buffer
	srvDesc = {};
	srvDesc.Format = DXGI_FORMAT_UNKNOWN;
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
	srvDesc.Buffer.FirstElement = 0;
	srvDesc.Buffer.NumElements = static_cast<UINT>(rm->dx12Materials.size());
	srvDesc.Buffer.StructureByteStride = sizeof(ResourceManager::DX12Material);
	rm->d3dDevice->CreateShaderResourceView(rm->materialsBuffer->defaultBuffers, &srvDesc, cpuHandle);
	cpuHandle.ptr += descriptorIncrementSize;

	// slot 6 Material Index Buffer
	srvDesc = {};
	srvDesc.Format = DXGI_FORMAT_R32_UINT;
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
	srvDesc.Buffer.FirstElement = 0;
	srvDesc.Buffer.NumElements = static_cast<UINT>(rm->materialIndexBuffer->defaultBuffers->GetDesc().Width / sizeof(uint32_t));
	srvDesc.Buffer.StructureByteStride = 0;
	rm->d3dDevice->CreateShaderResourceView(rm->materialIndexBuffer->defaultBuffers, &srvDesc, cpuHandle);
	cpuHandle.ptr += descriptorIncrementSize;

	// Camera CBV
	D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {};
	cbvDesc.BufferLocation = rm->cameraConstantBuffer->defaultBuffers->GetGPUVirtualAddress();
	cbvDesc.SizeInBytes = sizeof(ResourceManager::DX12Camera);

	rm->d3dDevice->CreateConstantBufferView(&cbvDesc, cpuHandle);
	cpuHandle.ptr += descriptorIncrementSize;

}

void RayTracingStage::initRTRootSignature() {

	if (debugstage) std::cout << "initRTRootSignature()" << std::endl;

	UINT NUM_BUFFERS = static_cast<UINT>(rm->allVertexBuffers.size());

	D3D12_DESCRIPTOR_RANGE accumRange = {
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

	D3D12_DESCRIPTOR_RANGE sceneRange = {
	.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
	.NumDescriptors = 1,
	.BaseShaderRegister = 0,
	.RegisterSpace = 0,
	.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND
	};

	D3D12_DESCRIPTOR_RANGE vertexRange = {
	.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
	.NumDescriptors = NUM_BUFFERS,
	.BaseShaderRegister = 1,
	.RegisterSpace = 1,
	.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND
	};

	D3D12_DESCRIPTOR_RANGE indexRange = {
	.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
	.NumDescriptors = NUM_BUFFERS,
	.BaseShaderRegister = 2,
	.RegisterSpace = 2,
	.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND
	};

	D3D12_DESCRIPTOR_RANGE materialRange = {
	.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
	.NumDescriptors = 1,
	.BaseShaderRegister = 3,
	.RegisterSpace = 3,
	.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND
	};

	D3D12_DESCRIPTOR_RANGE materialIndexRange = {
	.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
	.NumDescriptors = 1,
	.BaseShaderRegister = 3,
	.RegisterSpace = 4,
	.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND
	};

	D3D12_DESCRIPTOR_RANGE cameraRange = {
	.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_CBV,
	.NumDescriptors = 1,
	.BaseShaderRegister = 0,
	.RegisterSpace = 0,
	.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND
	};

	// CBV for Camera
	D3D12_ROOT_PARAMETER cameraParam = {};
	cameraParam.ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
	cameraParam.Descriptor.ShaderRegister = 0;
	cameraParam.Descriptor.RegisterSpace = 0;
	cameraParam.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

	D3D12_ROOT_PARAMETER params[8] = {												// num desriptor ranges, descriptor range
		{.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE, .DescriptorTable = {1, &accumRange}},
		{.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE, .DescriptorTable = {1, &randRange}},
		{.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE, .DescriptorTable = {1, &sceneRange}},
		{.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE, .DescriptorTable = {1, &vertexRange}},
		{.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE, .DescriptorTable = {1, &indexRange}},
		{.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE, .DescriptorTable = {1, &materialRange}},
		{.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE, .DescriptorTable = {1, &materialIndexRange}},
	};

	params[7] = cameraParam;

	D3D12_ROOT_SIGNATURE_DESC desc = {
		.NumParameters = 8,
		.pParameters = params,
		.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE
	};

	ID3DBlob* blob;
	ID3DBlob* errorblob;
	HRESULT hr = D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1_0, &blob, &errorblob);
	checkHR(hr, nullptr, "D3D12SerializeRootSignature: ");


	hr = rm->d3dDevice->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&raytracingRootSignature));
	checkHR(hr, errorblob, "CreateRootSignature: ");

	blob->Release();


}

void RayTracingStage::initRTPipeline() {


	if (debugstage) std::cout << "initRTPipeline()" << std::endl;

	D3D12_DXIL_LIBRARY_DESC lib = {
	.DXILLibrary = { rsBlob->GetBufferPointer(), rsBlob->GetBufferSize()} };

	D3D12_HIT_GROUP_DESC hitGroup = {
	.HitGroupExport = L"HitGroup",
	.Type = D3D12_HIT_GROUP_TYPE_TRIANGLES,
	.AnyHitShaderImport = nullptr,
	.ClosestHitShaderImport = L"ClosestHit",
	.IntersectionShaderImport = nullptr
	};

	D3D12_RAYTRACING_SHADER_CONFIG shaderCfg = {
	.MaxPayloadSizeInBytes = 76,
	.MaxAttributeSizeInBytes = 8, // triangle attribs
	};


	D3D12_GLOBAL_ROOT_SIGNATURE globalSig = { raytracingRootSignature };

	D3D12_RAYTRACING_PIPELINE_CONFIG pipelineCfg = { .MaxTraceRecursionDepth = 4 };

	D3D12_STATE_SUBOBJECT subobjects[] = {
		{.Type = D3D12_STATE_SUBOBJECT_TYPE_DXIL_LIBRARY, .pDesc = &lib},
		{.Type = D3D12_STATE_SUBOBJECT_TYPE_HIT_GROUP, .pDesc = &hitGroup},
		{.Type = D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_SHADER_CONFIG, .pDesc = &shaderCfg},
		{.Type = D3D12_STATE_SUBOBJECT_TYPE_GLOBAL_ROOT_SIGNATURE, .pDesc = &globalSig},
		{.Type = D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_PIPELINE_CONFIG, .pDesc = &pipelineCfg},
	};

	D3D12_STATE_OBJECT_DESC psoDesc = {
		.Type = D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE,
		.NumSubobjects = std::size(subobjects),
		.pSubobjects = subobjects };

	HRESULT hr = rm->d3dDevice->CreateStateObject(&psoDesc, IID_PPV_ARGS(&raytracingPSO));
	checkHR(hr, nullptr, "initPipeLine, CreateStateObject: ");

}

void RayTracingStage::initRTShaderTables() {

	if (debugstage) std::cout << "iniRTShaderTables()" << std::endl;

	D3D12_RESOURCE_DESC shaderIDDesc {};
	shaderIDDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	shaderIDDesc.Width = NUM_SHADER_IDS * D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT;;
	shaderIDDesc.Height = 1;
	shaderIDDesc.DepthOrArraySize = 1;
	shaderIDDesc.MipLevels = 1;
	shaderIDDesc.SampleDesc = rm->NO_AA;
	shaderIDDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

	HRESULT hr = rm->d3dDevice->CreateCommittedResource(&UPLOAD_HEAP, D3D12_HEAP_FLAG_NONE, &shaderIDDesc, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&shaderIDs));
	checkHR(hr, nullptr, "initShaderTables, CreateCommittedResource: ");
	ID3D12StateObjectProperties* props;
	raytracingPSO->QueryInterface(&props);

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

void RayTracingStage::traceRays() {

	rm->cmdList->SetPipelineState1(raytracingPSO);
	rm->cmdList->SetComputeRootSignature(raytracingRootSignature);

	ID3D12DescriptorHeap* heaps[] = { raytracingDescHeap };
	rm->cmdList->SetDescriptorHeaps(1, heaps);

	if (!config.accumulate || entityManager->camera->camMoved || UI::accumulationUpdate || UI::accelUpdate) {
		// slot 0 UAV for accumulation texture
		D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle = cpuDescHeap->GetCPUDescriptorHandleForHeapStart();
		D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle = raytracingDescHeap->GetGPUDescriptorHandleForHeapStart();
		rm->cmdList->SetComputeRootDescriptorTable(0, gpuHandle); // u0 accum UAV
		rm->cmdList->ClearUnorderedAccessViewFloat(gpuHandle, cpuHandle, rm->accumulationTexture, rm->clearColor, 0, nullptr);

		UI::numRays = config.raysPerPixel;
		UI::accelUpdate = false;
		UI::accumulationUpdate = false;
	}

	D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle = raytracingDescHeap->GetGPUDescriptorHandleForHeapStart();
	rm->cmdList->SetComputeRootDescriptorTable(0, gpuHandle); // u0 accum UAV
	gpuHandle.ptr += descriptorIncrementSize;
	rm->cmdList->SetComputeRootDescriptorTable(1, gpuHandle); // u1 rand UAV
	gpuHandle.ptr += descriptorIncrementSize;
	rm->cmdList->SetComputeRootDescriptorTable(2, gpuHandle); // t0 TLAS
	gpuHandle.ptr += descriptorIncrementSize;
	rm->cmdList->SetComputeRootDescriptorTable(3, gpuHandle); // t1 vertex buffer
	gpuHandle.ptr += descriptorIncrementSize * rm->allVertexBuffers.size();
	rm->cmdList->SetComputeRootDescriptorTable(4, gpuHandle); // t2 index buffer
	gpuHandle.ptr += descriptorIncrementSize * rm->allIndexBuffers.size();
	rm->cmdList->SetComputeRootDescriptorTable(5, gpuHandle); // t3 material buffer
	gpuHandle.ptr += descriptorIncrementSize;
	rm->cmdList->SetComputeRootDescriptorTable(6, gpuHandle); // t3 material index buffer

	rm->cmdList->SetComputeRootConstantBufferView(7, rm->cameraConstantBuffer->defaultBuffers->GetGPUVirtualAddress()); // b0 camera cbv

	// clear accumulation texture

	// Dispatch rays

	auto rtDesc = rm->renderTarget->GetDesc();

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

	for (size_t i = 0; i < config.raysPerPixel; i++) {
		rm->cmdList->DispatchRays(&dispatchDesc);
	}

	// transition accumulation texture from SRV TO UAV for next frame
	D3D12_RESOURCE_BARRIER barrier = {};
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrier.Transition.pResource = rm->accumulationTexture;
	barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
	barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
	rm->cmdList->ResourceBarrier(1, &barrier);

}


ResourceManager::Buffer* RayTracingStage::createBuffers(const void* data, size_t byteSize, D3D12_RESOURCE_STATES finalState, bool UAV) {
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

void RayTracingStage::pushBuffer(ResourceManager::Buffer* buffer, size_t dataSize, D3D12_RESOURCE_STATES state_before, D3D12_RESOURCE_STATES state_after) {

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

void RayTracingStage::createCBV(ResourceManager::Buffer* buffer, size_t byteSize) {

	D3D12_RESOURCE_DESC cbDesc = {};
	cbDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	cbDesc.Width = byteSize;
	cbDesc.Height = 1;
	cbDesc.DepthOrArraySize = 1;
	cbDesc.MipLevels = 1;
	cbDesc.SampleDesc = rm->NO_AA;;
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
	checkHR(hr, nullptr, "Create camera CB");

}

void RayTracingStage::checkHR(HRESULT hr, ID3DBlob* errorblob, std::string context) {
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

void RayTracingStage::flush() {
	rm->cmdQueue->Signal(rm->fence, rm->fenceState);
	rm->fence->SetEventOnCompletion(rm->fenceState++, nullptr);
}