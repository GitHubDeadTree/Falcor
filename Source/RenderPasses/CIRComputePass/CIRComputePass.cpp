/***************************************************************************
 # Copyright (c) 2015-23, NVIDIA CORPORATION. All rights reserved.
 #
 # Redistribution and use in source and binary forms, with or without
 # modification, are permitted provided that the following conditions
 # are met:
 #  * Redistributions of source code must retain the above copyright
 #    notice, this list of conditions and the following disclaimer.
 #  * Redistributions in binary form must reproduce the above copyright
 #    notice, this list of conditions and the following disclaimer in the
 #    documentation and/or other materials provided with the distribution.
 #  * Neither the name of NVIDIA CORPORATION nor the names of its
 #    contributors may be used to endorse or promote products derived
 #    from this software without specific prior written permission.
 #
 # THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS "AS IS" AND ANY
 # EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 # IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 # PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 # CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 # EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 # PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 # PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 # OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 # (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 # OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 **************************************************************************/
#include "CIRComputePass.h"
#include "RenderGraph/RenderPassStandardFlags.h"
#include <fstream>
#include <iomanip>
#include <cmath>
#include <algorithm>

namespace
{
    const std::string kShaderFile = "RenderPasses/CIRComputePass/CIRComputePass.cs.slang";

    // Input channels
    const std::string kInputPathData = "cirData";  // Input path data from PathTracer (matches PathTracer output)

    // Output channels
    const std::string kOutputCIR = "cir";          // Output CIR vector

    // Shader constants
    const std::string kPerFrameCB = "PerFrameCB";
    const std::string kTimeResolution = "gTimeResolution";
    const std::string kMaxDelay = "gMaxDelay";
    const std::string kCIRBins = "gCIRBins";
    const std::string kLEDPower = "gLEDPower";
    const std::string kHalfPowerAngle = "gHalfPowerAngle";
    const std::string kReceiverArea = "gReceiverArea";
    const std::string kFieldOfView = "gFieldOfView";
    const std::string kLambertianOrder = "gLambertianOrder";
    const std::string kPathCount = "gPathCount";
}

extern "C" FALCOR_API_EXPORT void registerPlugin(Falcor::PluginRegistry& registry)
{
    registry.registerClass<RenderPass, CIRComputePass>();
}

CIRComputePass::CIRComputePass(ref<Device> pDevice, const Properties& props) : RenderPass(pDevice)
{
    logInfo("CIR: Initializing CIRComputePass...");

    // Parse properties from input parameters
    parseProperties(props);

    // Validate all parameters and set defaults if necessary
    validateParameters();

    // Log initial parameter status
    logParameterStatus();

    // Create CIR result buffer
    createCIRBuffer();

    // Prepare compute pass for CIR calculation
    prepareComputePass();

    logInfo("CIR: CIRComputePass initialization complete");
}

Properties CIRComputePass::getProperties() const
{
    Properties props;
    props["timeResolution"] = mTimeResolution;
    props["maxDelay"] = mMaxDelay;
    props["cirBins"] = mCIRBins;
    props["ledPower"] = mLEDPower;
    props["halfPowerAngle"] = mHalfPowerAngle;
    props["receiverArea"] = mReceiverArea;
    props["fieldOfView"] = mFieldOfView;
    return props;
}

RenderPassReflection CIRComputePass::reflect(const CompileData& compileData)
{
    RenderPassReflection reflector;

    // Task 2.3: Input - Path data buffer from PathTracer using correct rawBuffer API
    reflector.addInput(kInputPathData, "Path data buffer from PathTracer for CIR calculation")
        .bindFlags(ResourceBindFlags::ShaderResource)
        .rawBuffer(0)  // Size will be determined by input
        .flags(RenderPassReflection::Field::Flags::Optional);

    // Task 2.3: Output - CIR buffer for atomic operations using rawBuffer instead of format
    reflector.addOutput(kOutputCIR, "CIR buffer for atomic accumulation")
        .bindFlags(ResourceBindFlags::UnorderedAccess)
        .rawBuffer(mCIRBins * sizeof(uint32_t));  // Specify exact buffer size

    return reflector;
}

void CIRComputePass::compile(RenderContext* pRenderContext, const CompileData& compileData)
{
    logInfo("CIR: Compiling CIRComputePass...");

    // Recreate compute pass if needed
    if (mNeedRecompile)
    {
        prepareComputePass();
        mNeedRecompile = false;
    }

    logInfo("CIR: CIRComputePass compilation complete");
}

void CIRComputePass::execute(RenderContext* pRenderContext, const RenderData& renderData)
{
    // Increment frame counter for debugging
    mFrameCount++;

    // Log execution status periodically
    if (mFrameCount % 100 == 0)
    {
        logInfo("CIR: Executing CIRComputePass frame {}", mFrameCount);
        logParameterStatus();
        logBufferStatus();
    }

    // Get input path data buffer from PathTracer
    auto pInputResource = renderData.getResource(kInputPathData);
    ref<Buffer> pInputPathData = nullptr;
    if (pInputResource)
    {
        pInputPathData = pInputResource->asBuffer();
    }
    
    if (!pInputPathData)
    {
        if (mFrameCount % 1000 == 0)  // Log warning less frequently
        {
            logWarning("CIR: Input path data buffer is missing. Make sure PathTracer is outputting path data.");
        }
        return;
    }

    // Task 2.3: Get output CIR buffer using correct Falcor API
    const auto& pResource = renderData.getResource(kOutputCIR);
    ref<Buffer> pOutputCIR = nullptr;
    if (pResource)
    {
        pOutputCIR = pResource->asBuffer();
    }
    
    if (!pOutputCIR)
    {
        logWarning("CIR: Output CIR buffer is missing.");
        return;
    }

    // Verify compute pass is ready
    if (!mpComputePass)
    {
        logError("CIR: Compute pass is not initialized.");
        return;
    }

    // Task 2.3: Clear output buffer and overflow counter
    pRenderContext->clearUAV(pOutputCIR->getUAV().get(), uint4(0));  // Clear as uint
    if (mpOverflowCounter)
    {
        pRenderContext->clearUAV(mpOverflowCounter->getUAV().get(), uint4(0));
    }

    // Calculate number of paths to process
    uint32_t pathCount = 0;
    if (pInputPathData)
    {
        pathCount = pInputPathData->getElementCount();
    }

    // Set shader variables through cbuffer
    auto var = mpComputePass->getRootVar();
    auto cbuffer = var[kPerFrameCB];
    cbuffer[kTimeResolution] = mTimeResolution;
    cbuffer[kMaxDelay] = mMaxDelay;
    cbuffer[kCIRBins] = mCIRBins;
    cbuffer[kLEDPower] = mLEDPower;
    cbuffer[kHalfPowerAngle] = mHalfPowerAngle;
    cbuffer[kReceiverArea] = mReceiverArea;
    cbuffer[kFieldOfView] = mFieldOfView;
    cbuffer[kPathCount] = pathCount;

    // Calculate Lambertian order: m = -ln(2)/ln(cos(half_power_angle))
    float lambertianOrder = -logf(2.0f) / logf(cosf(mHalfPowerAngle));
    cbuffer[kLambertianOrder] = lambertianOrder;

    // Bind input path data buffer and output buffers
    var["gPathDataBuffer"] = pInputPathData;
    var["gOutputCIR"] = pOutputCIR;
    if (mpOverflowCounter)
    {
        var["gOverflowCounter"] = mpOverflowCounter;
    }

    // Dispatch compute shader - process each path
    if (pathCount > 0)
    {
        uint32_t numThreadGroups = (pathCount + 255) / 256;  // 256 threads per group
        mpComputePass->execute(pRenderContext, numThreadGroups, 1, 1);
    }

    // Task 2.3: Read back overflow counter for validation (every 1000 frames)
    if (mFrameCount % 1000 == 0 && mpOverflowCounter)
    {
        validateCIRResults(pRenderContext, pOutputCIR, pathCount);
    }

    // Task 2.4: Complete CIR result output (every 5000 frames for full analysis)
    if (mFrameCount % 5000 == 0 && pathCount > 0)
    {
        outputCIRResults(pRenderContext, pOutputCIR, pathCount);
    }

    // Log execution details for debugging (less frequent)
    if (mFrameCount % 1000 == 0)
    {
        logInfo("CIR: Processing {} paths in {} thread groups", pathCount, pathCount > 0 ? (pathCount + 255) / 256 : 0);
        logInfo("CIR: Lambertian order calculated as {:.3f}", lambertianOrder);
        logInfo("CIR: CIR bins: {}, Time resolution: {:.2e}s", mCIRBins, mTimeResolution);
    }
}

void CIRComputePass::renderUI(Gui::Widgets& widget)
{
    // CIR Computation Parameters section
    if (widget.group("CIR Computation Parameters", true))
    {
        bool parametersChanged = false;

        // Time resolution
        float timeResNs = mTimeResolution * 1e9f;  // Convert to nanoseconds for UI
        if (widget.var("Time Resolution (ns)", timeResNs, 0.001f, 1000.0f, 0.001f))
        {
            setTimeResolution(timeResNs * 1e-9f);
            parametersChanged = true;
        }
        widget.tooltip("Time resolution for CIR discretization in nanoseconds");

        // Maximum delay
        float maxDelayUs = mMaxDelay * 1e6f;  // Convert to microseconds for UI
        if (widget.var("Max Delay (μs)", maxDelayUs, 0.001f, 1000.0f, 0.1f))
        {
            setMaxDelay(maxDelayUs * 1e-6f);
            parametersChanged = true;
        }
        widget.tooltip("Maximum propagation delay for CIR calculation in microseconds");

        // CIR bins
        int cirBins = static_cast<int>(mCIRBins);
        if (widget.var("CIR Bins", cirBins, static_cast<int>(kMinCIRBins), static_cast<int>(kMaxCIRBins)))
        {
            setCIRBins(static_cast<uint32_t>(cirBins));
            parametersChanged = true;
        }
        widget.tooltip("Number of bins in the CIR vector");

        if (parametersChanged)
        {
            logInfo("CIR: Parameters changed via UI, recompiling shaders");
            mNeedRecompile = true;
        }
    }

    // LED Parameters section
    if (widget.group("LED Parameters", true))
    {
        bool ledChanged = false;

        // LED power
        if (widget.var("LED Power (W)", mLEDPower, kMinLEDPower, kMaxLEDPower, 0.001f))
        {
            if (!validateLEDPower(mLEDPower))
            {
                mLEDPower = 1.0f;  // Reset to default
            }
            ledChanged = true;
        }
        widget.tooltip("LED power in watts");

        // Half power angle
        float halfPowerDeg = mHalfPowerAngle * 180.0f / 3.14159f;  // Convert to degrees for UI
        if (widget.var("Half Power Angle (deg)", halfPowerDeg, 1.0f, 90.0f, 0.1f))
        {
            setHalfPowerAngle(halfPowerDeg * 3.14159f / 180.0f);
            ledChanged = true;
        }
        widget.tooltip("LED half power angle in degrees");

        if (ledChanged)
        {
            logInfo("CIR: LED parameters changed via UI");
        }
    }

    // Receiver Parameters section
    if (widget.group("Receiver Parameters", true))
    {
        bool receiverChanged = false;

        // Receiver area
        float areaSquareCm = mReceiverArea * 10000.0f;  // Convert to cm^2 for UI
        if (widget.var("Receiver Area (cm²)", areaSquareCm, 0.01f, 10000.0f, 0.01f))
        {
            setReceiverArea(areaSquareCm * 1e-4f);
            receiverChanged = true;
        }
        widget.tooltip("Receiver area in square centimeters");

        // Field of view
        float fovDeg = mFieldOfView * 180.0f / 3.14159f;  // Convert to degrees for UI
        if (widget.var("Field of View (deg)", fovDeg, 1.0f, 180.0f, 0.1f))
        {
            setFieldOfView(fovDeg * 3.14159f / 180.0f);
            receiverChanged = true;
        }
        widget.tooltip("Receiver field of view in degrees");

        if (receiverChanged)
        {
            logInfo("CIR: Receiver parameters changed via UI");
        }
    }

    // Status Information section
    if (widget.group("Status Information", false))
    {
        widget.text("Frame Count: " + std::to_string(mFrameCount));
        widget.text("Buffer Status: " + std::string(mpCIRBuffer ? "Allocated" : "Not Allocated"));
        widget.text("Compute Pass: " + std::string(mpComputePass ? "Ready" : "Not Ready"));

        if (mpCIRBuffer)
        {
            float bufferSizeMB = static_cast<float>(mpCIRBuffer->getSize()) / (1024.0f * 1024.0f);
            widget.text("Buffer Size: " + std::to_string(bufferSizeMB) + " MB");
        }

        // Calculate and display Lambertian order
        float lambertianOrder = -logf(2.0f) / logf(cosf(mHalfPowerAngle));
        widget.text("Lambertian Order: " + std::to_string(lambertianOrder));
    }
}

// Parameter setter functions with validation
void CIRComputePass::setTimeResolution(float resolution)
{
    if (validateTimeResolution(resolution))
    {
        mTimeResolution = resolution;
        mNeedRecompile = true;
        logInfo("CIR: Time resolution set to {:.2e} seconds", resolution);
    }
    else
    {
        logError("CIR: Invalid time resolution {:.2e}, keeping current value {:.2e}", resolution, mTimeResolution);
    }
}

void CIRComputePass::setMaxDelay(float delay)
{
    if (validateMaxDelay(delay))
    {
        mMaxDelay = delay;
        // Update CIR bins if necessary to maintain reasonable time resolution
        uint32_t requiredBins = static_cast<uint32_t>(ceil(delay / mTimeResolution));
        if (requiredBins > mCIRBins)
        {
            logInfo("CIR: Adjusting CIR bins from {} to {} to accommodate new max delay", mCIRBins, requiredBins);
            setCIRBins(requiredBins);
        }
        logInfo("CIR: Max delay set to {:.2e} seconds", delay);
    }
    else
    {
        logError("CIR: Invalid max delay {:.2e}, keeping current value {:.2e}", delay, mMaxDelay);
    }
}

void CIRComputePass::setCIRBins(uint32_t bins)
{
    if (validateCIRBins(bins))
    {
        mCIRBins = bins;
        mNeedRecompile = true;
        createCIRBuffer();  // Recreate buffer with new size
        logInfo("CIR: CIR bins set to {}", bins);
    }
    else
    {
        logError("CIR: Invalid CIR bins {}, keeping current value {}", bins, mCIRBins);
    }
}

void CIRComputePass::setLEDPower(float power)
{
    if (validateLEDPower(power))
    {
        mLEDPower = power;
        logInfo("CIR: LED power set to {:.3f} watts", power);
    }
    else
    {
        logError("CIR: Invalid LED power {:.3f}, keeping current value {:.3f}", power, mLEDPower);
    }
}

void CIRComputePass::setHalfPowerAngle(float angle)
{
    if (validateHalfPowerAngle(angle))
    {
        mHalfPowerAngle = angle;
        logInfo("CIR: Half power angle set to {:.3f} radians ({:.1f} degrees)", angle, angle * 180.0f / 3.14159f);
    }
    else
    {
        logError("CIR: Invalid half power angle {:.3f}, keeping current value {:.3f}", angle, mHalfPowerAngle);
    }
}

void CIRComputePass::setReceiverArea(float area)
{
    if (validateReceiverArea(area))
    {
        mReceiverArea = area;
        logInfo("CIR: Receiver area set to {:.2e} m² ({:.2f} cm²)", area, area * 10000.0f);
    }
    else
    {
        logError("CIR: Invalid receiver area {:.2e}, keeping current value {:.2e}", area, mReceiverArea);
    }
}

void CIRComputePass::setFieldOfView(float fov)
{
    if (validateFieldOfView(fov))
    {
        mFieldOfView = fov;
        logInfo("CIR: Field of view set to {:.3f} radians ({:.1f} degrees)", fov, fov * 180.0f / 3.14159f);
    }
    else
    {
        logError("CIR: Invalid field of view {:.3f}, keeping current value {:.3f}", fov, mFieldOfView);
    }
}

// Private helper functions
void CIRComputePass::parseProperties(const Properties& props)
{
    logInfo("CIR: Parsing input properties...");

    for (const auto& [key, value] : props)
    {
        if (key == "timeResolution") 
        {
            float resolution = value;
            if (validateTimeResolution(resolution)) mTimeResolution = resolution;
        }
        else if (key == "maxDelay") 
        {
            float delay = value;
            if (validateMaxDelay(delay)) mMaxDelay = delay;
        }
        else if (key == "cirBins") 
        {
            uint32_t bins = value;
            if (validateCIRBins(bins)) mCIRBins = bins;
        }
        else if (key == "ledPower") 
        {
            float power = value;
            if (validateLEDPower(power)) mLEDPower = power;
        }
        else if (key == "halfPowerAngle") 
        {
            float angle = value;
            if (validateHalfPowerAngle(angle)) mHalfPowerAngle = angle;
        }
        else if (key == "receiverArea") 
        {
            float area = value;
            if (validateReceiverArea(area)) mReceiverArea = area;
        }
        else if (key == "fieldOfView") 
        {
            float fov = value;
            if (validateFieldOfView(fov)) mFieldOfView = fov;
        }
        else 
        {
            logWarning("CIR: Unknown property '{}' in CIRComputePass properties.", key);
        }
    }

    logInfo("CIR: Property parsing complete");
}

void CIRComputePass::validateParameters()
{
    logInfo("CIR: Validating all parameters...");

    bool hasErrors = false;

    if (!validateTimeResolution(mTimeResolution))
    {
        logError("CIR: Invalid time resolution detected during validation");
        hasErrors = true;
    }

    if (!validateMaxDelay(mMaxDelay))
    {
        logError("CIR: Invalid max delay detected during validation");
        hasErrors = true;
    }

    if (!validateCIRBins(mCIRBins))
    {
        logError("CIR: Invalid CIR bins detected during validation");
        hasErrors = true;
    }

    if (!validateLEDPower(mLEDPower))
    {
        logError("CIR: Invalid LED power detected during validation");
        hasErrors = true;
    }

    if (!validateHalfPowerAngle(mHalfPowerAngle))
    {
        logError("CIR: Invalid half power angle detected during validation");
        hasErrors = true;
    }

    if (!validateReceiverArea(mReceiverArea))
    {
        logError("CIR: Invalid receiver area detected during validation");
        hasErrors = true;
    }

    if (!validateFieldOfView(mFieldOfView))
    {
        logError("CIR: Invalid field of view detected during validation");
        hasErrors = true;
    }

    if (hasErrors)
    {
        logError("CIR: Parameter validation failed, setting default values");
        setDefaultParametersOnError();
    }
    else
    {
        logInfo("CIR: All parameters validated successfully");
    }
}

void CIRComputePass::createCIRBuffer()
{
    logInfo("CIR: Creating CIR result buffer...");

    try
    {
        // Task 2.3: Calculate buffer size for uint format (atomic operations)
        size_t bufferSize = mCIRBins * sizeof(uint32_t);  // Changed to uint32_t for atomic ops
        logInfo("CIR: Buffer size: {} bytes ({:.2f} MB)", bufferSize, static_cast<float>(bufferSize) / (1024.0f * 1024.0f));

        // Task 2.3: Create buffer using correct Falcor API
        mpCIRBuffer = mpDevice->createBuffer(
            bufferSize,
            ResourceBindFlags::ShaderResource | ResourceBindFlags::UnorderedAccess,
            MemoryType::DeviceLocal
        );

        if (!mpCIRBuffer)
        {
            logError("CIR: Failed to create CIR buffer");
            return;
        }

        mpCIRBuffer->setName("CIRComputePass::CIRBuffer");
        
        // Create overflow counter buffer (single uint32)
        mpOverflowCounter = mpDevice->createBuffer(
            sizeof(uint32_t),
            ResourceBindFlags::ShaderResource | ResourceBindFlags::UnorderedAccess,
            MemoryType::DeviceLocal
        );
        
        if (mpOverflowCounter)
        {
            mpOverflowCounter->setName("CIRComputePass::OverflowCounter");
            logInfo("CIR: Overflow counter buffer created successfully");
        }
        else
        {
            logWarning("CIR: Failed to create overflow counter buffer");
        }
        
        logInfo("CIR: CIR buffer created successfully");
        logBufferStatus();
    }
    catch (const std::exception& e)
    {
        logError("CIR: Exception during buffer creation: {}", e.what());
        mpCIRBuffer = nullptr;
    }
}

void CIRComputePass::prepareComputePass()
{
    logInfo("CIR: Preparing compute pass...");

    try
    {
        // Create compute pass
        mpComputePass = ComputePass::create(mpDevice, kShaderFile, "main");

        if (!mpComputePass)
        {
            logError("CIR: Failed to create compute pass");
            return;
        }

        logInfo("CIR: Compute pass created successfully");
    }
    catch (const std::exception& e)
    {
        logError("CIR: Exception during compute pass creation: {}", e.what());
        mpComputePass = nullptr;
    }
}

// Validation functions
bool CIRComputePass::validateTimeResolution(float resolution)
{
    bool valid = (resolution >= kMinTimeResolution && resolution <= kMaxTimeResolution);
    if (!valid)
    {
        logError("CIR: Time resolution {:.2e} is outside valid range [{:.2e}, {:.2e}]", 
                resolution, kMinTimeResolution, kMaxTimeResolution);
    }
    return valid;
}

bool CIRComputePass::validateMaxDelay(float delay)
{
    bool valid = (delay >= kMinMaxDelay && delay <= kMaxMaxDelay);
    if (!valid)
    {
        logError("CIR: Max delay {:.2e} is outside valid range [{:.2e}, {:.2e}]", 
                delay, kMinMaxDelay, kMaxMaxDelay);
    }
    return valid;
}

bool CIRComputePass::validateCIRBins(uint32_t bins)
{
    bool valid = (bins >= kMinCIRBins && bins <= kMaxCIRBins);
    if (!valid)
    {
        logError("CIR: CIR bins {} is outside valid range [{}, {}]", 
                bins, kMinCIRBins, kMaxCIRBins);
    }
    return valid;
}

bool CIRComputePass::validateLEDPower(float power)
{
    bool valid = (power >= kMinLEDPower && power <= kMaxLEDPower && !std::isnan(power) && !std::isinf(power));
    if (!valid)
    {
        logError("CIR: LED power {:.3f} is outside valid range [{:.2e}, {:.1f}] or is NaN/Inf", 
                power, kMinLEDPower, kMaxLEDPower);
    }
    return valid;
}

bool CIRComputePass::validateHalfPowerAngle(float angle)
{
    bool valid = (angle >= kMinHalfPowerAngle && angle <= kMaxHalfPowerAngle && !std::isnan(angle) && !std::isinf(angle));
    if (!valid)
    {
        logError("CIR: Half power angle {:.3f} is outside valid range [{:.3f}, {:.3f}] or is NaN/Inf", 
                angle, kMinHalfPowerAngle, kMaxHalfPowerAngle);
    }
    return valid;
}

bool CIRComputePass::validateReceiverArea(float area)
{
    bool valid = (area >= kMinReceiverArea && area <= kMaxReceiverArea && !std::isnan(area) && !std::isinf(area));
    if (!valid)
    {
        logError("CIR: Receiver area {:.2e} is outside valid range [{:.2e}, {:.1f}] or is NaN/Inf", 
                area, kMinReceiverArea, kMaxReceiverArea);
    }
    return valid;
}

bool CIRComputePass::validateFieldOfView(float fov)
{
    bool valid = (fov >= kMinFieldOfView && fov <= kMaxFieldOfView && !std::isnan(fov) && !std::isinf(fov));
    if (!valid)
    {
        logError("CIR: Field of view {:.3f} is outside valid range [{:.3f}, {:.3f}] or is NaN/Inf", 
                fov, kMinFieldOfView, kMaxFieldOfView);
    }
    return valid;
}

void CIRComputePass::logParameterStatus()
{
    logInfo("=== CIR Parameter Status ===");
    logInfo("CIR: Time resolution: {:.2e} seconds ({:.3f} ns)", mTimeResolution, mTimeResolution * 1e9f);
    logInfo("CIR: Max delay: {:.2e} seconds ({:.3f} μs)", mMaxDelay, mMaxDelay * 1e6f);
    logInfo("CIR: CIR bins: {}", mCIRBins);
    logInfo("CIR: LED power: {:.3f} watts", mLEDPower);
    logInfo("CIR: Half power angle: {:.3f} radians ({:.1f} degrees)", mHalfPowerAngle, mHalfPowerAngle * 180.0f / 3.14159f);
    logInfo("CIR: Receiver area: {:.2e} m² ({:.2f} cm²)", mReceiverArea, mReceiverArea * 10000.0f);
    logInfo("CIR: Field of view: {:.3f} radians ({:.1f} degrees)", mFieldOfView, mFieldOfView * 180.0f / 3.14159f);
    
    float lambertianOrder = -logf(2.0f) / logf(cosf(mHalfPowerAngle));
    logInfo("CIR: Calculated Lambertian order: {:.3f}", lambertianOrder);
    logInfo("===========================");
}

void CIRComputePass::logBufferStatus()
{
    logInfo("=== CIR Buffer Status ===");
    logInfo("CIR: Buffer allocated: {}", mpCIRBuffer ? "Yes" : "No");
    
    if (mpCIRBuffer)
    {
        logInfo("CIR: Buffer size: {} bytes ({:.2f} MB)", mpCIRBuffer->getSize(), 
               static_cast<float>(mpCIRBuffer->getSize()) / (1024.0f * 1024.0f));
        logInfo("CIR: Buffer element count: {}", mCIRBins);
        logInfo("CIR: Buffer element size: {} bytes", sizeof(uint32_t));  // Task 2.3: Updated for uint format
    }
    
    logInfo("CIR: Overflow counter allocated: {}", mpOverflowCounter ? "Yes" : "No");
    if (mpOverflowCounter)
    {
        logInfo("CIR: Overflow counter size: {} bytes", mpOverflowCounter->getSize());
    }
    
    logInfo("CIR: Compute pass ready: {}", mpComputePass ? "Yes" : "No");
    logInfo("========================");
}

void CIRComputePass::setDefaultParametersOnError()
{
    logInfo("CIR: Setting default parameters due to validation errors...");

    mTimeResolution = 1e-9f;     // 1 nanosecond
    mMaxDelay = 1e-6f;           // 1 microsecond
    mCIRBins = 1000;             // 1000 bins
    mLEDPower = 1.0f;            // 1 watt
    mHalfPowerAngle = 0.5236f;   // 30 degrees
    mReceiverArea = 1e-4f;       // 1 cm²
    mFieldOfView = 1.047f;       // 60 degrees

    logInfo("CIR: Default parameters set successfully");
    logParameterStatus();
}

// Task 2.3: CIR validation and statistics functions
void CIRComputePass::validateCIRResults(RenderContext* pRenderContext, const ref<Buffer>& pCIRBuffer, uint32_t pathCount)
{
    if (!pCIRBuffer || !mpOverflowCounter)
    {
        return;
    }

    try
    {
        // Read back overflow counter for diagnostics
        readAndLogOverflowCount(pRenderContext, pathCount);

        // Read back CIR data for validation (sample-based to avoid performance impact)
        uint32_t sampleSize = std::min(100u, mCIRBins);  // Sample first 100 bins or all if fewer
        std::vector<uint32_t> cirUintData(sampleSize);
        
        // Task 2.3: Create ReadBack buffer for CPU access using correct Falcor API
        size_t readbackSize = sampleSize * sizeof(uint32_t);
        ref<Buffer> pReadbackBuffer = mpDevice->createBuffer(
            readbackSize,
            ResourceBindFlags::None,
            MemoryType::ReadBack
        );

        if (pReadbackBuffer)
        {
            // Copy data from GPU buffer to ReadBack buffer
            pRenderContext->copyResource(pReadbackBuffer.get(), pCIRBuffer.get());
            
            // Map ReadBack buffer using correct API (no parameters needed)
            const uint32_t* pData = static_cast<const uint32_t*>(pReadbackBuffer->map());
            if (pData)
            {
                std::memcpy(cirUintData.data(), pData, readbackSize);
                pReadbackBuffer->unmap();

                // Convert uint data back to float for validation
                std::vector<float> cirData(sampleSize);
                for (uint32_t i = 0; i < sampleSize; ++i)
                {
                    cirData[i] = *reinterpret_cast<const float*>(&cirUintData[i]);
                }

                // Log CIR statistics
                logCIRStatistics(cirData, pathCount);
            }
            else
            {
                logWarning("CIR: Failed to map ReadBack buffer for validation");
            }
        }
        else
        {
            logWarning("CIR: Failed to create ReadBack buffer for validation");
        }
    }
    catch (const std::exception& e)
    {
        logError("CIR: Exception during CIR validation: {}", e.what());
    }
}

void CIRComputePass::readAndLogOverflowCount(RenderContext* pRenderContext, uint32_t pathCount)
{
    if (!mpOverflowCounter)
    {
        return;
    }

    try
    {
        // Task 2.3: Create ReadBack buffer for overflow counter using correct API
        ref<Buffer> pOverflowReadback = mpDevice->createBuffer(
            sizeof(uint32_t),
            ResourceBindFlags::None,
            MemoryType::ReadBack
        );

        if (pOverflowReadback)
        {
            // Copy overflow counter data to ReadBack buffer
            pRenderContext->copyResource(pOverflowReadback.get(), mpOverflowCounter.get());
            
            // Map overflow counter ReadBack buffer (no parameters needed)
            const uint32_t* pOverflowData = static_cast<const uint32_t*>(pOverflowReadback->map());
            if (pOverflowData)
            {
                uint32_t overflowCount = *pOverflowData;
                pOverflowReadback->unmap();

            // Log overflow statistics
            if (overflowCount > 0)
            {
                float overflowPercent = pathCount > 0 ? (static_cast<float>(overflowCount) / pathCount * 100.0f) : 0.0f;
                
                if (overflowPercent > 10.0f)
                {
                    logWarning("CIR: High overflow rate - {} paths ({:.2f}%) exceeded time range of {:.2e}s. Consider increasing maxDelay or reducing timeResolution.", 
                              overflowCount, overflowPercent, mMaxDelay);
                }
                else
                {
                    logInfo("CIR: Overflow count: {} paths ({:.2f}%) exceeded time range", overflowCount, overflowPercent);
                }
            }
            else
            {
                logInfo("CIR: No overflow detected - all {} paths within time range", pathCount);
            }
            }
            else
            {
                logWarning("CIR: Failed to map overflow counter ReadBack buffer");
            }
        }
        else
        {
            logWarning("CIR: Failed to create overflow counter ReadBack buffer");
        }
    }
    catch (const std::exception& e)
    {
        logError("CIR: Exception during overflow count reading: {}", e.what());
    }
}

void CIRComputePass::logCIRStatistics(const std::vector<float>& cirData, uint32_t pathCount)
{
    if (cirData.empty())
    {
        logInfo("CIR: No CIR data available for statistics");
        return;
    }

    // Calculate basic statistics
    float totalPower = 0.0f;
    float maxPower = 0.0f;
    uint32_t nonZeroBins = 0;
    uint32_t validBins = 0;
    uint32_t invalidBins = 0;

    for (uint32_t i = 0; i < cirData.size(); ++i)
    {
        float power = cirData[i];
        
        // Check for valid values
        if (std::isnan(power) || std::isinf(power))
        {
            invalidBins++;
            continue;
        }
        
        validBins++;
        
        if (power > 0.0f)
        {
            nonZeroBins++;
            totalPower += power;
            maxPower = std::max(maxPower, power);
        }
    }

    // Log detailed statistics
    logInfo("=== CIR Validation Statistics (Sample) ===");
    logInfo("CIR: Total paths processed: {}", pathCount);
    logInfo("CIR: Sampled bins: {} / {}", cirData.size(), mCIRBins);
    logInfo("CIR: Valid bins: {} ({:.1f}%)", validBins, validBins * 100.0f / cirData.size());
    logInfo("CIR: Invalid bins (NaN/Inf): {} ({:.1f}%)", invalidBins, invalidBins * 100.0f / cirData.size());
    logInfo("CIR: Non-zero bins: {} ({:.1f}%)", nonZeroBins, nonZeroBins * 100.0f / cirData.size());
    logInfo("CIR: Total power (sampled): {:.6e} W", totalPower);
    logInfo("CIR: Max power (sampled): {:.6e} W", maxPower);
    
    if (nonZeroBins > 0)
    {
        float avgPower = totalPower / nonZeroBins;
        logInfo("CIR: Average power per active bin: {:.6e} W", avgPower);
        
        // Find peak bin for delay statistics
        auto maxIt = std::max_element(cirData.begin(), cirData.end());
        if (maxIt != cirData.end())
        {
            uint32_t peakBin = static_cast<uint32_t>(std::distance(cirData.begin(), maxIt));
            float peakDelay = peakBin * mTimeResolution;
            logInfo("CIR: Peak power at bin {} (delay: {:.3f} ns)", peakBin, peakDelay * 1e9f);
        }
    }
    
    // Validation warnings
    if (invalidBins > 0)
    {
        logWarning("CIR: Found {} invalid values in CIR data - check computation stability", invalidBins);
    }
    
    if (nonZeroBins == 0)
    {
        logWarning("CIR: No non-zero CIR values found - check path data and computation parameters");
    }
    else if (nonZeroBins < cirData.size() / 10)
    {
        logInfo("CIR: Sparse CIR detected - only {:.1f}% of bins contain data", nonZeroBins * 100.0f / cirData.size());
    }
    
    logInfo("==========================================");
}

// ===========================================================================================
// Task 2.4: CIR Result Output and Visualization Implementation
// ===========================================================================================

void CIRComputePass::outputCIRResults(RenderContext* pRenderContext, const ref<Buffer>& pCIRBuffer, uint32_t pathCount)
{
    if (!pCIRBuffer)
    {
        logWarning("CIR: Cannot output results - CIR buffer is null");
        return;
    }

    if (pathCount == 0)
    {
        logInfo("CIR: No paths processed - skipping output");
        return;
    }

    logInfo("CIR: Starting CIR result output for {} paths...", pathCount);

    try
    {
        // Read complete CIR data from GPU buffer
        std::vector<float> cirData = readFullCIRData(pRenderContext, pCIRBuffer);
        
        if (cirData.empty())
        {
            logError("CIR: Failed to read CIR data from GPU buffer");
            return;
        }

        // Validate and clean CIR data
        validateAndCleanCIRData(cirData);

        // Compute detailed statistics
        float totalPower = 0.0f;
        uint32_t nonZeroBins = 0;
        float maxPower = 0.0f;
        uint32_t peakBin = 0;
        
        computeCIRStatistics(cirData, pathCount, totalPower, nonZeroBins, maxPower, peakBin);

        // Output detailed validation information
        logInfo("=== CIR Output Results ===");
        logInfo("CIR: Frame: {}, Paths processed: {}", mFrameCount, pathCount);
        logInfo("CIR: Total bins: {}, Non-zero bins: {} ({:.2f}%)", 
                mCIRBins, nonZeroBins, (nonZeroBins * 100.0f) / mCIRBins);
        logInfo("CIR: Total power: {:.6e} W", totalPower);
        logInfo("CIR: Max power: {:.6e} W at bin {} (delay: {:.3f} ns)", 
                maxPower, peakBin, peakBin * mTimeResolution * 1e9f);

        // Validate total power is reasonable
        if (totalPower <= 0.0f)
        {
            logWarning("CIR: Total power is zero or negative - check path data and computation");
        }
        else if (totalPower > mLEDPower * 10.0f)
        {
            logWarning("CIR: Total power {:.6e}W exceeds 10x LED power {:.3f}W - possible computation error", 
                      totalPower, mLEDPower);
        }

        // Save CIR data to file
        std::string filename = "cir_frame_" + std::to_string(mFrameCount) + ".csv";
        saveCIRToFile(cirData, filename);

        // Update visualization data
        updateVisualization(cirData);

        logInfo("CIR: Result output complete");
        logInfo("==========================");
    }
    catch (const std::exception& e)
    {
        logError("CIR: Exception during result output: {}", e.what());
    }
}

std::vector<float> CIRComputePass::readFullCIRData(RenderContext* pRenderContext, const ref<Buffer>& pCIRBuffer)
{
    std::vector<float> cirData;

    if (!pCIRBuffer)
    {
        logError("CIR: Cannot read data - CIR buffer is null");
        return cirData;
    }

    try
    {
        // Create ReadBack buffer for full CIR data
        size_t bufferSize = mCIRBins * sizeof(uint32_t);
        ref<Buffer> pReadbackBuffer = mpDevice->createBuffer(
            bufferSize,
            ResourceBindFlags::None,
            MemoryType::ReadBack
        );

        if (!pReadbackBuffer)
        {
            logError("CIR: Failed to create ReadBack buffer for full CIR data");
            return cirData;
        }

        // Copy CIR data from GPU to ReadBack buffer
        pRenderContext->copyResource(pReadbackBuffer.get(), pCIRBuffer.get());

        // Map and read data
        const uint32_t* pData = static_cast<const uint32_t*>(pReadbackBuffer->map());
        if (!pData)
        {
            logError("CIR: Failed to map ReadBack buffer");
            return cirData;
        }

        // Convert uint32 atomic data back to float
        cirData.resize(mCIRBins);
        for (uint32_t i = 0; i < mCIRBins; ++i)
        {
            cirData[i] = *reinterpret_cast<const float*>(&pData[i]);
        }

        pReadbackBuffer->unmap();
        
        logInfo("CIR: Successfully read {} bins of CIR data from GPU", mCIRBins);
    }
    catch (const std::exception& e)
    {
        logError("CIR: Exception during CIR data read: {}", e.what());
        cirData.clear();
    }

    return cirData;
}

void CIRComputePass::validateAndCleanCIRData(std::vector<float>& cirData)
{
    if (cirData.empty())
    {
        logWarning("CIR: Cannot validate empty CIR data");
        return;
    }

    uint32_t invalidCount = 0;
    uint32_t negativeCount = 0;
    uint32_t cleanedCount = 0;

    for (uint32_t i = 0; i < cirData.size(); ++i)
    {
        float& value = cirData[i];

        // Check for NaN or infinity
        if (std::isnan(value) || std::isinf(value))
        {
            invalidCount++;
            value = 0.0f;
            cleanedCount++;
        }
        // Check for negative values (should not happen in power calculations)
        else if (value < 0.0f)
        {
            negativeCount++;
            value = 0.0f;
            cleanedCount++;
        }
        // Check for extremely large values that might indicate computational errors
        else if (value > mLEDPower * 100.0f)
        {
            logWarning("CIR: Bin {} has unusually large value {:.6e}W (>100x LED power), clamping to zero", i, value);
            value = 0.0f;
            cleanedCount++;
        }
    }

    // Log validation results
    if (cleanedCount > 0)
    {
        logWarning("CIR: Data validation cleaned {} values:", cleanedCount);
        if (invalidCount > 0)
            logWarning("  - {} NaN/Infinity values", invalidCount);
        if (negativeCount > 0)
            logWarning("  - {} negative values", negativeCount);
    }
    else
    {
        logInfo("CIR: Data validation passed - all {} values are valid", cirData.size());
    }
}

void CIRComputePass::computeCIRStatistics(const std::vector<float>& cirData, uint32_t pathCount, 
                                         float& totalPower, uint32_t& nonZeroBins, float& maxPower, uint32_t& peakBin)
{
    totalPower = 0.0f;
    nonZeroBins = 0;
    maxPower = 0.0f;
    peakBin = 0;

    if (cirData.empty())
    {
        logWarning("CIR: Cannot compute statistics for empty CIR data");
        return;
    }

    // Compute basic statistics
    for (uint32_t i = 0; i < cirData.size(); ++i)
    {
        float power = cirData[i];
        
        if (power > 0.0f)
        {
            nonZeroBins++;
            totalPower += power;
            
            if (power > maxPower)
            {
                maxPower = power;
                peakBin = i;
            }
        }
    }

    // Compute additional statistics for analysis
    float avgPower = nonZeroBins > 0 ? totalPower / nonZeroBins : 0.0f;
    float peakDelay = peakBin * mTimeResolution;
    float delaySpread = 0.0f;

    // Calculate RMS delay spread
    float meanDelay = 0.0f;
    if (totalPower > 0.0f)
    {
        for (uint32_t i = 0; i < cirData.size(); ++i)
        {
            if (cirData[i] > 0.0f)
            {
                float delay = i * mTimeResolution;
                meanDelay += delay * cirData[i] / totalPower;
            }
        }

        for (uint32_t i = 0; i < cirData.size(); ++i)
        {
            if (cirData[i] > 0.0f)
            {
                float delay = i * mTimeResolution;
                float diff = delay - meanDelay;
                delaySpread += diff * diff * cirData[i] / totalPower;
            }
        }
        delaySpread = sqrtf(delaySpread);
    }

    // Log detailed statistics
    logInfo("CIR: Detailed Statistics:");
    logInfo("  - Average power per active bin: {:.6e} W", avgPower);
    logInfo("  - Peak delay: {:.3f} ns", peakDelay * 1e9f);
    logInfo("  - Mean delay: {:.3f} ns", meanDelay * 1e9f);
    logInfo("  - RMS delay spread: {:.3f} ns", delaySpread * 1e9f);
}

void CIRComputePass::saveCIRToFile(const std::vector<float>& cirData, const std::string& filename)
{
    if (cirData.empty())
    {
        logError("CIR: Cannot save empty CIR data to file");
        return;
    }

    try
    {
        std::ofstream file(filename);
        if (!file.is_open())
        {
            logError("CIR: Failed to open file for CIR output: {}", filename);
            return;
        }

        // Write CSV header
        file << "Time_ns,Power_W,Delay_s,Bin_Index\n";

        // Write CIR data with detailed information
        uint32_t nonZeroCount = 0;
        for (uint32_t i = 0; i < cirData.size(); ++i)
        {
            float timeNs = i * mTimeResolution * 1e9f;
            float delaySec = i * mTimeResolution;
            float power = cirData[i];

            // Always write header and non-zero values, also write some zero samples for completeness
            if (power > 0.0f || i % 100 == 0 || i < 10 || i >= cirData.size() - 10)
            {
                file << std::fixed << std::setprecision(6) << timeNs << ","
                     << std::scientific << std::setprecision(6) << power << ","
                     << std::scientific << std::setprecision(6) << delaySec << ","
                     << i << "\n";
                
                if (power > 0.0f) nonZeroCount++;
            }
        }

        file.close();

        logInfo("CIR: Data saved to '{}' ({} total bins, {} non-zero values)", 
                filename, cirData.size(), nonZeroCount);
    }
    catch (const std::exception& e)
    {
        logError("CIR: Exception during file save: {}", e.what());
    }
}

void CIRComputePass::updateVisualization(const std::vector<float>& cirData)
{
    if (cirData.empty())
    {
        logInfo("CIR: No data available for visualization update");
        return;
    }

    try
    {
        // Find peak value and its location for visualization scaling
        float maxPower = 0.0f;
        uint32_t peakBin = 0;
        uint32_t nonZeroBins = 0;
        
        for (uint32_t i = 0; i < cirData.size(); ++i)
        {
            if (cirData[i] > maxPower)
            {
                maxPower = cirData[i];
                peakBin = i;
            }
            if (cirData[i] > 0.0f) nonZeroBins++;
        }

        // Create visualization data summary (for potential GUI display)
        struct CIRVisualizationData
        {
            uint32_t totalBins;
            uint32_t activeBins;
            float maxPower;
            uint32_t peakBin;
            float peakDelay;
            std::vector<float> normalizedData;
        };

        CIRVisualizationData vizData;
        vizData.totalBins = static_cast<uint32_t>(cirData.size());
        vizData.activeBins = nonZeroBins;
        vizData.maxPower = maxPower;
        vizData.peakBin = peakBin;
        vizData.peakDelay = peakBin * mTimeResolution;

        // Create normalized data for visualization (scale to 0-1 range)
        vizData.normalizedData.resize(cirData.size());
        if (maxPower > 0.0f)
        {
            for (uint32_t i = 0; i < cirData.size(); ++i)
            {
                vizData.normalizedData[i] = cirData[i] / maxPower;
            }
        }
        else
        {
            std::fill(vizData.normalizedData.begin(), vizData.normalizedData.end(), 0.0f);
        }

        // Log visualization summary
        logInfo("CIR: Visualization update complete:");
        logInfo("  - Total bins: {}, Active bins: {} ({:.2f}%)", 
                vizData.totalBins, vizData.activeBins, 
                (vizData.activeBins * 100.0f) / vizData.totalBins);
        logInfo("  - Peak: {:.6e}W at {:.3f}ns (bin {})", 
                vizData.maxPower, vizData.peakDelay * 1e9f, vizData.peakBin);

        // Note: In a full implementation, this data would be passed to a GUI system
        // For now, we just log the summary as this is a compute-only pass
    }
    catch (const std::exception& e)
    {
        logError("CIR: Exception during visualization update: {}", e.what());
    }
} 