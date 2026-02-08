#include "DX12PathTracer.h"
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

	DX12PathTracer dx12pathtracer{entityManager, meshManager, materialManager};
	dx12pathtracer.run();

	return 0;
}
