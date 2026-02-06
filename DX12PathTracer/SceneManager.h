#pragma once

#include "Vector.h"
#include <vector>
#include <string>

class SceneManager {
public:
	struct SceneObject {


		SceneObject(std::string name) : name(name), position{ 0, 0, 0 }, rotation{0, 0, 0} {};
		SceneObject(std::string name, PT::Vector3 position, PT::Vector3 rotation) : name(name), position(position), rotation(rotation) {};

		std::string name; // name in assets folder
		PT::Vector3 position;
		PT::Vector3 rotation;
	};

	SceneManager() {};
	~SceneManager() {
		cleanUp();
	};

	void initScene();

	void cleanUp();


	std::vector<SceneObject*> sceneObjects;

};

