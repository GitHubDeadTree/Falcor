#include "LED_Emissive.h"
#include "Utils/Logger.h"
#include "Scene/Material/StandardMaterial.h"
#include "Core/API/Device.h"
#include <cmath>

namespace Falcor {

ref<LED_Emissive> LED_Emissive::create(const std::string& name) {
    return make_ref<LED_Emissive>(name);
}

LED_Emissive::LED_Emissive(const std::string& name) : Object(), mName(name) {
    // Initialize with default values
    mShape = LED_EmissiveShape::Sphere;
    mPosition = float3(0.0f);
    mScaling = float3(1.0f);
    mDirection = float3(0, 0, -1);
    mTotalPower = 1.0f;
    mColor = float3(1.0f);
    mLambertN = 1.0f;
    mOpeningAngle = (float)M_PI;
    mCosOpeningAngle = std::cos(mOpeningAngle);
    mHasCustomLightField = false;
    mIsAddedToScene = false;
    mCalculationError = false;

    logInfo("LED_Emissive '" + mName + "' created successfully");
}

void LED_Emissive::setShape(LED_EmissiveShape shape) {
    if (shape != LED_EmissiveShape::Sphere &&
        shape != LED_EmissiveShape::Rectangle &&
        shape != LED_EmissiveShape::Ellipsoid) {
        logWarning("LED_Emissive::setShape - Invalid shape, using default Sphere");
        mShape = LED_EmissiveShape::Sphere;
        mCalculationError = true;
        return;
    }
    mShape = shape;
}

void LED_Emissive::setPosition(const float3& pos) {
    mPosition = pos;
}

void LED_Emissive::setScaling(const float3& scale) {
    // Parameter validation: scaling > 0
    if (scale.x <= 0.0f || scale.y <= 0.0f || scale.z <= 0.0f) {
        logWarning("LED_Emissive::setScaling - Invalid scaling values, using default 0.666f");
        mScaling = float3(0.666f);
        mCalculationError = true;
        return;
    }
    mScaling = scale;

    // Update emissive intensity since surface area changed
    if (mpDevice) {
        updateEmissiveIntensity();
    }
}

void LED_Emissive::setDirection(const float3& dir) {
    float3 normalized = normalize(dir);
    if (length(dir) < 1e-6f) {
        logWarning("LED_Emissive::setDirection - Zero direction vector, using default (0,0,-1)");
        mDirection = float3(0, 0, -1);
        mCalculationError = true;
        return;
    }
    mDirection = normalized;
}

void LED_Emissive::setTotalPower(float power) {
    // Parameter validation: power >= 0
    if (power < 0.0f) {
        logWarning("LED_Emissive::setTotalPower - Negative power value, using default 0.666f");
        mTotalPower = 0.666f;
        mCalculationError = true;
        return;
    }
    mTotalPower = power;

    // Update emissive intensity if device is available
    if (mpDevice) {
        updateEmissiveIntensity();
    }
}

void LED_Emissive::setColor(const float3& color) {
    if (color.x < 0.0f || color.y < 0.0f || color.z < 0.0f) {
        logWarning("LED_Emissive::setColor - Negative color values, using default (1,1,1)");
        mColor = float3(1.0f);
        mCalculationError = true;
        return;
    }
    mColor = color;
}

void LED_Emissive::setLambertExponent(float n) {
    if (n < 0.1f || n > 100.0f) {
        logWarning("LED_Emissive::setLambertExponent - Value out of range [0.1, 100.0], using default 0.666f");
        mLambertN = 0.666f;
        mCalculationError = true;
        return;
    }
        mLambertN = n;

    // Update light profile and emissive intensity if not using custom data and device is available
    if (!mHasCustomLightField && mpDevice) {
        updateLightProfile();
        updateEmissiveIntensity();
    }
}

void LED_Emissive::setOpeningAngle(float angle) {
    // Parameter validation: angle ∈ [0, π]
    if (angle < 0.0f || angle > (float)M_PI) {
        logWarning("LED_Emissive::setOpeningAngle - Angle out of range [0, π], using default 0.666f");
        mOpeningAngle = 0.666f;
        mCosOpeningAngle = std::cos(0.666f);
        mCalculationError = true;
        return;
    }
        mOpeningAngle = angle;
    mCosOpeningAngle = std::cos(angle);

    // Update light profile and emissive intensity if device is available
    if (mpDevice) {
        updateLightProfile();
        updateEmissiveIntensity();
    }
}

void LED_Emissive::loadLightFieldData(const std::vector<float2>& data) {
    if (data.empty()) {
        logWarning("LED_Emissive::loadLightFieldData - Empty data provided");
        return;
    }

    try {
        // Validate data format (angle, intensity)
        for (const auto& point : data) {
            if (point.x < 0.0f || point.x > (float)M_PI || point.y < 0.0f) {
                logWarning("LED_Emissive::loadLightFieldData - Invalid data point, skipping");
                mCalculationError = true;
                return;
            }
        }

                mLightFieldData = data;
        mHasCustomLightField = true;

        // Update light profile and emissive intensity if device is available
        if (mpDevice) {
            updateLightProfile();
            updateEmissiveIntensity();
        }

        logInfo("LED_Emissive::loadLightFieldData - Loaded " + std::to_string(data.size()) + " data points");

    } catch (const std::exception& e) {
        logError("LED_Emissive::loadLightFieldData - Exception: " + std::string(e.what()));
        mCalculationError = true;
    }
}

void LED_Emissive::loadLightFieldFromFile(const std::string& filePath) {
    // Placeholder implementation - file loading will be implemented in later tasks
    logInfo("LED_Emissive::loadLightFieldFromFile - File loading not yet implemented: " + filePath);
}

void LED_Emissive::clearLightFieldData() {
    mLightFieldData.clear();
    mHasCustomLightField = false;
    mpLightProfile = nullptr;
    logInfo("LED_Emissive::clearLightFieldData - Custom light field data cleared");
}

void LED_Emissive::addToScene(Scene* pScene) {
    // Placeholder implementation - scene integration will be implemented in later tasks
    if (!pScene) {
        logError("LED_Emissive::addToScene - Invalid scene pointer");
        mCalculationError = true;
        return;
    }

    mpScene = pScene;
    mpDevice = pScene->getDevice();
    logInfo("LED_Emissive::addToScene - Scene integration not yet implemented");
}

void LED_Emissive::removeFromScene() {
    // Placeholder implementation - scene integration will be implemented in later tasks
    mpScene = nullptr;
    mIsAddedToScene = false;
    logInfo("LED_Emissive::removeFromScene - Scene removal not yet implemented");
}

void LED_Emissive::renderUI(Gui::Widgets& widget) {
    // Placeholder implementation - UI will be implemented in later tasks
    widget.text("LED_Emissive: " + mName);
    widget.text("Status: Basic framework implemented");

    if (mCalculationError) {
        widget.text("⚠️ Calculation errors detected!");
    }
}

// Private methods (placeholder implementations)
void LED_Emissive::generateGeometry(std::vector<Vertex>& vertices, std::vector<uint32_t>& indices) {
    // Placeholder - geometry generation will be implemented in later tasks
}

void LED_Emissive::updateLightProfile() {
    try {
        if (mHasCustomLightField) {
            // Create custom LightProfile
            mpLightProfile = createCustomLightProfile();
        } else {
            // Use Lambert distribution to create LightProfile
            mpLightProfile = createLambertLightProfile();
        }

        if (!mpLightProfile) {
            logError("LED_Emissive::updateLightProfile - Failed to create light profile");
            mCalculationError = true;

            // Create default Lambert distribution as fallback
            mpLightProfile = createDefaultLightProfile();
            return;
        }

        mCalculationError = false;
        logInfo("LED_Emissive::updateLightProfile - Light profile updated successfully");

    } catch (const std::exception& e) {
        logError("LED_Emissive::updateLightProfile - Exception: " + std::string(e.what()));
        mCalculationError = true;

        // Create default Lambert distribution as fallback
        mpLightProfile = createDefaultLightProfile();
    }
}

void LED_Emissive::updateEmissiveIntensity() {
    try {
        if (!mpDevice) {
            logWarning("LED_Emissive::updateEmissiveIntensity - Device not available");
            return;
        }

        // Recalculate emissive intensity and update material if it exists
        // This will be used when materials are created in scene integration
        float newIntensity = calculateEmissiveIntensity();

        if (!std::isfinite(newIntensity) || newIntensity < 0.0f) {
            logWarning("LED_Emissive::updateEmissiveIntensity - Invalid intensity calculated");
            mCalculationError = true;
            return;
        }

        logInfo("LED_Emissive::updateEmissiveIntensity - Updated intensity: " + std::to_string(newIntensity));

    } catch (const std::exception& e) {
        logError("LED_Emissive::updateEmissiveIntensity - Exception: " + std::string(e.what()));
        mCalculationError = true;
    }
}

float LED_Emissive::calculateEmissiveIntensity() {
    try {
        // Calculate surface area of the LED geometry
        float surfaceArea = calculateSurfaceArea();
        if (surfaceArea <= 0.0f) {
            logWarning("LED_Emissive::calculateEmissiveIntensity - Invalid surface area");
            return 0.666f;
        }

        // Calculate light cone solid angle
        float solidAngle = 2.0f * (float)M_PI * (1.0f - mCosOpeningAngle);
        if (solidAngle <= 0.0f) {
            logWarning("LED_Emissive::calculateEmissiveIntensity - Invalid solid angle");
            return 0.666f;
        }

        // Consider Lambert distribution normalization factor
        // For custom light field, use integration result; for Lambert, use analytical result
        float lambertFactor = 1.0f;
        if (!mHasCustomLightField) {
            // For Lambert distribution: integral of cos^n over hemisphere
            // Normalization factor = (n+1) for energy conservation
            lambertFactor = mLambertN + 1.0f;
        } else {
            // For custom light field, assume it's already properly normalized
            lambertFactor = 1.0f;
        }

        // Calculate unit emissive intensity
        // Formula: Intensity = TotalPower * LambertFactor / (SurfaceArea * SolidAngle)
        // This ensures energy conservation across the LED surface and emission cone
        float intensity = mTotalPower * lambertFactor / (surfaceArea * solidAngle);

        // Validate result
        if (!std::isfinite(intensity) || intensity < 0.0f) {
            logError("LED_Emissive::calculateEmissiveIntensity - Invalid calculation result");
            return 0.666f;
        }

        // Reasonable intensity range check (0.001 to 10000 for practical LED values)
        if (intensity < 0.001f || intensity > 10000.0f) {
            logWarning("LED_Emissive::calculateEmissiveIntensity - Intensity outside practical range: " + std::to_string(intensity));
        }

        return intensity;

    } catch (const std::exception& e) {
        logError("LED_Emissive::calculateEmissiveIntensity - Exception: " + std::string(e.what()));
        return 0.666f;
    }
}

float LED_Emissive::calculateSurfaceArea() const {
    try {
        switch (mShape) {
            case LED_EmissiveShape::Sphere:
                return 4.0f * (float)M_PI * mScaling.x * mScaling.x;

            case LED_EmissiveShape::Rectangle:
                return 2.0f * (mScaling.x * mScaling.y + mScaling.y * mScaling.z + mScaling.x * mScaling.z);

            case LED_EmissiveShape::Ellipsoid:
            {
                // Approximate ellipsoid surface area using Knud Thomsen's formula
                float a = mScaling.x, b = mScaling.y, c = mScaling.z;
                float p = 1.6075f; // approximation parameter
                float ap = std::pow(a, p), bp = std::pow(b, p), cp = std::pow(c, p);
                return 4.0f * (float)M_PI * std::pow((ap * bp + ap * cp + bp * cp) / 3.0f, 1.0f / p);
            }

            default:
                logWarning("LED_Emissive::calculateSurfaceArea - Unknown shape, using default");
                return 0.666f;
        }
    } catch (const std::exception& e) {
        logError("LED_Emissive::calculateSurfaceArea - Exception: " + std::string(e.what()));
        return 0.666f;
    }
}

ref<Material> LED_Emissive::createEmissiveMaterial() {
    try {
        if (!mpDevice) {
            logError("LED_Emissive::createEmissiveMaterial - Device not available");
            return createErrorMaterial();
        }

        auto pMaterial = StandardMaterial::create(mpDevice, mName + "_Material");

        // Set basic material properties
        pMaterial->setBaseColor(float4(0.05f, 0.05f, 0.05f, 1.0f)); // Dark base color
        pMaterial->setRoughness(0.9f); // High roughness for LED surface
        pMaterial->setMetallic(0.0f); // Non-metallic

        // Set emissive properties
        pMaterial->setEmissiveColor(mColor);

        // Calculate emissive intensity based on power and geometry
        float emissiveIntensity = calculateEmissiveIntensity();
        pMaterial->setEmissiveFactor(emissiveIntensity);

        // Enable LightProfile integration if available
        if (mpLightProfile) {
            pMaterial->setLightProfileEnabled(true);
            logInfo("LED_Emissive::createEmissiveMaterial - LightProfile integration enabled");
        }

        // Validate emissive intensity
        if (pMaterial->getEmissiveFactor() <= 0.0f) {
            logWarning("LED_Emissive::createEmissiveMaterial - Zero emissive intensity");
            pMaterial->setEmissiveFactor(0.666f);
            mCalculationError = true;
        }

        logInfo("LED_Emissive::createEmissiveMaterial - Material created successfully");
        return pMaterial;

    } catch (const std::exception& e) {
        logError("LED_Emissive::createEmissiveMaterial - Exception: " + std::string(e.what()));
        mCalculationError = true;
        return createErrorMaterial();
    }
}

ref<LightProfile> LED_Emissive::createLambertLightProfile() {
    try {
        if (!mpDevice) {
            logError("LED_Emissive::createLambertLightProfile - Device not available");
            return nullptr;
        }

        // Generate Lambert distribution data
        std::vector<float> lambertData;
        const uint32_t samples = 64;

        // Create IES-compatible data format
        // Header data (following IES format requirements)
        lambertData.resize(13 + 2 * samples + samples * samples, 0.0f);

        // Basic header information
        lambertData[0] = 1.0f; // normalization factor (will be set by LightProfile)
        lambertData[1] = 1.0f; // lumens per lamp
        lambertData[2] = 1.0f; // multiplier
        lambertData[3] = (float)samples; // number of vertical angles
        lambertData[4] = (float)samples; // number of horizontal angles
        lambertData[5] = 1.0f; // photometric type
        lambertData[6] = 1.0f; // units type
        lambertData[7] = 1.0f; // width
        lambertData[8] = 1.0f; // length
        lambertData[9] = 1.0f; // height
        lambertData[10] = 1.0f; // ballast factor
        lambertData[11] = 1.0f; // ballast lamp factor
        lambertData[12] = 1.0f; // input watts

        // Generate vertical angles (0 to opening angle)
        uint32_t verticalStart = 13;
        for (uint32_t i = 0; i < samples; ++i) {
            float angle = (float)i / (samples - 1) * mOpeningAngle;
            lambertData[verticalStart + i] = angle * 180.0f / (float)M_PI; // Convert to degrees
        }

        // Generate horizontal angles (0 to 360 degrees)
        uint32_t horizontalStart = verticalStart + samples;
        for (uint32_t i = 0; i < samples; ++i) {
            lambertData[horizontalStart + i] = (float)i / (samples - 1) * 360.0f;
        }

        // Generate candela values for Lambert distribution
        uint32_t candelaStart = horizontalStart + samples;
        for (uint32_t h = 0; h < samples; ++h) {
            for (uint32_t v = 0; v < samples; ++v) {
                float verticalAngle = (float)v / (samples - 1) * mOpeningAngle;
                float cosAngle = std::cos(verticalAngle);

                // Lambert distribution: I(θ) = I₀ * cos(θ)^n
                float intensity = std::pow(std::max(0.0f, cosAngle), mLambertN);

                // Apply opening angle limit
                if (verticalAngle > mOpeningAngle) intensity = 0.0f;

                lambertData[candelaStart + h * samples + v] = intensity;
            }
        }

        // Create LightProfile with generated data
        // TODO: Need to add static factory method to LightProfile class
        // For now, return nullptr as workaround
        logWarning("LED_Emissive::createLambertLightProfile - LightProfile creation not yet implemented");
        return nullptr;
        // return ref<LightProfile>(new LightProfile(mpDevice, mName + "_Lambert", lambertData));

    } catch (const std::exception& e) {
        logError("LED_Emissive::createLambertLightProfile - Exception: " + std::string(e.what()));
        return nullptr;
    }
}

ref<LightProfile> LED_Emissive::createCustomLightProfile() {
    try {
        if (!mpDevice) {
            logError("LED_Emissive::createCustomLightProfile - Device not available");
            return nullptr;
        }

        if (mLightFieldData.empty()) {
            logWarning("LED_Emissive::createCustomLightProfile - No custom light field data");
            return nullptr;
        }

        // Convert angle-intensity pairs to IES format
        const uint32_t samples = (uint32_t)mLightFieldData.size();
        std::vector<float> iesData;

        // Create IES-compatible data format
        iesData.resize(13 + 2 * samples + samples * samples, 0.0f);

        // Basic header information
        iesData[0] = 1.0f; // normalization factor
        iesData[1] = 1.0f; // lumens per lamp
        iesData[2] = 1.0f; // multiplier
        iesData[3] = (float)samples; // number of vertical angles
        iesData[4] = (float)samples; // number of horizontal angles
        iesData[5] = 1.0f; // photometric type
        iesData[6] = 1.0f; // units type
        iesData[7] = 1.0f; // width
        iesData[8] = 1.0f; // length
        iesData[9] = 1.0f; // height
        iesData[10] = 1.0f; // ballast factor
        iesData[11] = 1.0f; // ballast lamp factor
        iesData[12] = 1.0f; // input watts

        // Set vertical angles from custom data
        uint32_t verticalStart = 13;
        for (uint32_t i = 0; i < samples; ++i) {
            iesData[verticalStart + i] = mLightFieldData[i].x * 180.0f / (float)M_PI; // Convert to degrees
        }

        // Generate horizontal angles (symmetric around 0)
        uint32_t horizontalStart = verticalStart + samples;
        for (uint32_t i = 0; i < samples; ++i) {
            iesData[horizontalStart + i] = (float)i / (samples - 1) * 360.0f;
        }

        // Set candela values from custom data (assuming rotational symmetry)
        uint32_t candelaStart = horizontalStart + samples;
        for (uint32_t h = 0; h < samples; ++h) {
            for (uint32_t v = 0; v < samples; ++v) {
                iesData[candelaStart + h * samples + v] = mLightFieldData[v].y;
            }
        }

        // TODO: Need to add static factory method to LightProfile class
        // For now, return nullptr as workaround
        logWarning("LED_Emissive::createCustomLightProfile - LightProfile creation not yet implemented");
        return nullptr;
        // return ref<LightProfile>(new LightProfile(mpDevice, mName + "_Custom", iesData));

    } catch (const std::exception& e) {
        logError("LED_Emissive::createCustomLightProfile - Exception: " + std::string(e.what()));
        return nullptr;
    }
}

ref<LightProfile> LED_Emissive::createDefaultLightProfile() {
    try {
        if (!mpDevice) {
            logError("LED_Emissive::createDefaultLightProfile - Device not available");
            return nullptr;
        }

        // Create simple Lambert distribution with N=1 as fallback
        const uint32_t samples = 32;
        std::vector<float> defaultData;

        defaultData.resize(13 + 2 * samples + samples * samples, 0.0f);

        // Basic header
        defaultData[0] = 1.0f; // normalization factor
        defaultData[1] = 1.0f; // lumens per lamp
        defaultData[2] = 1.0f; // multiplier
        defaultData[3] = (float)samples; // vertical angles
        defaultData[4] = (float)samples; // horizontal angles
        defaultData[5] = 1.0f; // photometric type
        defaultData[6] = 1.0f; // units type
        defaultData[7] = 1.0f; // width
        defaultData[8] = 1.0f; // length
        defaultData[9] = 1.0f; // height
        defaultData[10] = 1.0f; // ballast factor
        defaultData[11] = 1.0f; // ballast lamp factor
        defaultData[12] = 1.0f; // input watts

        // Vertical angles (0 to π/2)
        uint32_t verticalStart = 13;
        for (uint32_t i = 0; i < samples; ++i) {
            defaultData[verticalStart + i] = (float)i / (samples - 1) * 90.0f;
        }

        // Horizontal angles
        uint32_t horizontalStart = verticalStart + samples;
        for (uint32_t i = 0; i < samples; ++i) {
            defaultData[horizontalStart + i] = (float)i / (samples - 1) * 360.0f;
        }

        // Simple cosine distribution
        uint32_t candelaStart = horizontalStart + samples;
        for (uint32_t h = 0; h < samples; ++h) {
            for (uint32_t v = 0; v < samples; ++v) {
                float angle = (float)v / (samples - 1) * (float)M_PI / 2.0f;
                defaultData[candelaStart + h * samples + v] = std::cos(angle);
            }
        }

        logInfo("LED_Emissive::createDefaultLightProfile - Created fallback Lambert profile");
        // TODO: Need to add static factory method to LightProfile class
        // For now, return nullptr as workaround
        logWarning("LED_Emissive::createDefaultLightProfile - LightProfile creation not yet implemented");
        return nullptr;
        // return ref<LightProfile>(new LightProfile(mpDevice, mName + "_Default", defaultData));

    } catch (const std::exception& e) {
        logError("LED_Emissive::createDefaultLightProfile - Exception: " + std::string(e.what()));
        return nullptr;
    }
}

ref<Material> LED_Emissive::createErrorMaterial() {
    try {
        if (!mpDevice) {
            logError("LED_Emissive::createErrorMaterial - Device not available");
            return nullptr;
        }

        // Create error indication material with distinctive orange color
        auto pMaterial = StandardMaterial::create(mpDevice, mName + "_Error");

        // Set distinctive error appearance
        pMaterial->setBaseColor(float4(0.1f, 0.1f, 0.1f, 1.0f)); // Dark base
        pMaterial->setRoughness(0.8f);
        pMaterial->setMetallic(0.0f);

        // Orange emissive color to indicate error
        pMaterial->setEmissiveColor(float3(0.666f, 0.333f, 0.0f));
        pMaterial->setEmissiveFactor(0.666f);

        logWarning("LED_Emissive::createErrorMaterial - Created error material");
        return pMaterial;

    } catch (const std::exception& e) {
        logError("LED_Emissive::createErrorMaterial - Exception: " + std::string(e.what()));
        return nullptr;
    }
}

} // namespace Falcor
