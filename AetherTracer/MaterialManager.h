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

		materials["White Plastic"] = new Material{ "White Plastic", {1.0f, 1.0f, 1.0f}, 0.5f, 0.0f, 1.0f, 0.0f, 0.0f };
		materials["Red Plastic"] = new Material{ "Red Plastic", {1.0f, 0.0f, 0.0f}, 0.5f, 0.0f, 1.0f, 0.0f, 0.0f };
		materials["Green Plastic"] = new Material{ "Green Plastic", {0.0f, 1.0f, 0.0f}, 0.5f, 0.0f, 1.0f, 0.0f, 0.0f };
		materials["Blue Plastic"] = new Material{ "Blue Plastic", {0.0f, 0.0f, 1.0f}, 0.5f, 0.0f, 1.0f, 0.0f, 0.0f };
		materials["Shiny Copper"] = new Material{ "Shiny Copper", {0.0f, 1.0f, 0.0f}, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f };
		materials["Mirror"] = new Material{ "Mirror", {1.0f, 1.0f, 1}, 0.0f, 1, 1, 0, 0 };
		materials["Light"] = new Material{ "Light", {1.0f, 1.0f, 1}, 0.0f, 0.0f, 1.0f, 0.0f, 15.0f };
		materials["Glass"] = new Material{ "Glass", {1.0f, 1.0f, 1}, 0.0f, 0.0f, 1.5f, 1.0f, 0.0f };
		materials["Orange Glass"] = new Material{ "Orange Glass", {1.0f, 0.4f, 0.0f}, 0.0f, 0.0f, 1.5f, 1.0f, 0.0f };
		materials["Black Glass"] = new Material{ "Black Glass", {0.1f, 0.1f, 0.1f}, 0.0f, 0.0f, 1.5f, 1.0f, 0.0f };
		materials["Diamond"] = new Material{ "Diamond", {1.0f, 1.0f, 1.0f}, 0.0f, 0.0f, 2.42f, 1.0f, 0.0f };

	}

	void cleanUp() {
		for (auto const& [name, material] : materials) {
			materials.erase(name);
			delete material;
		}
	
	}


	std::unordered_map<std::string, Material*> materials;
};

