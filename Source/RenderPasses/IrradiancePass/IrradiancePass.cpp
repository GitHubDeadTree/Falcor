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
    return props;
}

RenderPassReflection IrradiancePass::reflect(const CompileData& compileData)
{
    RenderPassReflection reflector;

    // Input: Initial ray direction and intensity
    reflector.addInput(kInputRayInfo, "Initial ray direction (xyz) and intensity (w)").bindFlags(ResourceBindFlags::ShaderResource);

    // Output: Irradiance
    reflector.addOutput(kOutputIrradiance, "Calculated irradiance per pixel").bindFlags(ResourceBindFlags::UnorderedAccess).format(ResourceFormat::RGBA32Float);

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

    // If disabled, clear output and return
    if (!mEnabled)
    {
        pRenderContext->clearUAV(renderData.getTexture(kOutputIrradiance)->getUAV().get(), float4(0.f));
        return;
    }

    // Prepare resources and ensure shader program is updated
    prepareResources(pRenderContext, renderData);

    // Set shader constants
    auto var = mpComputePass->getRootVar();

    // 通过cbuffer名称路径访问变量
    var[kPerFrameCB][kReverseRayDirection] = mReverseRayDirection;
    var[kPerFrameCB][kIntensityScale] = mIntensityScale;
    var[kPerFrameCB][kDebugNormalView] = mDebugNormalView;
    var[kPerFrameCB][kUseActualNormals] = mUseActualNormals;
    var[kPerFrameCB][kFixedNormal] = mFixedNormal;

    // 全局纹理资源绑定保持不变
    var["gInputRayInfo"] = pInputRayInfo;
    var["gOutputIrradiance"] = renderData.getTexture(kOutputIrradiance);

    // Execute compute pass
    uint32_t width = pInputRayInfo->getWidth();
    uint32_t height = pInputRayInfo->getHeight();
    mpComputePass->execute(pRenderContext, { (width + 15) / 16, (height + 15) / 16, 1 });
}

void IrradiancePass::renderUI(Gui::Widgets& widget)
{
    widget.checkbox("Enabled", mEnabled);
    widget.checkbox("Reverse Ray Direction", mReverseRayDirection);
    widget.tooltip("When enabled, inverts the ray direction to calculate irradiance.\n"
                   "This is usually required because ray directions in path tracing typically\n"
                   "point from camera/surface to the light source, but irradiance calculations\n"
                   "need directions pointing toward the receiving surface.");

    widget.var("Intensity Scale", mIntensityScale, 0.0f, 10.0f, 0.1f);
    widget.tooltip("Scaling factor applied to the calculated irradiance value.");

    widget.checkbox("Debug Normal View", mDebugNormalView);
    widget.tooltip("When enabled, visualizes the normal directions as colors for debugging.");

    widget.checkbox("Use Actual Normals", mUseActualNormals);
    widget.tooltip("When enabled, uses the actual surface normals from VBuffer\n"
                  "instead of assuming a fixed normal direction.");

    if (!mUseActualNormals)
    {
        // 只有在使用固定法线时显示方向控制
        widget.var("Fixed Normal", mFixedNormal, -1.0f, 1.0f, 0.01f);
        widget.tooltip("The fixed normal direction to use when not using actual normals.");
    }
}

void IrradiancePass::setScene(RenderContext* pRenderContext, const ref<Scene>& pScene) {}

void IrradiancePass::prepareProgram()
{
    // Create compute pass
    ProgramDesc desc;
    desc.addShaderLibrary(kShaderFile).csEntry("main");
    mpComputePass = ComputePass::create(mpDevice, desc);
}

void IrradiancePass::prepareResources(RenderContext* pRenderContext, const RenderData& renderData)
{
    // Nothing to prepare as resources are passed directly in execute()
}

extern "C" FALCOR_API_EXPORT void registerPlugin(Falcor::PluginRegistry& registry)
{
    registry.registerClass<RenderPass, IrradiancePass>();
}
