#pragma once
#include "Light.h"

namespace Falcor
{
class FALCOR_API LEDLight : public Light
{
public:
    enum class LEDShape
    {
        Sphere = 0,
        Ellipsoid = 1,
        Rectangle = 2
    };

    static ref<LEDLight> create(const std::string& name = "") { return make_ref<LEDLight>(name); }

    LEDLight(const std::string& name);
    ~LEDLight() = default;

    // Basic light interface
    void renderUI(Gui::Widgets& widget) override;
    float getPower() const override;
    void setIntensity(const float3& intensity) override;
    void updateFromAnimation(const float4x4& transform) override;

    // LED specific interface
    void setLEDShape(LEDShape shape);
    LEDShape getLEDShape() const { return mLEDShape; }

    void setLambertExponent(float n);
    float getLambertExponent() const { return mData.lambertN; }

    void setTotalPower(float power);
    float getTotalPower() const { return mData.totalPower; }

    // Geometry and transformation
    void setScaling(float3 scale);
    float3 getScaling() const { return mScaling; }
    void setTransformMatrix(const float4x4& mtx);
    float4x4 getTransformMatrix() const { return mTransformMatrix; }

    // Angle control features
    void setOpeningAngle(float openingAngle);
    float getOpeningAngle() const { return mData.openingAngle; }
    void setWorldDirection(const float3& dir);
    const float3& getWorldDirection() const { return mData.dirW; }

private:
    void updateGeometry();
    void updateIntensityFromPower();
    float calculateSurfaceArea() const;

    LEDShape mLEDShape = LEDShape::Sphere;
    float3 mScaling = float3(1.0f);
    float4x4 mTransformMatrix = float4x4::identity();

    // GPU data buffers
    ref<Buffer> mSpectrumBuffer;
    ref<Buffer> mLightFieldBuffer;

    // Error flag
    bool mCalculationError = false;
};
}
