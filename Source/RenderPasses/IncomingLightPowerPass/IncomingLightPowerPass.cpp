#include "IncomingLightPowerPass.h"
#include <algorithm>

namespace
{
    const std::string kShaderFile = "RenderPasses/IncomingLightPowerPass/IncomingLightPowerPass.cs.slang";

    // Input/output channels
    const std::string kInputRadiance = "radiance";               // Input texture with radiance values (from path tracer)
    const std::string kInputRayDirection = "rayDirection";       // Input texture with ray direction (if available)
    const std::string kInputWavelength = "wavelength";           // Input texture with wavelength information
    const std::string kOutputPower = "lightPower";               // Output texture with light power values
    const std::string kOutputWavelength = "lightWavelength";     // Output texture with filtered wavelength values

    // Shader constants
    const std::string kPerFrameCB = "PerFrameCB";           // cbuffer name
    const std::string kMinWavelength = "gMinWavelength";    // Minimum wavelength to consider
    const std::string kMaxWavelength = "gMaxWavelength";    // Maximum wavelength to consider
}

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

float4 IncomingLightPowerPass::CameraIncidentPower::compute(
    const uint2& pixel,
    const float3& rayDir,
    const float4& radiance,
    float wavelength,
    float minWavelength,
    float maxWavelength) const
{
    // Filter by wavelength range
    if (wavelength < minWavelength || wavelength > maxWavelength)
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
        else if (key == "outputPowerTexName") mOutputPowerTexName = value.operator std::string();
        else if (key == "outputWavelengthTexName") mOutputWavelengthTexName = value.operator std::string();
        else logWarning("Unknown property '{}' in IncomingLightPowerPass properties.", key);
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
    }

    // Prepare resources
    prepareResources(pRenderContext, renderData);

    // Get shader variables
    auto var = mpComputePass->getRootVar();

    // Set constants
    var[kPerFrameCB][kMinWavelength] = mMinWavelength;
    var[kPerFrameCB][kMaxWavelength] = mMaxWavelength;

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

    // Bind output resources
    var["gOutputPower"] = pOutputPower;
    var["gOutputWavelength"] = pOutputWavelength;

    // Bind scene for camera information
    if (mpScene)
    {
        mpScene->bindShaderData(var["gScene"]);
    }

    // Execute the compute pass
    mpComputePass->execute(pRenderContext, uint3(mFrameDim.x, mFrameDim.y, 1));
}

void IncomingLightPowerPass::renderUI(Gui::Widgets& widget)
{
    bool changed = false;

    changed |= widget.checkbox("Enabled", mEnabled);

    auto group = widget.group("Wavelength Filter", true);
    if (group)
    {
        changed |= widget.slider("Min Wavelength (nm)", mMinWavelength, 380.f, 780.f);
        changed |= widget.slider("Max Wavelength (nm)", mMaxWavelength, 380.f, 780.f);
    }

    if (changed)
    {
        mNeedRecompile = true;
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
    defines.add("WAVELENGTH_FILTER", "1");

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
