#pragma once

#include <cstdint>

enum Tone_Mapper {
    reinhard_extended,
    hable_filmic,
    aces_filmic,
    Count
};

enum GFX_API {
    DX12,
    CUDA
};

struct Config {

    // initial state

    uint32_t resX = 3440;
    uint32_t resY = 1440;
    uint32_t internal_resX = 0;
    uint32_t internal_resY = 0;
    float aspectX = 1;
    float aspectY = 1;

    // Multiple Importance Sampling
    int raysPerPixel = 1;
    int minBounces = 0;
    int maxBounces = 50;

    bool NEE = true;
    int NEE_samples = 1;

    bool accumulate = true;
    bool jitter = true;

    // Camera
    float fOV = 45;
    bool DepthOfField = false;
    float apertureRadius = 0.05f;
    float focalDistance = 15.0f;
 
    // Tone mapping
    Tone_Mapper tone_mapper = reinhard_extended;
    float whitepoint = 15.0f;
    bool sky = false;
    float skyBrightness = 1.0f;

    // Buffer Sizes
    uint32_t maxInstances = 512;
    uint32_t maxMaterials = 512;

    float mouseSensitivity = 0.1f;
    float sensitivity = 5.0f;

    int minBouncesMax = 100;
    int maxBouncesMax = 100;

    bool debug = true;

    GFX_API gfx_api = DX12;
    
};

extern Config config;