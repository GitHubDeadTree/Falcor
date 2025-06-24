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
    try {
        logError("LED_Emissive::addToScene - Scene integration requires SceneBuilder during scene construction");
        logError("LED_Emissive::addToScene - Call addToSceneBuilder() during scene building phase instead");
        mCalculationError = true;

    } catch (const std::exception& e) {
        logError("LED_Emissive::addToScene - Exception: " + std::string(e.what()));
        mCalculationError = true;
        cleanup();
    }
}

void LED_Emissive::addToSceneBuilder(SceneBuilder& sceneBuilder) {
    try {
        if (mIsAddedToScene) {
            logError("LED_Emissive::addToSceneBuilder - Already added to scene");
            return;
        }

        mpDevice = sceneBuilder.getDevice();

        // 1. Update LightProfile
        updateLightProfile();

        // 2. Generate geometry
        std::vector<Vertex> vertices;
        std::vector<uint32_t> indices;
        generateGeometry(vertices, indices);

        if (mCalculationError) {
            logError("LED_Emissive::addToSceneBuilder - Geometry generation failed");
            return;
        }

        // 3. Create emissive material
        auto pMaterial = createEmissiveMaterial();
        if (!pMaterial) {
            logError("LED_Emissive::addToSceneBuilder - Failed to create material");
            mCalculationError = true;
            return;
        }
        mMaterialID = sceneBuilder.addMaterial(pMaterial);

        // 4. Create triangle mesh
        auto pTriangleMesh = createTriangleMesh(vertices, indices);
        if (!pTriangleMesh) {
            logError("LED_Emissive::addToSceneBuilder - Failed to create triangle mesh");
            mCalculationError = true;
            return;
        }

        // 5. Apply transform
        float4x4 transform = createTransformMatrix();
        pTriangleMesh->applyTransform(transform);

        // 6. Add triangle mesh to scene using SceneBuilder
        MeshID meshIndex = sceneBuilder.addTriangleMesh(pTriangleMesh, pMaterial);
        mMeshIndices.push_back(meshIndex);

        mIsAddedToScene = true;
        logInfo("LED_Emissive::addToSceneBuilder - Successfully added '" + mName + "' to scene");

    } catch (const std::exception& e) {
        logError("LED_Emissive::addToSceneBuilder - Exception: " + std::string(e.what()));
        mCalculationError = true;
        cleanup();
    }
}

void LED_Emissive::removeFromScene() {
    try {
        if (!mIsAddedToScene) {
            logWarning("LED_Emissive::removeFromScene - Not added to scene");
            return;
        }

        // Scene removal not supported after construction
        // Meshes are immutable once scene is built
        logWarning("LED_Emissive::removeFromScene - Scene removal not supported after construction");
        logWarning("LED_Emissive::removeFromScene - Rebuild scene without this LED_Emissive instance");

        // Clear internal references
        mMeshIndices.clear();
        mpScene = nullptr;
        mpDevice = nullptr;
        mIsAddedToScene = false;
        mMaterialID = MaterialID();

        logInfo("LED_Emissive::removeFromScene - Cleared internal references for '" + mName + "'");

    } catch (const std::exception& e) {
        logError("LED_Emissive::removeFromScene - Exception: " + std::string(e.what()));
        mCalculationError = true;
    }
}

void LED_Emissive::renderUI(Gui::Widgets& widget) {
    // Basic properties
    widget.text("LED_Emissive: " + mName);

    float3 pos = mPosition;
    if (widget.var("Position", pos, -100.0f, 100.0f, 0.01f)) {
        try {
            setPosition(pos);
        } catch (const std::exception& e) {
            logWarning("LED_Emissive::renderUI - Position update failed: " + std::string(e.what()));
        }
    }

    float3 dir = mDirection;
    if (widget.direction("Direction", dir)) {
        try {
            setDirection(dir);
        } catch (const std::exception& e) {
            logWarning("LED_Emissive::renderUI - Direction update failed: " + std::string(e.what()));
        }
    }

    // Shape settings
    static const Gui::DropdownList kShapeList = {
        { (uint32_t)LED_EmissiveShape::Sphere, "Sphere" },
        { (uint32_t)LED_EmissiveShape::Rectangle, "Rectangle" },
        { (uint32_t)LED_EmissiveShape::Ellipsoid, "Ellipsoid" }
    };

    uint32_t shape = (uint32_t)mShape;
    if (widget.dropdown("Shape", kShapeList, shape)) {
        try {
            setShape((LED_EmissiveShape)shape);
        } catch (const std::exception& e) {
            logWarning("LED_Emissive::renderUI - Shape update failed: " + std::string(e.what()));
        }
    }

    float3 scale = mScaling;
    if (widget.var("Scale", scale, 0.001f, 10.0f, 0.001f)) {
        try {
            // Validate scale values
            if (scale.x <= 0.0f || scale.y <= 0.0f || scale.z <= 0.0f) {
                logWarning("LED_Emissive::renderUI - Invalid scale values, must be positive");
                scale = float3(std::max(0.001f, scale.x), std::max(0.001f, scale.y), std::max(0.001f, scale.z));
            }
            setScaling(scale);
        } catch (const std::exception& e) {
            logWarning("LED_Emissive::renderUI - Scale update failed: " + std::string(e.what()));
        }
    }

    // Light properties
    widget.separator();
    widget.text("Light Properties");

    float power = mTotalPower;
    if (widget.var("Total Power (W)", power, 0.0f, 1000.0f)) {
        try {
            // Validate power value
            if (power < 0.0f) {
                logWarning("LED_Emissive::renderUI - Power cannot be negative");
                power = 0.0f;
            }
            setTotalPower(power);
        } catch (const std::exception& e) {
            logWarning("LED_Emissive::renderUI - Power update failed: " + std::string(e.what()));
        }
    }

    float3 color = mColor;
    if (widget.rgbColor("Color", color)) {
        try {
            // Validate color values (should be non-negative)
            if (color.x < 0.0f || color.y < 0.0f || color.z < 0.0f) {
                logWarning("LED_Emissive::renderUI - Color values cannot be negative");
                color = float3(std::max(0.0f, color.x), std::max(0.0f, color.y), std::max(0.0f, color.z));
            }
            setColor(color);
        } catch (const std::exception& e) {
            logWarning("LED_Emissive::renderUI - Color update failed: " + std::string(e.what()));
        }
    }

    // Light field distribution control
    widget.separator();
    widget.text("Light Field Distribution");

    float openingAngle = mOpeningAngle;
    if (widget.var("Opening Angle", openingAngle, 0.0f, (float)M_PI)) {
        try {
            // Validate opening angle range
            if (openingAngle < 0.0f || openingAngle > (float)M_PI) {
                logWarning("LED_Emissive::renderUI - Opening angle must be between 0 and PI");
                openingAngle = std::clamp(openingAngle, 0.0f, (float)M_PI);
            }
            setOpeningAngle(openingAngle);
        } catch (const std::exception& e) {
            logWarning("LED_Emissive::renderUI - Opening angle update failed: " + std::string(e.what()));
        }
    }

    if (!mHasCustomLightField) {
        float lambertN = mLambertN;
        if (widget.var("Lambert Exponent", lambertN, 0.1f, 100.0f)) {
            try {
                // Validate Lambert exponent range
                if (lambertN < 0.1f || lambertN > 100.0f) {
                    logWarning("LED_Emissive::renderUI - Lambert exponent must be between 0.1 and 100.0");
                    lambertN = std::clamp(lambertN, 0.1f, 100.0f);
                }
                setLambertExponent(lambertN);
            } catch (const std::exception& e) {
                logWarning("LED_Emissive::renderUI - Lambert exponent update failed: " + std::string(e.what()));
            }
        }
    }

    // Custom light field status
    widget.separator();
    if (mHasCustomLightField) {
        widget.text("Custom Light Field: " + std::to_string(mLightFieldData.size()) + " points");
        if (widget.button("Clear Custom Data")) {
            try {
                clearLightFieldData();
                logInfo("LED_Emissive::renderUI - Custom light field data cleared");
            } catch (const std::exception& e) {
                logError("LED_Emissive::renderUI - Failed to clear custom data: " + std::string(e.what()));
            }
        }
    } else {
        widget.text("Using Lambert Distribution (N=" + std::to_string(mLambertN) + ")");
        if (widget.button("Load Light Field File")) {
            // File loading would be implemented with system file dialog
            widget.text("File dialog not yet implemented");
        }
    }

        // Status information
    widget.separator();
    widget.text("Status Information");

    try {
        float surfaceArea = calculateSurfaceArea();
        widget.text("Surface Area: " + std::to_string(surfaceArea));

        float emissiveIntensity = calculateEmissiveIntensity();
        widget.text("Emissive Intensity: " + std::to_string(emissiveIntensity));
    } catch (const std::exception& e) {
        widget.text("Surface Area: Calculation error - " + std::string(e.what()));
        widget.text("Emissive Intensity: Calculation error");
    }

    widget.text("Scene Integration: " + (mIsAddedToScene ? std::string("Yes") : std::string("No")));

    // Light profile status
    if (mpLightProfile) {
        widget.text("Light Profile: Created successfully");
    } else {
        widget.text("Light Profile: Not created");
    }

    // Device status
    if (mpDevice) {
        widget.text("Device: Available");
    } else {
        widget.text("Device: Not available");
    }

    // Error status with detailed information
    if (mCalculationError) {
        widget.text("⚠️ Calculation errors detected!");
        widget.text("Check log for detailed error information");
    } else {
        widget.text("✓ All calculations successful");
    }
}

// Private methods (placeholder implementations)
void LED_Emissive::generateGeometry(std::vector<Vertex>& vertices, std::vector<uint32_t>& indices) {
    try {
        vertices.clear();
        indices.clear();

        switch (mShape) {
            case LED_EmissiveShape::Sphere:
                generateSphereGeometry(vertices, indices);
                break;

            case LED_EmissiveShape::Rectangle:
                generateRectangleGeometry(vertices, indices);
                break;

            case LED_EmissiveShape::Ellipsoid:
                generateEllipsoidGeometry(vertices, indices);
                break;

            default:
                logError("LED_Emissive::generateGeometry - Unknown shape");
                mCalculationError = true;
                return;
        }

        // Validate generated geometry
        if (vertices.empty() || indices.empty()) {
            logError("LED_Emissive::generateGeometry - No geometry generated");
            mCalculationError = true;
            return;
        }

        // Check for valid triangle count (indices should be multiple of 3)
        if (indices.size() % 3 != 0) {
            logError("LED_Emissive::generateGeometry - Invalid index count");
            mCalculationError = true;
            return;
        }

        logInfo("LED_Emissive::generateGeometry - Generated " + std::to_string(vertices.size()) +
                " vertices, " + std::to_string(indices.size() / 3) + " triangles");

    } catch (const std::exception& e) {
        logError("LED_Emissive::generateGeometry - Exception: " + std::string(e.what()));
        mCalculationError = true;
        vertices.clear();
        indices.clear();
    }
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

void LED_Emissive::generateSphereGeometry(std::vector<Vertex>& vertices, std::vector<uint32_t>& indices) {
    try {
        const uint32_t latSegments = 16; // latitude segments
        const uint32_t lonSegments = 32; // longitude segments
        const float radius = 1.0f; // unit sphere, scaling applied via transform

        vertices.clear();
        indices.clear();

        // Generate vertices
        for (uint32_t lat = 0; lat <= latSegments; ++lat) {
            float theta = (float)lat / latSegments * (float)M_PI; // 0 to π
            float cosTheta = std::cos(theta);
            float sinTheta = std::sin(theta);

            for (uint32_t lon = 0; lon <= lonSegments; ++lon) {
                float phi = (float)lon / lonSegments * 2.0f * (float)M_PI; // 0 to 2π
                float cosPhi = std::cos(phi);
                float sinPhi = std::sin(phi);

                // Spherical coordinates to Cartesian
                float3 position = float3(
                    radius * sinTheta * cosPhi,
                    radius * cosTheta,
                    radius * sinTheta * sinPhi
                );

                // Normal vector for sphere is same as normalized position
                float3 normal = normalize(position);

                // Texture coordinates
                float2 texCoord = float2(
                    (float)lon / lonSegments,
                    (float)lat / latSegments
                );

                Vertex vertex;
                vertex.position = position;
                vertex.normal = normal;
                vertex.texCoord = texCoord;
                vertices.push_back(vertex);
            }
        }

        // Generate indices for triangles
        for (uint32_t lat = 0; lat < latSegments; ++lat) {
            for (uint32_t lon = 0; lon < lonSegments; ++lon) {
                uint32_t current = lat * (lonSegments + 1) + lon;
                uint32_t next = current + lonSegments + 1;

                // Two triangles per quad
                // Triangle 1: current, next, current+1
                indices.push_back(current);
                indices.push_back(next);
                indices.push_back(current + 1);

                // Triangle 2: current+1, next, next+1
                indices.push_back(current + 1);
                indices.push_back(next);
                indices.push_back(next + 1);
            }
        }

        logInfo("LED_Emissive::generateSphereGeometry - Generated sphere with " +
                std::to_string(vertices.size()) + " vertices");

    } catch (const std::exception& e) {
        logError("LED_Emissive::generateSphereGeometry - Exception: " + std::string(e.what()));
        mCalculationError = true;
        vertices.clear();
        indices.clear();
    }
}

void LED_Emissive::generateRectangleGeometry(std::vector<Vertex>& vertices, std::vector<uint32_t>& indices) {
    try {
        vertices.clear();
        indices.clear();

        // Create a unit cube (box) with 6 faces
        // Front face (z = +0.5)
        vertices.push_back({{-0.5f, -0.5f, 0.5f}, {0, 0, 1}, {0, 0}});
        vertices.push_back({{0.5f, -0.5f, 0.5f}, {0, 0, 1}, {1, 0}});
        vertices.push_back({{0.5f, 0.5f, 0.5f}, {0, 0, 1}, {1, 1}});
        vertices.push_back({{-0.5f, 0.5f, 0.5f}, {0, 0, 1}, {0, 1}});

        // Back face (z = -0.5)
        vertices.push_back({{0.5f, -0.5f, -0.5f}, {0, 0, -1}, {0, 0}});
        vertices.push_back({{-0.5f, -0.5f, -0.5f}, {0, 0, -1}, {1, 0}});
        vertices.push_back({{-0.5f, 0.5f, -0.5f}, {0, 0, -1}, {1, 1}});
        vertices.push_back({{0.5f, 0.5f, -0.5f}, {0, 0, -1}, {0, 1}});

        // Right face (x = +0.5)
        vertices.push_back({{0.5f, -0.5f, 0.5f}, {1, 0, 0}, {0, 0}});
        vertices.push_back({{0.5f, -0.5f, -0.5f}, {1, 0, 0}, {1, 0}});
        vertices.push_back({{0.5f, 0.5f, -0.5f}, {1, 0, 0}, {1, 1}});
        vertices.push_back({{0.5f, 0.5f, 0.5f}, {1, 0, 0}, {0, 1}});

        // Left face (x = -0.5)
        vertices.push_back({{-0.5f, -0.5f, -0.5f}, {-1, 0, 0}, {0, 0}});
        vertices.push_back({{-0.5f, -0.5f, 0.5f}, {-1, 0, 0}, {1, 0}});
        vertices.push_back({{-0.5f, 0.5f, 0.5f}, {-1, 0, 0}, {1, 1}});
        vertices.push_back({{-0.5f, 0.5f, -0.5f}, {-1, 0, 0}, {0, 1}});

        // Top face (y = +0.5)
        vertices.push_back({{-0.5f, 0.5f, 0.5f}, {0, 1, 0}, {0, 0}});
        vertices.push_back({{0.5f, 0.5f, 0.5f}, {0, 1, 0}, {1, 0}});
        vertices.push_back({{0.5f, 0.5f, -0.5f}, {0, 1, 0}, {1, 1}});
        vertices.push_back({{-0.5f, 0.5f, -0.5f}, {0, 1, 0}, {0, 1}});

        // Bottom face (y = -0.5)
        vertices.push_back({{-0.5f, -0.5f, -0.5f}, {0, -1, 0}, {0, 0}});
        vertices.push_back({{0.5f, -0.5f, -0.5f}, {0, -1, 0}, {1, 0}});
        vertices.push_back({{0.5f, -0.5f, 0.5f}, {0, -1, 0}, {1, 1}});
        vertices.push_back({{-0.5f, -0.5f, 0.5f}, {0, -1, 0}, {0, 1}});

        // Generate indices for 12 triangles (2 per face, 6 faces)
        uint32_t faceIndices[] = {
            // Front face
            0, 1, 2,    0, 2, 3,
            // Back face
            4, 5, 6,    4, 6, 7,
            // Right face
            8, 9, 10,   8, 10, 11,
            // Left face
            12, 13, 14, 12, 14, 15,
            // Top face
            16, 17, 18, 16, 18, 19,
            // Bottom face
            20, 21, 22, 20, 22, 23
        };

        indices.assign(std::begin(faceIndices), std::end(faceIndices));

        logInfo("LED_Emissive::generateRectangleGeometry - Generated cube with " +
                std::to_string(vertices.size()) + " vertices");

    } catch (const std::exception& e) {
        logError("LED_Emissive::generateRectangleGeometry - Exception: " + std::string(e.what()));
        mCalculationError = true;
        vertices.clear();
        indices.clear();
    }
}

void LED_Emissive::generateEllipsoidGeometry(std::vector<Vertex>& vertices, std::vector<uint32_t>& indices) {
    try {
        const uint32_t latSegments = 16;
        const uint32_t lonSegments = 32;

        vertices.clear();
        indices.clear();

        // Generate vertices for ellipsoid (similar to sphere but with different radii)
        for (uint32_t lat = 0; lat <= latSegments; ++lat) {
            float theta = (float)lat / latSegments * (float)M_PI; // 0 to π
            float cosTheta = std::cos(theta);
            float sinTheta = std::sin(theta);

            for (uint32_t lon = 0; lon <= lonSegments; ++lon) {
                float phi = (float)lon / lonSegments * 2.0f * (float)M_PI; // 0 to 2π
                float cosPhi = std::cos(phi);
                float sinPhi = std::sin(phi);

                // Ellipsoid coordinates (will be scaled by transform matrix)
                // Using unit ellipsoid (rx=ry=rz=1), scaling applied via mScaling
                float3 position = float3(
                    sinTheta * cosPhi,
                    cosTheta,
                    sinTheta * sinPhi
                );

                // Normal calculation for ellipsoid is more complex
                // For unit ellipsoid, normal is same as position
                float3 normal = normalize(position);

                // Texture coordinates
                float2 texCoord = float2(
                    (float)lon / lonSegments,
                    (float)lat / latSegments
                );

                Vertex vertex;
                vertex.position = position;
                vertex.normal = normal;
                vertex.texCoord = texCoord;
                vertices.push_back(vertex);
            }
        }

        // Generate indices (same as sphere)
        for (uint32_t lat = 0; lat < latSegments; ++lat) {
            for (uint32_t lon = 0; lon < lonSegments; ++lon) {
                uint32_t current = lat * (lonSegments + 1) + lon;
                uint32_t next = current + lonSegments + 1;

                // Two triangles per quad
                indices.push_back(current);
                indices.push_back(next);
                indices.push_back(current + 1);

                indices.push_back(current + 1);
                indices.push_back(next);
                indices.push_back(next + 1);
            }
        }

        logInfo("LED_Emissive::generateEllipsoidGeometry - Generated ellipsoid with " +
                std::to_string(vertices.size()) + " vertices");

    } catch (const std::exception& e) {
        logError("LED_Emissive::generateEllipsoidGeometry - Exception: " + std::string(e.what()));
        mCalculationError = true;
        vertices.clear();
        indices.clear();
    }
}

float4x4 LED_Emissive::createTransformMatrix() {
    try {
        // Create scaling matrix
        float4x4 scale = float4x4::identity();
        scale[0][0] = mScaling.x;
        scale[1][1] = mScaling.y;
        scale[2][2] = mScaling.z;

        // Create rotation matrix (align -Z axis to direction vector)
        float3 forward = normalize(mDirection);
        float3 up = float3(0, 1, 0);

        // Handle case where direction is parallel to up vector
        if (abs(dot(up, forward)) > 0.999f) {
            up = float3(1, 0, 0);
        }

        float3 right = normalize(cross(up, forward));
        up = cross(forward, right);

        float4x4 rotation = float4x4::identity();
        rotation[0] = float4(right, 0);
        rotation[1] = float4(up, 0);
        rotation[2] = float4(-forward, 0); // Falcor uses -Z as forward
        rotation[3] = float4(0, 0, 0, 1);

        // Create translation matrix
        float4x4 translation = float4x4::identity();
        translation[3] = float4(mPosition, 1);

        // Combine transformations: T * R * S
        return mul(translation, mul(rotation, scale));

    } catch (const std::exception& e) {
        logError("LED_Emissive::createTransformMatrix - Exception: " + std::string(e.what()));
        return float4x4::identity();
    }
}

ref<TriangleMesh> LED_Emissive::createTriangleMesh(const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices) {
    try {
        if (vertices.empty() || indices.empty()) {
            logError("LED_Emissive::createTriangleMesh - Empty vertex or index data");
            return nullptr;
        }

        // Convert our Vertex structure to TriangleMesh vertex format
        TriangleMesh::VertexList triangleVertices;
        triangleVertices.reserve(vertices.size());

        for (const auto& vertex : vertices) {
            TriangleMesh::Vertex triangleVertex;
            triangleVertex.position = vertex.position;
            triangleVertex.normal = vertex.normal;
            triangleVertex.texCoord = vertex.texCoord;
            triangleVertices.push_back(triangleVertex);
        }

        // Create TriangleMesh object
        auto pTriangleMesh = TriangleMesh::create(triangleVertices, indices);
        if (!pTriangleMesh) {
            logError("LED_Emissive::createTriangleMesh - Failed to create triangle mesh");
            return nullptr;
        }

        // Set mesh name
        pTriangleMesh->setName(mName + "_Mesh");

        logInfo("LED_Emissive::createTriangleMesh - Created triangle mesh with " +
                std::to_string(vertices.size()) + " vertices and " +
                std::to_string(indices.size() / 3) + " triangles");

        return pTriangleMesh;

    } catch (const std::exception& e) {
        logError("LED_Emissive::createTriangleMesh - Exception: " + std::string(e.what()));
        return nullptr;
    }
}

void LED_Emissive::cleanup() {
    try {
        if (mpScene && mIsAddedToScene) {
            // Remove from scene if still added
            removeFromScene();
        }

        // Clear all data
        mMeshIndices.clear();
        mpLightProfile = nullptr;
        mpScene = nullptr;
        mpDevice = nullptr;
        mIsAddedToScene = false;
        mMaterialID = MaterialID();

        logInfo("LED_Emissive::cleanup - Cleanup completed for '" + mName + "'");

    } catch (const std::exception& e) {
        logError("LED_Emissive::cleanup - Exception: " + std::string(e.what()));
    }
}

} // namespace Falcor


