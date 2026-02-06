#include "SceneManager.h"

void SceneManager::initScene() {

    //sceneObjects.emplace_back(new SceneObject{ "companionCubeLow", {-1, 0, 0}, {0, 0, 0} });
    sceneObjects.emplace_back(new SceneObject{ "cube", {2, 0, 1}, {0, 0, 0} });
    sceneObjects.emplace_back(new SceneObject{ "weirdTriangle", {0, 0, 0}, {0, 0, 0} });
    sceneObjects.emplace_back(new SceneObject{ "sphere", {-2, 0, 1}, {0, 0, 0} });


}

void SceneManager::cleanUp() {

    for (SceneObject* sceneObject : sceneObjects) {

        auto it = std::find(sceneObjects.begin(), sceneObjects.end(), sceneObject);

        sceneObjects.erase(it);
        delete sceneObject;
    }

}