#include "IncomingLightPowerPass.h"
#include "Utils/Math/FalcorMath.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <chrono>

namespace
{
    const std::string kShaderFile = "RenderPasses/IncomingLightPowerPass/IncomingLightPowerPass.cs.slang";

    // Input/output channels
    const std::string kInputRadiance = "radiance";               // Input texture with radiance values (from path tracer)
    const std::string kInputRayDirection = "rayDirection";       // Input texture with ray direction (if available)
    const std::string kInputWavelength = "wavelength";           // Input texture with wavelength information
    const std::string kInputSampleCount = "sampleCount";         // Input sample count from path tracer (optional)
    const std::string kOutputPower = "lightPower";               // Output texture with light power values
    const std::string kOutputWavelength = "lightWavelength";     // Output texture with filtered wavelength values
    const std::string kOutputDebug = "debugOutput";                // Output texture for debug information

    // Additional debug outputs
    const std::string kDebugInputData = "debugInputData";
    const std::string kDebugCalculation = "debugCalculation";

    // Shader constants
    const std::string kPerFrameCB = "PerFrameCB";           // cbuffer name
    const std::string kMinWavelength = "gMinWavelength";    // Minimum wavelength to consider
    const std::string kMaxWavelength = "gMaxWavelength";    // Maximum wavelength to consider
    const std::string kUseVisibleSpectrumOnly = "gUseVisibleSpectrumOnly"; // Whether to use visible spectrum only
    const std::string kInvertFilter = "gInvertFilter";      // Whether to invert the filter
    const std::string kFilterMode = "gFilterMode";          // Wavelength filtering mode
    const std::string kBandCount = "gBandCount";            // Number of specific bands to filter
    const std::string kPixelAreaScale = "gPixelAreaScale";  // Scale factor for pixel area

    // Helper function to get current time in microseconds
    uint64_t getTimeInMicroseconds()
    {
        auto now = std::chrono::high_resolution_clock::now();
        auto duration = now.time_since_epoch();
        return std::chrono::duration_cast<std::chrono::microseconds>(duration).count();
    }
}

// Define constants
const std::string IncomingLightPowerPass::kInputRadiance = "radiance";
const std::string IncomingLightPowerPass::kInputRayDirection = "rayDirection";
const std::string IncomingLightPowerPass::kInputWavelength = "wavelength";
const std::string IncomingLightPowerPass::kInputSampleCount = "sampleCount";
const std::string IncomingLightPowerPass::kOutputPower = "lightPower";
const std::string IncomingLightPowerPass::kOutputWavelength = "lightWavelength";
const std::string IncomingLightPowerPass::kOutputDebug = "debugOutput";
const std::string IncomingLightPowerPass::kDebugInputData = "debugInputData";
const std::string IncomingLightPowerPass::kDebugCalculation = "debugCalculation";
const std::string IncomingLightPowerPass::kPerFrameCB = "PerFrameCB";

// Shader parameter names
const std::string IncomingLightPowerPass::kMinWavelength = "gMinWavelength";
const std::string IncomingLightPowerPass::kMaxWavelength = "gMaxWavelength";
const std::string IncomingLightPowerPass::kUseVisibleSpectrumOnly = "gUseVisibleSpectrumOnly";
const std::string IncomingLightPowerPass::kInvertFilter = "gInvertFilter";
const std::string IncomingLightPowerPass::kFilterMode = "gFilterMode";
const std::string IncomingLightPowerPass::kBandCount = "gBandCount";
const std::string IncomingLightPowerPass::kPixelAreaScale = "gPixelAreaScale";

// Camera parameter names
const std::string IncomingLightPowerPass::kCameraInvViewProj = "gCameraInvViewProj";
const std::string IncomingLightPowerPass::kCameraPosition = "gCameraPosition";
const std::string IncomingLightPowerPass::kCameraTarget = "gCameraTarget";
const std::string IncomingLightPowerPass::kCameraFocalLength = "gCameraFocalLength";
const std::string IncomingLightPowerPass::kCameraFovY = "gCameraFovY";

// Implementation of CameraIncidentPower methods
void IncomingLightPowerPass::CameraIncidentPower::setup(const ref<Scene>& pScene, const uint2& dimensions)
{
    mpScene = pScene;
    mFrameDimensions = dimensions;
    mHasValidCamera = false;

    if (mpScene)
    {
        mpCamera = mpScene->getCamera();
        if (mpCamera)
        {
            mHasValidCamera = true;
            mCameraNormal = normalize(mpCamera->getTarget() - mpCamera->getPosition());

            // Pre-compute pixel area
            mPixelArea = computePixelArea();
        }
    }
}

float IncomingLightPowerPass::CameraIncidentPower::computePixelArea() const
{
    if (!mHasValidCamera || !mpCamera)
        return 1.0f;  // Default value if no camera

    // Calculate the total sensor area (photosensitive plane physical area)
    // instead of individual pixel area for proper power measurement

    // Get focal length in mm
    float focalLength = mpCamera->getFocalLength();
    float frameHeight = mpCamera->getFrameHeight();

    // Calculate horizontal FOV in radians using focal length and frame height
    // Formula: fovY = 2 * atan(frameHeight / (2 * focalLength))
    float hFOV = 2.0f * std::atan(frameHeight / (2.0f * focalLength));

    // Camera position and distance to image plane
    // We use 1.0 as a normalized distance to image plane
    float distToImagePlane = 1.0f;

    // Calculate total sensor dimensions (entire photosensitive plane)
    float sensorWidth = 2.0f * distToImagePlane * std::tan(hFOV * 0.5f);
    float aspectRatio = (float)mFrameDimensions.x / mFrameDimensions.y;
    float sensorHeight = sensorWidth / aspectRatio;

    // Calculate total sensor area (photosensitive plane physical area)
    float totalSensorArea = sensorWidth * sensorHeight;

    // Ensure total sensor area is not too small or zero
    const float minSensorArea = 0.001f;
    totalSensorArea = std::max(totalSensorArea, minSensorArea);

    return totalSensorArea;
}

float3 IncomingLightPowerPass::CameraIncidentPower::computeRayDirection(const uint2& pixel) const
{
    if (!mHasValidCamera || !mpCamera)
    {
        // Default implementation if no camera is available
        float2 uv = (float2(pixel) + 0.5f) / float2(mFrameDimensions);
        float2 ndc = float2(2.f, -2.f) * uv + float2(-1.f, 1.f);
        return normalize(float3(ndc, 1.f));
    }

    // Use camera ray generation
    const float2 pixelCenter = float2(pixel) + 0.5f;
    const float2 ndc = pixelCenter / float2(mFrameDimensions) * 2.f - 1.f;

    // Get view matrix and position
    const float4x4 invViewProj = mpCamera->getInvViewProjMatrix();
    const float3 cameraPos = mpCamera->getPosition();

    // Generate ray direction
    float4 worldPos = mul(invViewProj, float4(ndc.x, -ndc.y, 1.f, 1.f));
    worldPos /= worldPos.w;

    return normalize(float3(worldPos.x, worldPos.y, worldPos.z) - cameraPos);
}

float IncomingLightPowerPass::CameraIncidentPower::computeCosTheta(const float3& rayDir) const
{
    // Calculate the cosine of the angle between the ray direction and camera normal
    // For rays entering the camera, we need the angle with the inverted camera normal
    float3 invNormal = -mCameraNormal;

    // Calculate cosine using dot product
    float cosTheta = std::max(0.f, dot(rayDir, invNormal));

    // Ensure cosine is not too small or zero
    const float minCosTheta = 0.00001f;
    cosTheta = std::max(cosTheta, minCosTheta);

    return cosTheta;
}

bool IncomingLightPowerPass::CameraIncidentPower::isWavelengthAllowed(
    float wavelength,
    float minWavelength,
    float maxWavelength,
    IncomingLightPowerPass::FilterMode filterMode,
    bool useVisibleSpectrumOnly,
    bool invertFilter,
    const std::vector<float>& bandWavelengths,
    const std::vector<float>& bandTolerances,
    bool enableFilter) const
{
    // If wavelength filtering is disabled, all wavelengths are allowed
    if (!enableFilter)
    {
        return true;
    }

    // Apply visible spectrum filter if enabled
    if (useVisibleSpectrumOnly && (wavelength < 380.0f || wavelength > 780.0f))
    {
        return invertFilter; // If invert is true, return true for wavelengths outside visible spectrum
    }

    bool allowed = false;

    switch (filterMode)
    {
    case IncomingLightPowerPass::FilterMode::Range:
        // Range mode - check if wavelength is within min-max range
        allowed = (wavelength >= minWavelength && wavelength <= maxWavelength);
        break;

    case IncomingLightPowerPass::FilterMode::SpecificBands:
        // Specific bands mode - check if wavelength matches any of the specified bands
        if (bandWavelengths.empty())
        {
            // If no bands specified, fall back to range mode
            allowed = (wavelength >= minWavelength && wavelength <= maxWavelength);
        }
        else
        {
            // Check each band
            for (size_t i = 0; i < bandWavelengths.size(); i++)
            {
                float bandCenter = bandWavelengths[i];
                float tolerance = (i < bandTolerances.size()) ? bandTolerances[i] : 5.0f;

                if (std::abs(wavelength - bandCenter) <= tolerance)
                {
                    allowed = true;
                    break;
                }
            }
        }
        break;

    case IncomingLightPowerPass::FilterMode::Custom:
        // Custom mode - not implemented yet, fall back to range mode
        allowed = (wavelength >= minWavelength && wavelength <= maxWavelength);
        break;
    }

    // Apply filter inversion if enabled
    return invertFilter ? !allowed : allowed;
}

float4 IncomingLightPowerPass::CameraIncidentPower::compute(
    const uint2& pixel,
    const float3& rayDir,
    const float4& radiance,
    float wavelength,
    float minWavelength,
    float maxWavelength,
    IncomingLightPowerPass::FilterMode filterMode,
    bool useVisibleSpectrumOnly,
    bool invertFilter,
    const std::vector<float>& bandWavelengths,
    const std::vector<float>& bandTolerances,
    bool enableFilter) const
{
    // Apply wavelength filtering if explicitly enabled
    if (enableFilter && !isWavelengthAllowed(wavelength, minWavelength, maxWavelength,
        filterMode, useVisibleSpectrumOnly, invertFilter,
        bandWavelengths, bandTolerances, enableFilter))
    {
        return float4(0.f, 0.f, 0.f, 0.f);
    }

    // Calculate area directly without dividing by pixel count
    float area = computePixelArea();

    // Calculate cosine term
    float cosTheta = computeCosTheta(rayDir);

    // Calculate power using the formula: Power = Radiance * Area * cos(θ)
    // Use the full area directly without dividing by pixel count
    float3 power = float3(radiance.r, radiance.g, radiance.b) * area * cosTheta;

    // Return power with the wavelength
    return float4(power.x, power.y, power.z, wavelength > 0.0f ? wavelength : 550.0f);
}

extern "C" FALCOR_API_EXPORT void registerPlugin(Falcor::PluginRegistry& registry)
{
    registry.registerClass<RenderPass, IncomingLightPowerPass>();
}

IncomingLightPowerPass::IncomingLightPowerPass(ref<Device> pDevice, const Properties& props) : RenderPass(pDevice)
{
    // Parse properties
    for (const auto& [key, value] : props)
    {
        if (key == "enabled") mEnabled = value;
        else if (key == "minWavelength") mMinWavelength = value;
        else if (key == "maxWavelength") mMaxWavelength = value;
        else if (key == "filterMode") mFilterMode = static_cast<FilterMode>((uint32_t)value);
        else if (key == "useVisibleSpectrumOnly") mUseVisibleSpectrumOnly = value;
        else if (key == "invertFilter") mInvertFilter = value;
        else if (key == "enableWavelengthFilter") mEnableWavelengthFilter = value;
        else if (key == "statisticsFrequency") mStatisticsFrequency = value;
        else if (key == "outputPowerTexName") mOutputPowerTexName = value.operator std::string();
        else if (key == "outputWavelengthTexName") mOutputWavelengthTexName = value.operator std::string();
        else if (key == "enablePhotodetectorAnalysis") mEnablePhotodetectorAnalysis = value;
        else if (key == "detectorArea") mDetectorArea = value;
        else if (key == "sourceSolidAngle") mSourceSolidAngle = value;
        else if (key == "maxDataPoints") mMaxDataPoints = value;
        else if (key == "powerDataExportPath") mPowerDataExportPath = value.operator std::string();
        else logWarning("Unknown property '{}' in IncomingLightPowerPass properties.", key);
    }

    // Add default wavelength bands for common light sources
    mBandWavelengths = { 405.0f, 436.0f, 546.0f, 578.0f }; // Mercury lamp wavelengths
    mBandTolerances = { 5.0f, 5.0f, 5.0f, 5.0f };

    // Initialize power data storage for photodetector analysis
    if (mEnablePhotodetectorAnalysis)
    {
        initializePowerData();
    }

    // Create compute pass
    prepareProgram();
}

Properties IncomingLightPowerPass::getProperties() const
{
    Properties props;
    props["enabled"] = mEnabled;
    props["minWavelength"] = mMinWavelength;
    props["maxWavelength"] = mMaxWavelength;
    props["filterMode"] = static_cast<uint32_t>(mFilterMode);
    props["useVisibleSpectrumOnly"] = mUseVisibleSpectrumOnly;
    props["invertFilter"] = mInvertFilter;
    props["enableWavelengthFilter"] = mEnableWavelengthFilter;
    props["statisticsFrequency"] = mStatisticsFrequency;
    props["outputPowerTexName"] = mOutputPowerTexName;
    props["outputWavelengthTexName"] = mOutputWavelengthTexName;
    props["enablePhotodetectorAnalysis"] = mEnablePhotodetectorAnalysis;
    props["detectorArea"] = mDetectorArea;
    props["sourceSolidAngle"] = mSourceSolidAngle;
    props["maxDataPoints"] = mMaxDataPoints;
    props["powerDataExportPath"] = mPowerDataExportPath;
    return props;
}

RenderPassReflection IncomingLightPowerPass::reflect(const CompileData& compileData)
{
    RenderPassReflection reflector;

    // Input: Radiance from path tracer
    reflector.addInput(kInputRadiance, "Radiance values from path tracer")
        .bindFlags(ResourceBindFlags::ShaderResource);

    // Input: Ray direction (optional, can compute from pixel position if not available)
    reflector.addInput(kInputRayDirection, "Ray direction vectors")
        .bindFlags(ResourceBindFlags::ShaderResource)
        .flags(RenderPassReflection::Field::Flags::Optional);

    // Input: Wavelength information (optional)
    reflector.addInput(kInputWavelength, "Wavelength information for rays")
        .bindFlags(ResourceBindFlags::ShaderResource)
        .flags(RenderPassReflection::Field::Flags::Optional);

    // Input: Sample count information (optional, for handling multi-sample data)
    reflector.addInput(kInputSampleCount, "Sample count per pixel")
        .bindFlags(ResourceBindFlags::ShaderResource)
        .flags(RenderPassReflection::Field::Flags::Optional);

    // Output: Light power
    reflector.addOutput(kOutputPower, "Calculated light power per pixel")
        .bindFlags(ResourceBindFlags::UnorderedAccess)
        .format(ResourceFormat::RGBA32Float);

    // Output: Filtered wavelength (for rays that pass the wavelength filter)
    reflector.addOutput(kOutputWavelength, "Wavelengths of filtered rays")
        .bindFlags(ResourceBindFlags::UnorderedAccess)
        .format(ResourceFormat::R32Float);

    // Output: Debug information
    reflector.addOutput(kOutputDebug, "Debug information for original calculation")
        .bindFlags(ResourceBindFlags::UnorderedAccess)
        .format(ResourceFormat::RGBA32Float);

    // Additional debug outputs
    reflector.addOutput(kDebugInputData, "Debug information for input data")
        .bindFlags(ResourceBindFlags::UnorderedAccess)
        .format(ResourceFormat::RGBA32Float);

    reflector.addOutput(kDebugCalculation, "Debug information for calculation steps")
        .bindFlags(ResourceBindFlags::UnorderedAccess)
        .format(ResourceFormat::RGBA32Float);

    return reflector;
}

void IncomingLightPowerPass::setScene(RenderContext* pRenderContext, const ref<Scene>& pScene)
{
    mpScene = pScene;
    mNeedRecompile = true;
}

void IncomingLightPowerPass::execute(RenderContext* pRenderContext, const RenderData& renderData)
{
    // Increment frame counter
    mFrameCount++;
    bool shouldLogThisFrame = mDebugMode && (mFrameCount % mDebugLogFrequency == 0);

    // Print debug info - current settings (only in debug mode)
    if (shouldLogThisFrame)
    {
        logInfo(fmt::format("IncomingLightPowerPass executing - Frame: {0}, settings: enabled={1}, wavelength_filter_enabled={2}, filter_mode={3}, min_wavelength={4}, max_wavelength={5}",
                          mFrameCount, mEnabled, mEnableWavelengthFilter, static_cast<int>(mFilterMode), mMinWavelength, mMaxWavelength));
    }

    // Get input texture
    const auto& pInputRadiance = renderData.getTexture(kInputRadiance);
    if (!pInputRadiance)
    {
        logWarning("IncomingLightPowerPass::execute() - Input radiance texture is missing.");
        return;
    }

    // Get output textures
    const auto& pOutputPower = renderData.getTexture(kOutputPower);
    const auto& pOutputWavelength = renderData.getTexture(kOutputWavelength);
    const auto& pDebugOutput = renderData.getTexture(kOutputDebug);
    const auto& pDebugInputData = renderData.getTexture(kDebugInputData);
    const auto& pDebugCalculation = renderData.getTexture(kDebugCalculation);
    if (!pOutputPower || !pOutputWavelength)
    {
        logWarning("IncomingLightPowerPass::execute() - Output texture is missing.");
        return;
    }

    // Create debug output texture
    auto pDebugOutputTexture = renderData[kOutputDebug]->asTexture();

    // If disabled, use default non-zero value instead of clearing to zero
    if (!mEnabled)
    {
        // For floating-point textures, we use a non-zero value even when pass is disabled
        // This ensures the output is still visible for debugging
        // Use white with moderate power value
        pRenderContext->clearUAV(pOutputPower->getUAV().get(), float4(5.0f, 5.0f, 5.0f, 550.0f));

        // For R32Float, use a single float value for wavelength (middle of visible spectrum)
        float defaultWavelength = 550.0f;
        pRenderContext->clearUAV(pOutputWavelength->getUAV().get(), uint4(0, 0, 0, 0)); // Can't pass float directly

        // Log that we're using forced values (only in debug mode and at intervals)
        if (shouldLogThisFrame)
        {
            logInfo("IncomingLightPowerPass disabled but using forced non-zero values for debugging");
        }
        return;
    }

    // Update frame dimensions
    mFrameDim = uint2(pInputRadiance->getWidth(), pInputRadiance->getHeight());

    // Check if program needs to be recompiled
    if (mNeedRecompile)
    {
        prepareProgram();
        mNeedRecompile = false;

        // When filter settings change, optionally reset statistics
        if (mAutoClearStats)
        {
            resetStatistics();
        }
    }

    // Prepare resources
    prepareResources(pRenderContext, renderData);

    // Track performance if profiling is enabled
    uint64_t startTime = 0;
    if (mEnableProfiling)
    {
        startTime = getTimeInMicroseconds();
    }

    // Get shader variables
    auto var = mpComputePass->getRootVar();

    // Set constants
    var[kPerFrameCB][kMinWavelength] = mMinWavelength;
    var[kPerFrameCB][kMaxWavelength] = mMaxWavelength;
    var[kPerFrameCB][kUseVisibleSpectrumOnly] = mUseVisibleSpectrumOnly;
    var[kPerFrameCB][kInvertFilter] = mInvertFilter;
    var[kPerFrameCB][kFilterMode] = (uint32_t)mFilterMode;
    var[kPerFrameCB]["gEnableWavelengthFilter"] = mEnableWavelengthFilter;
    var[kPerFrameCB][kPixelAreaScale] = mPixelAreaScale;

    // Set photodetector analysis parameters
    mCurrentNumRays = mFrameDim.x * mFrameDim.y; // Calculate current number of rays
    var[kPerFrameCB]["gEnablePhotodetectorAnalysis"] = mEnablePhotodetectorAnalysis;
    var[kPerFrameCB]["gDetectorArea"] = mDetectorArea;
    var[kPerFrameCB]["gSourceSolidAngle"] = mSourceSolidAngle;
    var[kPerFrameCB]["gCurrentNumRays"] = mCurrentNumRays;

    // Set camera data
    if (mpScene && mpScene->getCamera())
    {
        auto pCamera = mpScene->getCamera();
        var[kPerFrameCB][kCameraInvViewProj] = pCamera->getInvViewProjMatrix();
        var[kPerFrameCB][kCameraPosition] = pCamera->getPosition();
        var[kPerFrameCB][kCameraTarget] = pCamera->getTarget();
        var[kPerFrameCB][kCameraFocalLength] = pCamera->getFocalLength();
        // Calculate FOV Y from focal length and frame height using Falcor's math utilities
        float focalLength = pCamera->getFocalLength(); // in mm
        float frameHeight = pCamera->getFrameHeight(); // in mm
        float fovY = focalLengthToFovY(focalLength, frameHeight); // in radians
        var[kPerFrameCB][kCameraFovY] = fovY; // Add FoV Y parameter for task 1
    }
    else
    {
        // Default values if no camera is available
        var[kPerFrameCB][kCameraInvViewProj] = float4x4::identity();
        var[kPerFrameCB][kCameraPosition] = float3(0.0f, 0.0f, 0.0f);
        var[kPerFrameCB][kCameraTarget] = float3(0.0f, 0.0f, -1.0f);
        var[kPerFrameCB][kCameraFocalLength] = 21.0f; // Default focal length
        var[kPerFrameCB][kCameraFovY] = 1.0f; // Default FoV Y in radians (approximately 57.3 degrees)
    }

    // Set band data if available
    if (!mBandWavelengths.empty() && mFilterMode == FilterMode::SpecificBands)
    {
        var[kPerFrameCB][kBandCount] = (uint32_t)mBandWavelengths.size();

        // Transfer band data to GPU buffers
        size_t bandCount = std::min(mBandWavelengths.size(), size_t(16)); // Limit to 16 bands max
        for (size_t i = 0; i < bandCount; i++)
        {
            float bandCenter = mBandWavelengths[i];
            float tolerance = (i < mBandTolerances.size()) ? mBandTolerances[i] : kDefaultTolerance;

            var[kPerFrameCB]["gBandWavelengths"][i] = bandCenter;
            var[kPerFrameCB]["gBandTolerances"][i] = tolerance;
        }
    }
    else
    {
        var[kPerFrameCB][kBandCount] = 0;
    }

    // Bind input resources
    var["gInputRadiance"] = pInputRadiance;

    // Bind optional ray direction (if available)
    const auto& pRayDirection = renderData.getTexture(kInputRayDirection);
    if (pRayDirection)
    {
        var["gInputRayDirection"] = pRayDirection;
    }

    // Bind optional wavelength texture (if available)
    const auto& pWavelength = renderData.getTexture(kInputWavelength);
    if (pWavelength)
    {
        var["gInputWavelength"] = pWavelength;
    }

    // Check for sample count texture for multi-sample handling
    const auto& pSampleCount = renderData.getTexture(kInputSampleCount);
    if (pSampleCount && shouldLogThisFrame)
    {
        // Log that multi-sample data is detected (only in debug mode and at intervals)
        logInfo("IncomingLightPowerPass: Multi-sample data detected");
    }

    // Bind output resources
    var["gOutputPower"] = pOutputPower;
    var["gOutputWavelength"] = pOutputWavelength;
    var["gDebugOutput"] = pDebugOutput;
    var["gDebugInputData"] = pDebugInputData;
    var["gDebugCalculation"] = pDebugCalculation;

    // Bind classification buffer for photodetector analysis
            if (mEnablePhotodetectorAnalysis && mpPowerDataBuffer)
        {
            var["gPowerDataBuffer"] = mpPowerDataBuffer;
    }

    // Execute the compute pass
    mpComputePass->execute(pRenderContext, uint3(mFrameDim.x, mFrameDim.y, 1));

    // Track performance if profiling is enabled
    if (mEnableProfiling && startTime > 0)
    {
        uint64_t endTime = getTimeInMicroseconds();
        mLastExecutionTime = (endTime - startTime) / 1000.0f; // Convert to milliseconds

        if (shouldLogThisFrame)
        {
            logInfo(fmt::format("Shader execution time: {0:.2f} ms", mLastExecutionTime));
        }
    }

    // Read debug information for first 4 pixels (only in debug mode and at specified intervals)
    if (mDebugMode && shouldLogThisFrame && pDebugOutput && pDebugInputData && pDebugCalculation)
    {
        pRenderContext->submit(true);  // Make sure compute pass is complete

        // Read debug data
        std::vector<uint8_t> debugData = pRenderContext->readTextureSubresource(pDebugOutput.get(), 0);
        std::vector<uint8_t> inputData = pRenderContext->readTextureSubresource(pDebugInputData.get(), 0);
        std::vector<uint8_t> calcData = pRenderContext->readTextureSubresource(pDebugCalculation.get(), 0);

        if (!debugData.empty() && !inputData.empty() && !calcData.empty())
        {
            const float4* debugValues = reinterpret_cast<const float4*>(debugData.data());
            const float4* inputValues = reinterpret_cast<const float4*>(inputData.data());
            const float4* calcValues = reinterpret_cast<const float4*>(calcData.data());

            // Log debug data for first 4 pixels (2x2 grid) - only at specified intervals
            logInfo("DEBUG - SLANG SHADER OUTPUT - Raw Debug Values:");

            for (uint32_t y = 0; y < 2; y++)
            {
                for (uint32_t x = 0; x < 2; x++)
                {
                    uint32_t pixelIndex = y * pDebugOutput->getWidth() + x;

                    if (pixelIndex < debugData.size() / sizeof(float4) &&
                        pixelIndex < inputData.size() / sizeof(float4) &&
                        pixelIndex < calcData.size() / sizeof(float4))
                    {
                        const float4& debugPixelData = debugValues[pixelIndex];
                        const float4& inputPixelData = inputValues[pixelIndex];
                        const float4& calcPixelData = calcValues[pixelIndex];

                        // First pixel has special values directly from the shader
                        if (x == 0 && y == 0) {

                            logInfo(fmt::format("DEBUG - Pixel [0,0] - SHADER POWER CALCULATION:"));
                            // Get camera normal and wavelength from debug output
                            float3 cameraNormal = float3(debugPixelData.x, debugPixelData.y, debugPixelData.z);
                            float wavelength = debugPixelData.w;

                            // Get computation details from calculation texture
                            float dotProduct = calcPixelData.x;
                            float rawCosTheta = calcPixelData.y;
                            float finalCosTheta = calcPixelData.z;
                            float pixelArea = calcPixelData.w;

                            // For pixel [0,0], the calculation texture also contains the calculation components
                            float radiance = calcPixelData.x;
                            float pixelArea2 = calcPixelData.y;
                            float cosTheta2 = calcPixelData.z;
                            float power = calcPixelData.w;

                            // Log original values
                            logInfo(fmt::format("  - CAMERA NORMAL: ({0:.8f}, {1:.8f}, {2:.8f})",
                                cameraNormal.x, cameraNormal.y, cameraNormal.z));
                            logInfo(fmt::format("  - DOT PRODUCT: {0:.8f}", dotProduct));
                            logInfo(fmt::format("  - RAW COSTHETA: {0:.8f}", rawCosTheta));
                            logInfo(fmt::format("  - FINAL COSTHETA: {0:.8f}", finalCosTheta));
                            logInfo(fmt::format("  - PIXEL AREA: {0:.8f}", pixelArea));
                            logInfo(fmt::format("  - RADIANCE: {0:.8f}", radiance));
                            logInfo(fmt::format("  - POWER: {0:.8f}", power));

                            // Check if pixel area values match
                            if (std::abs(pixelArea - pixelArea2) > 0.00001f) {
                                logInfo(fmt::format("  - PIXEL AREA MISMATCH: First calc={0:.8f}, Second calc={1:.8f}",
                                    pixelArea, pixelArea2));
                            }

                            // Check if cosTheta values match
                            if (std::abs(finalCosTheta - cosTheta2) > 0.00001f) {
                                logInfo(fmt::format("  - COSTHETA MISMATCH: First calc={0:.8f}, Second calc={1:.8f}",
                                    finalCosTheta, cosTheta2));
                            }

                            // Check if power calculation is correct
                            float expectedPower = radiance * pixelArea * finalCosTheta;
                            float powerDiff = std::abs(expectedPower - power);
                            logInfo(fmt::format("  - POWER CALCULATION CHECK: {0:.8f} * {1:.8f} * {2:.8f} = {3:.8f} (Expected: {4:.8f}, Diff: {5:.8f})",
                                radiance, pixelArea, finalCosTheta, power, expectedPower, powerDiff));
                        }

                        // Special debug data for pixel [1,1] (contains power calculation for pixel [0,0])
                        if (x == 1 && y == 1) {
                            // Get power calculation values
                            float rPower = inputPixelData.x;
                            float gPower = inputPixelData.y;
                            float bPower = inputPixelData.z;
                            float wavelength = inputPixelData.w;

                            logInfo(fmt::format("DEBUG - SPECIAL POWER CALCULATION FOR PIXEL [0,0]:"));
                            logInfo(fmt::format("  - RGB POWER: ({0:.8f}, {1:.8f}, {2:.8f})",
                                rPower, gPower, bPower));
                            logInfo(fmt::format("  - WAVELENGTH: {0:.2f}", wavelength));
                        }

                        // Log raw debug data from shader
                        logInfo(fmt::format("DEBUG - Pixel [{0},{1}] - FROM SHADER:", x, y));

                        // Ray direction (from input data texture)
                        logInfo(fmt::format("  - RAY DIR: ({0:.8f}, {1:.8f}, {2:.8f}), Length={3:.8f}",
                            inputPixelData.x, inputPixelData.y, inputPixelData.z, inputPixelData.w));

                        // Camera normal (from debug output texture)
                        logInfo(fmt::format("  - CAMERA NORMAL: ({0:.8f}, {1:.8f}, {2:.8f}), Wavelength={3:.2f}",
                            debugPixelData.x, debugPixelData.y, debugPixelData.z, debugPixelData.w));

                        // CosTheta calculation (from calculation texture)
                        logInfo(fmt::format("  - COSTHETA CALC: DotProduct={0:.8f}, RawCosTheta={1:.8f}, FinalCosTheta={2:.8f}, PixelArea={3:.8f}",
                            calcPixelData.x, calcPixelData.y, calcPixelData.z, calcPixelData.w));

                        // Ensure we also have the power texture
                        if (pOutputPower)
                        {
                            std::vector<uint8_t> powerData = pRenderContext->readTextureSubresource(pOutputPower.get(), 0);
                            if (!powerData.empty())
                            {
                                const float4* powerValues = reinterpret_cast<const float4*>(powerData.data());
                                if (pixelIndex < powerData.size() / sizeof(float4))
                                {
                                    const float4& pixelPower = powerValues[pixelIndex];

                                    // Show the final power calculated from the shader
                                    logInfo(fmt::format("  - FINAL POWER: ({0:.8f}, {1:.8f}, {2:.8f}), Wavelength={3:.2f}",
                                        pixelPower.x, pixelPower.y, pixelPower.z, pixelPower.w));
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    // Check if first pixel value verification is needed (only in debug mode and at intervals)
    if (mDebugMode && shouldLogThisFrame)
    {
        // Create a buffer to read data back from GPU
        std::vector<uint8_t> pixelData;

        // Submit previous commands to ensure compute shader has completed
        pRenderContext->submit(true);

        // Read the first pixel data
        pixelData = pRenderContext->readTextureSubresource(pOutputPower.get(), 0);

        // Get the first pixel value from the buffer
        float4 firstPixelValue = float4(0.0f);
        if (pixelData.size() >= sizeof(float4)) {
            firstPixelValue = *reinterpret_cast<const float4*>(pixelData.data());
        }

        // Print the first pixel value
        logInfo(fmt::format("DEBUG - First pixel value: R={0:.6f}, G={1:.6f}, B={2:.6f}, W={3:.2f}",
                          firstPixelValue.x, firstPixelValue.y, firstPixelValue.z, firstPixelValue.w));

        // Check the value to verify shader execution
        if (firstPixelValue.x > 49.0f && firstPixelValue.x < 51.0f)
        {
            logInfo("Debug pixel value successfully set to 50! Shader computation is working properly!");
        }
        else if (firstPixelValue.x <= 0.001f)
        {
            logWarning("Warning: First pixel value is close to zero! There might be an issue!");
        }
        else
        {
            logInfo(fmt::format("First pixel power value is {0:.2f}, which is in the normal range", firstPixelValue.x));
        }
    }

    // Calculate statistics if enabled - frequency controlled execution
    if (mEnableStatistics && (mFrameCount % mStatisticsFrequency == 0))
    {
        calculateStatistics(pRenderContext, renderData);
    }

    // Accumulate photodetector power data if enabled
    if (mEnablePhotodetectorAnalysis)
    {
        pRenderContext->submit(true); // Ensure GPU work is complete before reading back
        accumulatePowerData(pRenderContext);
    }

    processBatchExport();
}

void IncomingLightPowerPass::renderUI(Gui::Widgets& widget)
{
    bool changed = false;
    changed |= widget.checkbox("Enabled", mEnabled);

    // Add debugging controls
    auto debugGroup = widget.group("Debug Options", true);
    if (debugGroup)
    {
        widget.checkbox("Debug Mode", mDebugMode);
        if (mDebugMode)
        {
            widget.slider("Log Frequency (frames)", mDebugLogFrequency, 1u, 300u);
            widget.text(fmt::format("Current frame: {0}", mFrameCount));
            widget.checkbox("Performance Profiling", mEnableProfiling);
            if (mEnableProfiling && mLastExecutionTime > 0)
            {
                widget.text(fmt::format("Last execution time: {0:.2f} ms", mLastExecutionTime));
            }
        }
    }

    // Add area scale control
    auto areaGroup = widget.group("Area Scale Control", true);
    if (areaGroup)
    {
        changed |= widget.slider("Area Scale Factor", mPixelAreaScale, 1.0f, 10000.0f);
        widget.tooltip("Scales the area used for power calculation.\nArea is used directly without dividing by pixel count.\nFor PD mode: scales gDetectorArea. For camera mode: scales computed sensor area.");
    }

    auto filterGroup = widget.group("Wavelength Filter", true);
    if (filterGroup)
    {
        // Enable/disable wavelength filtering
        bool filterChanged = widget.checkbox("Enable Wavelength Filtering", mEnableWavelengthFilter);
        if (filterChanged)
        {
            changed = true;
            if (filterChanged && mAutoClearStats)
            {
                resetStatistics();
            }
        }

        // Only show filter options if filtering is enabled
        if (mEnableWavelengthFilter)
        {
            // Filter mode selection
            Gui::DropdownList filterModeList;
            filterModeList.push_back({ 0, "Range" });
            filterModeList.push_back({ 1, "Specific Bands" });
            filterModeList.push_back({ 2, "Custom" });

            uint32_t currentMode = static_cast<uint32_t>(mFilterMode);
            if (widget.dropdown("Filter Mode", filterModeList, currentMode))
            {
                mFilterMode = static_cast<FilterMode>(currentMode);
                changed = true;
            }

            // Range filter options
            if (mFilterMode == FilterMode::Range)
            {
                changed |= widget.slider("Min Wavelength (nm)", mMinWavelength, 100.f, 1500.f);
                changed |= widget.slider("Max Wavelength (nm)", mMaxWavelength, mMinWavelength, 1500.f);
            }
            // Specific bands filter options
            else if (mFilterMode == FilterMode::SpecificBands)
            {
                // Band presets dropdown
                Gui::DropdownList presetsList;
                presetsList.push_back({ 0, "Custom" });
                presetsList.push_back({ 1, "Mercury Lamp" });
                presetsList.push_back({ 2, "Hydrogen Lines" });
                presetsList.push_back({ 3, "Sodium D-lines" });

                static uint32_t selectedPreset = 0;
                if (widget.dropdown("Presets", presetsList, selectedPreset))
                {
                    changed = true;
                    switch (selectedPreset)
                    {
                    case 1: // Mercury Lamp
                        mBandWavelengths = { 405.0f, 436.0f, 546.0f, 578.0f };
                        mBandTolerances = { 5.0f, 5.0f, 5.0f, 5.0f };
                        break;
                    case 2: // Hydrogen
                        mBandWavelengths = { 434.0f, 486.0f, 656.0f };
                        mBandTolerances = { 5.0f, 5.0f, 5.0f };
                        break;
                    case 3: // Sodium
                        mBandWavelengths = { 589.0f, 589.6f };
                        mBandTolerances = { 2.0f, 2.0f };
                        break;
                    default:
                        break;
                    }
                }

                // Display bands and allow editing
                widget.text("Bands (nm):");

                // Add UI for each band
                std::vector<float> bandWavelengthsCopy = mBandWavelengths;
                std::vector<float> bandTolerancesCopy = mBandTolerances;
                std::vector<bool> bandToRemove(mBandWavelengths.size(), false);

                bool bandsChanged = false;

                for (size_t i = 0; i < bandWavelengthsCopy.size(); i++)
                {
                    // Ensure tolerance vector is large enough
                    if (i >= bandTolerancesCopy.size())
                        bandTolerancesCopy.push_back(kDefaultTolerance);

                    // Display band UI row
                    std::string id = "Band " + std::to_string(i+1);
                    auto bandGroup = widget.group(id);
                    if (bandGroup)
                    {
                        // Allow editing band center and tolerance
                        bandsChanged |= widget.slider("Center", bandWavelengthsCopy[i], 100.f, 1500.f);
                        bandsChanged |= widget.slider("±Range", bandTolerancesCopy[i], 1.f, 50.f);

                        // Remove button for this band
                        bandToRemove[i] = widget.button("Remove");
                    }
                }

                // Handle band removal
                for (int i = (int)bandToRemove.size() - 1; i >= 0; i--)
                {
                    if (bandToRemove[i])
                    {
                        bandWavelengthsCopy.erase(bandWavelengthsCopy.begin() + i);
                        if (i < (int)bandTolerancesCopy.size())
                            bandTolerancesCopy.erase(bandTolerancesCopy.begin() + i);
                        bandsChanged = true;
                    }
                }

                // Add new band button
                if (widget.button("Add Band"))
                {
                    bandWavelengthsCopy.push_back(550.0f); // Default to middle of visible spectrum
                    bandTolerancesCopy.push_back(kDefaultTolerance);
                    bandsChanged = true;
                }

                // Update bands if changed
                if (bandsChanged)
                {
                    mBandWavelengths = bandWavelengthsCopy;
                    mBandTolerances = bandTolerancesCopy;
                    changed = true;
                }
            }

            // Common filter options
            changed |= widget.checkbox("Visible Spectrum Only", mUseVisibleSpectrumOnly);
            if (mUseVisibleSpectrumOnly)
            {
                widget.text("Restricts to 380-780nm range");
            }

            changed |= widget.checkbox("Invert Filter", mInvertFilter);
            if (mInvertFilter)
            {
                widget.text("Selects wavelengths OUTSIDE the specified criteria");
            }
        }
        else
        {
            widget.text("All wavelengths will be passed through without filtering");
        }
    }

    // Statistics UI
    renderStatisticsUI(widget);

    // Photodetector Analysis UI - Enhanced for Task 4
    auto pdGroup = widget.group("Photodetector Analysis", true);
    if (pdGroup)
    {
        bool pdChanged = widget.checkbox("Enable Analysis", mEnablePhotodetectorAnalysis);
        if (pdChanged)
        {
            changed = true;
            if (mEnablePhotodetectorAnalysis)
            {
                initializePowerData();
                logInfo("Photodetector analysis enabled - data storage initialized");
            }
            else
            {
                logInfo("Photodetector analysis disabled");
            }
        }

        if (mEnablePhotodetectorAnalysis)
        {
            // Data storage information display
            const float dataSizeMB = (mPowerDataPoints.size() * sizeof(PowerDataPoint)) / (1024.0f * 1024.0f);
            widget.text(fmt::format("Data Points: {} / {} ({:.2f}MB)",
                                   mPowerDataPoints.size(), mMaxDataPoints, dataSizeMB));

            // Storage status indicator
            if (mPowerDataPoints.size() >= mMaxDataPoints)
            {
                widget.text("WARNING: Maximum data points reached", true);
            }
            else
            {
                widget.text("Status: Ready for data collection");
            }

            // Power status display with error detection
            if (mTotalAccumulatedPower == 0.666f)
            {
                widget.text("Status: ERROR - Check console for details", true);
                widget.text("Error Recovery: Try resetting data or restarting analysis");
            }
            else if (mTotalAccumulatedPower == 0.0f)
            {
                widget.text("Status: Waiting for power data...");
            }
            else
            {
                widget.text(fmt::format("Total Power: {:.6f} W", mTotalAccumulatedPower));

                // Calculate and display power density
                const float powerDensity = mTotalAccumulatedPower / mDetectorArea;
                widget.text(fmt::format("Power Density: {:.3e} W/m²", powerDensity));
            }

            // Enhanced physical parameters with validation
            auto paramsGroup = widget.group("Physical Parameters", false);
            if (paramsGroup)
            {
                float oldDetectorArea = mDetectorArea;
                changed |= widget.slider("Detector Area (m²)", mDetectorArea, 1e-9f, 1e-3f, true);
                if (mDetectorArea != oldDetectorArea)
                {
                    widget.tooltip("Physical effective area of the photodetector");
                    // Validate detector area is reasonable
                    if (mDetectorArea < 1e-8f)
                    {
                        widget.text("WARNING: Very small detector area may cause numerical issues", true);
                    }
                }

                float oldSolidAngle = mSourceSolidAngle;
                changed |= widget.slider("Source Solid Angle (sr)", mSourceSolidAngle, 1e-6f, 1e-1f, true);
                if (mSourceSolidAngle != oldSolidAngle)
                {
                    widget.tooltip("Solid angle subtended by the light source as seen from the detector");
                    // Validate solid angle is reasonable
                    if (mSourceSolidAngle > 6.28f) // > 2π steradians
                    {
                        widget.text("WARNING: Solid angle exceeds hemisphere (2π sr)", true);
                    }
                }

                widget.text(fmt::format("Current Ray Count: {}", mCurrentNumRays));

                // Display computed per-ray solid angle
                if (mCurrentNumRays > 0)
                {
                    const float deltaOmega = mSourceSolidAngle / float(mCurrentNumRays);
                    widget.text(fmt::format("Per-ray Δω: {:.3e} sr", deltaOmega));
                }
            }

            // Data collection settings
            auto settingsGroup = widget.group("Data Collection Settings", false);
            if (settingsGroup)
            {
                changed |= widget.slider("Max Data Points", mMaxDataPoints, 10000u, 2000000u);
                widget.tooltip("Maximum number of data points to store in memory");

                widget.text("Direct storage: angle-wavelength-power triplets");
                widget.text("No binning - full precision data retention");
            }

            // Data operations with better feedback
            auto operationsGroup = widget.group("Data Operations", true);
            if (operationsGroup)
            {
                if (widget.button("Reset Data"))
                {
                    resetPowerData();
                    if (mTotalAccumulatedPower != 0.666f)
                    {
                        widget.text("Data reset successful");
                    }
                }
                widget.tooltip("Clear all accumulated power data and reset counters");

                static bool exportInProgress = false;
                static std::string lastExportMessage = "";

                if (widget.button("Export Data"))
                {
                    exportInProgress = true;
                    bool success = exportPowerData();
                    if (success)
                    {
                        lastExportMessage = "Data exported successfully!";
                    }
                    else
                    {
                        lastExportMessage = "Export failed - check console for details";
                    }
                    exportInProgress = false;
                }
                widget.tooltip("Export power data as CSV file with angle,wavelength,power columns");

                // Display export status
                if (!lastExportMessage.empty())
                {
                    bool isError = lastExportMessage.find("failed") != std::string::npos;
                    widget.text(lastExportMessage, isError);
                }

                if (exportInProgress)
                {
                    widget.text("Exporting...");
                }

                // Export path setting with validation
                char pathBuffer[256];
                strncpy_s(pathBuffer, mPowerDataExportPath.c_str(), sizeof(pathBuffer) - 1);
                pathBuffer[sizeof(pathBuffer) - 1] = '\0';

                if (widget.textbox("Export Path", pathBuffer, sizeof(pathBuffer)))
                {
                    std::string newPath = std::string(pathBuffer);
                    // Basic path validation
                    if (newPath.empty())
                    {
                        newPath = "./";
                    }
                    if (newPath.back() != '/' && newPath.back() != '\\')
                    {
                        newPath += "/";
                    }
                    mPowerDataExportPath = newPath;
                }
                widget.tooltip("Directory path for data export (auto-adds trailing slash)");

                // Display current export path validation
                if (!std::filesystem::exists(mPowerDataExportPath))
                {
                    widget.text("⚠ Export path does not exist", true);
                }
            }
        }
        else
        {
            // Display helpful information when disabled
            widget.text("Enable analysis to access data collection features");
            widget.text("Direct storage: Saves angle-wavelength-power triplets without binning");
        }
    }

    // Export UI
    renderExportUI(widget);

    if (changed)
    {
        mNeedRecompile = true;
    }
}

void IncomingLightPowerPass::renderStatisticsUI(Gui::Widgets& widget)
{
    auto statsGroup = widget.group("Power Statistics", true);
    if (statsGroup)
    {
        // Enable/disable statistics
        bool statsChanged = widget.checkbox("Enable Statistics", mEnableStatistics);

        if (mEnableStatistics)
        {
            // Statistics frequency control
            statsChanged |= widget.slider("Statistics Frequency (frames)", mStatisticsFrequency, 1u, 60u);
            widget.tooltip("How often to calculate statistics. 1 = every frame, 60 = every 60 frames.\nHigher values improve performance but reduce update frequency.");

            // Display basic statistics
            if (mPowerStats.totalPixels > 0)
            {
                const float passRate = 100.0f * float(mPowerStats.pixelCount) / float(mPowerStats.totalPixels);

                // Use formatted string for better display
                widget.text(fmt::format("Filtered pixels: {0} / {1} ({2:.2f}%)",
                                       mPowerStats.pixelCount,
                                       mPowerStats.totalPixels,
                                       passRate));

                widget.text(fmt::format("Total Power (W): R={0:.6f}, G={1:.6f}, B={2:.6f}",
                                       mPowerStats.totalPower[0],
                                       mPowerStats.totalPower[1],
                                       mPowerStats.totalPower[2]));

                widget.text(fmt::format("Average Power (W): R={0:.6f}, G={1:.6f}, B={2:.6f}",
                                       mPowerStats.averagePower[0],
                                       mPowerStats.averagePower[1],
                                       mPowerStats.averagePower[2]));

                widget.text(fmt::format("Peak Power (W): R={0:.6f}, G={1:.6f}, B={2:.6f}",
                                       mPowerStats.peakPower[0],
                                       mPowerStats.peakPower[1],
                                       mPowerStats.peakPower[2]));

                // Add wavelength statistics summary
                if (!mPowerStats.wavelengthDistribution.empty())
                {
                    widget.text(fmt::format("Wavelength distribution: {0} distinct bands",
                                          mPowerStats.wavelengthDistribution.size()));

                    // Add expandable wavelength details
                    auto wlGroup = widget.group("Wavelength Details", false);
                    if (wlGroup)
                    {
                        int maxBandsToShow = 10; // Limit UI display to top 10 bands
                        int bandsShown = 0;

                        // Sort wavelength bins by count to show most common first
                        std::vector<std::pair<int, uint32_t>> sortedBins;
                        for (const auto& [wavelength, count] : mPowerStats.wavelengthDistribution)
                        {
                            sortedBins.push_back({wavelength, count});
                        }

                        std::sort(sortedBins.begin(), sortedBins.end(),
                                 [](const auto& a, const auto& b) { return a.second > b.second; });

                        for (const auto& [wavelength, count] : sortedBins)
                        {
                            if (bandsShown++ >= maxBandsToShow)
                            {
                                widget.text("... and more bands");
                                break;
                            }

                            widget.text(fmt::format("{0}-{1} nm: {2} pixels",
                                                  wavelength * 10,
                                                  wavelength * 10 + 10,
                                                  count));
                        }
                    }
                }
            }
            else
            {
                widget.text("No statistics available");
            }

            // Power accumulation options
            statsChanged |= widget.checkbox("Accumulate Power", mAccumulatePower);
            if (mAccumulatePower)
            {
                widget.text(fmt::format("Accumulated frames: {0}", mAccumulatedFrames));
            }

            // Manual reset button with better styling
            if (widget.button("Reset Statistics", true)) // Use primary button style
            {
                resetStatistics();
            }

            // Add force refresh button
            if (widget.button("Force Refresh Statistics"))
            {
                mNeedStatsUpdate = true;
            }

            // Add a button to output detailed camera and area information to log
            if (widget.button("Log Debug Info")) {
                logCameraAndAreaInfo();
            }

            statsChanged |= widget.checkbox("Auto-clear when filter changes", mAutoClearStats);
        }

        if (statsChanged)
        {
            mNeedStatsUpdate = true;
        }
    }
}

void IncomingLightPowerPass::renderExportUI(Gui::Widgets& widget)
{
    widget.text("Export Options");

    auto exportGroup = widget.group("Export Results", true);
    if (exportGroup)
    {
        // Export directory
        if (widget.textbox("Directory", mExportDirectory))
        {
            // Directory updated
        }

        // Export format selector
        Gui::DropdownList formatList;
        formatList.push_back({0, "PNG"});
        formatList.push_back({1, "EXR"});
        formatList.push_back({2, "CSV"});
        formatList.push_back({3, "JSON"});

        uint32_t currentFormat = static_cast<uint32_t>(mExportFormat);
        if (widget.dropdown("Export Format", formatList, currentFormat))
        {
            mExportFormat = static_cast<OutputFormat>(currentFormat);
        }

        // Checkboxes to select what to export
        static bool sExportPower = true;
        static bool sExportStats = true;
        widget.checkbox("Export Power Data", sExportPower);
        widget.checkbox("Export Statistics", sExportStats);

        // Export button
        if (widget.button("Export Selected Data"))
        {
            if (!sExportPower && !sExportStats)
            {
                logWarning("No data selected for export.");
            }
            else
            {
                const std::string timestamp = std::to_string(std::time(nullptr));
                bool powerSuccess = false;
                bool statsSuccess = false;

                if (sExportPower)
                {
                    const std::string ext = mExportFormat == OutputFormat::PNG   ? ".png"
                                            : mExportFormat == OutputFormat::EXR ? ".exr"
                                            : mExportFormat == OutputFormat::CSV ? ".csv"
                                                                                 : ".json";

                    const std::string filename = mExportDirectory + "/light_power_" + timestamp + ext;
                    if (exportPowerData(filename, mExportFormat))
                    {
                        powerSuccess = true;
                        logInfo("Power data exported successfully to " + filename);
                    }
                    else
                    {
                        logError("Failed to export power data.");
                    }
                }

                if (sExportStats)
                {
                    // Statistics only support CSV and JSON
                    OutputFormat statsFormat = (mExportFormat == OutputFormat::CSV) ? OutputFormat::CSV : OutputFormat::JSON;
                    const std::string ext = (statsFormat == OutputFormat::CSV) ? ".csv" : ".json";
                    const std::string filename = mExportDirectory + "/light_stats_" + timestamp + ext;

                    if (exportStatistics(filename, statsFormat))
                    {
                        statsSuccess = true;
                        logInfo("Statistics exported successfully to " + filename);
                    }
                    else
                    {
                        logError("Failed to export statistics.");
                    }
                }

                if (sExportPower && sExportStats)
                {
                    if (powerSuccess && statsSuccess)
                    {
                        logInfo("Successfully exported both power data and statistics.");
                    }
                    else if (powerSuccess)
                    {
                        logWarning("Power data exported, but statistics export failed.");
                    }
                    else if (statsSuccess)
                    {
                        logWarning("Statistics exported, but power data export failed.");
                    }
                    else
                    {
                        logError("Both power data and statistics exports failed.");
                    }
                }
            }
        }
    }

    widget.separator();
    widget.text("Batch Export");
    widget.tooltip("Export data for all viewpoints in the scene.");

    widget.var("Frames to wait", mBatchExportFramesToWait, 1u, 120u);
    widget.tooltip("Number of frames to wait for rendering to stabilize after switching viewpoints.");

    if (mpScene && mpScene->hasSavedViewpoints())
    {
        widget.text("Scene has loaded viewpoints");
        widget.tooltip("Batch export will use the scene's saved viewpoints.");
    }
    else
    {
        widget.text("No loaded viewpoints");
        widget.tooltip("Batch export will generate 8 viewpoints around the current camera.");
    }

    if (widget.button("Export All Viewpoints"))
    {
        if (mBatchExportActive)
        {
            logWarning("Batch export is already in progress.");
        }
        else
        {
            startBatchExport();
        }
    }
}

bool IncomingLightPowerPass::exportPowerData(const std::string& filename, OutputFormat format)
{
    try
    {
        // Create directories if needed
        std::filesystem::path filePath(filename);
        std::filesystem::create_directories(filePath.parent_path());

        // Handle different export formats
        if (format == OutputFormat::PNG || format == OutputFormat::EXR)
        {
            // Get the last rendered output
            if (mPowerReadbackBuffer.empty())
            {
                // We need a valid RenderContext and RenderData, which we don't have here
                // Just check if we already have data in the buffer
                logWarning("Failed to read back power data for export");
                return false;
            }

            // Create an image for export
            uint32_t width = mFrameDim.x;
            uint32_t height = mFrameDim.y;

            // Save as image using Falcor's image export
            Bitmap::FileFormat bitmapFormat = (format == OutputFormat::PNG) ?
                                            Bitmap::FileFormat::PngFile :
                                            Bitmap::FileFormat::ExrFile;

            // Save using Bitmap's static saveImage method
            Bitmap::saveImage(
                filename,
                width,
                height,
                bitmapFormat,
                Bitmap::ExportFlags::None,
                ResourceFormat::RGBA32Float,
                true, // top-down
                mPowerReadbackBuffer.data()
            );

            logInfo("Exported power data to " + filename);
            return true;
        }
        else if (format == OutputFormat::CSV)
        {
            // Export as CSV
            if (mPowerReadbackBuffer.empty())
            {
                logWarning("Failed to read back power data for export");
                return false;
            }

            std::ofstream csvFile(filename);
            if (!csvFile.is_open())
            {
                logWarning("Failed to open file for CSV export: " + filename);
                return false;
            }

            // Write CSV header
            csvFile << "pixel_x,pixel_y,power_r,power_g,power_b,wavelength\n";

            // Write pixel data
            for (uint32_t y = 0; y < mFrameDim.y; y++)
            {
                for (uint32_t x = 0; x < mFrameDim.x; x++)
                {
                    uint32_t index = y * mFrameDim.x + x;
                    const float4& power = mPowerReadbackBuffer[index];

                    // Only export non-zero power values (those that passed the filter)
                    if (power.x > 0 || power.y > 0 || power.z > 0)
                    {
                        csvFile << x << "," << y << ","
                                << power.x << "," << power.y << "," << power.z << ","
                                << power.w << "\n";
                    }
                }
            }

            csvFile.close();
            logInfo("Exported power data to " + filename);
            return true;
        }
        else if (format == OutputFormat::JSON)
        {
            // Export as JSON
            if (mPowerReadbackBuffer.empty())
            {
                logWarning("Failed to read back power data for export");
                return false;
            }

            std::ofstream jsonFile(filename);
            if (!jsonFile.is_open())
            {
                logWarning("Failed to open file for JSON export: " + filename);
                return false;
            }

            // Write JSON header
            jsonFile << "{\n";
            jsonFile << "  \"metadata\": {\n";
            jsonFile << "    \"width\": " << mFrameDim.x << ",\n";
            jsonFile << "    \"height\": " << mFrameDim.y << ",\n";
            jsonFile << "    \"minWavelength\": " << mMinWavelength << ",\n";
            jsonFile << "    \"maxWavelength\": " << mMaxWavelength << ",\n";
            jsonFile << "    \"filterMode\": " << static_cast<int>(mFilterMode) << "\n";
            jsonFile << "  },\n";

            // Write pixel data
            jsonFile << "  \"pixels\": [\n";

            bool firstEntry = true;
            for (uint32_t y = 0; y < mFrameDim.y; y++)
            {
                for (uint32_t x = 0; x < mFrameDim.x; x++)
                {
                    uint32_t index = y * mFrameDim.x + x;
                    const float4& power = mPowerReadbackBuffer[index];

                    // Only export non-zero power values (those that passed the filter)
                    if (power.x > 0 || power.y > 0 || power.z > 0)
                    {
                        if (!firstEntry)
                        {
                            jsonFile << ",\n";
                        }
                        firstEntry = false;

                        jsonFile << "    {\n";
                        jsonFile << "      \"x\": " << x << ",\n";
                        jsonFile << "      \"y\": " << y << ",\n";
                        jsonFile << "      \"power\": [" << power.x << ", " << power.y << ", " << power.z << "],\n";
                        jsonFile << "      \"wavelength\": " << power.w << "\n";
                        jsonFile << "    }";
                    }
                }
            }

            jsonFile << "\n  ]\n}\n";

            jsonFile.close();
            logInfo("Exported power data to " + filename);
            return true;
        }
    }
    catch (const std::exception& e)
    {
        logError("Error exporting power data: " + std::string(e.what()));
        return false;
    }

    return false;
}

bool IncomingLightPowerPass::exportStatistics(const std::string& filename, OutputFormat format)
{
    try
    {
        // Create directories if needed
        std::filesystem::path filePath(filename);
        std::filesystem::create_directories(filePath.parent_path());

        if (format == OutputFormat::CSV)
        {
            std::ofstream csvFile(filename);
            if (!csvFile.is_open())
            {
                logWarning("Failed to open file for CSV export: " + filename);
                return false;
            }

            // Write general statistics
            csvFile << "Statistic,Red,Green,Blue\n";
            csvFile << "Total Power," << mPowerStats.totalPower[0] << "," << mPowerStats.totalPower[1] << "," << mPowerStats.totalPower[2] << "\n";
            csvFile << "Average Power," << mPowerStats.averagePower[0] << "," << mPowerStats.averagePower[1] << "," << mPowerStats.averagePower[2] << "\n";
            csvFile << "Peak Power," << mPowerStats.peakPower[0] << "," << mPowerStats.peakPower[1] << "," << mPowerStats.peakPower[2] << "\n";
            csvFile << "\n";

            // Write pixel statistics
            csvFile << "Pixel Count," << mPowerStats.pixelCount << "\n";
            csvFile << "Total Pixels," << mPowerStats.totalPixels << "\n";
            csvFile << "Pass Rate (%)," << (100.0f * float(mPowerStats.pixelCount) / float(mPowerStats.totalPixels)) << "\n";
            csvFile << "\n";

            // Write wavelength distribution
            csvFile << "Wavelength Bin (nm),Count\n";
            for (const auto& [wavelength, count] : mPowerStats.wavelengthDistribution)
            {
                // Wavelength bins are 10nm wide, centered at the key value
                csvFile << (wavelength * 10) << "-" << (wavelength * 10 + 10) << "," << count << "\n";
            }

            csvFile.close();
            logInfo("Exported statistics to " + filename);
            return true;
        }
        else if (format == OutputFormat::JSON)
        {
            std::ofstream jsonFile(filename);
            if (!jsonFile.is_open())
            {
                logWarning("Failed to open file for JSON export: " + filename);
                return false;
            }

            // Write JSON data
            jsonFile << "{\n";

            // Write metadata
            jsonFile << "  \"metadata\": {\n";
            jsonFile << "    \"filterMode\": " << static_cast<int>(mFilterMode) << ",\n";
            jsonFile << "    \"minWavelength\": " << mMinWavelength << ",\n";
            jsonFile << "    \"maxWavelength\": " << mMaxWavelength << ",\n";
            jsonFile << "    \"useVisibleSpectrumOnly\": " << (mUseVisibleSpectrumOnly ? "true" : "false") << ",\n";
            jsonFile << "    \"invertFilter\": " << (mInvertFilter ? "true" : "false") << "\n";
            jsonFile << "  },\n";

            // Write power statistics
            jsonFile << "  \"powerStatistics\": {\n";
            jsonFile << "    \"totalPower\": [" << mPowerStats.totalPower[0] << ", " << mPowerStats.totalPower[1] << ", " << mPowerStats.totalPower[2] << "],\n";
            jsonFile << "    \"averagePower\": [" << mPowerStats.averagePower[0] << ", " << mPowerStats.averagePower[1] << ", " << mPowerStats.averagePower[2] << "],\n";
            jsonFile << "    \"peakPower\": [" << mPowerStats.peakPower[0] << ", " << mPowerStats.peakPower[1] << ", " << mPowerStats.peakPower[2] << "]\n";
            jsonFile << "  },\n";

            // Write pixel statistics
            jsonFile << "  \"pixelStatistics\": {\n";
            jsonFile << "    \"pixelCount\": " << mPowerStats.pixelCount << ",\n";
            jsonFile << "    \"totalPixels\": " << mPowerStats.totalPixels << ",\n";
            jsonFile << "    \"passRate\": " << (100.0f * float(mPowerStats.pixelCount) / float(mPowerStats.totalPixels)) << "\n";
            jsonFile << "  },\n";

            // Write wavelength distribution
            jsonFile << "  \"wavelengthDistribution\": {\n";

            bool firstEntry = true;
            for (const auto& [wavelength, count] : mPowerStats.wavelengthDistribution)
            {
                if (!firstEntry)
                {
                    jsonFile << ",\n";
                }
                firstEntry = false;

                jsonFile << "    \"" << (wavelength * 10) << "-" << (wavelength * 10 + 10) << "\": " << count;
            }

            jsonFile << "\n  }\n}\n";

            jsonFile.close();
            logInfo("Exported statistics to " + filename);
            return true;
        }
    }
    catch (const std::exception& e)
    {
        logError("Error exporting statistics: " + std::string(e.what()));
        return false;
    }

    return false;
}

// New helper function: Calculate single pixel area for physics-based calculation
float IncomingLightPowerPass::calculateSinglePixelArea() const
{
    if (!mpScene || !mpScene->getCamera() || mFrameDim.x == 0 || mFrameDim.y == 0)
    {
        logWarning("Cannot calculate pixel area: No scene, camera, or invalid dimensions");
        return 0.666e-8f; // Error identifier value
    }

    auto camera = mpScene->getCamera();
    // Calculate FOV Y from focal length and frame height using Falcor's math utilities
    float focalLength = camera->getFocalLength(); // in mm
    float frameHeight = camera->getFrameHeight(); // in mm
    float fovY = focalLengthToFovY(focalLength, frameHeight); // in radians

    float aspectRatio = float(mFrameDim.x) / float(mFrameDim.y);
    float distToImagePlane = 1.0f; // Standard normalized distance

    // Calculate total sensor dimensions
    float sensorHeight = 2.0f * distToImagePlane * std::tan(fovY * 0.5f);
    float sensorWidth = sensorHeight * aspectRatio;
    float totalSensorArea = sensorWidth * sensorHeight;

    // Calculate single pixel area
    float pixelArea = totalSensorArea / (float(mFrameDim.x) * float(mFrameDim.y));

    // Validate calculation result
    if (pixelArea <= 0.0f || !std::isfinite(pixelArea)) {
        logError("Invalid single pixel area calculation result");
        return 0.666e-8f; // Error identifier value
    }

    return pixelArea * mPixelAreaScale; // Apply scaling factor
}

// New helper function: Calculate total detector area for physics-based calculation
float IncomingLightPowerPass::calculateTotalDetectorArea() const
{
    return calculateSinglePixelArea() * float(mFrameDim.x * mFrameDim.y);
}

// New helper function: Log detailed camera and area information for debugging
void IncomingLightPowerPass::logCameraAndAreaInfo() const
{
    if (!mpScene || !mpScene->getCamera()) {
        logError("Cannot log debug info: No scene or camera available.");
        return;
    }

    auto camera = mpScene->getCamera();
    // Calculate FOV Y from focal length and frame height using Falcor's math utilities
    float focalLength = camera->getFocalLength(); // in mm
    float frameHeight = camera->getFrameHeight(); // in mm
    float fovY = focalLengthToFovY(focalLength, frameHeight); // in radians

    float aspectRatio = float(mFrameDim.x) / float(mFrameDim.y);
    float totalArea = calculateTotalDetectorArea();
    float pixelArea = calculateSinglePixelArea();

    logInfo("====== Power Calculation Debug Info ======");
    logInfo(fmt::format("FoV Y: {:.2f} rad ({:.2f} degrees)", fovY, fovY * 180.0f / 3.14159f));
    logInfo(fmt::format("Aspect Ratio: {:.2f}", aspectRatio));
    logInfo(fmt::format("Dimensions: {}x{}", mFrameDim.x, mFrameDim.y));
    logInfo(fmt::format("Pixel Area Scale: {:.6e}", mPixelAreaScale));
    logInfo(fmt::format("Calculated Total Sensor Area: {:.6e} m^2", totalArea));
    logInfo(fmt::format("Calculated Single Pixel Area: {:.6e} m^2", pixelArea));
    logInfo("========================================");
}

void IncomingLightPowerPass::calculateStatistics(RenderContext* pRenderContext, const RenderData& renderData)
{
    // Get start time for performance measurement
    uint64_t startTime = 0;
    bool shouldLogThisFrame = mDebugMode && (mFrameCount % mDebugLogFrequency == 0);
    if (shouldLogThisFrame) {
        startTime = getTimeInMicroseconds();
    }

    // Read back the data from GPU
    if (!readbackData(pRenderContext, renderData))
    {
        logError("Failed to read back data for statistics calculation");
        return;
    }

    // Log filter settings for debugging (only in debug mode and at intervals)
    if (shouldLogThisFrame)
    {
        logInfo(fmt::format("Calculating statistics with settings: wavelength_filter_enabled={0}, filter_mode={1}, min={2}, max={3}, invert={4}",
                          mEnableWavelengthFilter, static_cast<int>(mFilterMode), mMinWavelength, mMaxWavelength, mInvertFilter));
    }

    // Initialize statistics if needed
    if (!mAccumulatePower || mAccumulatedFrames == 0)
    {
        resetStatistics();
    }

    // Ensure buffer size is reasonable before processing
    if (mPowerReadbackBuffer.empty())
    {
        logError("Power readback buffer is empty, cannot calculate statistics");
        return;
    }

    // Add safety limit to prevent pixel count from growing indefinitely
    const uint32_t maxPixelCount = mFrameDim.x * mFrameDim.y * 10; // Allow up to 10 frames accumulation
    if (mAccumulatePower && mPowerStats.pixelCount >= maxPixelCount)
    {
        logWarning(fmt::format("Pixel count reached limit ({0}), resetting statistics", maxPixelCount));
        resetStatistics();
    }

    // DIRECT POWER ACCUMULATION: Simply sum up all valid pixel powers
    float3 totalPower = float3(0.0f);
    uint32_t validPixelCount = 0;

    // Direct accumulation of pixel power values
    for (uint32_t i = 0; i < mPowerReadbackBuffer.size(); i++)
    {
        const float4& pixelPowerData = mPowerReadbackBuffer[i];

        // Check if pixel power is valid
        if (pixelPowerData.x > 1e-12f || pixelPowerData.y > 1e-12f || pixelPowerData.z > 1e-12f)
        {
            // Check for shader error identifier values
            if (std::abs(pixelPowerData.x - 0.666f) < 1e-6f) {
                logWarning("Shader reported a calculation error for a pixel.");
                continue;
            }

            // Direct accumulation: P_total = Σ P_pixel
            totalPower += pixelPowerData.xyz();
            validPixelCount++;
        }
    }

    if (validPixelCount == 0)
    {
        logWarning("No valid pixels found for power calculation");
        mPowerStats.totalPower[0] = mPowerStats.totalPower[1] = mPowerStats.totalPower[2] = 0.666f;
        return;
    }

    // Update statistics with direct power accumulation results
    mPowerStats.totalPower[0] = totalPower.x;
    mPowerStats.totalPower[1] = totalPower.y;
    mPowerStats.totalPower[2] = totalPower.z;
    mPowerStats.pixelCount = validPixelCount;
    mPowerStats.totalPixels = mFrameDim.x * mFrameDim.y;

    // Calculate average power (per valid pixel equivalent)
    mPowerStats.averagePower[0] = totalPower.x / float(validPixelCount);
    mPowerStats.averagePower[1] = totalPower.y / float(validPixelCount);
    mPowerStats.averagePower[2] = totalPower.z / float(validPixelCount);

    // Track peak power from individual pixels for comparison
    float maxR = 0.0f, maxG = 0.0f, maxB = 0.0f;
    for (uint32_t i = 0; i < mPowerReadbackBuffer.size(); i++)
    {
        const float4& power = mPowerReadbackBuffer[i];
        maxR = std::max(maxR, power.x);
        maxG = std::max(maxG, power.y);
        maxB = std::max(maxB, power.z);

        // Track wavelength distribution (bin by 10nm intervals)
        if (power.w > 0.0f && power.w < 2000.0f) // Typical wavelength range: 0-2000nm
        {
            int wavelengthBin = static_cast<int>(power.w / 10.0f);
            mPowerStats.wavelengthDistribution[wavelengthBin]++;
        }
    }

    mPowerStats.peakPower[0] = maxR;
    mPowerStats.peakPower[1] = maxG;
    mPowerStats.peakPower[2] = maxB;

    // Validate final results
    if (totalPower.x < 0.0f || totalPower.y < 0.0f || totalPower.z < 0.0f)
    {
        logError("Calculated negative total power, using error marker");
        mPowerStats.totalPower[0] = mPowerStats.totalPower[1] = mPowerStats.totalPower[2] = 0.666f;
    }

    // Update accumulated frames count
    if (mAccumulatePower)
    {
        mAccumulatedFrames++;
    }

    // Debug output for direct power accumulation (only in debug mode and at intervals)
    if (shouldLogThisFrame)
    {
        float percentage = mPowerReadbackBuffer.size() > 0 ?
                        100.0f * validPixelCount / mPowerReadbackBuffer.size() : 0.0f;

        logInfo(fmt::format("Image sensor direct power accumulation:"));
        logInfo(fmt::format("  Valid pixels: {0} out of {1} ({2:.2f}%)",
                           validPixelCount, mPowerReadbackBuffer.size(), percentage));
        logInfo(fmt::format("  Calculation method: Direct pixel power summation"));
        logInfo(fmt::format("  Total power: [{:.6e}, {:.6e}, {:.6e}] W",
                           totalPower.x, totalPower.y, totalPower.z));
        logInfo(fmt::format("  Peak pixel power: [{:.6e}, {:.6e}, {:.6e}] W",
                           maxR, maxG, maxB));

        // Output wavelength distribution summary if available
        if (!mPowerStats.wavelengthDistribution.empty())
        {
            uint32_t totalBins = mPowerStats.wavelengthDistribution.size();
            uint32_t countedWavelengths = 0;

            for (const auto& [binIndex, count] : mPowerStats.wavelengthDistribution)
            {
                countedWavelengths += count;
            }

            logInfo(fmt::format("  Wavelength distribution: {0} distinct bands, {1} wavelengths counted",
                            totalBins, countedWavelengths));
        }

        // Log calculation time
        if (startTime > 0)
        {
            uint64_t endTime = getTimeInMicroseconds();
            float calculationTime = (endTime - startTime) / 1000.0f; // Convert to milliseconds
            logInfo(fmt::format("Direct power accumulation completed in {0:.2f} ms", calculationTime));
        }
    }

    // Stats are now up to date
    mNeedStatsUpdate = false;
}

bool IncomingLightPowerPass::readbackData(RenderContext* pRenderContext, const RenderData& renderData)
{
    // If pRenderContext is null, we're called from an export function with no context
    // In this case, use the already read back data if available
    if (!pRenderContext)
    {
        return !mPowerReadbackBuffer.empty();
    }

    // Create a flag to check if we need readback
    bool needReadback = mDebugMode || mNeedStatsUpdate || mAccumulatePower ||
                       (mEnableStatistics && (mFrameCount % mStatisticsFrequency == 0));
    bool shouldLogThisFrame = mDebugMode && (mFrameCount % mDebugLogFrequency == 0);

    // If no readback is needed, skip the expensive operation
    if (!needReadback)
    {
        if (shouldLogThisFrame) logInfo("Skipping texture readback as it's not requested");
        return false;
    }

    const auto& pOutputPower = renderData.getTexture(kOutputPower);
    const auto& pOutputWavelength = renderData.getTexture(kOutputWavelength);

    if (!pOutputPower || !pOutputWavelength)
    {
        logError("readbackData: Missing output textures");
        return false;
    }

    // Get dimensions
    uint32_t width = pOutputPower->getWidth();
    uint32_t height = pOutputPower->getHeight();
    uint32_t numPixels = width * height;

    // Only log dimensions in debug mode and at intervals
    if (shouldLogThisFrame)
    {
        logInfo(fmt::format("readbackData: Texture dimensions: {0}x{1}, total pixels: {2}",
                          width, height, numPixels));
        logInfo(fmt::format("Power texture format: {0}, Wavelength texture format: {1}",
                          to_string(pOutputPower->getFormat()),
                          to_string(pOutputWavelength->getFormat())));
    }

    try
    {
        // Get raw texture data using the correct readTextureSubresource call that returns a vector
        std::vector<uint8_t> powerRawData = pRenderContext->readTextureSubresource(pOutputPower.get(), 0);
        std::vector<uint8_t> wavelengthRawData = pRenderContext->readTextureSubresource(pOutputWavelength.get(), 0);

        // Wait for operations to complete
        pRenderContext->submit(true);

        // Check if we got valid data
        if (powerRawData.empty() || wavelengthRawData.empty())
        {
            logWarning("Failed to read texture data: empty raw data");
            return false;
        }

        // Log data size information for debugging (only in debug mode and at intervals)
        if (shouldLogThisFrame)
        {
            logInfo(fmt::format("readbackData: Power raw data size: {0} bytes, expected: {1} bytes (float4 per pixel)",
                             powerRawData.size(), numPixels * sizeof(float4)));
            logInfo(fmt::format("readbackData: Wavelength raw data size: {0} bytes, expected: {1} bytes (float per pixel)",
                             wavelengthRawData.size(), numPixels * sizeof(float)));
        }

        // Resize the destination buffers
        mPowerReadbackBuffer.resize(numPixels);
        mWavelengthReadbackBuffer.resize(numPixels);

        // Properly parse the RGBA32Float format for power data
        if (powerRawData.size() >= numPixels * sizeof(float4))
        {
            // Use proper type casting to interpret the raw bytes as float4 data
            const float4* floatData = reinterpret_cast<const float4*>(powerRawData.data());
            for (uint32_t i = 0; i < numPixels; i++)
            {
                mPowerReadbackBuffer[i] = floatData[i];
            }

            if (shouldLogThisFrame) logInfo("Successfully parsed power data");
        }
        else
        {
            logError(fmt::format("Power data size mismatch: expected at least {0} bytes, got {1} bytes",
                              numPixels * sizeof(float4), powerRawData.size()));
            return false;
        }

        // Properly parse the R32Float format for wavelength data
        if (wavelengthRawData.size() >= numPixels * sizeof(float))
        {
            // Use proper type casting to interpret the raw bytes as float data
            const float* floatData = reinterpret_cast<const float*>(wavelengthRawData.data());
            for (uint32_t i = 0; i < numPixels; i++)
            {
                mWavelengthReadbackBuffer[i] = floatData[i];
            }

            if (shouldLogThisFrame) logInfo("Successfully parsed wavelength data");
        }
        else
        {
            logError(fmt::format("Wavelength data size mismatch: expected at least {0} bytes, got {1} bytes",
                              numPixels * sizeof(float), wavelengthRawData.size()));
            return false;
        }

        // Validate data by checking a few values (only in debug mode and at intervals)
        if (shouldLogThisFrame)
        {
            for (uint32_t i = 0; i < std::min(static_cast<size_t>(5), mPowerReadbackBuffer.size()); i++)
            {
                const float4& power = mPowerReadbackBuffer[i];
                logInfo(fmt::format("readbackData: Sample power[{0}] = ({1:.6f}, {2:.6f}, {3:.6f}, {4:.2f})",
                                  i, power.x, power.y, power.z, power.w));
            }
        }

        return true;
    }
    catch (const std::exception& e)
    {
        logError(fmt::format("Error reading texture data: {0}", e.what()));

        // Set default values in case of error, to prevent crashes
        if (mPowerReadbackBuffer.empty())
        {
            mPowerReadbackBuffer.resize(numPixels, float4(0.0f, 0.0f, 0.0f, 550.0f));
            logWarning("Using default power values due to readback failure");
        }

        return false;
    }
}

void IncomingLightPowerPass::resetStatistics()
{
    // Store previous values for logging
    uint32_t prevPixelCount = mPowerStats.pixelCount;
    uint32_t prevTotalPixels = mPowerStats.totalPixels;
    uint32_t prevWavelengthBins = mPowerStats.wavelengthDistribution.size();

    // Clear all statistics
    std::memset(mPowerStats.totalPower, 0, sizeof(mPowerStats.totalPower));
    std::memset(mPowerStats.peakPower, 0, sizeof(mPowerStats.peakPower));
    std::memset(mPowerStats.averagePower, 0, sizeof(mPowerStats.averagePower));
    mPowerStats.pixelCount = 0;
    mPowerStats.totalPixels = 0;
    mPowerStats.wavelengthDistribution.clear();

    // Reset frame accumulation
    uint32_t prevAccumulatedFrames = mAccumulatedFrames;
    mAccumulatedFrames = 0;

    // Log reset action (only in debug mode and at intervals)
    bool shouldLogThisFrame = mDebugMode && (mFrameCount % mDebugLogFrequency == 0);
    if (shouldLogThisFrame && (prevPixelCount > 0 || prevAccumulatedFrames > 0))
    {
        logInfo(fmt::format("Statistics reset: Cleared {0} filtered pixels over {1} frames, {2} wavelength bins",
                          prevPixelCount, prevAccumulatedFrames, prevWavelengthBins));
    }

    // Mark stats as needing update
    mNeedStatsUpdate = true;
}

std::string IncomingLightPowerPass::getFormattedStatistics() const
{
    std::stringstream ss;

    ss << "Light Power Statistics:" << std::endl;
    ss << "--------------------" << std::endl;

    if (mPowerStats.totalPixels > 0)
    {
        const float passRate = 100.0f * float(mPowerStats.pixelCount) / float(mPowerStats.totalPixels);

        ss << "Filtered pixels: " << mPowerStats.pixelCount << " / " << mPowerStats.totalPixels;
        ss << " (" << std::fixed << std::setprecision(2) << passRate << "%)" << std::endl;

        ss << std::endl << "Power Statistics:" << std::endl;
        ss << "Total Power (W): R=" << mPowerStats.totalPower[0];
        ss << ", G=" << mPowerStats.totalPower[1];
        ss << ", B=" << mPowerStats.totalPower[2] << std::endl;

        ss << "Average Power (W): R=" << mPowerStats.averagePower[0];
        ss << ", G=" << mPowerStats.averagePower[1];
        ss << ", B=" << mPowerStats.averagePower[2] << std::endl;

        ss << "Peak Power (W): R=" << mPowerStats.peakPower[0];
        ss << ", G=" << mPowerStats.peakPower[1];
        ss << ", B=" << mPowerStats.peakPower[2] << std::endl;

        ss << std::endl << "Wavelength Distribution:" << std::endl;
        for (const auto& [wavelength, count] : mPowerStats.wavelengthDistribution)
        {
            ss << (wavelength * 10) << "-" << (wavelength * 10 + 10) << " nm: " << count << " pixels" << std::endl;
        }
    }
    else
    {
        ss << "No statistics available." << std::endl;
    }

    return ss.str();
}

void IncomingLightPowerPass::updateFilterDefines(DefineList& defines)
{
    // Add basic wavelength filter define
    defines.add("WAVELENGTH_FILTER", "1");

    // Add filter enable/disable flag
    defines.add("ENABLE_WAVELENGTH_FILTER", mEnableWavelengthFilter ? "1" : "0");

    // Add filter mode
    int filterMode = static_cast<int>(mFilterMode);
    defines.add("FILTER_MODE", std::to_string(filterMode));

    // Add other filter options
    defines.add("USE_VISIBLE_SPECTRUM_ONLY", mUseVisibleSpectrumOnly ? "1" : "0");
    defines.add("INVERT_FILTER", mInvertFilter ? "1" : "0");

    // Add specific band support if needed
    if (mFilterMode == FilterMode::SpecificBands && !mBandWavelengths.empty())
    {
        defines.add("SPECIFIC_BANDS", "1");
        defines.add("MAX_BANDS", std::to_string(std::min(mBandWavelengths.size(), size_t(16))));
    }
}

void IncomingLightPowerPass::prepareProgram()
{
    // Create program
    ProgramDesc desc;
    desc.addShaderLibrary(kShaderFile).csEntry("main");
    desc.setShaderModel(ShaderModel::SM6_5);

    // Add compile-time constants
    DefineList defines;
    updateFilterDefines(defines);

    // Create compute pass
    mpComputePass = ComputePass::create(mpDevice, desc, defines);
}

void IncomingLightPowerPass::prepareResources(RenderContext* pRenderContext, const RenderData& renderData)
{
    // Update the power calculator with current scene and frame dimensions
    if (mpScene)
    {
        mPowerCalculator.setup(mpScene, mFrameDim);
    }

    // Create classification buffer for photodetector analysis if needed
    if (mEnablePhotodetectorAnalysis)
    {
        uint32_t bufferSize = mFrameDim.x * mFrameDim.y;

        // Create or resize power data buffer if needed
        if (!mpPowerDataBuffer || mpPowerDataBuffer->getElementCount() != bufferSize)
        {
            try
            {
                mpPowerDataBuffer = mpDevice->createStructuredBuffer(
                    sizeof(float) * 4,  // 4 floats per entry (incidentAngle, wavelength, power, validFlag)
                    bufferSize,
                    ResourceBindFlags::UnorderedAccess | ResourceBindFlags::ShaderResource,
                    MemoryType::DeviceLocal,
                    nullptr,  // No initial data
                    false     // Not constant buffer
                );

                logInfo("Created power data buffer with {} entries ({}KB)",
                        bufferSize, (bufferSize * sizeof(float) * 4) / 1024);
            }
            catch (const std::exception& e)
            {
                logError("Failed to create power data buffer: {}", e.what());
                mTotalAccumulatedPower = 0.666f; // Error marker
                mEnablePhotodetectorAnalysis = false; // Disable PD analysis on error
            }
        }
    }
}

void IncomingLightPowerPass::startBatchExport()
{
    if (!mpScene)
    {
        logWarning("No scene available for batch export.");
        return;
    }

    mBatchExportActive = true;
    mBatchExportCurrentViewpoint = 0;
    mBatchExportFrameCount = mBatchExportFramesToWait;
    mBatchExportFormat = mExportFormat;

    const std::string timestamp = std::to_string(std::time(nullptr));
    mBatchExportBaseDirectory = mExportDirectory + "/batch_export_" + timestamp;

    if (!std::filesystem::create_directories(mBatchExportBaseDirectory))
    {
        logError("Failed to create base directory for batch export: " + mBatchExportBaseDirectory);
        mBatchExportActive = false;
        return;
    }

    // Store the original camera position and parameters
    if (mpScene->getCamera())
    {
        mOriginalCameraPosition = mpScene->getCamera()->getPosition();
        mOriginalCameraTarget = mpScene->getCamera()->getTarget();
        mOriginalCameraUp = mpScene->getCamera()->getUpVector();
    }
    else
    {
        logWarning("No camera in scene. Using default positions for batch export.");
        mOriginalCameraPosition = float3(0, 0, 5);
        mOriginalCameraTarget = float3(0, 0, 0);
        mOriginalCameraUp = float3(0, 1, 0);
    }

    // Check if the scene has saved viewpoints
    mUseLoadedViewpoints = mpScene->hasSavedViewpoints();

    if (mUseLoadedViewpoints)
    {
        // Store original camera state to restore it later
        float3 originalPosition = mpScene->getCamera()->getPosition();
        float3 originalTarget = mpScene->getCamera()->getTarget();
        float3 originalUp = mpScene->getCamera()->getUpVector();

        // Store original viewpoint to restore later
        mOriginalViewpoint = 0; // Assume 0 is current viewpoint (default)

        // Set first viewpoint (index 1, since 0 is typically the default viewpoint)
        mBatchExportCurrentViewpoint = 1; // Start from first non-default viewpoint
        mpScene->selectViewpoint(mBatchExportCurrentViewpoint);

        logInfo("Starting batch export for loaded viewpoints to " + mBatchExportBaseDirectory);
    }

    if (!mUseLoadedViewpoints)
    {
        // Fallback to generating our own viewpoints around the original camera position
        mTotalViewpoints = 8; // Default to 8 generated viewpoints
        mBatchExportCurrentViewpoint = 0; // Start from 0 for generated viewpoints

        // Set first viewpoint
        setViewpointPosition(mBatchExportCurrentViewpoint);

        logInfo("Starting batch export for " + std::to_string(mTotalViewpoints) +
               " generated viewpoints to " + mBatchExportBaseDirectory);
    }
}

void IncomingLightPowerPass::finishBatchExport()
{
    logInfo("Batch export finished for all viewpoints.");

    // Restore original camera state
    if (mpScene)
    {
        if (mUseLoadedViewpoints)
        {
            // Restore to original viewpoint
            mpScene->selectViewpoint(mOriginalViewpoint);
        }
        else if (mpScene->getCamera())
        {
            // Restore original camera position
            mpScene->getCamera()->setPosition(mOriginalCameraPosition);
            mpScene->getCamera()->setTarget(mOriginalCameraTarget);
            mpScene->getCamera()->setUpVector(mOriginalCameraUp);
        }
    }

    mBatchExportActive = false;
    mBatchExportCurrentViewpoint = 0;
    mBatchExportFrameCount = 0;
}

void IncomingLightPowerPass::setViewpointPosition(uint32_t viewpointIndex)
{
    if (!mpScene || !mpScene->getCamera())
        return;

    // Calculate position around a circle centered on the original target
    float angle = (float)viewpointIndex / (float)mTotalViewpoints * 2.0f * 3.14159f;
    float distance = length(mOriginalCameraPosition - mOriginalCameraTarget);

    float3 newPosition;
    newPosition.x = mOriginalCameraTarget.x + distance * std::cos(angle);
    newPosition.y = mOriginalCameraPosition.y; // Keep the same height
    newPosition.z = mOriginalCameraTarget.z + distance * std::sin(angle);

    mpScene->getCamera()->setPosition(newPosition);
    mpScene->getCamera()->setTarget(mOriginalCameraTarget);
    mpScene->getCamera()->setUpVector(mOriginalCameraUp);
}

void IncomingLightPowerPass::processBatchExport()
{
    if (!mBatchExportActive) return;

    if (mBatchExportFrameCount > 0)
    {
        mBatchExportFrameCount--;
        return;
    }

    const std::string timestamp = std::to_string(std::time(nullptr));
    std::string viewpointDir = mBatchExportBaseDirectory + "/viewpoint_" + std::to_string(mBatchExportCurrentViewpoint);
    std::filesystem::create_directories(viewpointDir);

    const std::string powerExt = mBatchExportFormat == OutputFormat::PNG ? ".png" :
                                 mBatchExportFormat == OutputFormat::EXR ? ".exr" :
                                 mBatchExportFormat == OutputFormat::CSV ? ".csv" : ".json";
    std::string powerFilename = viewpointDir + "/power_" + timestamp + powerExt;
    bool powerSuccess = exportPowerData(powerFilename, mBatchExportFormat);

    OutputFormat statsFormat = (mBatchExportFormat == OutputFormat::CSV) ? OutputFormat::CSV : OutputFormat::JSON;
    const std::string statsExt = (statsFormat == OutputFormat::CSV) ? ".csv" : ".json";
    std::string statsFilename = viewpointDir + "/stats_" + timestamp + statsExt;
    bool statsSuccess = exportStatistics(statsFilename, statsFormat);

    if (!powerSuccess || !statsSuccess)
    {
        logWarning("Failed to export data for viewpoint " + std::to_string(mBatchExportCurrentViewpoint));
    }
    else
    {
        logInfo("Successfully exported data for viewpoint " + std::to_string(mBatchExportCurrentViewpoint));
    }

    // Advance to next viewpoint
    mBatchExportCurrentViewpoint++;

    if (mUseLoadedViewpoints)
    {

        // Try to select the next viewpoint
        // Note: selectViewpoint doesn't throw exceptions, it just logs a warning if index is invalid
        // We need to remember the camera state before trying to select a new viewpoint
        float3 prevPosition = mpScene->getCamera()->getPosition();
        float3 prevTarget = mpScene->getCamera()->getTarget();

        // Try to select the viewpoint
        mpScene->selectViewpoint(mBatchExportCurrentViewpoint);

        // Check if camera actually moved (if viewpoint was valid)
        float3 currentPosition = mpScene->getCamera()->getPosition();
        float3 currentTarget = mpScene->getCamera()->getTarget();

        // Calculate distances between previous and current positions
        float posDistance = length(prevPosition - currentPosition);
        float targetDistance = length(prevTarget - currentTarget);

        // If distances are very small, consider positions unchanged
        const float epsilon = 0.0001f; // Small threshold for floating-point comparison
        if (posDistance < epsilon && targetDistance < epsilon)
        {
            // Camera didn't move, meaning we've reached the end of viewpoint list
            logInfo("Reached the end of loaded viewpoints at index " + std::to_string(mBatchExportCurrentViewpoint-1));
            finishBatchExport();
            return;
        }

        mBatchExportFrameCount = mBatchExportFramesToWait;
    }
    else
    {
        // Using generated viewpoints
        if (mBatchExportCurrentViewpoint >= mTotalViewpoints)
        {
            finishBatchExport();
        }
        else
        {
            setViewpointPosition(mBatchExportCurrentViewpoint);
            mBatchExportFrameCount = mBatchExportFramesToWait;
        }
    }
}

// Photodetector data management functions implementation
void IncomingLightPowerPass::initializePowerData()
{
    try
    {
        // Clear existing data points
        mPowerDataPoints.clear();
        mPowerDataPoints.reserve(mMaxDataPoints);

        // Reset total accumulated power
        mTotalAccumulatedPower = 0.0f;

        // Calculate expected memory usage
        const float expectedMemoryMB = (mMaxDataPoints * sizeof(PowerDataPoint)) / (1024.0f * 1024.0f);
        const float maxAllowedMemoryMB = 100.0f; // Reasonable limit for data storage

        if (expectedMemoryMB > maxAllowedMemoryMB)
        {
            logError("Data storage size {} would use {:.2f}MB, exceeding {:.2f}MB limit",
                    mMaxDataPoints, expectedMemoryMB, maxAllowedMemoryMB);
            mTotalAccumulatedPower = 0.666f; // Error marker
            return;
        }

        logInfo("Power data storage initialized: max {} data points ({:.2f}MB)",
                mMaxDataPoints, expectedMemoryMB);
    }
    catch (const std::exception& e)
    {
        logError("Failed to initialize power data storage: {}", e.what());
        mTotalAccumulatedPower = 0.666f; // Error marker

        // Attempt recovery with smaller data size
        try
        {
            mPowerDataPoints.clear();
            mMaxDataPoints = 100000; // Fallback to smaller size
            mPowerDataPoints.reserve(mMaxDataPoints);
            logInfo("Recovery data storage initialized with {} data points", mMaxDataPoints);
        }
        catch (...)
        {
            logError("Data storage recovery failed - PD analysis will be disabled");
            mEnablePhotodetectorAnalysis = false;
        }
    }
}

void IncomingLightPowerPass::resetPowerData()
{
    try
    {
        // Clear all data points
        mPowerDataPoints.clear();

        // Reset accumulation counter
        mTotalAccumulatedPower = 0.0f;

        logInfo("Power data reset successfully - {} data points cleared",
               mPowerDataPoints.size());
    }
    catch (const std::exception& e)
    {
        logError("Failed to reset power data: {}", e.what());
        mTotalAccumulatedPower = 0.666f; // Error marker

        // Attempt recovery by reinitializing
        try
        {
            initializePowerData();
            logInfo("Power data reset recovery successful");
        }
        catch (...)
        {
            logError("Power data reset recovery failed - PD analysis may be unstable");
        }
    }
}

bool IncomingLightPowerPass::exportPowerData()
{
    try
    {
        // Validate data before export
        if (mPowerDataPoints.empty())
        {
            logError("Power data is empty, cannot export");
            return false;
        }

        // Generate filename with timestamp
        std::string filename = mPowerDataExportPath + "power_data_" +
                              std::to_string(std::time(nullptr)) + ".csv";

        std::ofstream file(filename);
        if (!file.is_open())
        {
            logError("Failed to create export file: {}", filename);
            return false;
        }

        // Write CSV header
        file << "# Photodetector Power Data Export\n";
        file << "# Data points: " << mPowerDataPoints.size() << "\n";
        file << "# Total accumulated power: " << mTotalAccumulatedPower << " W\n";
        file << "# Format: incident_angle_deg,wavelength_nm,power_w\n";
        file << "incident_angle,wavelength,power\n";

        // Write data points
        for (const auto& dataPoint : mPowerDataPoints)
        {
            file << dataPoint.incidentAngle << ","
                 << dataPoint.wavelength << ","
                 << dataPoint.power << "\n";
        }

        file.close();
        logInfo("Power data exported to {} ({} data points)", filename, mPowerDataPoints.size());
        return true;
    }
    catch (const std::exception& e)
    {
        logError("CSV export failed: {}", e.what());
        return false;
    }
}

void IncomingLightPowerPass::accumulatePowerData(RenderContext* pRenderContext)
{
    if (!mEnablePhotodetectorAnalysis || !mpPowerDataBuffer)
    {
        return;
    }

    try
    {
        // Check if we're approaching data point limit
        if (mPowerDataPoints.size() >= mMaxDataPoints)
        {
            logWarning("Maximum data points reached ({}), skipping accumulation", mMaxDataPoints);
            return;
        }

        // Read back power data from GPU buffer
        uint32_t bufferSize = mFrameDim.x * mFrameDim.y;
        uint32_t totalBytes = bufferSize * sizeof(float) * 4; // 4 floats per entry

        // Create ReadBack staging buffer if it doesn't exist or is too small
        if (!mpPowerDataStagingBuffer || mpPowerDataStagingBuffer->getSize() < totalBytes)
        {
            mpPowerDataStagingBuffer = mpDevice->createBuffer(
                totalBytes,
                ResourceBindFlags::None,
                MemoryType::ReadBack
            );
            if (!mpPowerDataStagingBuffer)
            {
                logError("Failed to create power data staging buffer");
                mTotalAccumulatedPower = 0.666f; // Error marker
                return;
            }
        }

        // Copy data from GPU buffer to staging buffer
        pRenderContext->copyResource(mpPowerDataStagingBuffer.get(), mpPowerDataBuffer.get());

        // Wait for copy to complete
        pRenderContext->submit(true);

        // Map staging buffer for reading
        const float* pData = static_cast<const float*>(mpPowerDataStagingBuffer->map());
        if (!pData)
        {
            logError("Failed to map power data staging buffer for reading");
            mTotalAccumulatedPower = 0.666f; // Error marker
            return;
        }

        uint32_t validPixels = 0;
        uint32_t invalidPixels = 0;

        // Process each pixel's power data
        for (uint32_t i = 0; i < bufferSize && mPowerDataPoints.size() < mMaxDataPoints; i++)
        {
            uint32_t dataOffset = i * 4; // 4 floats per entry

            float incidentAngle = pData[dataOffset + 0];
            float wavelength = pData[dataOffset + 1];
            float power = pData[dataOffset + 2];
            float validFlag = pData[dataOffset + 3];

            // Check if this pixel has valid data
            if (validFlag > 0.5f &&
                incidentAngle >= 0.0f && incidentAngle <= 90.0f &&
                wavelength >= 300.0f && wavelength <= 1000.0f &&
                power >= 0.0f && power < 1e6f)
            {
                // Store direct data point
                PowerDataPoint dataPoint;
                dataPoint.incidentAngle = incidentAngle;
                dataPoint.wavelength = wavelength;
                dataPoint.power = power;

                mPowerDataPoints.push_back(dataPoint);
                mTotalAccumulatedPower += power;
                validPixels++;
            }
            else
            {
                invalidPixels++;
            }
        }

        // Unmap staging buffer
        mpPowerDataStagingBuffer->unmap();

        // Log accumulation results for debugging
        if (mDebugMode && (mFrameCount % mDebugLogFrequency == 0))
        {
            logInfo("Power data accumulation: {} valid pixels, {} invalid pixels, {} total data points, {:.6f} W total power",
                    validPixels, invalidPixels, mPowerDataPoints.size(), mTotalAccumulatedPower);
        }

        // Check for errors
        if (validPixels == 0 && mFrameCount > 10) // Allow some startup frames
        {
            logWarning("No valid power data accumulated after frame {}", mFrameCount);
        }
    }
    catch (const std::exception& e)
    {
        logError("Failed to accumulate power data: {}", e.what());
        mTotalAccumulatedPower = 0.666f; // Error marker
    }
}
