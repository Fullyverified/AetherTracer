#pragma once

struct Config {

    // initial state

    float resX = 1200;
    float resY = 1200;
    float aspectX = 1;
    float aspectY = 1;

    // Multiple Importance Sampling
    int raysPerPixel = 1;
    int numBounces = 0;

    // ReSTIR

    float fOV = 45;
    bool DepthOfField = false;
    float apertureRadius = 0.05f;
    float focalDistance = 15.0f;
 
    float exposure = 1;
    float mouseSensitivity = 0.1f;
    bool sky = false;

};

extern Config config;