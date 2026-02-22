#pragma once

#include <cstdint>
#include <string>

class UI {
public:

	static bool accelUpdate;
	static bool accumulationUpdate;
	static bool isWindowHovered;
	static bool renderUI;

	static void renderSettings();

	static float frameTime;
	
	static uint64_t raysPerSecond;
	static uint32_t numRays;
};

