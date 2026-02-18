#pragma once

#include "AetherTracer.h"

int main() {

	auto aetherTracer = new AetherTracer{};

	aetherTracer->run();

	delete aetherTracer;
	return 0;
}
