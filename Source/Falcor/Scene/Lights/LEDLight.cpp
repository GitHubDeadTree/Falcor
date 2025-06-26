#include "LEDLight.h"
#include "Core/Program/ShaderVar.h"
#include "Utils/UI/Gui.h"
#include "Utils/Logger.h"
#include "Utils/Color/ColorHelpers.slang"
#include "Utils/Math/MathHelpers.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>
#include <vector>

namespace Falcor
{
LEDLight::LEDLight(const std::string& name) : Light(name, LightType::LED)
{
    // Default settings
    mData.openingAngle = (float)M_PI;
    mData.cosOpeningAngle = std::cos(mData.openingAngle);
    mData.dirW = float3(0.0f, 0.0f, -1.0f);
    mData.lambertN = 1.0f;
    mData.shapeType = (uint32_t)LEDShape::Sphere;

    // Set initial position to (10, 10, 10)
    mTransformMatrix[3] = float4(10.0f, 10.0f, 10.0f, 1.0f);

    update();
    mPrevData = mData;
}

void LEDLight::updateFromAnimation(const float4x4& transform)
{
    // Directly use the transform from the animation system
    setTransformMatrix(transform);
}

void LEDLight::update()
{
    // Like AnalyticAreaLight::update() - ensure position consistency
    // Update mData.posW from transform matrix to maintain consistency
    mData.posW = mTransformMatrix[3].xyz();

    // Update the final transformation matrix
    updateGeometry();
}

void LEDLight::updateGeometry()
{
    try {
        // Decompose the full transform matrix
        // mData.posW is already synced from mTransformMatrix in the update() function
        float4x4 rotationScaleMatrix = mTransformMatrix;
        rotationScaleMatrix[3] = float4(0.f, 0.f, 0.f, 1.f); // Remove the translation component

        // Update transformation matrix like AnalyticAreaLight, but now without translation
        float4x4 scaleMat = math::scale(float4x4::identity(), mScaling);
        mData.transMat = mul(rotationScaleMatrix, scaleMat); // transMat now only contains rotation and scale
        mData.transMatIT = inverse(transpose(mData.transMat));

        // Update other geometric data
        mData.surfaceArea = calculateSurfaceArea();

        // Update tangent and bitangent vectors safely
        float3 baseTangent = float3(1, 0, 0);
        float3 baseBitangent = float3(0, 1, 0);

        float3 transformedTangent = transformVector(mData.transMat, baseTangent);
        float3 transformedBitangent = transformVector(mData.transMat, baseBitangent);

        // Only normalize if the transformed vectors are valid
        float tangentLength = length(transformedTangent);
        float bitangentLength = length(transformedBitangent);

        if (tangentLength > 1e-6f) {
            mData.tangent = transformedTangent / tangentLength;
        } else {
            mData.tangent = baseTangent; // Fallback to original vector
        }

        if (bitangentLength > 1e-6f) {
            mData.bitangent = transformedBitangent / bitangentLength;
        } else {
            mData.bitangent = baseBitangent; // Fallback to original vector
        }

        mCalculationError = false;
    } catch (const std::exception&) {
        mCalculationError = true;
        // Set fallback values in case of error
        mData.tangent = float3(1, 0, 0);
        mData.bitangent = float3(0, 1, 0);
        logError("LEDLight::updateGeometry - Failed to calculate geometry data");
    }

    // Synchronize only tangent and bitangent to prevent assertion failures in Light::beginFrame()
    // while preserving change detection for position, direction, and other properties
    mPrevData.tangent = mData.tangent;
    mPrevData.bitangent = mData.bitangent;
}

float LEDLight::calculateSurfaceArea() const
{
    try {
        switch (mLEDShape)
        {
        case LEDShape::Sphere:
            return 4.0f * (float)M_PI * mScaling.x * mScaling.x;

        case LEDShape::Rectangle:
            return 2.0f * (mScaling.x * mScaling.y + mScaling.y * mScaling.z + mScaling.x * mScaling.z);

        case LEDShape::Ellipsoid:
            // Approximate calculation for ellipsoid surface area
            float a = mScaling.x, b = mScaling.y, c = mScaling.z;
            float p = 1.6075f;
            return 4.0f * (float)M_PI * std::pow((std::pow(a*b, p) + std::pow(b*c, p) + std::pow(a*c, p)) / 3.0f, 1.0f/p);
        }

        // Should not reach here
        return 1.0f;
    } catch (const std::exception&) {
        logWarning("LEDLight::calculateSurfaceArea - Error in surface area calculation");
        return 0.666f; // Distinctive error value
    }
}

void LEDLight::setLEDShape(LEDShape shape)
{
    if (mLEDShape != shape)
    {
        mLEDShape = shape;
        mData.shapeType = (uint32_t)shape;
        update();
        updateIntensityFromPower();
    }
}

void LEDLight::setScaling(float3 scale)
{
    if (any(mScaling != scale))
    {
        mScaling = scale;
        update();
        updateIntensityFromPower();
    }
}

void LEDLight::setTransformMatrix(const float4x4& mtx)
{
    mTransformMatrix = mtx;
    // Use update() like AnalyticAreaLight to ensure proper position synchronization
    update();
}

void LEDLight::setOpeningAngle(float openingAngle)
{
    if (openingAngle < 0.0f) openingAngle = 0.0f;
    if (openingAngle > (float)M_PI) openingAngle = (float)M_PI;
    if (mData.openingAngle != openingAngle)
    {
        mData.openingAngle = openingAngle;
        mData.cosOpeningAngle = std::cos(openingAngle);
        updateIntensityFromPower();
    }
}

void LEDLight::setWorldDirection(const float3& dir)
{
    float3 normDir = normalize(dir);
    if (any(mData.dirW != normDir))
    {
        mData.dirW = normDir;

        // Rebuild the rotation part of mTransformMatrix while preserving translation.
        // Falcor's coordinate system uses -Z as the forward direction.
        float3 forward = normDir;
        float3 zAxis = -forward;
        float3 up = float3(0.f, 1.f, 0.f);

        // Handle the case where the direction is parallel to the up vector.
        if (abs(dot(up, zAxis)) > 0.999f)
        {
            up = float3(1.f, 0.f, 0.f);
        }

        float3 xAxis = normalize(cross(up, zAxis));
        float3 yAxis = cross(zAxis, xAxis);

        // Update the rotation component of the transform matrix.
        mTransformMatrix[0] = float4(xAxis, 0.f);
        mTransformMatrix[1] = float4(yAxis, 0.f);
        mTransformMatrix[2] = float4(zAxis, 0.f);
        // The translation component in mTransformMatrix[3] is preserved.

        update();
    }
}

void LEDLight::setWorldPosition(const float3& pos)
{
    float3 oldPos = mTransformMatrix[3].xyz();
    if (any(oldPos != pos))
    {
        mTransformMatrix[3] = float4(pos, 1.0f);
        update();
    }
}

void LEDLight::setLambertExponent(float n)
{
    n = std::max(0.1f, n);
    if (mData.lambertN != n)
    {
        mData.lambertN = n;
        updateIntensityFromPower();
    }
}

void LEDLight::setTotalPower(float power)
{
    power = std::max(0.0f, power);
    if (mData.totalPower != power)
    {
        mData.totalPower = power;
        updateIntensityFromPower();
    }
}

float LEDLight::getPower() const
{
    // Return stored power if available
    if (mData.totalPower > 0.0f)
    {
        return mData.totalPower;
    }

    try {
        // Calculate power from intensity using reverse Lambert formula
        float surfaceArea = calculateSurfaceArea();
        if (surfaceArea <= 0.0f || surfaceArea == 0.666f) {
            logWarning("LEDLight::getPower - Invalid surface area for power calculation");
            return 0.666f; // Error indicator
        }

        // Calculate solid angle
        float solidAngle = 2.0f * (float)M_PI * (1.0f - mData.cosOpeningAngle);
        if (solidAngle <= 0.0f) {
            logWarning("LEDLight::getPower - Invalid solid angle for power calculation");
            return 0.666f; // Error indicator
        }

        // Lambert correction factor
        float lambertFactor = mData.lambertN + 1.0f;
        if (lambertFactor <= 0.0f) {
            logWarning("LEDLight::getPower - Invalid Lambert factor for power calculation");
            return 0.666f; // Error indicator
        }

        // Calculate power: P = I_unit * Area * SolidAngle / LambertFactor
        // Using luminance to convert RGB intensity to scalar
        float intensityLuminance = luminance(mData.intensity);
        float calculatedPower = intensityLuminance * surfaceArea * solidAngle / lambertFactor;

        // Validate result
        if (!std::isfinite(calculatedPower) || calculatedPower < 0.0f) {
            logError("LEDLight::getPower - Invalid power calculation result");
            return 0.666f; // Error indicator
        }

        return calculatedPower;

    }
    catch (const std::exception& e) {
        logError("LEDLight::getPower - Exception in power calculation: " + std::string(e.what()));
        return 0.666f; // Error indicator
    }
}

void LEDLight::updateIntensityFromPower()
{
    if (mData.totalPower <= 0.0f) return;

    try {
        // Calculate intensity per unit solid angle based on Lambert model
        float surfaceArea = calculateSurfaceArea();
        if (surfaceArea <= 0.0f || surfaceArea == 0.666f) {
            logWarning("LEDLight::updateIntensityFromPower - Invalid surface area");
            mCalculationError = true;
            return;
        }

        // Calculate solid angle based on opening angle
        float solidAngle = 2.0f * (float)M_PI * (1.0f - mData.cosOpeningAngle);
        if (solidAngle <= 0.0f) {
            logWarning("LEDLight::updateIntensityFromPower - Invalid solid angle");
            mCalculationError = true;
            return;
        }

        // Normalization factor depends on whether we're using custom light field or Lambert model
        float normalizationFactor = 1.0f;

        if (mHasCustomLightField) {
            // For custom light field, the data is already normalized to represent relative distribution
            // No additional normalization factor needed since the integral is already 1
            normalizationFactor = 1.0f;
        } else {
            // Lambert correction factor: (N + 1) where N is Lambert exponent
            // This ensures proper normalization for angular distribution
            normalizationFactor = mData.lambertN + 1.0f;
            if (normalizationFactor <= 0.0f) {
                logWarning("LEDLight::updateIntensityFromPower - Invalid Lambert factor");
                mCalculationError = true;
                return;
            }
        }

        // Calculate unit intensity: I_unit = Power / (Area * SolidAngle / NormalizationFactor)
        // For custom light field: I_unit = Power / (Area * SolidAngle) since normalization = 1
        // For Lambert model: I_unit = Power / (Area * SolidAngle / (N + 1))
        float unitIntensity = mData.totalPower / (surfaceArea * solidAngle / normalizationFactor);

        // Validate result
        if (!std::isfinite(unitIntensity) || unitIntensity < 0.0f) {
            logError("LEDLight::updateIntensityFromPower - Invalid intensity calculation result");
            mCalculationError = true;
            return;
        }

        mData.intensity = float3(unitIntensity);
        mCalculationError = false;

    } catch (const std::exception& e) {
        mCalculationError = true;
        logError("LEDLight::updateIntensityFromPower - Exception in intensity calculation: " + std::string(e.what()));
    }
}

void LEDLight::setIntensity(const float3& intensity)
{
    if (any(mData.intensity != intensity))
    {
        Light::setIntensity(intensity);
        mData.totalPower = 0.0f; // Reset total power when intensity is set manually
    }
}

const LightData& LEDLight::getData() const
{
    // Debug output when light field data is present
    if (mHasCustomLightField)
    {
        static bool firstCall = true;
        if (firstCall)
        {
            logError("LEDLight::getData - DEBUG INFO for light: " + getName());
            logError("  - mHasCustomLightField: " + std::to_string(mHasCustomLightField));
            logError("  - mData.hasCustomLightField: " + std::to_string(mData.hasCustomLightField));
            logError("  - mData.lightFieldDataSize: " + std::to_string(mData.lightFieldDataSize));
            logError("  - mData.lightFieldDataOffset: " + std::to_string(mData.lightFieldDataOffset));
            logError("  - mLightFieldData.size(): " + std::to_string(mLightFieldData.size()));
            firstCall = false;
        }
    }
    return mData;
}

Light::Changes LEDLight::beginFrame()
{
    auto changes = Light::beginFrame();
    return changes;
}

void LEDLight::renderUI(Gui::Widgets& widget)
{
    Light::renderUI(widget);

    // Basic properties
    float3 pos = mData.posW;
    if (widget.var("World Position", pos, -FLT_MAX, FLT_MAX, 0.001f, false, "%.8f"))
    {
        setWorldPosition(pos);
    }

    float3 dir = mData.dirW;
    if (widget.direction("Direction", dir))
    {
        setWorldDirection(dir);
    }

    // Geometry shape settings
    static const Gui::DropdownList kShapeTypeList = {
        { (uint32_t)LEDShape::Sphere, "Sphere" },
        { (uint32_t)LEDShape::Ellipsoid, "Ellipsoid" },
        { (uint32_t)LEDShape::Rectangle, "Rectangle" }
    };

    uint32_t shapeType = (uint32_t)mLEDShape;
    if (widget.dropdown("Shape Type", kShapeTypeList, shapeType))
    {
        setLEDShape((LEDShape)shapeType);
    }

    float3 scaling = mScaling;
    if (widget.var("Scale", scaling, 0.00001f, 10.0f, 0.001f, false, "%.8f"))
    {
        setScaling(scaling);
    }

    // Opening angle control
    float openingAngle = getOpeningAngle();
    if (widget.var("Opening Angle", openingAngle, 0.0f, (float)M_PI))
    {
        setOpeningAngle(openingAngle);
    }

    // Lambert exponent control (disabled when using custom light field)
    float lambertN = getLambertExponent();
    if (mHasCustomLightField)
    {
        // Show read-only value when custom light field is loaded
        widget.text("Lambert Exponent: " + std::to_string(lambertN) + " (Disabled - Using Custom Light Field)");
        widget.text("DEBUG - mHasCustomLightField: " + std::to_string(mHasCustomLightField));
        widget.text("DEBUG - mData.hasCustomLightField: " + std::to_string(mData.hasCustomLightField));
    }
    else
    {
        // Allow adjustment only when using Lambert distribution
        if (widget.var("Lambert Exponent", lambertN, 0.1f, 100.0f))
        {
            setLambertExponent(lambertN);
        }
        widget.text("DEBUG - Using Lambert distribution");
    }

    // Power control
    widget.separator();
    float power = mData.totalPower;
    if (widget.var("Power (Watts)", power, 0.0f, 1000.0f))
    {
        setTotalPower(power);
    }

    // Spectrum and light field data status
    widget.separator();
    widget.text("Light Distribution Mode:");

    if (mHasCustomSpectrum)
    {
        widget.text("Spectrum: " + std::to_string(mSpectrumData.size()) + " data points loaded");
    }
    else
    {
        widget.text("Spectrum: Using default spectrum");
    }

    if (mHasCustomLightField)
    {
        widget.text("Light Field: " + std::to_string(mLightFieldData.size()) + " data points loaded");
        widget.text("Note: Custom light field overrides Lambert distribution");
    }
    else
    {
        widget.text("Light Field: Using Lambert distribution (Exponent: " + std::to_string(lambertN) + ")");
    }

    if (widget.button("Clear Custom Data"))
    {
        clearCustomData();
    }

    if (mCalculationError)
    {
        widget.text("WARNING: Calculation errors detected!");
    }
}

void LEDLight::loadSpectrumData(const std::vector<float2>& spectrumData)
{
    if (spectrumData.empty())
    {
        logWarning("LEDLight::loadSpectrumData - Empty spectrum data provided");
        return;
    }

    try {
        mSpectrumData = spectrumData;
        mHasCustomSpectrum = true;

        // Update LightData sizes
        mData.spectrumDataSize = (uint32_t)mSpectrumData.size();

        // Note: GPU buffer creation is deferred to scene renderer
        // This allows the scene to manage all GPU resources centrally
        mData.spectrumDataOffset = 0; // Will be set by scene renderer
    }
    catch (const std::exception& e) {
        mHasCustomSpectrum = false;
        logError("LEDLight::loadSpectrumData - Failed to load spectrum data: " + std::string(e.what()));
    }
}

void LEDLight::loadLightFieldData(const std::vector<float2>& lightFieldData)
{
    if (lightFieldData.empty())
    {
        logWarning("LEDLight::loadLightFieldData - Empty light field data provided");
        return;
    }

    try {
        // Normalize the light field data to ensure it represents relative distribution only
        std::vector<float2> normalizedData = normalizeLightFieldData(lightFieldData);

        mLightFieldData = normalizedData;
        mHasCustomLightField = true;
        mData.hasCustomLightField = 1;

        // Update LightData sizes
        mData.lightFieldDataSize = (uint32_t)mLightFieldData.size();

        // Note: GPU buffer creation is deferred to scene renderer
        // This allows the scene to manage all GPU resources centrally
        mData.lightFieldDataOffset = 0; // Will be set by scene renderer

        // Debug output
        logError("LEDLight::loadLightFieldData - SUCCESS!");
        logError("  - Light name: " + getName());
        logError("  - Data points loaded: " + std::to_string(mLightFieldData.size()));
        logError("  - Light field data normalized for relative distribution");
        logError("  - mHasCustomLightField: " + std::to_string(mHasCustomLightField));
        logError("  - mData.hasCustomLightField: " + std::to_string(mData.hasCustomLightField));
        logError("  - mData.lightFieldDataSize: " + std::to_string(mData.lightFieldDataSize));

        // Print first few normalized data points
        for (size_t i = 0; i < std::min((size_t)5, mLightFieldData.size()); ++i)
        {
            logError("  - Normalized Data[" + std::to_string(i) + "]: angle=" + std::to_string(mLightFieldData[i].x) +
                   ", normalized_intensity=" + std::to_string(mLightFieldData[i].y));
        }
    }
    catch (const std::exception& e) {
        mHasCustomLightField = false;
        mData.hasCustomLightField = 0;
        logError("LEDLight::loadLightFieldData - Failed to load light field data: " + std::string(e.what()));
    }
}

std::vector<float2> LEDLight::normalizeLightFieldData(const std::vector<float2>& rawData) const
{
    if (rawData.empty()) {
        logWarning("LEDLight::normalizeLightFieldData - Empty data provided");
        return rawData;
    }

    try {
        std::vector<float2> normalizedData = rawData;

        // Calculate the integral of the distribution using trapezoidal rule
        // This gives us the total "energy" in the distribution
        float totalIntegral = 0.0f;

        for (size_t i = 1; i < rawData.size(); ++i) {
            float angle1 = rawData[i-1].x;
            float intensity1 = rawData[i-1].y;
            float angle2 = rawData[i].x;
            float intensity2 = rawData[i].y;

            // Trapezoidal integration: (b-a) * (f(a) + f(b)) / 2
            float deltaAngle = angle2 - angle1;
            float avgIntensity = (intensity1 + intensity2) * 0.5f;

            // Weight by sin(theta) for spherical coordinates (solid angle element)
            float sin1 = std::sin(angle1);
            float sin2 = std::sin(angle2);
            float avgSin = (sin1 + sin2) * 0.5f;

            totalIntegral += deltaAngle * avgIntensity * avgSin;
        }

        // Check for zero or very small integral
        if (totalIntegral <= 1e-9f) {
            logWarning("LEDLight::normalizeLightFieldData - Near-zero integral, using uniform distribution");
            // Set uniform distribution
            for (auto& point : normalizedData) {
                point.y = 1.0f;
            }
            return normalizedData;
        }

        // Normalize the intensities so the integral equals 1
        // This ensures the distribution represents relative probabilities
        float normalizationFactor = 1.0f / totalIntegral;

        for (auto& point : normalizedData) {
            point.y *= normalizationFactor;
            // Ensure non-negative values
            point.y = std::max(0.0f, point.y);
        }

        logError("LEDLight::normalizeLightFieldData - Normalization completed");
        logError("  - Original integral: " + std::to_string(totalIntegral));
        logError("  - Normalization factor: " + std::to_string(normalizationFactor));

        return normalizedData;

    } catch (const std::exception& e) {
        logError("LEDLight::normalizeLightFieldData - Exception: " + std::string(e.what()));
        return rawData; // Return original data on error
    }
}

void LEDLight::clearCustomData()
{
    mSpectrumData.clear();
    mLightFieldData.clear();
    mHasCustomSpectrum = false;
    mHasCustomLightField = false;
    mData.hasCustomLightField = 0;
    mData.spectrumDataSize = 0;
    mData.lightFieldDataSize = 0;
}

void LEDLight::loadSpectrumFromFile(const std::string& filePath)
{
    try {
        // Read spectrum data from CSV file
        std::vector<float2> spectrumData;
        std::ifstream file(filePath);
        if (!file.is_open()) {
            logError("LEDLight::loadSpectrumFromFile - Failed to open file: " + filePath);
            return;
        }

        std::string line;
        while (std::getline(file, line)) {
            float wavelength, intensity;
            std::istringstream ss(line);
            std::string wavelengthStr, intensityStr;

            if (std::getline(ss, wavelengthStr, ',') && std::getline(ss, intensityStr, ',')) {
                try {
                    wavelength = std::stof(wavelengthStr);
                    intensity = std::stof(intensityStr);
                    spectrumData.push_back({wavelength, intensity});
                } catch(...) {
                    // Skip invalid lines
                    continue;
                }
            }
        }

        if (spectrumData.empty()) {
            logWarning("LEDLight::loadSpectrumFromFile - No valid data in file: " + filePath);
            return;
        }

        loadSpectrumData(spectrumData);
    }
    catch (const std::exception& e) {
        logError("LEDLight::loadSpectrumFromFile - Exception: " + std::string(e.what()));
    }
}

void LEDLight::loadLightFieldFromFile(const std::string& filePath)
{
    try {
        // Read light field data from CSV file
        std::vector<float2> lightFieldData;
        std::ifstream file(filePath);
        if (!file.is_open()) {
            logError("LEDLight::loadLightFieldFromFile - Failed to open file: " + filePath);
            return;
        }

        std::string line;
        while (std::getline(file, line)) {
            float angle, intensity;
            std::istringstream ss(line);
            std::string angleStr, intensityStr;

            if (std::getline(ss, angleStr, ',') && std::getline(ss, intensityStr, ',')) {
                try {
                    angle = std::stof(angleStr);
                    intensity = std::stof(intensityStr);
                    lightFieldData.push_back({angle, intensity});
                } catch(...) {
                    // Skip invalid lines
                    continue;
                }
            }
        }

        if (lightFieldData.empty()) {
            logWarning("LEDLight::loadLightFieldFromFile - No valid data in file: " + filePath);
            return;
        }

        loadLightFieldData(lightFieldData);
    }
    catch (const std::exception& e) {
        logError("LEDLight::loadLightFieldFromFile - Exception: " + std::string(e.what()));
    }
}

}
