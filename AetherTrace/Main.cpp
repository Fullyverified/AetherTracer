#include "DX12Renderer.h"
#include "EntityManager.h"
#include "MeshManager.h"
#include "MaterialManager.h"

int main() {

	auto meshManager = new MeshManager();
	auto materialManager = new MaterialManager();
	auto entityManager = new EntityManager(materialManager);
	
	meshManager->initMeshes();
	materialManager->initDefaultMaterials();
	entityManager->initScene();

	DX12Renderer dx12renderer{entityManager, meshManager, materialManager};
	dx12renderer.run();

	return 0;
}
