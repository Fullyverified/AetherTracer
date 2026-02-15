#pragma once

#include <vector>
#include <iostream>
#include <unordered_map>
#include <string>
#include <unordered_set>
#include "Vector.h"


struct VertexKey {
	float px, py, pz;
	float nx, ny, nz;
	float tu, tv;

	bool operator==(const VertexKey& other) const {
		constexpr float eps = 1e-6f;
		return std::abs(px - other.px) < eps && std::abs(py - other.py) < eps && std::abs(pz - other.pz) < eps &&
			std::abs(nx - other.nx) < eps && std::abs(ny - other.ny) < eps && std::abs(nz - other.nz) < eps &&
			std::abs(tu - other.tu) < eps && std::abs(tv - other.tv) < eps;
	}
};


// weird hashmap magic thanks grok
namespace std {
	template<> struct hash<VertexKey> {
		size_t operator()(const VertexKey& k) const {
			size_t h1 = std::hash<float>()(k.px);
			size_t h2 = std::hash<float>()(k.py);
			size_t h3 = std::hash<float>()(k.pz);
			size_t h4 = std::hash<float>()(k.nx);
			size_t h5 = std::hash<float>()(k.ny);
			size_t h6 = std::hash<float>()(k.nz);
			size_t h7 = std::hash<float>()(k.tu);
			size_t h8 = std::hash<float>()(k.tv);
			return h1 ^ (h2 << 1) ^ (h3 << 2) ^ (h4 << 3) ^ (h5 << 4) ^ (h6 << 5) ^ (h7 << 6) ^ (h8 << 7);
		}
	};
}

class MeshManager {
public:

	MeshManager() {}
	~MeshManager() {}

	struct Vertex {
		PT::Vector3 position;
		PT::Vector3 normal;
		PT::Vector2 texcoord;
		uint32_t materialIndex;
	};

	struct Mesh {
		std::vector<Vertex> vertices;
		std::vector<uint32_t> indices;
		std::string name;
		PT::Vector3 boundsMin, boundsMax;
		uint32_t materialIndex;
	};

	struct LoadedModel {

		LoadedModel(std::string name) : name(name) {};

		std::string name;
		std::vector<Mesh> meshes;
	};


	void initMeshes();

	void loadFromObject(const std::string& fileName, bool forceOpaque = false, bool computeNormalsIfMissing = false);

	void cleanUp();
	
	std::unordered_map<std::string, MeshManager::LoadedModel*> loadedModels;

	std::unordered_set<std::string> uniqueModels;


};	


