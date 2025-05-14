#include "IrradiancePass.h"
#include "RenderGraph/RenderPassStandardFlags.h"

namespace
{
    const std::string kShaderFile = "RenderPasses/IrradiancePass/IrradiancePass.cs.slang";

    // Input/output channels
    const std::string kInputRayInfo = "initialRayInfo";      // Input texture with ray direction and intensity
    const std::string kOutputIrradiance = "irradiance";      // Output texture with irradiance values

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
        else logWarning("Unknown property '{}' in IrradiancePass properties.", key);
    }

    // Create compute pass
    prepareProgram();
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

    // Output: Irradiance
    reflector.addOutput(kOutputIrradiance, "Calculated irradiance per pixel")
        .bindFlags(ResourceBindFlags::UnorderedAccess)
        .format(ResourceFormat::RGBA32Float);

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
        return;
    }

    // Check if program needs to be recompiled
    if (mNeedRecompile)
    {
        prepareProgram();
    }

    // Store input and output resolutions for debug display
    mInputResolution = uint2(pInputRayInfo->getWidth(), pInputRayInfo->getHeight());
    mOutputResolution = uint2(pOutputIrradiance->getWidth(), pOutputIrradiance->getHeight());

    // Log resolutions to help with debugging
    logInfo("IrradiancePass - Input Resolution: {}x{}", mInputResolution.x, mInputResolution.y);
    logInfo("IrradiancePass - Output Resolution: {}x{}", mOutputResolution.x, mOutputResolution.y);

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
    if (useActualNormals)
    {
        if (var.findMember("gScene").isValid())
        {
            mpScene->bindShaderData(var["gScene"]);
            logInfo("IrradiancePass::execute() - Successfully bound scene data to shader for normal extraction.");
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

    // Execute compute pass (dispatch based on the output resolution)
    uint32_t width = mOutputResolution.x;
    uint32_t height = mOutputResolution.y;
    logInfo("IrradiancePass::execute() - Dispatching compute with dimensions {}x{}", width, height);
    mpComputePass->execute(pRenderContext, width, height, 1);
}

void IrradiancePass::renderUI(Gui::Widgets& widget)
{
    widget.checkbox("Enabled", mEnabled);

    // Add passthrough mode controls
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

    // Only show other settings in non-passthrough mode
    if (!mPassthrough)
    {
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

extern "C" FALCOR_API_EXPORT void registerPlugin(Falcor::PluginRegistry& registry)
{
    registry.registerClass<RenderPass, IrradiancePass>();
}
