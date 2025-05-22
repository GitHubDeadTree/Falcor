#include "IncomingLightPowerPass.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iomanip>

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

    // Get camera sensor dimensions
    // In a real camera, this would be the physical sensor size
    // For our virtual camera, we use the FOV and distance to calculate the sensor size

    // Get focal length in mm
    float focalLength = mpCamera->getFocalLength();
    float frameHeight = mpCamera->getFrameHeight();

    // Calculate horizontal FOV in radians using focal length and frame height
    // Formula: fovY = 2 * atan(frameHeight / (2 * focalLength))
    float hFOV = 2.0f * std::atan(frameHeight / (2.0f * focalLength));

    // Camera position and distance to image plane
    // We use 1.0 as a normalized distance to image plane
    float distToImagePlane = 1.0f;

    // Calculate sensor width and height at this distance
    float sensorWidth = 2.0f * distToImagePlane * std::tan(hFOV * 0.5f);
    float aspectRatio = (float)mFrameDimensions.x / mFrameDimensions.y;
    float sensorHeight = sensorWidth / aspectRatio;

    // Calculate pixel dimensions
    float pixelWidth = sensorWidth / mFrameDimensions.x;
    float pixelHeight = sensorHeight / mFrameDimensions.y;

    // Calculate pixel area
    float pixelArea = pixelWidth * pixelHeight;

    // Ensure pixel area is not too small or zero
    const float minPixelArea = 0.00001f;
    pixelArea = std::max(pixelArea, minPixelArea);

    return pixelArea;
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

    // Calculate pixel area
    float pixelArea = computePixelArea();

    // Calculate cosine term
    float cosTheta = computeCosTheta(rayDir);

    // Calculate power using the formula: Power = Radiance * Area * cos(θ)
    float3 power = float3(radiance.r, radiance.g, radiance.b) * pixelArea * cosTheta;

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
        else if (key == "outputPowerTexName") mOutputPowerTexName = value.operator std::string();
        else if (key == "outputWavelengthTexName") mOutputWavelengthTexName = value.operator std::string();
        else logWarning("Unknown property '{}' in IncomingLightPowerPass properties.", key);
    }

    // Add default wavelength bands for common light sources
    mBandWavelengths = { 405.0f, 436.0f, 546.0f, 578.0f }; // Mercury lamp wavelengths
    mBandTolerances = { 5.0f, 5.0f, 5.0f, 5.0f };

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
    props["outputPowerTexName"] = mOutputPowerTexName;
    props["outputWavelengthTexName"] = mOutputWavelengthTexName;
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
    // Print debug info - current settings
    logInfo(fmt::format("IncomingLightPowerPass executing - settings: enabled={0}, wavelength_filter_enabled={1}, filter_mode={2}, min_wavelength={3}, max_wavelength={4}",
                       mEnabled, mEnableWavelengthFilter, static_cast<int>(mFilterMode), mMinWavelength, mMaxWavelength));

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

        // Log that we're using forced values
        logInfo("IncomingLightPowerPass disabled but using forced non-zero values for debugging");
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

    // Set camera data
    if (mpScene && mpScene->getCamera())
    {
        auto pCamera = mpScene->getCamera();
        var[kPerFrameCB][kCameraInvViewProj] = pCamera->getInvViewProjMatrix();
        var[kPerFrameCB][kCameraPosition] = pCamera->getPosition();
        var[kPerFrameCB][kCameraTarget] = pCamera->getTarget();
        var[kPerFrameCB][kCameraFocalLength] = pCamera->getFocalLength();
    }
    else
    {
        // Default values if no camera is available
        var[kPerFrameCB][kCameraInvViewProj] = float4x4::identity();
        var[kPerFrameCB][kCameraPosition] = float3(0.0f, 0.0f, 0.0f);
        var[kPerFrameCB][kCameraTarget] = float3(0.0f, 0.0f, -1.0f);
        var[kPerFrameCB][kCameraFocalLength] = 21.0f; // Default focal length
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
    if (pSampleCount)
    {
        // If multi-sample data is available, we could bind it to the shader
        // for per-pixel sample count if needed in the future
        // var["gSampleCount"] = pSampleCount;

        // Log that multi-sample data is detected
        logInfo("IncomingLightPowerPass: Multi-sample data detected");
    }

    // Bind output resources
    var["gOutputPower"] = pOutputPower;
    var["gOutputWavelength"] = pOutputWavelength;
    var["gDebugOutput"] = pDebugOutput;
    var["gDebugInputData"] = pDebugInputData;
    var["gDebugCalculation"] = pDebugCalculation;

    // Execute the compute pass
    mpComputePass->execute(pRenderContext, uint3(mFrameDim.x, mFrameDim.y, 1));

    // Read debug information for first 4 pixels
    if (pDebugOutput && pDebugInputData && pDebugCalculation)
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

            // First, log camera information for debugging CosTheta calculations
            if (mpScene && mpScene->getCamera())
            {
                float3 cameraPos = mpScene->getCamera()->getPosition();
                float3 cameraTarget = mpScene->getCamera()->getTarget();
                float3 cameraNormal = normalize(cameraTarget - cameraPos);
                float3 invNormal = -cameraNormal;

                logInfo(fmt::format("DEBUG - CAMERA INFO: Position=({0:.6f}, {1:.6f}, {2:.6f})",
                                   cameraPos.x, cameraPos.y, cameraPos.z));
                logInfo(fmt::format("DEBUG - CAMERA INFO: Target=({0:.6f}, {1:.6f}, {2:.6f})",
                                   cameraTarget.x, cameraTarget.y, cameraTarget.z));
                logInfo(fmt::format("DEBUG - CAMERA INFO: Forward Normal=({0:.6f}, {1:.6f}, {2:.6f}), Length={3:.6f}",
                                   cameraNormal.x, cameraNormal.y, cameraNormal.z, length(cameraNormal)));
                logInfo(fmt::format("DEBUG - CAMERA INFO: Inverse Normal=({0:.6f}, {1:.6f}, {2:.6f})",
                                   invNormal.x, invNormal.y, invNormal.z));

                // Also check camera orientation to verify how the coordinate system is set up
                float3 cameraUp = mpScene->getCamera()->getUpVector();

                // Check if camera forward is nearly parallel to world up
                float upDotForward = dot(cameraNormal, float3(0, 1, 0));
                if (std::abs(upDotForward) > 0.99f)
                {
                    logInfo(fmt::format("DEBUG - CAMERA WARNING: Camera forward is nearly parallel to world up (dot={0:.6f})",
                                    upDotForward));
                }

                // Construct proper camera basis
                float3 worldUp = float3(0, 1, 0);
                if (std::abs(upDotForward) > 0.99f)
                {
                    worldUp = float3(0, 0, 1); // Use a different up vector
                    logInfo("DEBUG - CAMERA INFO: Using alternate world up vector (0,0,1)");
                }

                float3 cameraRight = normalize(cross(worldUp, cameraNormal));
                float3 computedUp = normalize(cross(cameraNormal, cameraRight));

                logInfo(fmt::format("DEBUG - CAMERA ORIENTATION: Up=({0:.6f}, {1:.6f}, {2:.6f})",
                                  computedUp.x, computedUp.y, computedUp.z));
                logInfo(fmt::format("DEBUG - CAMERA ORIENTATION: Right=({0:.6f}, {1:.6f}, {2:.6f})",
                                  cameraRight.x, cameraRight.y, cameraRight.z));

                // Check basis orthogonality
                logInfo(fmt::format("DEBUG - CAMERA BASIS: Right·Up={0:.6f}, Up·Forward={1:.6f}, Forward·Right={2:.6f}",
                                  dot(cameraRight, computedUp),
                                  dot(computedUp, cameraNormal),
                                  dot(cameraNormal, cameraRight)));
            }

            // Also try to get ray directions from the input texture if available
            const auto& pInputRayDirection = renderData.getTexture(kInputRayDirection);
            if (pInputRayDirection)
            {
                logInfo("DEBUG - Reading ray directions from input texture");
                std::vector<uint8_t> rayDirData = pRenderContext->readTextureSubresource(pInputRayDirection.get(), 0);

                if (!rayDirData.empty())
                {
                    const float3* rayDirs = reinterpret_cast<const float3*>(rayDirData.data());

                    for (uint32_t y = 0; y < 2; y++)
                    {
                        for (uint32_t x = 0; x < 2; x++)
                        {
                            uint32_t pixelIndex = y * pInputRayDirection->getWidth() + x;

                            if (pixelIndex < rayDirData.size() / sizeof(float3))
                            {
                                const float3& rayDir = rayDirs[pixelIndex];
                                logInfo(fmt::format("DEBUG - Pixel [{0},{1}] - INPUT RAY DIR: ({2:.8f}, {3:.8f}, {4:.8f})",
                                                 x, y, rayDir.x, rayDir.y, rayDir.z));

                                // Calculate dot product with camera inverse normal for comparison
                                if (mpScene && mpScene->getCamera())
                                {
                                    float3 cameraPos = mpScene->getCamera()->getPosition();
                                    float3 cameraTarget = mpScene->getCamera()->getTarget();
                                    float3 cameraNormal = normalize(cameraTarget - cameraPos);
                                    float3 invNormal = -cameraNormal;

                                    float dotProduct = dot(rayDir, invNormal);
                                    logInfo(fmt::format("DEBUG - Pixel [{0},{1}] - DIRECT DOT PRODUCT: {2:.8f}",
                                                     x, y, dotProduct));
                                }
                            }
                        }
                    }
                }
                else
                {
                    logInfo("DEBUG - Input ray direction texture not available, will use computed ray directions");
                }
            }

            // Output debug info for first 4 pixels
            for (uint32_t y = 0; y < 2; y++)
            {
                for (uint32_t x = 0; x < 2; x++)
                {
                    uint32_t pixelIndex = y * pDebugOutput->getWidth() + x;

                    if (pixelIndex < debugData.size() / sizeof(float4))
                    {
                        const float4& pixelData = debugValues[pixelIndex];
                        const float4& inputPixelData = inputValues[pixelIndex];
                        const float4& calcPixelData = calcValues[pixelIndex];

                        // Extract camera-space ray direction (now stored in debugOutput)
                        float3 rayDirInCameraSpace = float3(pixelData.x, pixelData.y, pixelData.z);

                        // Check if this pixel was filtered out (special value for filtered rays)
                        if (pixelData.x == -1 && pixelData.y == -1 && pixelData.z == -1)
                        {
                            logInfo(fmt::format("DEBUG - Pixel [{0},{1}]: FILTERED OUT (wavelength = {2:.2f}nm)",
                                              x, y, pixelData.w));
                        }
                        else
                        {
                            // Get ray direction from input data (now stored in debugInputData)
                            float3 rayDir = float3(inputPixelData.x, inputPixelData.y, inputPixelData.z);
                            float wavelength = inputPixelData.w;

                            // Log raw ray direction
                            logInfo(fmt::format("DEBUG - Pixel [{0},{1}] - RAY DIRECTION: World=({2:.8f}, {3:.8f}, {4:.8f})",
                                               x, y, rayDir.x, rayDir.y, rayDir.z));

                            // Log camera-space ray direction
                            logInfo(fmt::format("DEBUG - Pixel [{0},{1}] - RAY DIRECTION IN CAMERA SPACE: ({2:.8f}, {3:.8f}, {4:.8f})",
                                               x, y,
                                               rayDirInCameraSpace.x, rayDirInCameraSpace.y, rayDirInCameraSpace.z));

                            // Log radiance data
                            logInfo(fmt::format("DEBUG - Pixel [{0},{1}] - INPUT: Radiance=({2:.8f}, {3:.8f}, {4:.8f}), Wavelength={5:.2f}nm",
                                               x, y,
                                               inputPixelData.x, inputPixelData.y, inputPixelData.z, wavelength));

                            // Log calculation steps with enhanced cosTheta details
                            // calcPixelData: x=pixelArea, y=finalCosTheta, z=rawDotProduct, w=rayLength
                            logInfo(fmt::format("DEBUG - Pixel [{0},{1}] - CALCULATION: PixelArea={2:.8f}, CosTheta={3:.8f}, RawDotProduct={4:.8f}, RayLength={5:.8f}",
                                               x, y,
                                               calcPixelData.x, calcPixelData.y, calcPixelData.z, calcPixelData.w));

                            // Analysis of CosTheta being zero
                            if (std::abs(calcPixelData.z) < 0.0001f) {
                                logInfo(fmt::format("DEBUG - Pixel [{0},{1}] - COSTHETA ANALYSIS: Raw dot product is {2:.8f} which suggests the ray direction and camera inverse normal are perpendicular",
                                                  x, y, calcPixelData.z));

                                // Additional analysis of camera-space coordinates
                                if (std::abs(rayDirInCameraSpace.z) < 0.0001f) {
                                    logInfo(fmt::format("DEBUG - Pixel [{0},{1}] - CAMERA SPACE ANALYSIS: Ray is perpendicular to camera forward direction (z={2:.8f})",
                                                  x, y, rayDirInCameraSpace.z));
                                }

                                // Check if ray is aligned with camera plane (X-Y plane in camera space)
                                float rayAlignmentWithCameraPlane = std::abs(rayDirInCameraSpace.x) + std::abs(rayDirInCameraSpace.y);
                                if (rayAlignmentWithCameraPlane > 0.9f) {
                                    logInfo(fmt::format("DEBUG - Pixel [{0},{1}] - CAMERA SPACE ANALYSIS: Ray is primarily in camera X-Y plane, X={2:.8f}, Y={3:.8f}",
                                                  x, y, rayDirInCameraSpace.x, rayDirInCameraSpace.y));
                                }

                                // Try to compute ray direction to confirm this
                                if (mpScene && mpScene->getCamera())
                                {
                                    float2 pixelCenter = float2(x, y) + 0.5f;
                                    float2 ndc = pixelCenter / float2(mFrameDim) * 2.f - 1.f;

                                    const float4x4 invViewProj = mpScene->getCamera()->getInvViewProjMatrix();
                                    const float3 cameraPos = mpScene->getCamera()->getPosition();

                                    float4 worldPos = mul(invViewProj, float4(ndc.x, -ndc.y, 1.f, 1.f));
                                    worldPos /= worldPos.w;

                                    float3 computedRayDir = normalize(float3(worldPos.x, worldPos.y, worldPos.z) - cameraPos);

                                    logInfo(fmt::format("DEBUG - Pixel [{0},{1}] - COMPUTED RAY DIR: ({2:.8f}, {3:.8f}, {4:.8f})",
                                                      x, y, computedRayDir.x, computedRayDir.y, computedRayDir.z));

                                    // Calculate dot product with inverse normal
                                    float3 cameraTarget = mpScene->getCamera()->getTarget();
                                    float3 cameraNormal = normalize(cameraTarget - cameraPos);
                                    float3 invNormal = -cameraNormal;

                                    float dotProduct = dot(computedRayDir, invNormal);
                                    float cosTheta = std::max(0.0f, dotProduct);
                                    // Use the same minimum value as in the shader
                                    const float minCosTheta = 0.01f;
                                    cosTheta = std::max(cosTheta, minCosTheta);

                                    logInfo(fmt::format("DEBUG - Pixel [{0},{1}] - RECOMPUTED DOT: {2:.8f}, FINAL COS THETA: {3:.8f}",
                                                      x, y, dotProduct, cosTheta));

                                    // Convert to camera space using proper camera basis
                                    float3 worldUp = float3(0, 1, 0);
                                    float upDotForward = dot(cameraNormal, worldUp);
                                    if (std::abs(upDotForward) > 0.99f)
                                    {
                                        worldUp = float3(0, 0, 1); // Use a different up vector
                                    }

                                    float3 cameraRight = normalize(cross(worldUp, cameraNormal));
                                    float3 cameraUp = normalize(cross(cameraNormal, cameraRight));

                                    float3 computedRayDirInCameraSpace;
                                    computedRayDirInCameraSpace.x = dot(computedRayDir, cameraRight);
                                    computedRayDirInCameraSpace.y = dot(computedRayDir, cameraUp);
                                    computedRayDirInCameraSpace.z = dot(computedRayDir, cameraNormal);

                                    logInfo(fmt::format("DEBUG - Pixel [{0},{1}] - RAY IN CAMERA SPACE: ({2:.8f}, {3:.8f}, {4:.8f})",
                                                      x, y,
                                                      computedRayDirInCameraSpace.x,
                                                      computedRayDirInCameraSpace.y,
                                                      computedRayDirInCameraSpace.z));
                                }
                            }

                            // Log calculation formula
                            logInfo(fmt::format("DEBUG - Pixel [{0},{1}] - FORMULA: Power = Radiance * PixelArea * CosTheta",
                                              x, y));

                            // Log calculation result
                            logInfo(fmt::format("DEBUG - Pixel [{0},{1}] - RESULT: {2:.8f} * {3:.8f} * {4:.8f} = ({5:.8f}, {6:.8f}, {7:.8f})",
                                              x, y,
                                              inputPixelData.x, calcPixelData.x, calcPixelData.y,
                                              pixelData.x, pixelData.y, pixelData.z));
                        }
                    }
                }
            }
        }
    }

    // Add debug code to read the value of the first pixel
    // Create a buffer to read data back from GPU
    std::vector<uint8_t> pixelData;

    // Submit previous commands to ensure compute shader has completed
    pRenderContext->submit(true);

    // Read the first pixel data using the vector version of readTextureSubresource
    pixelData = pRenderContext->readTextureSubresource(pOutputPower.get(), 0);

    // Ensure reading is completed
    pRenderContext->submit(true);

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

    // Calculate statistics if enabled
    if (mEnableStatistics && mNeedStatsUpdate)
    {
        calculateStatistics(pRenderContext, renderData);
    }
}

void IncomingLightPowerPass::renderUI(Gui::Widgets& widget)
{
    bool changed = false;
    changed |= widget.checkbox("Enabled", mEnabled);

    // Add pixel area scale control
    auto pixelAreaGroup = widget.group("Pixel Area Adjustment", true);
    if (pixelAreaGroup)
    {
        changed |= widget.slider("Pixel Area Scale", mPixelAreaScale, 1.0f, 10000.0f);
        widget.tooltip("Scales the calculated pixel area to make power values more visible.\nDefault: 1000.0");
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
        formatList.push_back({ 0, "PNG" });
        formatList.push_back({ 1, "EXR" });
        formatList.push_back({ 2, "CSV" });
        formatList.push_back({ 3, "JSON" });

        uint32_t currentFormat = static_cast<uint32_t>(mExportFormat);
        if (widget.dropdown("Export Format", formatList, currentFormat))
        {
            mExportFormat = static_cast<OutputFormat>(currentFormat);
        }

        // Export buttons
        if (widget.button("Export Power Data"))
        {
            const std::string baseName = "light_power_" + std::to_string(std::time(nullptr));
            const std::string ext = mExportFormat == OutputFormat::PNG ? ".png" :
                                   mExportFormat == OutputFormat::EXR ? ".exr" :
                                   mExportFormat == OutputFormat::CSV ? ".csv" : ".json";

            exportPowerData(mExportDirectory + "/" + baseName + ext, mExportFormat);
        }

        if (widget.button("Export Statistics"))
        {
            const std::string baseName = "light_stats_" + std::to_string(std::time(nullptr));
            const std::string ext = mExportFormat == OutputFormat::CSV ? ".csv" : ".json";

            exportStatistics(mExportDirectory + "/" + baseName + ext,
                            mExportFormat == OutputFormat::CSV ? OutputFormat::CSV : OutputFormat::JSON);
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

void IncomingLightPowerPass::calculateStatistics(RenderContext* pRenderContext, const RenderData& renderData)
{
    // Read back the data from GPU
    if (!readbackData(pRenderContext, renderData))
    {
        logError("Failed to read back data for statistics calculation");
        return;
    }

    // Log filter settings for debugging
    logInfo(fmt::format("Calculating statistics with settings: wavelength_filter_enabled={0}, filter_mode={1}, min={2}, max={3}, invert={4}",
                       mEnableWavelengthFilter, static_cast<int>(mFilterMode), mMinWavelength, mMaxWavelength, mInvertFilter));

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

    // Count pixels, accumulate power values
    uint32_t nonZeroPixels = 0;
    float maxR = 0.0f, maxG = 0.0f, maxB = 0.0f;
    float sumR = 0.0f, sumG = 0.0f, sumB = 0.0f;

    // First pass: count non-zero pixels and gather statistics
    for (uint32_t i = 0; i < mPowerReadbackBuffer.size(); i++)
    {
        const float4& power = mPowerReadbackBuffer[i];

        // Use a small epsilon to filter out nearly-zero values that might be due to precision errors
        const float epsilon = 1e-6f;
        if (power.x > epsilon || power.y > epsilon || power.z > epsilon)
        {
            nonZeroPixels++;

            // Track local maximums for validation
            maxR = std::max(maxR, power.x);
            maxG = std::max(maxG, power.y);
            maxB = std::max(maxB, power.z);

            // Track local sums for validation
            sumR += power.x;
            sumG += power.y;
            sumB += power.z;

            if (nonZeroPixels <= 10) // Log first 10 non-zero pixels for debugging
            {
                logInfo(fmt::format("NonZero pixel [{0}]: R={1:.8f}, G={2:.8f}, B={3:.8f}, W={4:.2f}",
                                   i, power.x, power.y, power.z, power.w));
            }

            // Update stats
            mPowerStats.pixelCount++;

            // Accumulate power using precise addition to minimize floating point errors
            mPowerStats.totalPower[0] += power.x;
            mPowerStats.totalPower[1] += power.y;
            mPowerStats.totalPower[2] += power.z;

            // Track peak power
            mPowerStats.peakPower[0] = std::max(mPowerStats.peakPower[0], power.x);
            mPowerStats.peakPower[1] = std::max(mPowerStats.peakPower[1], power.y);
            mPowerStats.peakPower[2] = std::max(mPowerStats.peakPower[2], power.z);

            // Track wavelength distribution (bin by 10nm intervals)
            // Ensure wavelength is valid and within reasonable range before binning
            if (power.w > 0.0f && power.w < 2000.0f) // Typical wavelength range: 0-2000nm
            {
                int wavelengthBin = static_cast<int>(power.w / 10.0f);
                mPowerStats.wavelengthDistribution[wavelengthBin]++;
            }
        }
    }

    // Validate consistency of our statistics tracking
    if (nonZeroPixels > 0)
    {
        // Check if peak power tracking is working correctly
        if (std::abs(maxR - mPowerStats.peakPower[0]) > 1e-5f ||
            std::abs(maxG - mPowerStats.peakPower[1]) > 1e-5f ||
            std::abs(maxB - mPowerStats.peakPower[2]) > 1e-5f)
        {
            logWarning("Peak power tracking may be inconsistent");
            // Correct the values if needed
            mPowerStats.peakPower[0] = std::max(mPowerStats.peakPower[0], maxR);
            mPowerStats.peakPower[1] = std::max(mPowerStats.peakPower[1], maxG);
            mPowerStats.peakPower[2] = std::max(mPowerStats.peakPower[2], maxB);
        }

        // Check if total power tracking is working correctly (with some tolerance for floating point)
        const float relativeTolerance = 0.01f; // 1% tolerance
        if (mAccumulatedFrames == 0) // Only check for first frame
        {
            if (std::abs(sumR - mPowerStats.totalPower[0]) / std::max(sumR, 1e-5f) > relativeTolerance ||
                std::abs(sumG - mPowerStats.totalPower[1]) / std::max(sumG, 1e-5f) > relativeTolerance ||
                std::abs(sumB - mPowerStats.totalPower[2]) / std::max(sumB, 1e-5f) > relativeTolerance)
            {
                logWarning("Total power tracking may be inconsistent");
                // Log differences for debugging
                logInfo(fmt::format("Power sum difference: R={0:.8f}, G={1:.8f}, B={2:.8f}",
                                   sumR - mPowerStats.totalPower[0],
                                   sumG - mPowerStats.totalPower[1],
                                   sumB - mPowerStats.totalPower[2]));
            }
        }
    }

    // Update total pixels count
    mPowerStats.totalPixels = mFrameDim.x * mFrameDim.y;

    // Calculate averages with safe division to prevent divide-by-zero
    if (mPowerStats.pixelCount > 0)
    {
        mPowerStats.averagePower[0] = mPowerStats.totalPower[0] / mPowerStats.pixelCount;
        mPowerStats.averagePower[1] = mPowerStats.totalPower[1] / mPowerStats.pixelCount;
        mPowerStats.averagePower[2] = mPowerStats.totalPower[2] / mPowerStats.pixelCount;
    }
    else
    {
        // Reset average power to zero if no pixels passed filtering
        mPowerStats.averagePower[0] = 0.0f;
        mPowerStats.averagePower[1] = 0.0f;
        mPowerStats.averagePower[2] = 0.0f;
    }

    // Update accumulated frames count
    if (mAccumulatePower)
    {
        mAccumulatedFrames++;
    }

    // Log detailed statistics summary for debugging
    float percentage = mPowerReadbackBuffer.size() > 0 ?
                      100.0f * nonZeroPixels / mPowerReadbackBuffer.size() : 0.0f;

    logInfo(fmt::format("Statistics: Found {0} non-zero power pixels out of {1} ({2:.2f}%)",
                       nonZeroPixels,
                       mPowerReadbackBuffer.size(),
                       percentage));

    logInfo(fmt::format("Total Power (W): R={0:.8f}, G={1:.8f}, B={2:.8f}",
                      mPowerStats.totalPower[0],
                      mPowerStats.totalPower[1],
                      mPowerStats.totalPower[2]));

    logInfo(fmt::format("Peak Power (W): R={0:.8f}, G={1:.8f}, B={2:.8f}",
                      mPowerStats.peakPower[0],
                      mPowerStats.peakPower[1],
                      mPowerStats.peakPower[2]));

    if (nonZeroPixels == 0 && mPowerReadbackBuffer.size() > 0)
    {
        // Log sample pixels to debug when no non-zero pixels are found
        logWarning("No non-zero pixels found in the current frame");
        for (uint32_t i = 0; i < std::min(static_cast<size_t>(5), mPowerReadbackBuffer.size()); i++)
        {
            const float4& power = mPowerReadbackBuffer[i];
            logInfo(fmt::format("Sample pixel [{0}]: R={1:.8f}, G={2:.8f}, B={3:.8f}, W={4:.2f}",
                               i, power.x, power.y, power.z, power.w));
        }
    }

    // Output wavelength distribution summary if available
    if (!mPowerStats.wavelengthDistribution.empty())
    {
        uint32_t totalBins = mPowerStats.wavelengthDistribution.size();
        uint32_t countedWavelengths = 0;

        for (const auto& [binIndex, count] : mPowerStats.wavelengthDistribution)
        {
            countedWavelengths += count;
        }

        logInfo(fmt::format("Wavelength distribution: {0} distinct bands, {1} wavelengths counted",
                          totalBins, countedWavelengths));
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

    logInfo(fmt::format("readbackData: Texture dimensions: {0}x{1}, total pixels: {2}",
                       width, height, numPixels));
    logInfo(fmt::format("Power texture format: {0}, Wavelength texture format: {1}",
                       to_string(pOutputPower->getFormat()),
                       to_string(pOutputWavelength->getFormat())));

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

        // Log data size information for debugging
        logInfo(fmt::format("readbackData: Power raw data size: {0} bytes, expected: {1} bytes (float4 per pixel)",
                          powerRawData.size(), numPixels * sizeof(float4)));
        logInfo(fmt::format("readbackData: Wavelength raw data size: {0} bytes, expected: {1} bytes (float per pixel)",
                          wavelengthRawData.size(), numPixels * sizeof(float)));

        // Resize the destination buffers
        mPowerReadbackBuffer.resize(numPixels);
        mWavelengthReadbackBuffer.resize(numPixels);

        // NEW IMPLEMENTATION: Properly parse the RGBA32Float format for power data
        if (powerRawData.size() >= numPixels * sizeof(float4))
        {
            // Use proper type casting to interpret the raw bytes as float4 data
            const float4* floatData = reinterpret_cast<const float4*>(powerRawData.data());
            for (uint32_t i = 0; i < numPixels; i++)
            {
                mPowerReadbackBuffer[i] = floatData[i];
            }
            logInfo("Successfully parsed power data");
        }
        else
        {
            logError(fmt::format("Power data size mismatch: expected at least {0} bytes, got {1} bytes",
                              numPixels * sizeof(float4), powerRawData.size()));
            return false;
        }

        // NEW IMPLEMENTATION: Properly parse the R32Float format for wavelength data
        if (wavelengthRawData.size() >= numPixels * sizeof(float))
        {
            // Use proper type casting to interpret the raw bytes as float data
            const float* floatData = reinterpret_cast<const float*>(wavelengthRawData.data());
            for (uint32_t i = 0; i < numPixels; i++)
            {
                mWavelengthReadbackBuffer[i] = floatData[i];
            }
            logInfo("Successfully parsed wavelength data");
        }
        else
        {
            logError(fmt::format("Wavelength data size mismatch: expected at least {0} bytes, got {1} bytes",
                              numPixels * sizeof(float), wavelengthRawData.size()));
            return false;
        }

        // Validate data by checking a few values (debugging)
        for (uint32_t i = 0; i < std::min(static_cast<size_t>(5), mPowerReadbackBuffer.size()); i++)
        {
            const float4& power = mPowerReadbackBuffer[i];
            logInfo(fmt::format("readbackData: Sample power[{0}] = ({1:.6f}, {2:.6f}, {3:.6f}, {4:.2f})",
                              i, power.x, power.y, power.z, power.w));
        }

        return true;
    }
    catch (const std::exception& e)
    {
        logError(fmt::format("Error reading texture data: {0}", e.what()));
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

    // Log reset action
    if (prevPixelCount > 0 || prevAccumulatedFrames > 0)
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

    // Add any buffer initialization or other resource preparation here
}
