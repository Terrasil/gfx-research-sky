#pragma once

#include <array>

// The LOM profile follows SkySettings.hpp from the supplied LOM source.
// Noise is generated locally; no game texture or captured environment is required.
struct VolumeSettings {
    int cloudSamples = 64;
    int lightSamples = 6;
    int emptyStep = 2;
    int auroraSamples = 128;
    float coverage = 0.30f;
    float scale = 1.25f;
    float density = 2.0f;
    float detail = 0.25f;
    float softness = 0.02f;
    float depth = 0.28f;
    float selfShadow = 1.3f;
    float type = 0.8f;
    float weather = 0.2f;
    float anisotropy = 0.65f;
    float multipleScattering = 0.35f;
    float powder = 0.8f;
    float detailScale = 2.07f;
    float lightCone = 0.12f;
    float jitter = 0.15f;
    float turbidity = 2.5f;
    float atmosphereExposure = 0.035f;
    float auroraBase = 1.2f;
    float auroraHeight = 2.4f;
    float auroraWidth = 0.035f;
    float auroraSpeed = 0.08f;
    float auroraIntensity = 1.15f;

    void budget(int index) {
        static constexpr std::array<int, 3> view{32, 64, 128};
        static constexpr std::array<int, 3> light{3, 6, 12};
        static constexpr std::array<int, 3> aurora{64, 128, 256};
        cloudSamples = view.at(index);
        lightSamples = light.at(index);
        auroraSamples = aurora.at(index);
    }
};

inline constexpr std::array<const char*, 3> VOLUME_BUDGET_IDS{"low", "lom", "high"};
