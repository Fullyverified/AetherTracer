#include "DX12MaterialManager.h"


#include <limits>

DX12MaterialManager::DX12MaterialManager(MeshManager* meshManager, MaterialManager* materialManager, EntityManager* entityManager, IDXGIFactory4* factory, ID3D12Device5* d3dDevice, ID3D12CommandQueue* cmdQueue, ID3D12CommandAllocator* cmdAlloc, ID3D12GraphicsCommandList4* cmdList)
	: meshManager(meshManager), materialManager(materialManager), entityManager(entityManager), factory(factory), d3dDevice(d3dDevice), cmdQueue(cmdQueue), cmdAlloc(cmdAlloc), cmdList(cmdList) {
}

void DX12MaterialManager::initMaterials(bool new_material, bool changed_material, bool entity_material_changed) {

	for (auto& [name, dx12material] : dx12materials_map) {
		delete dx12material;
	}

	dx12materials.clear();
	dx12materials_map.clear();
	dx12materials_indices_map.clear();
	material_indices.clear();

	std::cout << "Creating DX12 Materials" << std::endl;

	texture_maps.push_back(createTextureFromVector({ 0.0f })); // dummy material in first slot, indice = 0 means no image map, use fallback

	for (auto& [mat_name, material] : materialManager->materials) {
	
		// create material if it doesn't already exist
		if (dx12materials_map.find(mat_name) == dx12materials_map.end()) {

			MaterialManager::Material* material = materialManager->materials[mat_name]; // original material
			DX12Material* dx12Material = new DX12Material{}; // DX12 Texture material

			dx12Material->albedo = { material->color.x, material->color.y, material->color.z };
			dx12Material->roughness = { material->roughness };
			dx12Material->metallic = { material->metallic };
			dx12Material->ior = { material->ior };
			dx12Material->transmission = { material->transmission };
			dx12Material->emission = { material->emission };


			std::string texture_name;

			// albedo texture
			texture_name = material->textureMap;
			if (texture_name == "") {
				dx12Material->textureMap = {0}; // fallback to raw value
			}
			else { // load from texture file
				if (texture_indices_map.find(texture_name) == texture_indices_map.end()) { // not in map
					texture_maps.push_back(createTextureFromImage(texture_name));
					texture_indices_map[texture_name] = static_cast<UINT>(texture_maps.size() - 1);
					dx12Material->textureMap = static_cast<UINT>(texture_maps.size() - 1);
				}
				else {
					// in map
					dx12Material->textureMap = texture_indices_map[texture_name];
				}
			}
			
			// roughness texture
			texture_name = material->roughnessMap;
			if (texture_name == "") {
				dx12Material->roughnessMap = 0; // fallback to raw value
			}
			else { // load from texture file
				if (texture_indices_map.find(texture_name) == texture_indices_map.end()) { // not in map
					texture_maps.push_back(createTextureFromImage(texture_name));
					texture_indices_map[texture_name] = static_cast<UINT>(texture_maps.size() - 1);
					dx12Material->roughnessMap = static_cast<UINT>(texture_maps.size() - 1);
				}
				else {
					// in map
					dx12Material->roughnessMap = texture_indices_map[texture_name];
				}
			}


			// metallic texture
			texture_name = material->metallicMap;
			if (texture_name == "") {
				dx12Material->metallicMap = 0; // fallback to raw value
			}
			else { // load from texture file
				if (texture_indices_map.find(texture_name) == texture_indices_map.end()) { // not in map
					texture_maps.push_back(createTextureFromImage(texture_name));
					texture_indices_map[texture_name] = static_cast<UINT>(texture_maps.size() - 1);
					dx12Material->metallicMap = static_cast<UINT>(texture_maps.size() - 1);
				}
				else {
					// in map
					dx12Material->metallicMap = texture_indices_map[texture_name];
				}
			}

			// ior // NO MAP FOR IOR
			dx12Material->ior = material->ior;

			// tranmission // NO MAP FOR Transmission
			dx12Material->transmission = material->transmission;


			// emission texture
			texture_name = material->emissionMap;
			if (texture_name == "") {
				dx12Material->emissionMap = 0; // fallback to raw value
			}
			else { // load from texture file
				if (texture_indices_map.find(texture_name) == texture_indices_map.end()) { // not in map
					texture_maps.push_back(createTextureFromImage(texture_name));
					texture_indices_map[texture_name] = static_cast<UINT>(texture_maps.size() - 1);
					dx12Material->emissionMap = static_cast<UINT>(texture_maps.size() - 1);
				}
				else {
					// in map
					dx12Material->emissionMap = texture_indices_map[texture_name];
				}
			}

			// normal texture
			texture_name = material->normalMap;
			if (texture_name == "") {
				dx12Material->normalMap = 0; // fallback to raw value
			}
			else { // load from texture file
				if (texture_indices_map.find(texture_name) == texture_indices_map.end()) { // not in map
					texture_maps.push_back(createTextureFromImage(texture_name));
					texture_indices_map[texture_name] = static_cast<UINT>(texture_maps.size() - 1);
					dx12Material->normalMap = static_cast<UINT>(texture_maps.size() - 1);
				}
				else {
					// in map
					dx12Material->normalMap = texture_indices_map[texture_name];
				}
			}


			dx12Material->displacementMap = 0;

			// store
			dx12materials_map[mat_name] = dx12Material;
			dx12materials.push_back(*dx12Material);
			dx12materials_indices_map[material->name] = static_cast<UINT>(dx12materials.size() - 1);
		}

	
	}
	if (config.debug) std::cout << "Finished Materials" << std::endl;

	for (size_t i = 0; i < entityManager->entities.size(); i++) {

		EntityManager::Entity* entity = entityManager->entities[i];

		UINT material_indice = dx12materials_indices_map[entity->material];
		material_indices.push_back(material_indice);
	}

}

void DX12MaterialManager::initMaterialBuffers(bool is_update) {


	size_t materialsSize = dx12materials.size() * sizeof(DX12Material);
	size_t indexSize = material_indices.size() * sizeof(UINT);

	size_t materials_size_max = config.maxMaterials * sizeof(DX12Material);
	size_t index_size_max = config.maxInstances * sizeof(UINT);

	if (!is_update) {
		materialsBuffer = createResourceHandle(dx12materials.data(), materialsSize, materials_size_max, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, false);
		materialIndexBuffer = createResourceHandle(material_indices.data(), indexSize, index_size_max, D3D12_RESOURCE_STATE_INDEX_BUFFER, false);

		materialsBuffer->upload_buffer->SetName(L"Materials Upload Buffer");
		materialIndexBuffer->upload_buffer->SetName(L"Materials Index Upload Default Buffer");
		materialsBuffer->default_buffer->SetName(L"Materials Default Buffer");
		materialIndexBuffer->default_buffer->SetName(L"Materials Index Default Buffer");
	}
	else {
		updateResourceHandle(materialsBuffer, dx12materials.data(), materialsSize, materials_size_max);
		updateResourceHandle(materialIndexBuffer, material_indices.data(), indexSize, index_size_max);

	}

	pushResourceHandle(materialsBuffer, materialsSize, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	pushResourceHandle(materialIndexBuffer, indexSize, D3D12_RESOURCE_STATE_INDEX_BUFFER, D3D12_RESOURCE_STATE_INDEX_BUFFER);
}

void DX12MaterialManager::initEmissiveIndexBuffer(bool is_update) {
	
	emissive_entities_indices.clear();

	for (size_t i = 0; i < entityManager->entities.size(); i++) {

		EntityManager::Entity* entity = entityManager->entities[i];

		if (dx12materials_map[entity->material]->emission > 0) {

			std::string name = entity->model + entity->material;
			if (emissive_entities_map.find(name) == emissive_entities_map.end()) {

				emissive_entities_indices.push_back(i);
				emissive_entities_map[name] = i;
			}
			else {
				emissive_entities_indices.push_back(emissive_entities_map[name]);
			}
		}

	}

	size_t indexSize = emissive_entities_indices.size() * sizeof(UINT);

	size_t index_size_max = config.maxInstances * sizeof(UINT);

	if (!is_update) {
		
		emissive_entities_indices_buffer = createResourceHandle(emissive_entities_indices.data(), indexSize, index_size_max, D3D12_RESOURCE_STATE_INDEX_BUFFER, false);
		emissive_entities_indices_buffer->upload_buffer->SetName(L"Emissive Entities Index Upload Buffer");
		emissive_entities_indices_buffer->default_buffer->SetName(L"Emissive Entities Index Default Buffer");
	}
	else {
			
		updateResourceHandle(emissive_entities_indices_buffer, emissive_entities_indices.data(), indexSize, index_size_max);

	}

	pushResourceHandle(emissive_entities_indices_buffer, indexSize, D3D12_RESOURCE_STATE_INDEX_BUFFER, D3D12_RESOURCE_STATE_INDEX_BUFFER);

}


DX12ResourceHandle* DX12MaterialManager::createTextureFromVector(PT::Vector3 value) {

	std::cout << "createTextureFromVector()" << std::endl;


	uint8_t r = static_cast<uint8_t>(std::round(value.x * 255.0f));
	uint8_t g = static_cast<uint8_t>(std::round(value.y * 255.0f));
	uint8_t b = static_cast<uint8_t>(std::round(value.z * 255.0f));
	uint8_t a = 255;  // usually opaque for color textures

	uint32_t pixel = (a << 24) | (b << 16) | (g << 8) | r;

	std::cout << "New resource handle " << std::endl;
	DX12ResourceHandle* texture = new DX12ResourceHandle{};

	// upload buffer
	D3D12_RESOURCE_DESC descUpload = {};
	descUpload.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER; // upload buffer must be a dimension buffer
	descUpload.Width = 256;
	descUpload.Height = 1;
	descUpload.DepthOrArraySize = 1;
	descUpload.MipLevels = 1;
	descUpload.SampleDesc = NO_AA;
	descUpload.Format = DXGI_FORMAT_UNKNOWN;
	descUpload.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
	descUpload.Flags = D3D12_RESOURCE_FLAG_NONE;

	HRESULT hr = d3dDevice->CreateCommittedResource(&UPLOAD_HEAP, D3D12_HEAP_FLAG_NONE, &descUpload, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&texture->upload_buffer));
	checkHR(hr, nullptr, "Create texture upload buffer");

	std::cout << "upload data" << std::endl;
	// upload data
	void* mapped;
	texture->upload_buffer->Map(0, nullptr, &mapped); // mapped now points to the upload buffer
	memcpy(mapped, &pixel, sizeof(UINT)); // copy data to upload buffer
	texture->upload_buffer->Unmap(0, nullptr); //
	
	std::cout << "target buffer" << std::endl;
	// Create target buffer in DEFAULT heap (VRAM)
	D3D12_RESOURCE_DESC descDefault = {};
	descDefault.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D; // default buffer can be TEXTURE2D
	descDefault.Width = 1;
	descDefault.Height = 1;
	descDefault.DepthOrArraySize = 1;
	descDefault.MipLevels = 1;
	descDefault.SampleDesc.Count = 1;
	descDefault.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	descDefault.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN; // GPU choses swizzle layout
	descDefault.Flags = D3D12_RESOURCE_FLAG_NONE;


	hr = d3dDevice->CreateCommittedResource(&DEFAULT_HEAP, D3D12_HEAP_FLAG_NONE, &descDefault, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&texture->default_buffer));
	checkHR(hr, nullptr, "Create texture target buffer");

	
	std::cout << "push " << std::endl;
	D3D12_PLACED_SUBRESOURCE_FOOTPRINT tex_footprint = {};
	pushTexture(texture, tex_footprint, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, true); // 1x1 texture now exists on GPU
	
	std::cout << "return texture " << std::endl;
	return texture;
}

DX12ResourceHandle* DX12MaterialManager::createTextureFromImage(std::string name) {

	std::cout << "createTextureFromImage()" << std::endl;

	MaterialManager::Texture* texture_data = materialManager->textures[name];

	std::cout << "New resource handle " << std::endl;
	DX12ResourceHandle* texture = new DX12ResourceHandle{};

	std::cout << "target buffer" << std::endl;
	// Create target buffer in DEFAULT heap (VRAM)
	D3D12_RESOURCE_DESC descDefault = {};
	descDefault.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D; // default buffer can be TEXTURE2D
	descDefault.Width = texture_data->width;
	descDefault.Height = texture_data->height;
	descDefault.DepthOrArraySize = 1;
	descDefault.MipLevels = 1;
	descDefault.SampleDesc.Count = 1;
	descDefault.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	descDefault.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN; // GPU choses swizzle layout
	descDefault.Flags = D3D12_RESOURCE_FLAG_NONE;

	HRESULT hr = d3dDevice->CreateCommittedResource(&DEFAULT_HEAP, D3D12_HEAP_FLAG_NONE, &descDefault, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&texture->default_buffer));
	checkHR(hr, nullptr, "Create texture target buffer");

	UINT64 upload_buffer_size;
	D3D12_PLACED_SUBRESOURCE_FOOTPRINT tex_footprint;
	d3dDevice->GetCopyableFootprints(&descDefault, 0, 1, 0, &tex_footprint, nullptr, nullptr, &upload_buffer_size);

	// upload buffer
	D3D12_RESOURCE_DESC descUpload = {};
	descUpload.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER; // upload buffer must be a dimension buffer
	descUpload.Width = upload_buffer_size;
	descUpload.Height = 1;
	descUpload.DepthOrArraySize = 1;
	descUpload.MipLevels = 1;
	descUpload.SampleDesc.Count = 1;
	descUpload.Format = DXGI_FORMAT_UNKNOWN;
	descUpload.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
	descUpload.Flags = D3D12_RESOURCE_FLAG_NONE;

	hr = d3dDevice->CreateCommittedResource(&UPLOAD_HEAP, D3D12_HEAP_FLAG_NONE, &descUpload, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&texture->upload_buffer));
	checkHR(hr, nullptr, "Create texture upload buffer");

	std::cout << "upload data with padding" << std::endl;
	// upload data
	void* mapped;
	texture->upload_buffer->Map(0, nullptr, &mapped); // mapped now points to the upload buffer

	// one row at a time
	for (int y = 0; y < texture_data->height; ++y) {
		memcpy((unsigned char*)mapped + (y * tex_footprint.Footprint.RowPitch), texture_data->data + (y * texture_data->width * 4), texture_data->width * 4);
	}

	texture->upload_buffer->Unmap(0, nullptr);

	std::cout << "push " << std::endl;
	pushTexture(texture, tex_footprint, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, false); // 1x1 texture now exists on GPU

	std::cout << "return texture " << std::endl;
	return texture;
}

void DX12MaterialManager::pushTexture(DX12ResourceHandle* texture_handle, D3D12_PLACED_SUBRESOURCE_FOOTPRINT tex_footprint, D3D12_RESOURCE_STATES state_before, D3D12_RESOURCE_STATES state_after, bool is_1x1) {

	// Barrier - transition target to COPY_DEST
	D3D12_RESOURCE_BARRIER barrier = {};
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
	barrier.Transition.pResource = texture_handle->default_buffer;
	barrier.Transition.StateBefore = state_before;
	barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
	barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

	if (state_before != D3D12_RESOURCE_STATE_COPY_DEST) {
		cmdList->ResourceBarrier(1, &barrier);
	}
	

	// Copy to GPU
	D3D12_TEXTURE_COPY_LOCATION dstLoc = {};
	dstLoc.pResource = texture_handle->default_buffer;
	dstLoc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
	dstLoc.SubresourceIndex = 0;

	D3D12_TEXTURE_COPY_LOCATION srcLoc = {};
	srcLoc.pResource = texture_handle->upload_buffer;
	srcLoc.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;

	if (is_1x1) {
		tex_footprint.Offset = 0;
		tex_footprint.Footprint.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		tex_footprint.Footprint.Width = 1;
		tex_footprint.Footprint.Height = 1;
		tex_footprint.Footprint.Depth = 1;
		tex_footprint.Footprint.RowPitch = 256;
	}

	srcLoc.PlacedFootprint = tex_footprint;

	cmdList->CopyTextureRegion(&dstLoc, 0, 0, 0, &srcLoc, nullptr);

	// Barrier - transition to final state
	barrier.Transition.pResource = texture_handle->default_buffer;
	barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
	barrier.Transition.StateAfter = state_after;
	cmdList->ResourceBarrier(1, &barrier);
	
}


// utility

DX12ResourceHandle* DX12MaterialManager::createResourceHandle(const void* data, size_t byte_size, size_t max_size, D3D12_RESOURCE_STATES finalState, bool UAV) {
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
	HRESULT hr = d3dDevice->CreateCommittedResource(&UPLOAD_HEAP, D3D12_HEAP_FLAG_NONE, &DESC, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&upload));
	checkHR(hr, nullptr, "CreateCommittedResource UPLOAD for either materials buffer or materials index buffer failed");

	void* mapped;
	upload->Map(0, nullptr, &mapped); // mapped now points to the upload buffer
	memcpy(mapped, data, byte_size); // copy data to upload buffer
	upload->Unmap(0, nullptr); // 7

	// Create target buffer in DEFAULT heap (VRAM)

	if (UAV) {
		DESC.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
	}
	ID3D12Resource* target = nullptr;
	hr = d3dDevice->CreateCommittedResource(&DEFAULT_HEAP, D3D12_HEAP_FLAG_NONE, &DESC, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&target));
	checkHR(hr, nullptr, "CreateCommittedResource DEFAULT for either materials buffer or materials index buffer failed");


	DX12ResourceHandle* resource_handle = new DX12ResourceHandle();
	resource_handle->upload_buffer = upload;
	resource_handle->default_buffer = target;

	// Barrier - transition target to COPY_DEST
	D3D12_RESOURCE_BARRIER barrier = {}; 
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
	barrier.Transition.pResource = resource_handle->default_buffer;
	barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
	barrier.Transition.StateAfter = finalState;
	barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	cmdList->ResourceBarrier(1, &barrier);

	return resource_handle;
}

void DX12MaterialManager::updateResourceHandle(DX12ResourceHandle* resource_handle, const void* data, size_t byte_size, size_t max_size) {
	//std::cout << "byteSize: " << byteSize << std::endl;

	void* mapped;
	resource_handle->upload_buffer->Map(0, nullptr, &mapped); // mapped now points to the upload buffer
	memcpy(mapped, data, byte_size); // copy data to upload buffer
	resource_handle->upload_buffer->Unmap(0, nullptr); //
}

void DX12MaterialManager::pushResourceHandle(DX12ResourceHandle* resource_handle, size_t data_size, D3D12_RESOURCE_STATES state_before, D3D12_RESOURCE_STATES state_after) {

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

void DX12MaterialManager::checkHR(HRESULT hr, ID3DBlob* errorblob, std::string context) {
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

void DX12MaterialManager::waitForGPU() {

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