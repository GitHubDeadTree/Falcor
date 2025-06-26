#pragma once
#include "Light.h"
#include "Utils/Color/Spectrum.h"

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
    Light::Changes beginFrame() override;

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

    // Spectrum sampling interface (Task 1)
    void setSpectrum(const std::vector<float2>& spectrumData);
    size_t getSpectrumSampleCount() const;
    float2 getSpectrumRange() const;
    const std::vector<float>& getSpectrumCDF() const { return mSpectrumCDF; }

    // Internal methods for scene renderer
    void setLightFieldDataOffset(uint32_t offset)
    {
        mData.lightFieldDataOffset = offset;
    }

    // Override getData() for consistency
    const LightData& getData() const override;

private:
    void update();
    void updateGeometry();
    void updateIntensityFromPower();
    void updateTransformData();
    float calculateSurfaceArea() const;
    std::vector<float2> normalizeLightFieldData(const std::vector<float2>& rawData) const;

    // Spectrum processing functions (Task 1)
    void buildSpectrumCDF();
    float sampleWavelengthFromSpectrum(float u) const;

    LEDShape mLEDShape = LEDShape::Sphere;
    float3 mScaling = float3(1.0f);
    float4x4 mTransformMatrix = float4x4::identity();

    // Geometry cache to avoid unnecessary recalculation
    bool mGeometryDirty = true;
    float4x4 mPrevTransformMatrix = float4x4::identity();
    float3 mPrevScaling = float3(1.0f);
    LEDShape mPrevLEDShape = LEDShape::Sphere;

    // Spectrum and light field data
    std::vector<float2> mSpectrumData;      // wavelength, intensity pairs
    std::vector<float2> mLightFieldData;    // angle, intensity pairs
    bool mHasCustomSpectrum = false;
    bool mHasCustomLightField = false;

    // Spectrum sampling support (Task 1)
    std::vector<float> mSpectrumCDF;
};
}
