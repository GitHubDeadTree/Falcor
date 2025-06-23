#include "LEDLight.h"
#include "Core/Program/ShaderVar.h"
#include "Utils/UI/Gui.h"
#include "Utils/Logger.h"

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
}

void LEDLight::updateFromAnimation(const float4x4& transform)
{
    float3 position = transform[3].xyz;
    float3 direction = normalize(transformVector(transform, float3(0.0f, 0.0f, -1.0f)));
    float3 scaling = float3(length(transform[0].xyz), length(transform[1].xyz), length(transform[2].xyz));

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
        float4x4 scaleMat = float4x4::scale(mScaling);
        mData.transMat = mTransformMatrix * scaleMat;
        mData.transMatIT = inverse(transpose(mData.transMat));

        // Update other geometric data
        mData.surfaceArea = calculateSurfaceArea();

        // Update tangent and bitangent vectors
        mData.tangent = normalize(transformVector(mData.transMat, float3(1, 0, 0)));
        mData.bitangent = normalize(transformVector(mData.transMat, float3(0, 1, 0)));

        mCalculationError = false;
    } catch (const std::exception&) {
        mCalculationError = true;
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
    openingAngle = clamp(openingAngle, 0.0f, (float)M_PI);
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

void LEDLight::setLambertExponent(float n)
{
    n = max(0.1f, n);
    if (mData.lambertN != n)
    {
        mData.lambertN = n;
        updateIntensityFromPower();
    }
}

void LEDLight::setTotalPower(float power)
{
    power = max(0.0f, power);
    if (mData.totalPower != power)
    {
        mData.totalPower = power;
        updateIntensityFromPower();
    }
}

float LEDLight::getPower() const
{
    if (mData.totalPower > 0.0f)
    {
        return mData.totalPower;
    }

    try {
        // Calculate power based on intensity, surface area and angle distribution
        float surfaceArea = calculateSurfaceArea();
        if (surfaceArea <= 0.0f) return 0.666f; // Error value

        float solidAngle = 2.0f * (float)M_PI * (1.0f - mData.cosOpeningAngle);
        if (solidAngle <= 0.0f) return 0.666f; // Error value

        return luminance(mData.intensity) * surfaceArea * solidAngle / (mData.lambertN + 1.0f);
    }
    catch (const std::exception&) {
        logError("LEDLight::getPower - Failed to calculate power");
        return 0.666f;
    }
}

void LEDLight::updateIntensityFromPower()
{
    if (mData.totalPower <= 0.0f) return;

    try {
        // Calculate intensity per unit solid angle
        float surfaceArea = calculateSurfaceArea();
        if (surfaceArea <= 0.0f) {
            logWarning("LEDLight::updateIntensityFromPower - Invalid surface area");
            return;
        }

        // Calculate angle modulation factor
        float angleFactor = mData.lambertN + 1.0f;
        float solidAngle = 2.0f * (float)M_PI * (1.0f - mData.cosOpeningAngle);
        if (solidAngle <= 0.0f) {
            logWarning("LEDLight::updateIntensityFromPower - Invalid solid angle");
            return;
        }

        // Calculate intensity from power
        float unitIntensity = mData.totalPower / (surfaceArea * solidAngle * angleFactor);
        mData.intensity = float3(unitIntensity);

        mCalculationError = false;
    } catch (const std::exception&) {
        mCalculationError = true;
        logError("LEDLight::updateIntensityFromPower - Failed to calculate intensity");
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

    if (mCalculationError)
    {
        widget.textLine("WARNING: Calculation errors detected!");
    }
}
