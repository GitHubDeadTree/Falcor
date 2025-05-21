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

    // Shader constants
    const std::string kPerFrameCB = "PerFrameCB";           // cbuffer name
    const std::string kMinWavelength = "gMinWavelength";    // Minimum wavelength to consider
    const std::string kMaxWavelength = "gMaxWavelength";    // Maximum wavelength to consider
    const std::string kUseVisibleSpectrumOnly = "gUseVisibleSpectrumOnly"; // Whether to use visible spectrum only
    const std::string kInvertFilter = "gInvertFilter";      // Whether to invert the filter
    const std::string kFilterMode = "gFilterMode";          // Wavelength filtering mode
    const std::string kBandCount = "gBandCount";            // Number of specific bands to filter
}

// Define constants
const std::string IncomingLightPowerPass::kInputRadiance = "radiance";
const std::string IncomingLightPowerPass::kInputRayDirection = "rayDirection";
const std::string IncomingLightPowerPass::kInputWavelength = "wavelength";
const std::string IncomingLightPowerPass::kInputSampleCount = "sampleCount";
const std::string IncomingLightPowerPass::kOutputPower = "lightPower";
const std::string IncomingLightPowerPass::kOutputWavelength = "lightWavelength";
const std::string IncomingLightPowerPass::kPerFrameCB = "PerFrameCB";

// Shader parameter names
const std::string IncomingLightPowerPass::kMinWavelength = "gMinWavelength";
const std::string IncomingLightPowerPass::kMaxWavelength = "gMaxWavelength";
const std::string IncomingLightPowerPass::kUseVisibleSpectrumOnly = "gUseVisibleSpectrumOnly";
const std::string IncomingLightPowerPass::kInvertFilter = "gInvertFilter";
const std::string IncomingLightPowerPass::kFilterMode = "gFilterMode";
const std::string IncomingLightPowerPass::kBandCount = "gBandCount";

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
    const std::vector<float>& bandTolerances) const
{
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
    const std::vector<float>& bandTolerances) const
{
    // Apply wavelength filtering
    if (!isWavelengthAllowed(wavelength, minWavelength, maxWavelength,
        filterMode, useVisibleSpectrumOnly, invertFilter,
        bandWavelengths, bandTolerances))
    {
        return float4(0.f, 0.f, 0.f, 0.f);
    }

    // Calculate power using the formula: Power = Radiance * Area * cos(θ)
    float cosTheta = computeCosTheta(rayDir);
    float pixelArea = mPixelArea; // Use cached value

    // Calculate power for each color channel
    float3 power = float3(radiance.r, radiance.g, radiance.b) * pixelArea * cosTheta;

    return float4(power.x, power.y, power.z, wavelength);
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

    return reflector;
}

void IncomingLightPowerPass::setScene(RenderContext* pRenderContext, const ref<Scene>& pScene)
{
    mpScene = pScene;
    mNeedRecompile = true;
}

void IncomingLightPowerPass::execute(RenderContext* pRenderContext, const RenderData& renderData)
{
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
    if (!pOutputPower || !pOutputWavelength)
    {
        logWarning("IncomingLightPowerPass::execute() - Output texture is missing.");
        return;
    }

    // If disabled, clear output and return
    if (!mEnabled)
    {
        // For floating-point textures, we need to use different clearUAV overloads based on the texture format
        // For RGBA32Float, we use float4
        pRenderContext->clearUAV(pOutputPower->getUAV().get(), float4(0.f, 0.f, 0.f, 0.f));

        // For R32Float, we need to use the uint4 version to match the expected overload
        // This is necessary because the framework has specific overloads for different format types
        pRenderContext->clearUAV(pOutputWavelength->getUAV().get(), uint4(0));
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

    // Execute the compute pass
    mpComputePass->execute(pRenderContext, uint3(mFrameDim.x, mFrameDim.y, 1));

    // Calculate statistics if enabled
    if (mEnableStatistics)
    {
        calculateStatistics(pRenderContext, renderData);
    }
}

void IncomingLightPowerPass::renderUI(Gui::Widgets& widget)
{
    bool changed = false;
    changed |= widget.checkbox("Enabled", mEnabled);

    auto filterGroup = widget.group("Wavelength Filter", true);
    if (filterGroup)
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
                widget.text("Filtered pixels: " + std::to_string(mPowerStats.pixelCount) + " / " +
                            std::to_string(mPowerStats.totalPixels) + " (" +
                            std::to_string(int(passRate)) + "%)");

                widget.text("Total Power (W): R=" + std::to_string(mPowerStats.totalPower[0]) +
                            ", G=" + std::to_string(mPowerStats.totalPower[1]) +
                            ", B=" + std::to_string(mPowerStats.totalPower[2]));

                widget.text("Average Power (W): R=" + std::to_string(mPowerStats.averagePower[0]) +
                            ", G=" + std::to_string(mPowerStats.averagePower[1]) +
                            ", B=" + std::to_string(mPowerStats.averagePower[2]));

                widget.text("Peak Power (W): R=" + std::to_string(mPowerStats.peakPower[0]) +
                            ", G=" + std::to_string(mPowerStats.peakPower[1]) +
                            ", B=" + std::to_string(mPowerStats.peakPower[2]));
            }
            else
            {
                widget.text("No statistics available");
            }

            // Power accumulation options
            statsChanged |= widget.checkbox("Accumulate Power", mAccumulatePower);
            if (mAccumulatePower)
            {
                widget.text("Accumulated frames: " + std::to_string(mAccumulatedFrames));
            }

            // Manual reset button
            if (widget.button("Reset Statistics"))
            {
                resetStatistics();
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
        return;
    }

    // Initialize statistics if needed
    if (!mAccumulatePower || mAccumulatedFrames == 0)
    {
        resetStatistics();
    }

    // Count pixels, accumulate power values
    for (uint32_t i = 0; i < mPowerReadbackBuffer.size(); i++)
    {
        const float4& power = mPowerReadbackBuffer[i];

        // Only process pixels with non-zero power (those that passed the filter)
        if (power.x > 0 || power.y > 0 || power.z > 0)
        {
            mPowerStats.pixelCount++;

            // Accumulate total power
            mPowerStats.totalPower[0] += power.x;
            mPowerStats.totalPower[1] += power.y;
            mPowerStats.totalPower[2] += power.z;

            // Track peak power
            mPowerStats.peakPower[0] = std::max(mPowerStats.peakPower[0], power.x);
            mPowerStats.peakPower[1] = std::max(mPowerStats.peakPower[1], power.y);
            mPowerStats.peakPower[2] = std::max(mPowerStats.peakPower[2], power.z);

            // Track wavelength distribution (bin by 10nm intervals)
            int wavelengthBin = static_cast<int>(power.w / 10.0f);
            mPowerStats.wavelengthDistribution[wavelengthBin]++;
        }
    }

    // Update total pixels count
    mPowerStats.totalPixels = mFrameDim.x * mFrameDim.y;

    // Calculate averages
    if (mPowerStats.pixelCount > 0)
    {
        mPowerStats.averagePower[0] = mPowerStats.totalPower[0] / mPowerStats.pixelCount;
        mPowerStats.averagePower[1] = mPowerStats.totalPower[1] / mPowerStats.pixelCount;
        mPowerStats.averagePower[2] = mPowerStats.totalPower[2] / mPowerStats.pixelCount;
    }

    // Update accumulated frames count
    if (mAccumulatePower)
    {
        mAccumulatedFrames++;
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
        return false;
    }

    // Get dimensions
    uint32_t width = pOutputPower->getWidth();
    uint32_t height = pOutputPower->getHeight();
    uint32_t numPixels = width * height;

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
            logWarning("Failed to read texture data");
            return false;
        }

        // Resize the destination buffers
        mPowerReadbackBuffer.resize(numPixels);
        mWavelengthReadbackBuffer.resize(numPixels);

        // Copy the data to our float4 and float buffers
        // The raw data should be in the correct format: RGBA32Float for power, R32Float for wavelength
        std::memcpy(mPowerReadbackBuffer.data(), powerRawData.data(), powerRawData.size());
        std::memcpy(mWavelengthReadbackBuffer.data(), wavelengthRawData.data(), wavelengthRawData.size());

        return true;
    }
    catch (const std::exception& e)
    {
        logError("Error reading texture data: " + std::string(e.what()));
        return false;
    }
}

void IncomingLightPowerPass::resetStatistics()
{
    // Clear all statistics
    std::memset(mPowerStats.totalPower, 0, sizeof(mPowerStats.totalPower));
    std::memset(mPowerStats.peakPower, 0, sizeof(mPowerStats.peakPower));
    std::memset(mPowerStats.averagePower, 0, sizeof(mPowerStats.averagePower));
    mPowerStats.pixelCount = 0;
    mPowerStats.totalPixels = 0;
    mPowerStats.wavelengthDistribution.clear();

    // Reset frame accumulation
    mAccumulatedFrames = 0;

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
