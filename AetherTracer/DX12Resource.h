#pragma once

#define NOMINMAX

#include <vector>
#include "DirectXMath.h"
#include <d3d12.h>
#include <dxgi1_4.h>
#include <d3dcompiler.h> // for compiling shaders
#pragma comment(lib, "d3d12")
#pragma comment(lib, "dxgi")

#include "EntityManager.h"
#include "MaterialManager.h"
#include "MeshManager.h"

struct DX12ResourceHandle {
	// for resource that don't need an upload buffer, only the default_buffer is used
	ID3D12Resource* upload_buffer;
	ID3D12Resource* default_buffer;

	// offset in the global heap
	UINT64 heap_index_srv; // or cbv
	UINT64 heap_index_uav;
};

struct DX12Model {
	DX12Model() : loadedModel(nullptr), BLAS(nullptr) {};

	MeshManager::LoadedModel* loadedModel; // mesh and raw vertex data
	DX12ResourceHandle* BLAS;

	std::vector<DX12ResourceHandle*> vertexBuffers;
	std::vector<DX12ResourceHandle*> indexBuffers;;
};

struct DX12Entity {
	DX12Entity() : entity(nullptr), model(nullptr) {};

	EntityManager::Entity* entity; // name, position, rotation, scale
	DX12Model* model;
};

struct alignas(256)DX12Camera {

	DX12Camera() : position{} {};
	DX12Camera(PT::Vector3 position) : position{ position.x, position.y, position.z } {};

	DirectX::XMFLOAT3 position;
	float pad0;

	DirectX::XMFLOAT4X4 invViewProj;
	UINT seed;
	UINT sky;
	float skyBrighness;
	UINT minBounces;
	UINT maxBounces;
	UINT jitter;
	UINT next_event_estimation;
	UINT NEE_samples;
};