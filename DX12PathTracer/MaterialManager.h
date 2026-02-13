#pragma once

#include <vector>
#include <unordered_map>
#include <string>
#include "Vector.h"

class MaterialManager {

public:

	struct Material {
		std::string name;
		PT::Vector3 color;
		float roughness;
		float metallic;
		float ior;
		float transmission;
		float emission;
		std::string textureMap;
		std::string roughnessMap;
		std::string metallicMap;
		std::string emissionMap;
		std::string normalMap;
		std::string displacementMap;
	};

	MaterialManager() {};
	~MaterialManager() {
		cleanUp();
	};

	void createMaterial(Material* material, std::string name) {
		materials[name] = material;
	}

	void initDefaultMaterials() {

		materials["White Plastic"] = new Material{ "White Plastic", {1, 1, 1}, 0.5, 0, 1, 0, 0 };
		materials["Red Plastic"] = new Material{ "Red Plastic", {1, 0, 0}, 0.5, 0, 1, 0, 0 };
		materials["Green Plastic"] = new Material{ "Green Plastic", {0, 1, 0}, 0.5, 0, 1, 0, 0 };
		materials["Blue Plastic"] = new Material{ "Blue Plastic", {0, 0, 1}, 0.5, 0, 1, 0, 0 };
		materials["Shiny Copper"] = new Material{ "Shiny Copper", {0, 1, 0}, 0, 1, 1, 0, 0 };
		materials["Mirror"] = new Material{ "Mirror", {1, 1, 1}, 0, 1, 1, 0, 0 };
		materials["Light"] = new Material{ "Light", {1, 1, 1}, 0, 0, 1, 0, 20 };

	}

	void cleanUp() {
		for (auto const& [name, material] : materials) {
			materials.erase(name);
			delete material;
		}
	
	}


	std::unordered_map<std::string, Material*> materials;
};

