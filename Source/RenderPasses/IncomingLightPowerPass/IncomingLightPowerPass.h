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

    // Output format enum
    enum class OutputFormat
    {
        PNG,            ///< PNG format for image data
        EXR,            ///< EXR format for HDR image data
        CSV,            ///< CSV format for tabular data
        JSON            ///< JSON format for structured data
    };

    // Statistics struct to hold power calculation results
    struct PowerStatistics
    {
        float totalPower[3] = { 0.0f, 0.0f, 0.0f }; ///< Total power (RGB)
        float peakPower[3] = { 0.0f, 0.0f, 0.0f };  ///< Maximum power value (RGB)
        float averagePower[3] = { 0.0f, 0.0f, 0.0f }; ///< Average power (RGB)
        uint32_t pixelCount = 0;                     ///< Number of pixels passing the wavelength filter
        uint32_t totalPixels = 0;                    ///< Total number of pixels processed
        std::map<int, uint32_t> wavelengthDistribution; ///< Histogram of wavelengths (binned by 10nm)
    };

    // Scripting functions
    float getMinWavelength() const { return mMinWavelength; }
    void setMinWavelength(float minWavelength) { mMinWavelength = minWavelength; mNeedRecompile = true; }

    float getMaxWavelength() const { return mMaxWavelength; }
    void setMaxWavelength(float maxWavelength) { mMaxWavelength = maxWavelength; mNeedRecompile = true; }

    float getPixelAreaScale() const { return mPixelAreaScale; }
    void setPixelAreaScale(float scale) { mPixelAreaScale = scale; mNeedRecompile = true; }

    FilterMode getFilterMode() const { return mFilterMode; }
    void setFilterMode(FilterMode mode) { mFilterMode = mode; mNeedRecompile = true; }

    bool getUseVisibleSpectrumOnly() const { return mUseVisibleSpectrumOnly; }
    void setUseVisibleSpectrumOnly(bool value) { mUseVisibleSpectrumOnly = value; mNeedRecompile = true; }

    bool getInvertFilter() const { return mInvertFilter; }
    void setInvertFilter(bool value) { mInvertFilter = value; mNeedRecompile = true; }

    bool getEnableWavelengthFilter() const { return mEnableWavelengthFilter; }
    void setEnableWavelengthFilter(bool enable) { mEnableWavelengthFilter = enable; mNeedRecompile = true; }

    // Photodetector analysis functions
    bool getEnablePhotodetectorAnalysis() const { return mEnablePhotodetectorAnalysis; }
    void setEnablePhotodetectorAnalysis(bool enable) { mEnablePhotodetectorAnalysis = enable; mNeedRecompile = true; }

    float getDetectorArea() const { return mDetectorArea; }
    void setDetectorArea(float area) { mDetectorArea = area; }

    float getSourceSolidAngle() const { return mSourceSolidAngle; }
    void setSourceSolidAngle(float angle) { mSourceSolidAngle = angle; }

    const std::string& getPowerDataExportPath() const { return mPowerDataExportPath; }
    void setPowerDataExportPath(const std::string& path) { mPowerDataExportPath = path; }
    
    uint32_t getMaxDataPoints() const { return mMaxDataPoints; }
    void setMaxDataPoints(uint32_t maxPoints) { mMaxDataPoints = maxPoints; }
    
    uint32_t getCurrentDataPointCount() const { return static_cast<uint32_t>(mPowerDataPoints.size()); }

    float getTotalAccumulatedPower() const { return mTotalAccumulatedPower; }

    // New export functions
    bool exportPowerData(const std::string& filename, OutputFormat format = OutputFormat::EXR);
    bool exportStatistics(const std::string& filename, OutputFormat format = OutputFormat::CSV);

    // Statistics accessor
    const PowerStatistics& getPowerStatistics() const { return mPowerStats; }

    /** Camera Incident Power Calculator
        Utility class for calculating the power of light rays entering the camera.
        This implements the core calculation functionality for the pass.
    */
    class CameraIncidentPower
    {
    public:
        CameraIncidentPower() = default;

        // Initialize with scene and dimensions
        void setup(const ref<Scene>& pScene, const uint2& dimensions);

        // Compute methods
        float computePixelArea() const;
        float3 computeRayDirection(const uint2& pixel) const;
        float computeCosTheta(const float3& rayDir) const;

        // Main compute method
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
            const std::vector<float>& bandTolerances = {},
            bool enableFilter = true
        ) const;

        // Wavelength filtering method
        bool isWavelengthAllowed(
            float wavelength,
            float minWavelength,
            float maxWavelength,
            FilterMode filterMode = FilterMode::Range,
            bool useVisibleSpectrumOnly = false,
            bool invertFilter = false,
            const std::vector<float>& bandWavelengths = {},
            const std::vector<float>& bandTolerances = {},
            bool enableFilter = true
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
    bool mEnableWavelengthFilter = true; ///< Whether to enable wavelength filtering at all
    std::vector<float> mBandWavelengths; ///< Specific wavelength bands to filter
    std::vector<float> mBandTolerances;  ///< Tolerances for specific wavelength bands
    static constexpr float kDefaultTolerance = 5.0f; ///< Default tolerance for specific bands in nm
    float mPixelAreaScale = 1.0f;     ///< Scale factor for area calculation (no pixel division)

    // UI variables
    bool mEnabled = true;                ///< Enable/disable the pass
    std::string mOutputPowerTexName = "lightPower";     ///< Name of the output power texture
    std::string mOutputWavelengthTexName = "lightWavelength"; ///< Name of the output wavelength texture

    // Debug related members
    bool mDebugMode = false;             ///< Enable/disable debug mode with detailed logging
    uint32_t mDebugLogFrequency = 60;    ///< How often to log debug info (in frames)
    uint32_t mFrameCount = 0;            ///< Frame counter for logging frequency control
    bool mEnableProfiling = false;       ///< Enable performance profiling
    float mLastExecutionTime = 0.0f;     ///< Last recorded execution time in ms

    // Internal constants
    static const std::string kInputRadiance;         ///< Input radiance texture name
    static const std::string kInputRayDirection;     ///< Input ray direction texture name
    static const std::string kInputWavelength;       ///< Input wavelength texture name
    static const std::string kInputSampleCount;      ///< Input sample count texture name
    static const std::string kOutputPower;           ///< Output power texture name
    static const std::string kOutputWavelength;      ///< Output wavelength texture name
    static const std::string kPerFrameCB;            ///< Per-frame constant buffer name

    // Shader parameter names
    static const std::string kMinWavelength;         ///< Min wavelength parameter name
    static const std::string kMaxWavelength;         ///< Max wavelength parameter name
    static const std::string kUseVisibleSpectrumOnly;///< Use visible spectrum only parameter name
    static const std::string kInvertFilter;          ///< Invert filter parameter name
    static const std::string kFilterMode;            ///< Filter mode parameter name
    static const std::string kBandCount;             ///< Band count parameter name
    static const std::string kPixelAreaScale;        ///< Pixel area scale factor parameter name
    static const std::string kOutputDebug;           ///< Debug output texture name
    static const std::string kDebugInputData;        ///< Debug input data texture name
    static const std::string kDebugCalculation;      ///< Debug calculation texture name

    // Camera parameters
    static const std::string kCameraInvViewProj;     ///< Camera inverse view-projection matrix name
    static const std::string kCameraPosition;        ///< Camera position parameter name
    static const std::string kCameraTarget;          ///< Camera target parameter name
    static const std::string kCameraFocalLength;     ///< Camera focal length parameter name

    // Photodetector analysis parameters
    bool mEnablePhotodetectorAnalysis = false;    ///< Enable PD power matrix analysis
    float mDetectorArea = 1e-6f;                  ///< PD effective area in m²
    float mSourceSolidAngle = 1e-3f;              ///< Source solid angle in sr
    uint32_t mCurrentNumRays = 0;                 ///< Current number of rays

    // Simplified data storage - direct angle-wavelength-power triplets
    struct PowerDataPoint
    {
        float incidentAngle;  ///< Incident angle in degrees
        float wavelength;     ///< Wavelength in nanometers
        float power;          ///< Power in watts
    };
    
    std::vector<PowerDataPoint> mPowerDataPoints; ///< Direct storage of power data points
    float mTotalAccumulatedPower = 0.0f;          ///< Total accumulated power
    std::string mPowerDataExportPath = "./";      ///< Export path
    uint32_t mMaxDataPoints = 1000000;            ///< Maximum number of data points to store

    // Statistics and export-related members
    PowerStatistics mPowerStats;              ///< Statistics about the calculated power
    bool mEnableStatistics = true;            ///< Whether to calculate statistics
    uint32_t mStatisticsFrequency = 1;        ///< How often to calculate statistics (in frames, 1 = every frame)
    bool mNeedStatsUpdate = true;             ///< Flag indicating statistics need to be updated
    bool mAccumulatePower = false;            ///< Whether to accumulate power over frames
    uint32_t mAccumulatedFrames = 0;          ///< Number of accumulated frames
    bool mAutoClearStats = true;              ///< Whether to auto-clear stats when filter changes
    std::string mExportDirectory = "./";      ///< Directory for exporting data
    OutputFormat mExportFormat = OutputFormat::EXR; ///< Default export format

    // CPU buffers for data readback
    std::vector<float4> mPowerReadbackBuffer;  ///< Buffer for power readback
    std::vector<float> mWavelengthReadbackBuffer; ///< Buffer for wavelength readback

    // Photodetector analysis buffers
    ref<Buffer> mpPowerDataBuffer;        ///< GPU buffer for power data
    ref<Buffer> mpPowerDataStagingBuffer; ///< ReadBack staging buffer for CPU access

    void prepareResources(RenderContext* pRenderContext, const RenderData& renderData);
    void prepareProgram();

    // Helper method to update filter-specific defines
    void updateFilterDefines(DefineList& defines);

    // New methods
    void calculateStatistics(RenderContext* pRenderContext, const RenderData& renderData);
    bool readbackData(RenderContext* pRenderContext, const RenderData& renderData);
    void renderStatisticsUI(Gui::Widgets& widget);
    void renderExportUI(Gui::Widgets& widget);
    std::string getFormattedStatistics() const;
    void resetStatistics();

    // Batch export state
    bool mBatchExportActive = false;
    uint32_t mBatchExportFrameCount = 0;
    uint32_t mBatchExportFramesToWait = 20;
    uint32_t mBatchExportCurrentViewpoint = 0;
    uint32_t mOriginalViewpoint = 0;
    std::string mBatchExportBaseDirectory;
    OutputFormat mBatchExportFormat;

    // Use loaded viewpoints or generate positions
    bool mUseLoadedViewpoints = true;

    // Camera position-based viewpoints (fallback if no viewpoints loaded)
    float3 mOriginalCameraPosition;
    float3 mOriginalCameraTarget;
    float3 mOriginalCameraUp;
    uint32_t mTotalViewpoints = 8;

    // Batch export helper functions
    void startBatchExport();
    void processBatchExport();
    void finishBatchExport();
    void setViewpointPosition(uint32_t viewpointIndex);

    // Photodetector matrix management functions
    void initializePowerData();
    void resetPowerData();
    bool exportPowerData();
    void accumulatePowerData(RenderContext* pRenderContext);
};
