#pragma once
#include "Falcor.h"
#include "RenderGraph/RenderPass.h"
#include "RenderGraph/RenderPassHelpers.h"
#include <memory>

using namespace Falcor;

/** Incoming Light Power calculation render pass.
    This pass calculates the power of light rays entering the camera, based on:
    1. Ray direction and radiance from the path tracer
    2. The specified wavelength range (only rays with wavelengths in this range are considered)
    3. The camera's geometric parameters

    The pass outputs the power of each qualifying light ray and its wavelength.
*/
class IncomingLightPowerPass : public RenderPass
{
public:
    FALCOR_PLUGIN_CLASS(IncomingLightPowerPass, "IncomingLightPowerPass", "Render pass for computing the power of light rays entering the camera.");

    static ref<IncomingLightPowerPass> create(ref<Device> pDevice, const Properties& props) { return make_ref<IncomingLightPowerPass>(pDevice, props); }

    IncomingLightPowerPass(ref<Device> pDevice, const Properties& props);

    virtual Properties getProperties() const override;
    virtual RenderPassReflection reflect(const CompileData& compileData) override;
    virtual void execute(RenderContext* pRenderContext, const RenderData& renderData) override;
    virtual void renderUI(Gui::Widgets& widget) override;
    virtual void setScene(RenderContext* pRenderContext, const ref<Scene>& pScene) override;

    // Wavelength filtering modes
    enum class FilterMode
    {
        Range,          ///< Filter wavelengths within a specified range
        SpecificBands,  ///< Filter specific wavelength bands
        Custom          ///< Custom filter function
    };

    // Scripting functions
    float getMinWavelength() const { return mMinWavelength; }
    void setMinWavelength(float minWavelength) { mMinWavelength = minWavelength; mNeedRecompile = true; }

    float getMaxWavelength() const { return mMaxWavelength; }
    void setMaxWavelength(float maxWavelength) { mMaxWavelength = maxWavelength; mNeedRecompile = true; }

    FilterMode getFilterMode() const { return mFilterMode; }
    void setFilterMode(FilterMode mode) { mFilterMode = mode; mNeedRecompile = true; }

    bool getUseVisibleSpectrumOnly() const { return mUseVisibleSpectrumOnly; }
    void setUseVisibleSpectrumOnly(bool value) { mUseVisibleSpectrumOnly = value; mNeedRecompile = true; }

    bool getInvertFilter() const { return mInvertFilter; }
    void setInvertFilter(bool value) { mInvertFilter = value; mNeedRecompile = true; }

    /** Camera Incident Power Calculator
        Utility class for calculating the power of light rays entering the camera.
        This implements the core calculation functionality for the pass.
    */
    class CameraIncidentPower
    {
    public:
        CameraIncidentPower() = default;

        /** Initialize the calculator with scene and camera information.
            @param[in] pScene Pointer to the scene.
            @param[in] dimensions Frame dimensions (width, height).
        */
        void setup(const ref<Scene>& pScene, const uint2& dimensions);

        /** Compute the effective pixel area on the camera sensor.
            @return The effective area of a pixel in world units.
        */
        float computePixelArea() const;

        /** Compute ray direction for a given pixel.
            @param[in] pixel The pixel coordinates.
            @return The ray direction in world space.
        */
        float3 computeRayDirection(const uint2& pixel) const;

        /** Compute the cosine term (angle between ray and camera normal).
            @param[in] rayDir The ray direction.
            @return The cosine value.
        */
        float computeCosTheta(const float3& rayDir) const;

        /** Compute the power of incoming light for the given parameters.
            @param[in] pixel The pixel coordinates.
            @param[in] rayDir The ray direction.
            @param[in] radiance The radiance value.
            @param[in] wavelength The wavelength of the ray.
            @param[in] minWavelength The minimum wavelength to consider.
            @param[in] maxWavelength The maximum wavelength to consider.
            @param[in] filterMode The wavelength filtering mode.
            @param[in] useVisibleSpectrumOnly Whether to use visible spectrum only.
            @param[in] invertFilter Whether to invert the filter.
            @param[in] bandWavelengths Array of specific wavelength bands to filter.
            @param[in] bandTolerances Array of tolerances for each band.
            @return The calculated power (rgb) and wavelength (a).
        */
        float4 compute(
            const uint2& pixel,
            const float3& rayDir,
            const float4& radiance,
            float wavelength,
            float minWavelength,
            float maxWavelength,
            FilterMode filterMode = FilterMode::Range,
            bool useVisibleSpectrumOnly = false,
            bool invertFilter = false,
            const std::vector<float>& bandWavelengths = {},
            const std::vector<float>& bandTolerances = {}
        ) const;

        /** Check if a wavelength passes the wavelength filter.
            @param[in] wavelength The wavelength to check.
            @param[in] minWavelength The minimum wavelength to consider.
            @param[in] maxWavelength The maximum wavelength to consider.
            @param[in] filterMode The wavelength filtering mode.
            @param[in] useVisibleSpectrumOnly Whether to use visible spectrum only.
            @param[in] invertFilter Whether to invert the filter.
            @param[in] bandWavelengths Array of specific wavelength bands to filter.
            @param[in] bandTolerances Array of tolerances for each band.
            @return True if the wavelength passes the filter, false otherwise.
        */
        bool isWavelengthAllowed(
            float wavelength,
            float minWavelength,
            float maxWavelength,
            FilterMode filterMode = FilterMode::Range,
            bool useVisibleSpectrumOnly = false,
            bool invertFilter = false,
            const std::vector<float>& bandWavelengths = {},
            const std::vector<float>& bandTolerances = {}
        ) const;

    private:
        ref<Scene> mpScene;                  ///< Scene reference
        ref<Camera> mpCamera;                ///< Camera reference
        uint2 mFrameDimensions = {0, 0};     ///< Frame dimensions
        float mPixelArea = 0.0f;             ///< Cached pixel area value
        bool mHasValidCamera = false;        ///< Flag indicating if a valid camera is available
        float3 mCameraNormal = {0.0f, 0.0f, 1.0f}; ///< Camera normal direction (forward)
    };

private:
    // Internal state
    ref<ComputePass> mpComputePass;      ///< Compute pass for power calculation
    ref<Scene> mpScene;                  ///< Scene reference for camera parameters
    bool mNeedRecompile = false;         ///< Flag indicating if shader program needs to be recompiled
    uint2 mFrameDim = {0, 0};            ///< Current frame dimensions
    CameraIncidentPower mPowerCalculator; ///< Power calculator instance

    // Wavelength filtering parameters
    float mMinWavelength = 380.0f;       ///< Minimum wavelength in nanometers
    float mMaxWavelength = 780.0f;       ///< Maximum wavelength in nanometers
    FilterMode mFilterMode = FilterMode::Range; ///< Wavelength filtering mode
    bool mUseVisibleSpectrumOnly = true; ///< Whether to only consider visible light spectrum (380-780nm)
    bool mInvertFilter = false;          ///< Whether to invert the wavelength filter
    std::vector<float> mBandWavelengths; ///< Specific wavelength bands to filter
    std::vector<float> mBandTolerances;  ///< Tolerances for specific wavelength bands
    static constexpr float kDefaultTolerance = 5.0f; ///< Default tolerance for specific bands in nm

    // UI variables
    bool mEnabled = true;                ///< Enable/disable the pass
    std::string mOutputPowerTexName = "lightPower";     ///< Name of the output power texture
    std::string mOutputWavelengthTexName = "lightWavelength"; ///< Name of the output wavelength texture

    void prepareResources(RenderContext* pRenderContext, const RenderData& renderData);
    void prepareProgram();

    // Helper method to update filter-specific defines
    void updateFilterDefines(DefineList& defines);
};
