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
    void setWorldPosition(const float3& pos);
    const float3& getWorldPosition() const { return mData.posW; }

    // Spectrum and light field distribution
    void loadSpectrumData(const std::vector<float2>& spectrumData);
    void loadLightFieldData(const std::vector<float2>& lightFieldData);
    bool hasCustomSpectrum() const { return mHasCustomSpectrum; }
    bool hasCustomLightField() const { return mHasCustomLightField; }
    void clearCustomData();

    // File loading helper methods
    void loadSpectrumFromFile(const std::string& filePath);
    void loadLightFieldFromFile(const std::string& filePath);

    // Data access methods (for scene renderer)
    const std::vector<float2>& getSpectrumData() const { return mSpectrumData; }
    const std::vector<float2>& getLightFieldData() const { return mLightFieldData; }

private:
    void update();
    void updateGeometry();
    void updateIntensityFromPower();
    float calculateSurfaceArea() const;

    LEDShape mLEDShape = LEDShape::Sphere;
    float3 mScaling = float3(1.0f);
    float4x4 mTransformMatrix = float4x4::identity();

    // GPU data buffers
    ref<Buffer> mSpectrumBuffer;
    ref<Buffer> mLightFieldBuffer;

    // Spectrum and light field data
    std::vector<float2> mSpectrumData;      // wavelength, intensity pairs
    std::vector<float2> mLightFieldData;    // angle, intensity pairs
    bool mHasCustomSpectrum = false;
    bool mHasCustomLightField = false;

    // Error flag
    bool mCalculationError = false;
};
}
