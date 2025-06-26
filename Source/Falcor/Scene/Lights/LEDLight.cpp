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
    mScaling = math::max(scale, float3(0.01f));
    updateGeometry();
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
    mData.openingAngle = math::clamp(openingAngle, 0.01f, (float)M_PI);
    mData.cosOpeningAngle = std::cos(mData.openingAngle);
}

void LEDLight::setWorldDirection(const float3& dir)
{
    mData.dirW = normalize(dir);
}

void LEDLight::setWorldPosition(const float3& pos)
{
    mData.posW = pos;
}

void LEDLight::setLambertExponent(float n)
{
    mData.lambertN = math::max(0.1f, n);
}

void LEDLight::setTotalPower(float power)
{
    mData.totalPower = math::max(0.0f, power);
    updateIntensityFromPower();
}

float LEDLight::getPower() const
{
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

    // Shape selection - use proper DropdownList construction
    static const Gui::DropdownList kShapeList = {
        {(uint32_t)LEDShape::Sphere, "Sphere"},
        {(uint32_t)LEDShape::Ellipsoid, "Ellipsoid"},
        {(uint32_t)LEDShape::Rectangle, "Rectangle"}
    };

    uint32_t shape = (uint32_t)mLEDShape;
    if (widget.dropdown("Shape", kShapeList, shape))
    {
        setLEDShape((LEDShape)shape);
    }

    // Lambert exponent
    float lambertN = getLambertExponent();
    if (widget.var("Lambert Exponent", lambertN, 0.1f, 10.0f))
    {
        setLambertExponent(lambertN);
    }

    // Total power
    float totalPower = getTotalPower();
    if (widget.var("Total Power (W)", totalPower, 0.0f, 1000.0f))
    {
        setTotalPower(totalPower);
    }

    // Scaling
    float3 scaling = getScaling();
    if (widget.var("Scaling", scaling, 0.01f, 10.0f))
    {
        setScaling(scaling);
    }

    // Opening angle
    float openingAngle = getOpeningAngle();
    float openingAngleDeg = math::degrees(openingAngle);
    if (widget.var("Opening Angle (deg)", openingAngleDeg, 1.0f, 180.0f))
    {
        setOpeningAngle(math::radians(openingAngleDeg));
    }

    // Spectrum info
    widget.text("Spectrum: " + std::string(hasCustomSpectrum() ? "Custom" : "Default"));
    widget.text("Light Field: " + std::string(hasCustomLightField() ? "Custom" : "Default"));

    if (hasCustomSpectrum())
    {
        widget.text("Spectrum samples: " + std::to_string(getSpectrumSampleCount()));
        auto range = getSpectrumRange();
        widget.text("Wavelength range: " + std::to_string(range.x) + " - " + std::to_string(range.y) + " nm");
    }
}

void LEDLight::loadSpectrumData(const std::vector<float2>& spectrumData)
{
    mSpectrumData = spectrumData;
    mHasCustomSpectrum = !spectrumData.empty();
    if (mHasCustomSpectrum)
    {
        setSpectrum(spectrumData);
    }
}

void LEDLight::loadLightFieldData(const std::vector<float2>& lightFieldData)
{
    mLightFieldData = normalizeLightFieldData(lightFieldData);
    mHasCustomLightField = !lightFieldData.empty();
    mData.hasCustomLightField = mHasCustomLightField ? 1 : 0;
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
