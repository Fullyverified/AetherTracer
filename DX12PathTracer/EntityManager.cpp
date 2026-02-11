#include "EntityManager.h"
#include <string>

void EntityManager::initScene() {

    camera->position = { -2, 0, 0 };
    camera->rotation = { 0 , 0 };

    //sceneObjects.emplace_back(new SceneObject{ "cube", {-2, 0, 1}, {0, 0, 0} });
    //sceneObjects.emplace_back(new SceneObject{ "weirdTriangle", {0, 0, 0}, {0, 0, 0} });
    //entitys.emplace_back(new Entity{ "companionCubeOne", {5, 0, 0}, {0, 0, 0}, materialManager->materials["Blue Plastic"] });

    entitys.emplace_back(new Entity{ "cornell", {6, -6, 0}, {0, 0, 0}, materialManager->materials["White Plastic"] }); // floor

    entitys.emplace_back(new Entity{ "cornell", {6, 6, 0}, {0, 0, 0}, materialManager->materials["White Plastic"] }); // roof

    entitys.emplace_back(new Entity{ "cornell", {6, 0, 6}, {0, 0, 0}, materialManager->materials["Red Plastic"] }); // left wall
    entitys.emplace_back(new Entity{ "cornell", {6, 0, -6}, {0, 0, 0}, materialManager->materials["Green Plastic"] }); // right wall

    entitys.emplace_back(new Entity{ "cornell", {12, 0, 0}, {0, 0, 0}, materialManager->materials["White Plastic"] }); // back wall

    //entitys.emplace_back(new Entity{ "cube", {6, 0, 0}, {0, 0, 0}, materialManager->materials["Light"] }); // light

    entitys.emplace_back(new Entity{ "sphere", {6, -2.25, -1.5}, {0, 0, 0}, materialManager->materials["White Plastic"] });

    entitys.emplace_back(new Entity{ "cube", {6, -2, 1.5}, {0, 0, 0}, materialManager->materials["White Plastic"] });


    // KNOWN BUGS, final unique material will use first material
    entitys.emplace_back(new Entity{ "cornell", {20, 0, 0}, {0, 0, 0}, materialManager->materials["Shiny Copper"] }); // back wall


    for (Entity* entity : entitys) {
        if (entity->material == nullptr) entity->material = materialManager->materials["White Plastic"];
    }
}

void EntityManager::cleanUp() {

    for (Entity* entity : entitys) {

        auto it = std::find(entitys.begin(), entitys.end(), entity);

        entitys.erase(it);
        delete entity;
    }

}

