#include "LEDLight.h"
#include "Core/Program/ShaderVar.h"
#include "Utils/UI/Gui.h"
#include "Utils/Logger.h"
#include "Utils/Color/ColorHelpers.slang"
#include "Utils/Math/MathHelpers.h"
#include "Utils/Math/MathConstants.slangh"
#include "Scene/SceneDefines.slangh"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>
#include <vector>
#include <numeric>

namespace Falcor
{
LEDLight::LEDLight(const std::string& name) : Light(name, LightType::LED)
{
    mData.type = (uint32_t)LightType::LED;
    update();
}

void LEDLight::updateFromAnimation(const float4x4& transform)
{
    mData.transMat = transform;
    mData.transMatIT = transpose(inverse(transform));
    updateGeometry();
}

void LEDLight::update()
{
    updateGeometry();
    updateIntensityFromPower();
}

void LEDLight::updateGeometry()
{
    mData.surfaceArea = calculateSurfaceArea();

    // Apply scaling to transformation matrix using Falcor's math API
    float4x4 scaleMatrix = math::matrixFromScaling(mScaling);
    mData.transMat = mul(mTransformMatrix, scaleMatrix);
    mData.transMatIT = transpose(inverse(mData.transMat));

    // Update tangent and bitangent vectors based on shape
    float3 localTangent, localBitangent;
    switch (mLEDShape)
    {
    case LEDShape::Sphere:
        localTangent = float3(1, 0, 0);
        localBitangent = float3(0, 1, 0);
        break;
    case LEDShape::Ellipsoid:
        localTangent = float3(mScaling.x, 0, 0);
        localBitangent = float3(0, mScaling.y, 0);
        break;
    case LEDShape::Rectangle:
        localTangent = float3(mScaling.x, 0, 0);
        localBitangent = float3(0, mScaling.y, 0);
        break;
    }

    // Transform to world space
    mData.tangent = transformVector(mData.transMat, localTangent);
    mData.bitangent = transformVector(mData.transMat, localBitangent);

    // Synchronize only tangent and bitangent to prevent assertion failures in Light::beginFrame()
    // while preserving change detection for position, direction, and other properties
    mPrevData.tangent = mData.tangent;
    mPrevData.bitangent = mData.bitangent;
}

float LEDLight::calculateSurfaceArea() const
{
    switch (mLEDShape)
    {
    case LEDShape::Sphere:
        return 4.0f * (float)M_PI * mScaling.x * mScaling.x;
    case LEDShape::Ellipsoid:
        {
            float a = mScaling.x, b = mScaling.y, c = mScaling.z;
            // Approximate surface area for ellipsoid
            float p = 1.6075f;
            float ap = std::pow(a, p), bp = std::pow(b, p), cp = std::pow(c, p);
            return 4.0f * (float)M_PI * std::pow((ap*bp + ap*cp + bp*cp) / 3.0f, 1.0f/p);
        }
    case LEDShape::Rectangle:
        return 2.0f * (mScaling.x * mScaling.y + mScaling.x * mScaling.z + mScaling.y * mScaling.z);
    default:
        return 1.0f;
    }
}

void LEDLight::setLEDShape(LEDShape shape)
{
    if (mLEDShape != shape)
    {
        mLEDShape = shape;
        mData.shapeType = (uint32_t)shape;
        updateGeometry();
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
    mData.transMat = mtx;
    mData.transMatIT = transpose(inverse(mtx));
    updateGeometry();
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

        updateGeometry();
    }
}

void LEDLight::setWorldPosition(const float3& pos)
{
    float3 oldPos = mTransformMatrix[3].xyz();
    if (any(oldPos != pos))
    {
        mTransformMatrix[3] = float4(pos, 1.0f);
        mData.posW = pos;
        updateGeometry();
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

    // If no power is set, return the total power value as-is
    return mData.totalPower;
}

void LEDLight::updateIntensityFromPower()
{
    if (mData.totalPower > 0.0f && mData.surfaceArea > 0.0f)
    {
        // Convert total power to radiance
        float radiance = mData.totalPower / (M_PI * mData.surfaceArea);
        mData.intensity = float3(radiance);
    }
}

void LEDLight::setIntensity(const float3& intensity)
{
    Light::setIntensity(intensity);
    updateIntensityFromPower();
}

const LightData& LEDLight::getData() const
{
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

    widget.text("LED Light Settings:");

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

    float3 scaling = getScaling();
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

    if (hasCustomSpectrum())
    {
        widget.text("Spectrum samples: " + std::to_string(getSpectrumSampleCount()));
        auto range = getSpectrumRange();
        widget.text("Wavelength range: " + std::to_string(range.x) + " - " + std::to_string(range.y) + " nm");
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

        if (mHasCustomSpectrum)
        {
            setSpectrum(spectrumData);
        }
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
        mLightFieldData = normalizeLightFieldData(lightFieldData);
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
        logError("  - mHasCustomLightField: " + std::to_string(mHasCustomLightField));
        logError("  - mData.hasCustomLightField: " + std::to_string(mData.hasCustomLightField));
        logError("  - mData.lightFieldDataSize: " + std::to_string(mData.lightFieldDataSize));

        // Print first few data points
        for (size_t i = 0; i < std::min((size_t)5, mLightFieldData.size()); ++i)
        {
            logError("  - Data[" + std::to_string(i) + "]: angle=" + std::to_string(mLightFieldData[i].x) +
                   ", intensity=" + std::to_string(mLightFieldData[i].y));
        }
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
    mSpectrumCDF.clear();
    mHasCustomSpectrum = false;
    mHasCustomLightField = false;
    mData.hasCustomLightField = 0;
    mData.hasCustomSpectrum = 0;
    mData.spectrumDataSize = 0;
    mData.lightFieldDataSize = 0;
}

void LEDLight::loadSpectrumFromFile(const std::string& filePath)
{
    std::ifstream file(filePath);
    if (!file.is_open())
    {
        logError("Failed to open spectrum file: " + filePath);
        return;
    }

    std::vector<float2> data;
    std::string line;
    while (std::getline(file, line))
    {
        if (line.empty() || line[0] == '#') continue;

        std::istringstream iss(line);
        float wavelength, intensity;
        if (iss >> wavelength >> intensity)
        {
            data.push_back(float2(wavelength, intensity));
        }
    }

    if (!data.empty())
    {
        loadSpectrumData(data);
        logInfo("Loaded spectrum data: " + std::to_string(data.size()) + " samples from " + filePath);
    }
}

void LEDLight::loadLightFieldFromFile(const std::string& filePath)
{
    std::ifstream file(filePath);
    if (!file.is_open())
    {
        logError("Failed to open light field file: " + filePath);
        return;
    }

    std::vector<float2> data;
    std::string line;
    while (std::getline(file, line))
    {
        if (line.empty() || line[0] == '#') continue;

        std::istringstream iss(line);
        float angle, intensity;
        if (iss >> angle >> intensity)
        {
            data.push_back(float2(angle, intensity));
        }
    }

    if (!data.empty())
    {
        loadLightFieldData(data);
        logInfo("Loaded light field data: " + std::to_string(data.size()) + " samples from " + filePath);
    }
}

void LEDLight::setSpectrum(const std::vector<float2>& spectrumData)
{
    if (spectrumData.empty()) return;

    mSpectrumData = spectrumData;
    mHasCustomSpectrum = true;
    mData.hasCustomSpectrum = 1;

    // Build CDF for importance sampling
    buildSpectrumCDF();

    // Update wavelength range - fix pointer arithmetic error
    auto minMaxResult = std::minmax_element(spectrumData.begin(), spectrumData.end(),
        [](const float2& a, const float2& b) { return a.x < b.x; });

    mData.spectrumMinWavelength = minMaxResult.first->x;
    mData.spectrumMaxWavelength = minMaxResult.second->x;
}

size_t LEDLight::getSpectrumSampleCount() const
{
    return mSpectrumCDF.size();
}

float2 LEDLight::getSpectrumRange() const
{
    return float2(mData.spectrumMinWavelength, mData.spectrumMaxWavelength);
}

std::vector<float2> LEDLight::normalizeLightFieldData(const std::vector<float2>& rawData) const
{
    if (rawData.empty()) return {};

    // Find maximum intensity for normalization
    float maxIntensity = 0.0f;
    for (const auto& sample : rawData)
    {
        maxIntensity = math::max(maxIntensity, sample.y);
    }

    if (maxIntensity <= 0.0f) return rawData;

    // Normalize intensities
    std::vector<float2> normalized;
    normalized.reserve(rawData.size());
    for (const auto& sample : rawData)
    {
        normalized.push_back(float2(sample.x, sample.y / maxIntensity));
    }

    return normalized;
}

void LEDLight::buildSpectrumCDF()
{
    if (mSpectrumData.empty())
    {
        mSpectrumCDF.clear();
        return;
    }

    // Build cumulative distribution function for importance sampling
    mSpectrumCDF.reserve(mSpectrumData.size());
    mSpectrumCDF.clear();

    float cumulativeSum = 0.0f;
    for (size_t i = 0; i < mSpectrumData.size(); ++i)
    {
        float intensity = math::max(0.0f, mSpectrumData[i].y);
        if (i > 0)
        {
            float wavelenDiff = mSpectrumData[i].x - mSpectrumData[i-1].x;
            cumulativeSum += intensity * wavelenDiff;
        }
        mSpectrumCDF.push_back(cumulativeSum);
    }

    // Normalize CDF
    if (cumulativeSum > 0.0f)
    {
        for (float& value : mSpectrumCDF)
        {
            value /= cumulativeSum;
        }
    }
}

float LEDLight::sampleWavelengthFromSpectrum(float u) const
{
    if (mSpectrumCDF.empty() || mSpectrumData.empty())
    {
        // Fallback to uniform sampling in visible range
        return math::lerp(380.0f, 780.0f, u);
    }

    // Binary search in CDF
    auto it = std::lower_bound(mSpectrumCDF.begin(), mSpectrumCDF.end(), u);
    size_t index = std::distance(mSpectrumCDF.begin(), it);

    if (index == 0) return mSpectrumData[0].x;
    if (index >= mSpectrumData.size()) return mSpectrumData.back().x;

    // Linear interpolation between samples
    float t = (u - mSpectrumCDF[index-1]) / (mSpectrumCDF[index] - mSpectrumCDF[index-1]);
    return math::lerp(mSpectrumData[index-1].x, mSpectrumData[index].x, t);
}

}
