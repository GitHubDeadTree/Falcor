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

    updateGeometry();
    mPrevData = mData;
}

void LEDLight::updateFromAnimation(const float4x4& transform)
{
    float3 position = transform[3].xyz();
    float3 direction = normalize(transformVector(transform, float3(0.0f, 0.0f, -1.0f)));
    float3 scaling = float3(math::length(transform[0].xyz()), math::length(transform[1].xyz()), math::length(transform[2].xyz()));

    mTransformMatrix = transform;
    mScaling = scaling;
    setWorldDirection(direction);
    setWorldPosition(position);
    updateGeometry();
}

void LEDLight::updateGeometry()
{
    try {
        // Update transformation matrix
        float4x4 scaleMat = math::scale(float4x4::identity(), mScaling);
        mData.transMat = mul(mTransformMatrix, scaleMat);
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
        updateGeometry();
        updateIntensityFromPower();
    }
}

void LEDLight::setScaling(float3 scale)
{
    if (any(mScaling != scale))
    {
        mScaling = scale;
        updateGeometry();
        updateIntensityFromPower();
    }
}

void LEDLight::setTransformMatrix(const float4x4& mtx)
{
    mTransformMatrix = mtx;
    updateGeometry();
}

void LEDLight::setOpeningAngle(float openingAngle)
{
    openingAngle = std::clamp(openingAngle, 0.0f, (float)M_PI);
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
    }
}

void LEDLight::setWorldPosition(const float3& pos)
{
    mData.posW = pos;
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

        // Lambert correction factor: (N + 1) where N is Lambert exponent
        // This ensures proper normalization for angular distribution
        float lambertFactor = mData.lambertN + 1.0f;
        if (lambertFactor <= 0.0f) {
            logWarning("LEDLight::updateIntensityFromPower - Invalid Lambert factor");
            mCalculationError = true;
            return;
        }

        // Calculate unit intensity: I_unit = Power / (Area * SolidAngle * LambertFactor)
        // This follows the formula: I(θ) = I_unit * cos(θ)^N / (N + 1)
        float unitIntensity = mData.totalPower / (surfaceArea * solidAngle / lambertFactor);

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

void LEDLight::renderUI(Gui::Widgets& widget)
{
    Light::renderUI(widget);

    // Basic properties
    widget.var("World Position", mData.posW, -FLT_MAX, FLT_MAX);
    widget.direction("Direction", mData.dirW);

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

    widget.var("Scale", mScaling, 0.1f, 10.0f);

    // Lambert exponent control
    float lambertN = getLambertExponent();
    if (widget.var("Lambert Exponent", lambertN, 0.1f, 10.0f))
    {
        setLambertExponent(lambertN);
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
    widget.text("Custom Data Status:");
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
    }
    else
    {
        widget.text("Light Field: Using Lambert distribution");
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
        mLightFieldData = lightFieldData;
        mHasCustomLightField = true;
        mData.hasCustomLightField = 1;

        // Update LightData sizes
        mData.lightFieldDataSize = (uint32_t)mLightFieldData.size();

        // Note: GPU buffer creation is deferred to scene renderer
        // This allows the scene to manage all GPU resources centrally
        mData.lightFieldDataOffset = 0; // Will be set by scene renderer
    }
    catch (const std::exception& e) {
        mHasCustomLightField = false;
        mData.hasCustomLightField = 0;
        logError("LEDLight::loadLightFieldData - Failed to load light field data: " + std::string(e.what()));
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
