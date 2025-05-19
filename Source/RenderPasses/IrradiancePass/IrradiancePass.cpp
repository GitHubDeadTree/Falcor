#include "IrradiancePass.h"
#include "RenderGraph/RenderPassStandardFlags.h"

namespace
{
    const std::string kShaderFile = "RenderPasses/IrradiancePass/IrradiancePass.cs.slang";

    // Input/output channels
    const std::string kInputRayInfo = "initialRayInfo";      // Input texture with ray direction and intensity
    const std::string kOutputIrradiance = "irradiance";      // Output texture with irradiance values
    const std::string kOutputIrradianceScalar = "irradianceScalar"; // Output texture with scalar irradiance values

    // Shader constants
    const std::string kPerFrameCB = "PerFrameCB";           // cbuffer name
    const std::string kReverseRayDirection = "gReverseRayDirection";
    const std::string kIntensityScale = "gIntensityScale";
    const std::string kDebugNormalView = "gDebugNormalView"; // Debug view for normal visualization
    const std::string kUseActualNormals = "gUseActualNormals"; // Whether to use actual normals
    const std::string kFixedNormal = "gFixedNormal";         // Fixed normal direction
    const std::string kPassthrough = "gPassthrough";         // When enabled, directly outputs input rayinfo
}

IrradiancePass::IrradiancePass(ref<Device> pDevice, const Properties& props) : RenderPass(pDevice)
{
    // Parse properties
    for (const auto& [key, value] : props)
    {
        if (key == "enabled") mEnabled = value;
        else if (key == "reverseRayDirection") mReverseRayDirection = value;
        else if (key == "intensityScale") mIntensityScale = value;
        else if (key == "debugNormalView") mDebugNormalView = value;
        else if (key == "useActualNormals") mUseActualNormals = value;
        else if (key == "fixedNormal") mFixedNormal = float3(value);
        else if (key == "passthrough") mPassthrough = value;
        else if (key == "computeInterval") mComputeInterval = value;
        else if (key == "frameInterval") mFrameInterval = value;
        else if (key == "useLastResult") mUseLastResult = value;
        else if (key == "computeAverage") mComputeAverage = value;
        else logWarning("Unknown property '{}' in IrradiancePass properties.", key);
    }

    // Create compute pass
    prepareProgram();

    // Create ParallelReduction instance
    mpParallelReduction = std::make_unique<ParallelReduction>(pDevice);

    // Create result buffer (16 bytes, size of float4)
    mpAverageResultBuffer = pDevice->createBuffer(sizeof(float4), ResourceBindFlags::None, MemoryType::ReadBack);
    mpAverageResultBuffer->setName("IrradiancePass::AverageResultBuffer");
}

Properties IrradiancePass::getProperties() const
{
    Properties props;
    props["enabled"] = mEnabled;
    props["reverseRayDirection"] = mReverseRayDirection;
    props["intensityScale"] = mIntensityScale;
    props["debugNormalView"] = mDebugNormalView;
    props["useActualNormals"] = mUseActualNormals;
    props["fixedNormal"] = mFixedNormal;
    props["passthrough"] = mPassthrough;
    props["computeInterval"] = mComputeInterval;
    props["frameInterval"] = mFrameInterval;
    props["useLastResult"] = mUseLastResult;
    props["computeAverage"] = mComputeAverage;
    return props;
}

RenderPassReflection IrradiancePass::reflect(const CompileData& compileData)
{
    RenderPassReflection reflector;

    // Input: Initial ray direction and intensity
    reflector.addInput(kInputRayInfo, "Initial ray direction (xyz) and intensity (w)")
        .bindFlags(ResourceBindFlags::ShaderResource);

    // Add VBuffer input, mark as optional for now
    reflector.addInput("vbuffer", "Visibility buffer for surface identification")
        .bindFlags(ResourceBindFlags::ShaderResource)
        .flags(RenderPassReflection::Field::Flags::Optional);

    // Output: Irradiance (RGB)
    reflector.addOutput(kOutputIrradiance, "Calculated irradiance per pixel")
        .bindFlags(ResourceBindFlags::UnorderedAccess)
        .format(ResourceFormat::RGBA32Float);

    // Output: Scalar Irradiance (单通道)
    reflector.addOutput(kOutputIrradianceScalar, "Calculated scalar irradiance per pixel")
        .bindFlags(ResourceBindFlags::UnorderedAccess)
        .format(ResourceFormat::R32Float);

    return reflector;
}

void IrradiancePass::execute(RenderContext* pRenderContext, const RenderData& renderData)
{
    // Get input texture
    const auto& pInputRayInfo = renderData.getTexture(kInputRayInfo);
    if (!pInputRayInfo)
    {
        logWarning("IrradiancePass::execute() - Input ray info texture is missing. Make sure the PathTracer is outputting initialRayInfo data.");
        return;
    }

    // Get output texture
    const auto& pOutputIrradiance = renderData.getTexture(kOutputIrradiance);
    if (!pOutputIrradiance)
    {
        logWarning("IrradiancePass::execute() - Output irradiance texture is missing.");
        return;
    }

    // Get scalar output texture
    const auto& pOutputScalarIrradiance = renderData.getTexture(kOutputIrradianceScalar);
    if (!pOutputScalarIrradiance)
    {
        logWarning("IrradiancePass::execute() - Output scalar irradiance texture is missing.");
        return;
    }

    // Get VBuffer input (optional at this stage)
    const auto& pVBuffer = renderData.getTexture("vbuffer");
    bool hasVBuffer = pVBuffer != nullptr;

    if (mUseActualNormals)
    {
        if (!hasVBuffer)
        {
            logWarning("IrradiancePass::execute() - VBuffer texture is missing but useActualNormals is enabled. Falling back to fixed normal.");
        }
        else
        {
            logInfo("IrradiancePass::execute() - VBuffer texture is available. Resolution: {}x{}",
                    pVBuffer->getWidth(), pVBuffer->getHeight());
        }
    }

    // If disabled, clear output and return
    if (!mEnabled)
    {
        pRenderContext->clearUAV(pOutputIrradiance->getUAV().get(), float4(0.f));
        pRenderContext->clearUAV(pOutputScalarIrradiance->getUAV().get(), float4(0.f));
        return;
    }

    // Store resolutions for debug display and texture management
    mInputResolution = uint2(pInputRayInfo->getWidth(), pInputRayInfo->getHeight());
    mOutputResolution = uint2(pOutputIrradiance->getWidth(), pOutputIrradiance->getHeight());

    // Check if we should perform computation this frame
    bool computeThisFrame = shouldCompute(pRenderContext);

    // If we should reuse last result, check if we have one and if it matches current output dimensions
    if (!computeThisFrame && mUseLastResult && mpLastIrradianceResult && mpLastIrradianceScalarResult)
    {
        // If dimensions match, copy last result to output
        if (mpLastIrradianceResult->getWidth() == mOutputResolution.x &&
            mpLastIrradianceResult->getHeight() == mOutputResolution.y &&
            mpLastIrradianceScalarResult->getWidth() == mOutputResolution.x &&
            mpLastIrradianceScalarResult->getHeight() == mOutputResolution.y)
        {
            copyLastResultToOutput(pRenderContext, pOutputIrradiance);
            copyLastScalarResultToOutput(pRenderContext, pOutputScalarIrradiance);
            return;
        }
        else
        {
            // Dimensions don't match, we need to recompute
            logInfo("IrradiancePass::execute() - Output dimensions changed, forcing recomputation");
            computeThisFrame = true;
        }
    }
    else if (!computeThisFrame && !mUseLastResult)
    {
        // If we don't want to use last result, just skip processing
        return;
    }
    else if (!computeThisFrame)
    {
        // No last result available but we should skip, clear output
        pRenderContext->clearUAV(pOutputIrradiance->getUAV().get(), float4(0.f));
        pRenderContext->clearUAV(pOutputScalarIrradiance->getUAV().get(), float4(0.f));
        return;
    }

    // If we get here, we're computing this frame
    logInfo("IrradiancePass::execute() - Computing irradiance this frame");

    // Check if program needs to be recompiled
    if (mNeedRecompile)
    {
        prepareProgram();
    }

    // Prepare resources and ensure shader program is updated
    prepareResources(pRenderContext, renderData);

    // Set shader constants
    auto var = mpComputePass->getRootVar();

    var[kPerFrameCB][kReverseRayDirection] = mReverseRayDirection;
    var[kPerFrameCB][kIntensityScale] = mIntensityScale;
    var[kPerFrameCB][kDebugNormalView] = mDebugNormalView;
    var[kPerFrameCB][kPassthrough] = mPassthrough;

    // Add passthrough mode log information
    if (mPassthrough)
    {
        logInfo("IrradiancePass::execute() - Running in PASSTHROUGH mode: directly outputting input rayinfo");
    }

    bool useActualNormals = mUseActualNormals && hasVBuffer && mpScene;

    var[kPerFrameCB][kUseActualNormals] = useActualNormals;
    logInfo("IrradiancePass::execute() - UseActualNormals setting: {} (UI: {}, HasVBuffer: {}, HasScene: {})",
            useActualNormals ? "Enabled" : "Disabled",
            mUseActualNormals ? "True" : "False",
            hasVBuffer ? "True" : "False",
            mpScene ? "True" : "False");

    var[kPerFrameCB][kFixedNormal] = mFixedNormal;

    var["gInputRayInfo"] = pInputRayInfo;
    var["gOutputIrradiance"] = pOutputIrradiance;
    var["gOutputIrradianceScalar"] = pOutputScalarIrradiance;

    // Bind VBuffer if available
    if (hasVBuffer)
    {
        var["gVBuffer"] = pVBuffer;
        logInfo("IrradiancePass::execute() - Successfully bound VBuffer texture to shader.");
    }
    else if (mUseActualNormals)
    {
        logWarning("IrradiancePass::execute() - Cannot use actual normals because VBuffer is not available.");
    }

    // Bind scene data if available and using actual normals
    bool sceneDataBound = false;
    if (useActualNormals)
    {
        if (var.findMember("gScene").isValid())
        {
            mpScene->bindShaderData(var["gScene"]);
            logInfo("IrradiancePass::execute() - Successfully bound scene data to shader for normal extraction.");
            sceneDataBound = true;
        }
        else
        {
            logWarning("IrradiancePass::execute() - Cannot find gScene in shader. Check if USE_ACTUAL_NORMALS is correctly defined.");
        }
    }
    else if (mUseActualNormals && !mpScene)
    {
        logWarning("IrradiancePass::execute() - Cannot use actual normals because Scene is not available.");
    }

    // Update normals extraction status based on all required conditions
    mNormalsSuccessfullyExtracted = useActualNormals && sceneDataBound;

    // Execute compute pass (dispatch based on the output resolution)
    uint32_t width = mOutputResolution.x;
    uint32_t height = mOutputResolution.y;
    logInfo("IrradiancePass::execute() - Dispatching compute with dimensions {}x{}", width, height);
    mpComputePass->execute(pRenderContext, width, height, 1);


    if (mComputeAverage && !mDebugNormalView)
    {
        computeAverageIrradiance(pRenderContext, pOutputScalarIrradiance);
    }

    // Store the result for future frames
    if (mUseLastResult)
    {
        // Create texture if it doesn't exist or if dimensions don't match
        if (!mpLastIrradianceResult ||
            mpLastIrradianceResult->getWidth() != width ||
            mpLastIrradianceResult->getHeight() != height)
        {
            mpLastIrradianceResult = mpDevice->createTexture2D(
                width, height,
                pOutputIrradiance->getFormat(),
                1, 1,
                nullptr,
                ResourceBindFlags::ShaderResource | ResourceBindFlags::UnorderedAccess
            );
        }

        // Create scalar texture if it doesn't exist or if dimensions don't match
        if (!mpLastIrradianceScalarResult ||
            mpLastIrradianceScalarResult->getWidth() != width ||
            mpLastIrradianceScalarResult->getHeight() != height)
        {
            mpLastIrradianceScalarResult = mpDevice->createTexture2D(
                width, height,
                pOutputScalarIrradiance->getFormat(),
                1, 1,
                nullptr,
                ResourceBindFlags::ShaderResource | ResourceBindFlags::UnorderedAccess
            );
        }

        // Copy current result to storage texture
        pRenderContext->copyResource(mpLastIrradianceResult.get(), pOutputIrradiance.get());
        pRenderContext->copyResource(mpLastIrradianceScalarResult.get(), pOutputScalarIrradiance.get());
    }
}

void IrradiancePass::renderUI(Gui::Widgets& widget)
{
    widget.checkbox("Enabled", mEnabled);

    // Add computation interval controls
    widget.separator();
    widget.text("--- Computation Interval ---");

    bool useFrameInterval = mFrameInterval > 0;
    if (widget.checkbox("Use Frame Interval", useFrameInterval))
    {
        if (useFrameInterval && mFrameInterval == 0)
        {
            mFrameInterval = 60; // Default to every 60 frames if switching from time-based
        }
        else if (!useFrameInterval)
        {
            mFrameInterval = 0; // Disable frame interval
        }
    }
    widget.tooltip("When checked, the computation interval is specified in frames.\n"
                  "Otherwise, it's specified in seconds.");

    if (useFrameInterval)
    {
        widget.var("Frame Interval", mFrameInterval, 1u, 1000u, 1u);
        widget.tooltip("Number of frames between computations.\n"
                      "Higher values improve performance but reduce temporal responsiveness.");
    }
    else
    {
        widget.var("Time Interval (s)", mComputeInterval, 0.01f, 10.0f, 0.01f);
        widget.tooltip("Time in seconds between computations.\n"
                      "Higher values improve performance but reduce temporal responsiveness.");
    }

    widget.checkbox("Use Last Result", mUseLastResult);
    widget.tooltip("When enabled, uses the last computed result when skipping computation.\n"
                  "When disabled, the output is unchanged during skipped frames.");

    // Add passthrough mode controls
    widget.separator();
    bool prevPassthrough = mPassthrough;
    widget.checkbox("Passthrough Mode", mPassthrough);
    widget.tooltip("When enabled, directly outputs the input rayinfo texture without any calculations.\n"
                   "Useful for debugging to verify if the problem is in the input data or the calculation.");

    if (prevPassthrough != mPassthrough)
    {
        logInfo("IrradiancePass::renderUI() - Passthrough mode changed to {}. Calculations are {}.",
                mPassthrough ? "enabled" : "disabled",
                mPassthrough ? "bypassed" : "active");
    }


    widget.separator();
    widget.text("--- Average Irradiance ---");
    widget.checkbox("Compute Average", mComputeAverage);
    widget.tooltip("When enabled, computes the average value of the scalar irradiance texture.");


    if (mComputeAverage && !mDebugNormalView)
    {
        std::string avgText = "Average Irradiance: " + std::to_string(mAverageIrradiance);
        widget.text(avgText);
    }
    else if (mDebugNormalView)
    {
        widget.text("Average not available in debug view mode");
    }
    else
    {
        widget.text("Average calculation disabled");
    }

    // Only show other settings in non-passthrough mode
    if (!mPassthrough)
    {
        widget.separator();
        widget.checkbox("Reverse Ray Direction", mReverseRayDirection);
        widget.tooltip("When enabled, inverts the ray direction to calculate irradiance.\n"
                      "This is usually required because ray directions in path tracing typically\n"
                      "point from camera/surface to the light source, but irradiance calculations\n"
                      "need directions pointing toward the receiving surface.");

        widget.var("Intensity Scale", mIntensityScale, 0.0f, 10.0f, 0.1f);
        widget.tooltip("Scaling factor applied to the calculated irradiance value.");

        widget.checkbox("Debug Normal View", mDebugNormalView);
        widget.tooltip("When enabled, visualizes the normal directions as colors for debugging.");

        // Save previous useActualNormals value to detect changes
        bool prevUseActualNormals = mUseActualNormals;
        widget.checkbox("Use Actual Normals", mUseActualNormals);
        widget.tooltip("When enabled, uses the actual surface normals from VBuffer and Scene data\n"
                      "instead of assuming a fixed normal direction.\n"
                      "This provides accurate irradiance calculation on curved surfaces.\n"
                      "Requires a valid VBuffer input and Scene connection.");


        if (mUseActualNormals)
        {

            bool success = mNormalsSuccessfullyExtracted;
            std::string statusText = success ?
                "Actual Normals Status: ACTIVE (using real surface normals)" :
                "Actual Normals Status: INACTIVE (using fixed normal)";

            widget.text(statusText, success);

            if (!success)
            {
                widget.tooltip("Normal extraction is not active. Possible causes:\n"
                              "1. VBuffer is not available (check connections)\n"
                              "2. Scene data is not available (check scene loading)\n"
                              "3. Shader compilation issues with USE_ACTUAL_NORMALS\n\n"
                              "The pass is currently using the fixed normal instead.");
            }
            else
            {
                widget.tooltip("Normal extraction is active.\n"
                              "The pass is using actual surface normals from the geometry.\n"
                              "You can verify this by enabling 'Debug Normal View'.");
            }
        }

        // Check if shader recompilation is needed
        if (prevUseActualNormals != mUseActualNormals)
        {
            logInfo("IrradiancePass::renderUI() - Use Actual Normals changed to {}. Marking for shader recompilation.",
                    mUseActualNormals ? "enabled" : "disabled");
            mNeedRecompile = true;
        }

        if (!mUseActualNormals)
        {
            widget.var("Fixed Normal", mFixedNormal, -1.0f, 1.0f, 0.01f);
            widget.tooltip("The fixed normal direction to use when not using actual normals.");
        }
    }

    // Display resolution information
    widget.separator();
    widget.text("--- Resolution Information ---");

    // Input resolution
    if (mInputResolution.x > 0 && mInputResolution.y > 0)
    {
        std::string inputResText = "Input Resolution: " + std::to_string(mInputResolution.x) + " x " + std::to_string(mInputResolution.y);
        widget.text(inputResText);
        uint32_t inputPixels = mInputResolution.x * mInputResolution.y;
        std::string inputPixelCountText = "Input Pixels: " + std::to_string(inputPixels);
        widget.text(inputPixelCountText);
    }
    else
    {
        widget.text("Input Resolution: Not available");
    }

    // Output resolution
    if (mOutputResolution.x > 0 && mOutputResolution.y > 0)
    {
        std::string outputResText = "Output Resolution: " + std::to_string(mOutputResolution.x) + " x " + std::to_string(mOutputResolution.y);
        widget.text(outputResText);
        uint32_t outputPixels = mOutputResolution.x * mOutputResolution.y;
        std::string outputPixelCountText = "Output Pixels: " + std::to_string(outputPixels);
        widget.text(outputPixelCountText);
    }
    else
    {
        widget.text("Output Resolution: Not available");
    }

    // Check if resolutions match
    if (mInputResolution.x > 0 && mInputResolution.y > 0 &&
        mOutputResolution.x > 0 && mOutputResolution.y > 0)
    {
        bool resolutionsMatch = (mInputResolution.x == mOutputResolution.x) &&
                               (mInputResolution.y == mOutputResolution.y);

        if (resolutionsMatch)
        {
            widget.text("Resolution Status: Input and Output match", true);
        }
        else
        {
            widget.text("Resolution Status: Input and Output DO NOT match", false);
            widget.tooltip("Different input and output resolutions may cause scaling or sampling issues.");
        }
    }
}

void IrradiancePass::setScene(RenderContext* pRenderContext, const ref<Scene>& pScene)
{
    // Store scene reference
    mpScene = pScene;

    // Log scene status
    if (pScene)
    {
        logInfo("IrradiancePass::setScene() - Scene set successfully.");
    }
    else
    {
        logWarning("IrradiancePass::setScene() - Null scene provided.");
    }

    // Mark for program recompilation since scene may provide shader modules
    mNeedRecompile = true;
}

void IrradiancePass::prepareProgram()
{
    // Create program description
    ProgramDesc desc;
    desc.addShaderLibrary(kShaderFile).csEntry("main");

    // Create shader defines
    DefineList defines;

    // 确保正确设置USE_ACTUAL_NORMALS宏
    defines.add("USE_ACTUAL_NORMALS", mUseActualNormals ? "1" : "0");

    // Add scene shader modules and defines if available and we need them
    if (mpScene && mUseActualNormals)
    {
        // Get scene defines for proper shader compilation
        defines.add(mpScene->getSceneDefines());

        // Add scene shader modules
        desc.addShaderModules(mpScene->getShaderModules());
        desc.addTypeConformances(mpScene->getTypeConformances());

        logInfo("IrradiancePass::prepareProgram() - Added scene defines and shader modules for normal extraction.");

        auto sceneDefines = mpScene->getSceneDefines();
        if (sceneDefines.find("SCENE_GEOMETRY_TYPES") != sceneDefines.end())
        {
            logInfo("IrradiancePass::prepareProgram() - SCENE_GEOMETRY_TYPES = {}", sceneDefines["SCENE_GEOMETRY_TYPES"]);
        }
    }

    try
    {
        // Create the compute pass with the defines
        mpComputePass = ComputePass::create(mpDevice, desc, defines);
        logInfo("IrradiancePass::prepareProgram() - Successfully created compute program.");

        // Reset recompile flag
        mNeedRecompile = false;
    }
    catch (const Exception& e)
    {
        logError("IrradiancePass::prepareProgram() - Error creating compute program: {}", e.what());
    }
}

void IrradiancePass::prepareResources(RenderContext* pRenderContext, const RenderData& renderData)
{
    // Nothing to prepare as resources are passed directly in execute()
}

bool IrradiancePass::shouldCompute(RenderContext* pRenderContext)
{
    // Always compute on the first frame
    if (mFrameCount == 0)
    {
        mTimeSinceLastCompute = 0.0f;
        mFrameCount = 1;
        return true;
    }

    // Increment counters
    mFrameCount++;

    // Get time between frames - we'll use a fixed estimate of 16.7ms (60 FPS)
    // since RenderContext doesn't have a direct way to get frame time
    float frameTime = 0.0167f; // 16.7ms = ~60 FPS
    mTimeSinceLastCompute += frameTime;

    // Check if we should compute based on specified interval
    if (mFrameInterval > 0)
    {
        // Using frame-based interval
        bool shouldCompute = ((mFrameCount - 1) % mFrameInterval) == 0;
        if (shouldCompute)
        {
            logInfo("IrradiancePass::shouldCompute() - Computing on frame {} (every {} frames)",
                    mFrameCount, mFrameInterval);
        }
        return shouldCompute;
    }
    else
    {
        // Using time-based interval
        bool shouldCompute = mTimeSinceLastCompute >= mComputeInterval;
        if (shouldCompute)
        {
            logInfo("IrradiancePass::shouldCompute() - Computing after {} seconds (interval: {} seconds)",
                   mTimeSinceLastCompute, mComputeInterval);
            mTimeSinceLastCompute = 0.0f;
        }
        return shouldCompute;
    }
}

void IrradiancePass::copyLastResultToOutput(RenderContext* pRenderContext, const ref<Texture>& pOutputIrradiance)
{
    if (!mpLastIrradianceResult)
    {
        logWarning("IrradiancePass::copyLastResultToOutput() - No last result available");
        return;
    }

    logInfo("IrradiancePass::copyLastResultToOutput() - Reusing last computed result");
    pRenderContext->copyResource(pOutputIrradiance.get(), mpLastIrradianceResult.get());
}

void IrradiancePass::copyLastScalarResultToOutput(RenderContext* pRenderContext, const ref<Texture>& pOutputScalarIrradiance)
{
    if (!mpLastIrradianceScalarResult)
    {
        logWarning("IrradiancePass::copyLastScalarResultToOutput() - No last scalar result available");
        return;
    }

    logInfo("IrradiancePass::copyLastScalarResultToOutput() - Reusing last computed scalar result");
    pRenderContext->copyResource(pOutputScalarIrradiance.get(), mpLastIrradianceScalarResult.get());
}

void IrradiancePass::computeAverageIrradiance(RenderContext* pRenderContext, const ref<Texture>& pTexture)
{
    if (!mpParallelReduction || !pTexture)
    {
        logWarning("IrradiancePass::computeAverageIrradiance() - ParallelReduction or texture is missing");
        return;
    }

    try
    {
        // Execute parallel reduction (sum)
        mpParallelReduction->execute<float4>(
            pRenderContext,
            pTexture,
            ParallelReduction::Type::Sum,
            nullptr,  // Don't directly read results to avoid GPU sync wait
            mpAverageResultBuffer,
            0
        );

        // Wait for computation to complete (submit and sync)
        pRenderContext->submit(true);

        float4 sum;
        mpAverageResultBuffer->getBlob(&sum, 0, sizeof(float4));

        // Calculate average (total divided by pixel count)
        const uint32_t pixelCount = pTexture->getWidth() * pTexture->getHeight();
        if (pixelCount > 0)
        {
            mAverageIrradiance = sum.x / pixelCount;
            logInfo("IrradiancePass::computeAverageIrradiance() - Average irradiance: {}", mAverageIrradiance);
        }
    }
    catch (const std::exception& e)
    {
        logError("IrradiancePass::computeAverageIrradiance() - Error calculating average: {}", e.what());
    }
}

extern "C" FALCOR_API_EXPORT void registerPlugin(Falcor::PluginRegistry& registry)
{
    registry.registerClass<RenderPass, IrradiancePass>();
}
