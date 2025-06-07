/***************************************************************************
 # Copyright (c) 2015-24, NVIDIA CORPORATION. All rights reserved.
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
#include "PathTracer.h"
#include "RenderGraph/RenderPassHelpers.h"
#include "RenderGraph/RenderPassStandardFlags.h"
#include "Rendering/Lights/EmissiveUniformSampler.h"

// CIR debugging support
#include <filesystem>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <ctime>

namespace
{
    const std::string kGeneratePathsFilename = "RenderPasses/PathTracer/GeneratePaths.cs.slang";
    const std::string kTracePassFilename = "RenderPasses/PathTracer/TracePass.rt.slang";
    const std::string kResolvePassFilename = "RenderPasses/PathTracer/ResolvePass.cs.slang";
    const std::string kReflectTypesFile = "RenderPasses/PathTracer/ReflectTypes.cs.slang";

    // Render pass inputs and outputs.
    const std::string kInputVBuffer = "vbuffer";
    const std::string kInputMotionVectors = "mvec";
    const std::string kInputViewDir = "viewW";
    const std::string kInputSampleCount = "sampleCount";

    const Falcor::ChannelList kInputChannels =
    {
        { kInputVBuffer,        "gVBuffer",         "Visibility buffer in packed format" },
        { kInputMotionVectors,  "gMotionVectors",   "Motion vector buffer (float format)", true /* optional */ },
        { kInputViewDir,        "gViewW",           "World-space view direction (xyz float format)", true /* optional */ },
        { kInputSampleCount,    "gSampleCount",     "Sample count buffer (integer format)", true /* optional */, ResourceFormat::R8Uint },
    };

    const std::string kOutputColor = "color";
    const std::string kOutputAlbedo = "albedo";
    const std::string kOutputSpecularAlbedo = "specularAlbedo";
    const std::string kOutputIndirectAlbedo = "indirectAlbedo";
    const std::string kOutputGuideNormal = "guideNormal";
    const std::string kOutputReflectionPosW = "reflectionPosW";
    const std::string kOutputRayCount = "rayCount";
    const std::string kOutputPathLength = "pathLength";
    const std::string kOutputInitialRayInfo = "initialRayInfo"; // 新增: 初始光线信息输出通道
    const std::string kOutputNRDDiffuseRadianceHitDist = "nrdDiffuseRadianceHitDist";
    const std::string kOutputNRDSpecularRadianceHitDist = "nrdSpecularRadianceHitDist";
    const std::string kOutputNRDEmission = "nrdEmission";
    const std::string kOutputNRDDiffuseReflectance = "nrdDiffuseReflectance";
    const std::string kOutputNRDSpecularReflectance = "nrdSpecularReflectance";
    const std::string kOutputNRDDeltaReflectionRadianceHitDist = "nrdDeltaReflectionRadianceHitDist";
    const std::string kOutputNRDDeltaReflectionReflectance = "nrdDeltaReflectionReflectance";
    const std::string kOutputNRDDeltaReflectionEmission = "nrdDeltaReflectionEmission";
    const std::string kOutputNRDDeltaReflectionNormWRoughMaterialID = "nrdDeltaReflectionNormWRoughMaterialID";
    const std::string kOutputNRDDeltaReflectionPathLength = "nrdDeltaReflectionPathLength";
    const std::string kOutputNRDDeltaReflectionHitDist = "nrdDeltaReflectionHitDist";
    const std::string kOutputNRDDeltaTransmissionRadianceHitDist = "nrdDeltaTransmissionRadianceHitDist";
    const std::string kOutputNRDDeltaTransmissionReflectance = "nrdDeltaTransmissionReflectance";
    const std::string kOutputNRDDeltaTransmissionEmission = "nrdDeltaTransmissionEmission";
    const std::string kOutputNRDDeltaTransmissionNormWRoughMaterialID = "nrdDeltaTransmissionNormWRoughMaterialID";
    const std::string kOutputNRDDeltaTransmissionPathLength = "nrdDeltaTransmissionPathLength";
    const std::string kOutputNRDDeltaTransmissionPosW = "nrdDeltaTransmissionPosW";
    const std::string kOutputNRDResidualRadianceHitDist = "nrdResidualRadianceHitDist";

    const Falcor::ChannelList kOutputChannels =
    {
        { kOutputColor,                                     "",     "Output color (linear)", true /* optional */, ResourceFormat::RGBA32Float },
        { kOutputAlbedo,                                    "",     "Output albedo (linear)", true /* optional */, ResourceFormat::RGBA8Unorm },
        { kOutputSpecularAlbedo,                            "",     "Output specular albedo (linear)", true /* optional */, ResourceFormat::RGBA8Unorm },
        { kOutputIndirectAlbedo,                            "",     "Output indirect albedo (linear)", true /* optional */, ResourceFormat::RGBA8Unorm },
        { kOutputGuideNormal,                               "",     "Output guide normal (linear)", true /* optional */, ResourceFormat::RGBA16Float },
        { kOutputReflectionPosW,                            "",     "Output reflection pos (world space)", true /* optional */, ResourceFormat::RGBA32Float },
        { kOutputRayCount,                                  "",     "Per-pixel ray count", true /* optional */, ResourceFormat::R32Uint },
        { kOutputPathLength,                                "",     "Per-pixel path length", true /* optional */, ResourceFormat::R32Uint },
        { kOutputInitialRayInfo,                            "",     "Initial ray direction and intensity", true /* optional */, ResourceFormat::RGBA32Float }, // 新增: 初始光线信息输出通道
        // NRD outputs
        { kOutputNRDDiffuseRadianceHitDist,                 "",     "Output demodulated diffuse color (linear) and hit distance", true /* optional */, ResourceFormat::RGBA32Float },
        { kOutputNRDSpecularRadianceHitDist,                "",     "Output demodulated specular color (linear) and hit distance", true /* optional */, ResourceFormat::RGBA32Float },
        { kOutputNRDEmission,                               "",     "Output primary surface emission", true /* optional */, ResourceFormat::RGBA32Float },
        { kOutputNRDDiffuseReflectance,                     "",     "Output primary surface diffuse reflectance", true /* optional */, ResourceFormat::RGBA16Float },
        { kOutputNRDSpecularReflectance,                    "",     "Output primary surface specular reflectance", true /* optional */, ResourceFormat::RGBA16Float },
        { kOutputNRDDeltaReflectionRadianceHitDist,         "",     "Output demodulated delta reflection color (linear)", true /* optional */, ResourceFormat::RGBA32Float },
        { kOutputNRDDeltaReflectionReflectance,             "",     "Output delta reflection reflectance color (linear)", true /* optional */, ResourceFormat::RGBA16Float },
        { kOutputNRDDeltaReflectionEmission,                "",     "Output delta reflection emission color (linear)", true /* optional */, ResourceFormat::RGBA32Float },
        { kOutputNRDDeltaReflectionNormWRoughMaterialID,    "",     "Output delta reflection world normal, roughness, and material ID", true /* optional */, ResourceFormat::RGB10A2Unorm },
        { kOutputNRDDeltaReflectionPathLength,              "",     "Output delta reflection path length", true /* optional */, ResourceFormat::R16Float },
        { kOutputNRDDeltaReflectionHitDist,                 "",     "Output delta reflection hit distance", true /* optional */, ResourceFormat::R16Float },
        { kOutputNRDDeltaTransmissionRadianceHitDist,       "",     "Output demodulated delta transmission color (linear)", true /* optional */, ResourceFormat::RGBA32Float },
        { kOutputNRDDeltaTransmissionReflectance,           "",     "Output delta transmission reflectance color (linear)", true /* optional */, ResourceFormat::RGBA16Float },
        { kOutputNRDDeltaTransmissionEmission,              "",     "Output delta transmission emission color (linear)", true /* optional */, ResourceFormat::RGBA32Float },
        { kOutputNRDDeltaTransmissionNormWRoughMaterialID,  "",     "Output delta transmission world normal, roughness, and material ID", true /* optional */, ResourceFormat::RGB10A2Unorm },
        { kOutputNRDDeltaTransmissionPathLength,            "",     "Output delta transmission path length", true /* optional */, ResourceFormat::R16Float },
        { kOutputNRDDeltaTransmissionPosW,                  "",     "Output delta transmission position", true /* optional */, ResourceFormat::RGBA32Float },
        { kOutputNRDResidualRadianceHitDist,                "",     "Output residual color (linear) and hit distance", true /* optional */, ResourceFormat::RGBA32Float },
    };

    // Scripting options.
    const std::string kSamplesPerPixel = "samplesPerPixel";
    const std::string kMaxSurfaceBounces = "maxSurfaceBounces";
    const std::string kMaxDiffuseBounces = "maxDiffuseBounces";
    const std::string kMaxSpecularBounces = "maxSpecularBounces";
    const std::string kMaxTransmissionBounces = "maxTransmissionBounces";

    const std::string kSampleGenerator = "sampleGenerator";
    const std::string kFixedSeed = "fixedSeed";
    const std::string kUseBSDFSampling = "useBSDFSampling";
    const std::string kUseRussianRoulette = "useRussianRoulette";
    const std::string kUseNEE = "useNEE";
    const std::string kUseMIS = "useMIS";
    const std::string kMISHeuristic = "misHeuristic";
    const std::string kMISPowerExponent = "misPowerExponent";
    const std::string kEmissiveSampler = "emissiveSampler";
    const std::string kLightBVHOptions = "lightBVHOptions";
    const std::string kUseRTXDI = "useRTXDI";
    const std::string kRTXDIOptions = "RTXDIOptions";

    const std::string kUseAlphaTest = "useAlphaTest";
    const std::string kAdjustShadingNormals = "adjustShadingNormals";
    const std::string kMaxNestedMaterials = "maxNestedMaterials";
    const std::string kUseLightsInDielectricVolumes = "useLightsInDielectricVolumes";
    const std::string kDisableCaustics = "disableCaustics";
    const std::string kSpecularRoughnessThreshold = "specularRoughnessThreshold";
    const std::string kPrimaryLodMode = "primaryLodMode";
    const std::string kLODBias = "lodBias";

    const std::string kUseNRDDemodulation = "useNRDDemodulation";

    const std::string kUseSER = "useSER";

    const std::string kOutputSize = "outputSize";
    const std::string kFixedOutputSize = "fixedOutputSize";
    const std::string kColorFormat = "colorFormat";
}

extern "C" FALCOR_API_EXPORT void registerPlugin(Falcor::PluginRegistry& registry)
{
    registry.registerClass<RenderPass, PathTracer>();
    ScriptBindings::registerBinding(PathTracer::registerBindings);
}

void PathTracer::registerBindings(pybind11::module& m)
{
    pybind11::class_<PathTracer, RenderPass, ref<PathTracer>> pass(m, "PathTracer");
    pass.def("reset", &PathTracer::reset);
    pass.def_property_readonly("pixelStats", &PathTracer::getPixelStats);

    pass.def_property("useFixedSeed",
        [](const PathTracer* pt) { return pt->mParams.useFixedSeed ? true : false; },
        [](PathTracer* pt, bool value) { pt->mParams.useFixedSeed = value ? 1 : 0; }
    );
    pass.def_property("fixedSeed",
        [](const PathTracer* pt) { return pt->mParams.fixedSeed; },
        [](PathTracer* pt, uint32_t value) { pt->mParams.fixedSeed = value; }
    );
}

PathTracer::PathTracer(ref<Device> pDevice, const Properties& props)
    : RenderPass(pDevice)
{
    if (!mpDevice->isShaderModelSupported(ShaderModel::SM6_5))
        FALCOR_THROW("PathTracer requires Shader Model 6.5 support.");
    if (!mpDevice->isFeatureSupported(Device::SupportedFeatures::RaytracingTier1_1))
        FALCOR_THROW("PathTracer requires Raytracing Tier 1.1 support.");

    mSERSupported = mpDevice->isFeatureSupported(Device::SupportedFeatures::ShaderExecutionReorderingAPI);

    parseProperties(props);
    validateOptions();

    // Create sample generator.
    mpSampleGenerator = SampleGenerator::create(mpDevice, mStaticParams.sampleGenerator);

    // Create resolve pass. This doesn't depend on the scene so can be created here.
    auto defines = mStaticParams.getDefines(*this);
    mpResolvePass = ComputePass::create(mpDevice, ProgramDesc().addShaderLibrary(kResolvePassFilename).csEntry("main"), defines, false);

    // Note: The other programs are lazily created in updatePrograms() because a scene needs to be present when creating them.

    mpPixelStats = std::make_unique<PixelStats>(mpDevice);
    mpPixelDebug = std::make_unique<PixelDebug>(mpDevice);

    // Initialize CIR buffer management state
    mCurrentCIRPathCount = 0;
    mCIRBufferBound = false;
    
    // Allocate CIR buffers for visible light communication analysis
    allocateCIRBuffers();
}

void PathTracer::setProperties(const Properties& props)
{
    parseProperties(props);
    validateOptions();
    if (auto lightBVHSampler = dynamic_cast<LightBVHSampler*>(mpEmissiveSampler.get()))
        lightBVHSampler->setOptions(mLightBVHOptions);
    if (mpRTXDI)
        mpRTXDI->setOptions(mRTXDIOptions);
    mRecompile = true;
    mOptionsChanged = true;
}

void PathTracer::parseProperties(const Properties& props)
{
    for (const auto& [key, value] : props)
    {
        // Rendering parameters
        if (key == kSamplesPerPixel) mStaticParams.samplesPerPixel = value;
        else if (key == kMaxSurfaceBounces) mStaticParams.maxSurfaceBounces = value;
        else if (key == kMaxDiffuseBounces) mStaticParams.maxDiffuseBounces = value;
        else if (key == kMaxSpecularBounces) mStaticParams.maxSpecularBounces = value;
        else if (key == kMaxTransmissionBounces) mStaticParams.maxTransmissionBounces = value;

        // Sampling parameters
        else if (key == kSampleGenerator) mStaticParams.sampleGenerator = value;
        else if (key == kFixedSeed) { mParams.fixedSeed = value; mParams.useFixedSeed = true; }
        else if (key == kUseBSDFSampling) mStaticParams.useBSDFSampling = value;
        else if (key == kUseRussianRoulette) mStaticParams.useRussianRoulette = value;
        else if (key == kUseNEE) mStaticParams.useNEE = value;
        else if (key == kUseMIS) mStaticParams.useMIS = value;
        else if (key == kMISHeuristic) mStaticParams.misHeuristic = value;
        else if (key == kMISPowerExponent) mStaticParams.misPowerExponent = value;
        else if (key == kEmissiveSampler) mStaticParams.emissiveSampler = value;
        else if (key == kLightBVHOptions) mLightBVHOptions = value;
        else if (key == kUseRTXDI) mStaticParams.useRTXDI = value;
        else if (key == kRTXDIOptions) mRTXDIOptions = value;

        // Material parameters
        else if (key == kUseAlphaTest) mStaticParams.useAlphaTest = value;
        else if (key == kAdjustShadingNormals) mStaticParams.adjustShadingNormals = value;
        else if (key == kMaxNestedMaterials) mStaticParams.maxNestedMaterials = value;
        else if (key == kUseLightsInDielectricVolumes) mStaticParams.useLightsInDielectricVolumes = value;
        else if (key == kDisableCaustics) mStaticParams.disableCaustics = value;
        else if (key == kSpecularRoughnessThreshold) mParams.specularRoughnessThreshold = value;
        else if (key == kPrimaryLodMode) mStaticParams.primaryLodMode = value;
        else if (key == kLODBias) mParams.lodBias = value;

        // Denoising parameters
        else if (key == kUseNRDDemodulation) mStaticParams.useNRDDemodulation = value;

        // Scheduling parameters
        else if (key == kUseSER) mStaticParams.useSER = value;

        // Output parameters
        else if (key == kOutputSize) mOutputSizeSelection = value;
        else if (key == kFixedOutputSize) mFixedOutputSize = value;
        else if (key == kColorFormat) mStaticParams.colorFormat = value;

        else logWarning("Unknown property '{}' in PathTracer properties.", key);
    }

    if (props.has(kMaxSurfaceBounces))
    {
        // Initialize bounce counts to 'maxSurfaceBounces' if they weren't explicitly set.
        if (!props.has(kMaxDiffuseBounces)) mStaticParams.maxDiffuseBounces = mStaticParams.maxSurfaceBounces;
        if (!props.has(kMaxSpecularBounces)) mStaticParams.maxSpecularBounces = mStaticParams.maxSurfaceBounces;
        if (!props.has(kMaxTransmissionBounces)) mStaticParams.maxTransmissionBounces = mStaticParams.maxSurfaceBounces;
    }
    else
    {
        // Initialize surface bounces.
        mStaticParams.maxSurfaceBounces = std::max(mStaticParams.maxDiffuseBounces, std::max(mStaticParams.maxSpecularBounces, mStaticParams.maxTransmissionBounces));
    }

    bool maxSurfaceBouncesNeedsAdjustment =
        mStaticParams.maxSurfaceBounces < mStaticParams.maxDiffuseBounces ||
        mStaticParams.maxSurfaceBounces < mStaticParams.maxSpecularBounces ||
        mStaticParams.maxSurfaceBounces < mStaticParams.maxTransmissionBounces;

    // Show a warning if maxSurfaceBounces will be adjusted in validateOptions().
    if (props.has(kMaxSurfaceBounces) && maxSurfaceBouncesNeedsAdjustment)
    {
        logWarning("'{}' is set lower than '{}', '{}' or '{}' and will be increased.", kMaxSurfaceBounces, kMaxDiffuseBounces, kMaxSpecularBounces, kMaxTransmissionBounces);
    }
}

void PathTracer::validateOptions()
{
    if (mParams.specularRoughnessThreshold < 0.f || mParams.specularRoughnessThreshold > 1.f)
    {
        logWarning("'specularRoughnessThreshold' has invalid value. Clamping to range [0,1].");
        mParams.specularRoughnessThreshold = std::clamp(mParams.specularRoughnessThreshold, 0.f, 1.f);
    }

    // Static parameters.
    if (mStaticParams.samplesPerPixel < 1 || mStaticParams.samplesPerPixel > kMaxSamplesPerPixel)
    {
        logWarning("'samplesPerPixel' must be in the range [1, {}]. Clamping to this range.", kMaxSamplesPerPixel);
        mStaticParams.samplesPerPixel = std::clamp(mStaticParams.samplesPerPixel, 1u, kMaxSamplesPerPixel);
    }

    auto clampBounces = [] (uint32_t& bounces, const std::string& name)
    {
        if (bounces > kMaxBounces)
        {
            logWarning("'{}' exceeds the maximum supported bounces. Clamping to {}.", name, kMaxBounces);
            bounces = kMaxBounces;
        }
    };

    clampBounces(mStaticParams.maxSurfaceBounces, kMaxSurfaceBounces);
    clampBounces(mStaticParams.maxDiffuseBounces, kMaxDiffuseBounces);
    clampBounces(mStaticParams.maxSpecularBounces, kMaxSpecularBounces);
    clampBounces(mStaticParams.maxTransmissionBounces, kMaxTransmissionBounces);

    // Make sure maxSurfaceBounces is at least as many as any of diffuse, specular or transmission.
    uint32_t minSurfaceBounces = std::max(mStaticParams.maxDiffuseBounces, std::max(mStaticParams.maxSpecularBounces, mStaticParams.maxTransmissionBounces));
    mStaticParams.maxSurfaceBounces = std::max(mStaticParams.maxSurfaceBounces, minSurfaceBounces);

    if (mStaticParams.primaryLodMode == TexLODMode::RayCones)
    {
        logWarning("Unsupported tex lod mode. Defaulting to Mip0.");
        mStaticParams.primaryLodMode = TexLODMode::Mip0;
    }

    if (mStaticParams.useSER && !mSERSupported)
    {
        logWarning("Shader Execution Reordering (SER) is not supported on this device. Disabling SER.");
        mStaticParams.useSER = false;
    }
}

Properties PathTracer::getProperties() const
{
    if (auto lightBVHSampler = dynamic_cast<LightBVHSampler*>(mpEmissiveSampler.get()))
    {
        mLightBVHOptions = lightBVHSampler->getOptions();
    }

    Properties props;

    // Rendering parameters
    props[kSamplesPerPixel] = mStaticParams.samplesPerPixel;
    props[kMaxSurfaceBounces] = mStaticParams.maxSurfaceBounces;
    props[kMaxDiffuseBounces] = mStaticParams.maxDiffuseBounces;
    props[kMaxSpecularBounces] = mStaticParams.maxSpecularBounces;
    props[kMaxTransmissionBounces] = mStaticParams.maxTransmissionBounces;

    // Sampling parameters
    props[kSampleGenerator] = mStaticParams.sampleGenerator;
    if (mParams.useFixedSeed) props[kFixedSeed] = mParams.fixedSeed;
    props[kUseBSDFSampling] = mStaticParams.useBSDFSampling;
    props[kUseRussianRoulette] = mStaticParams.useRussianRoulette;
    props[kUseNEE] = mStaticParams.useNEE;
    props[kUseMIS] = mStaticParams.useMIS;
    props[kMISHeuristic] = mStaticParams.misHeuristic;
    props[kMISPowerExponent] = mStaticParams.misPowerExponent;
    props[kEmissiveSampler] = mStaticParams.emissiveSampler;
    if (mStaticParams.emissiveSampler == EmissiveLightSamplerType::LightBVH) props[kLightBVHOptions] = mLightBVHOptions;
    props[kUseRTXDI] = mStaticParams.useRTXDI;
    props[kRTXDIOptions] = mRTXDIOptions;

    // Material parameters
    props[kUseAlphaTest] = mStaticParams.useAlphaTest;
    props[kAdjustShadingNormals] = mStaticParams.adjustShadingNormals;
    props[kMaxNestedMaterials] = mStaticParams.maxNestedMaterials;
    props[kUseLightsInDielectricVolumes] = mStaticParams.useLightsInDielectricVolumes;
    props[kDisableCaustics] = mStaticParams.disableCaustics;
    props[kSpecularRoughnessThreshold] = mParams.specularRoughnessThreshold;
    props[kPrimaryLodMode] = mStaticParams.primaryLodMode;
    props[kLODBias] = mParams.lodBias;

    // Denoising parameters
    props[kUseNRDDemodulation] = mStaticParams.useNRDDemodulation;

    // Scheduling parameters
    props[kUseSER] = mStaticParams.useSER;

    // Output parameters
    props[kOutputSize] = mOutputSizeSelection;
    if (mOutputSizeSelection == RenderPassHelpers::IOSize::Fixed) props[kFixedOutputSize] = mFixedOutputSize;
    props[kColorFormat] = mStaticParams.colorFormat;

    return props;
}

RenderPassReflection PathTracer::reflect(const CompileData& compileData)
{
    RenderPassReflection reflector;
    const uint2 sz = RenderPassHelpers::calculateIOSize(mOutputSizeSelection, mFixedOutputSize, compileData.defaultTexDims);

    addRenderPassInputs(reflector, kInputChannels);
    addRenderPassOutputs(reflector, kOutputChannels, ResourceBindFlags::UnorderedAccess, sz);
    return reflector;
}

void PathTracer::setFrameDim(const uint2 frameDim)
{
    auto prevFrameDim = mParams.frameDim;
    auto prevScreenTiles = mParams.screenTiles;

    mParams.frameDim = frameDim;
    if (mParams.frameDim.x > kMaxFrameDimension || mParams.frameDim.y > kMaxFrameDimension)
    {
        FALCOR_THROW("Frame dimensions up to {} pixels width/height are supported.", kMaxFrameDimension);
    }

    // Tile dimensions have to be powers-of-two.
    FALCOR_ASSERT(isPowerOf2(kScreenTileDim.x) && isPowerOf2(kScreenTileDim.y));
    FALCOR_ASSERT(kScreenTileDim.x == (1 << kScreenTileBits.x) && kScreenTileDim.y == (1 << kScreenTileBits.y));
    mParams.screenTiles = div_round_up(mParams.frameDim, kScreenTileDim);

    if (any(mParams.frameDim != prevFrameDim) || any(mParams.screenTiles != prevScreenTiles))
    {
        mVarsChanged = true;
    }
}

void PathTracer::setScene(RenderContext* pRenderContext, const ref<Scene>& pScene)
{
    mUpdateFlagsConnection = {};
    mUpdateFlags = IScene::UpdateFlags::None;

    mpScene = pScene;
    mParams.frameCount = 0;
    mParams.frameDim = {};
    mParams.screenTiles = {};

    // Need to recreate the RTXDI module when the scene changes.
    mpRTXDI = nullptr;

    resetPrograms();
    resetLighting();

    if (mpScene)
    {
        mUpdateFlagsConnection = mpScene->getUpdateFlagsSignal().connect([&](IScene::UpdateFlags flags) { mUpdateFlags |= flags; });

        if (pScene->hasGeometryType(Scene::GeometryType::Custom))
        {
            logWarning("PathTracer: This render pass does not support custom primitives.");
        }

        validateOptions();
    }
}


void PathTracer::execute(RenderContext* pRenderContext, const RenderData& renderData)
{
    // Clear outputs if pass is disabled.
    if (!mEnabled)
    {
        // ...existing clear code...
        auto clearTex = [&](const ChannelDesc& channel)
        {
            auto pTex = renderData.getTexture(channel.name);
            if (pTex) pRenderContext->clearTexture(pTex.get());
        };
        for (const auto& channel : kOutputChannels) clearTex(channel);

        return;
    }

    // Check if scene is loaded.
    if (!mpScene)
    {
        logWarning("PathTracer::execute() - no scene is loaded");
        return;
    }

    // Configure render pass.
    if (!beginFrame(pRenderContext, renderData)) return;

    // Perform CIR data verification if debugging is enabled
    triggerCIRDataVerification(pRenderContext);

    // Update shader program specialization.
    updatePrograms();

    // Prepare resources.
    prepareResources(pRenderContext, renderData);

    // Prepare the path tracer parameter block.
    // This should be called after all resources have been created.
    preparePathTracer(renderData);

    // Generate paths at primary hits.
    generatePaths(pRenderContext, renderData);

    // Update RTXDI.
    if (mpRTXDI)
    {
        const auto& pMotionVectors = renderData.getTexture(kInputMotionVectors);
        mpRTXDI->update(pRenderContext, pMotionVectors);
    }

    // Trace pass.
    FALCOR_ASSERT(mpTracePass);
    tracePass(pRenderContext, renderData, *mpTracePass);

    // Launch separate passes to trace delta reflection and transmission paths to generate respective guide buffers.
    if (mOutputNRDAdditionalData)
    {
        FALCOR_ASSERT(mpTraceDeltaReflectionPass && mpTraceDeltaTransmissionPass);
        tracePass(pRenderContext, renderData, *mpTraceDeltaReflectionPass);
        tracePass(pRenderContext, renderData, *mpTraceDeltaTransmissionPass);
    }

    // Resolve pass.
    resolvePass(pRenderContext, renderData);

    endFrame(pRenderContext, renderData);
}

void PathTracer::renderUI(Gui::Widgets& widget)
{
    bool dirty = false;

    // Rendering options.
    dirty |= renderRenderingUI(widget);

    // Stats and debug options.
    renderStatsUI(widget);
    dirty |= renderDebugUI(widget);

    if (dirty)
    {
        validateOptions();
        mOptionsChanged = true;
    }
}

bool PathTracer::renderRenderingUI(Gui::Widgets& widget)
{
    bool dirty = false;
    bool runtimeDirty = false;

    if (mFixedSampleCount)
    {
        dirty |= widget.var("Samples/pixel", mStaticParams.samplesPerPixel, 1u, kMaxSamplesPerPixel);
    }
    else widget.text("Samples/pixel: Variable");
    widget.tooltip("Number of samples per pixel. One path is traced for each sample.\n\n"
        "When the '" + kInputSampleCount + "' input is connected, the number of samples per pixel is loaded from the texture.");

    if (widget.var("Max surface bounces", mStaticParams.maxSurfaceBounces, 0u, kMaxBounces))
    {
        // Allow users to change the max surface bounce parameter in the UI to clamp all other surface bounce parameters.
        mStaticParams.maxDiffuseBounces = std::min(mStaticParams.maxDiffuseBounces, mStaticParams.maxSurfaceBounces);
        mStaticParams.maxSpecularBounces = std::min(mStaticParams.maxSpecularBounces, mStaticParams.maxSurfaceBounces);
        mStaticParams.maxTransmissionBounces = std::min(mStaticParams.maxTransmissionBounces, mStaticParams.maxSurfaceBounces);
        dirty = true;
    }
    widget.tooltip("Maximum number of surface bounces (diffuse + specular + transmission).\n"
        "Note that specular reflection events from a material with a roughness greater than specularRoughnessThreshold are also classified as diffuse events.");

    dirty |= widget.var("Max diffuse bounces", mStaticParams.maxDiffuseBounces, 0u, kMaxBounces);
    widget.tooltip("Maximum number of diffuse bounces.\n0 = direct only\n1 = one indirect bounce etc.");

    dirty |= widget.var("Max specular bounces", mStaticParams.maxSpecularBounces, 0u, kMaxBounces);
    widget.tooltip("Maximum number of specular bounces.\n0 = direct only\n1 = one indirect bounce etc.");

    dirty |= widget.var("Max transmission bounces", mStaticParams.maxTransmissionBounces, 0u, kMaxBounces);
    widget.tooltip("Maximum number of transmission bounces.\n0 = no transmission\n1 = one transmission bounce etc.");

    // Sampling options.

    if (widget.dropdown("Sample generator", SampleGenerator::getGuiDropdownList(), mStaticParams.sampleGenerator))
    {
        mpSampleGenerator = SampleGenerator::create(mpDevice, mStaticParams.sampleGenerator);
        dirty = true;
    }

    dirty |= widget.checkbox("BSDF importance sampling", mStaticParams.useBSDFSampling);
    widget.tooltip("BSDF importance sampling should normally be enabled.\n\n"
        "If disabled, cosine-weighted hemisphere sampling is used for debugging purposes");

    dirty |= widget.checkbox("Russian roulette", mStaticParams.useRussianRoulette);
    widget.tooltip("Use russian roulette to terminate low throughput paths.");

    dirty |= widget.checkbox("Next-event estimation (NEE)", mStaticParams.useNEE);
    widget.tooltip("Use next-event estimation.\nThis option enables direct illumination sampling at each path vertex.");

    if (mStaticParams.useNEE)
    {
        dirty |= widget.checkbox("Multiple importance sampling (MIS)", mStaticParams.useMIS);
        widget.tooltip("When enabled, BSDF sampling is combined with light sampling for the environment map and emissive lights.\n"
            "Note that MIS has currently no effect on analytic lights.");

        if (mStaticParams.useMIS)
        {
            dirty |= widget.dropdown("MIS heuristic", mStaticParams.misHeuristic);

            if (mStaticParams.misHeuristic == MISHeuristic::PowerExp)
            {
                dirty |= widget.var("MIS power exponent", mStaticParams.misPowerExponent, 0.01f, 10.f);
            }
        }

        if (mpScene && mpScene->useEmissiveLights())
        {
            if (auto group = widget.group("Emissive sampler"))
            {
                if (widget.dropdown("Emissive sampler", mStaticParams.emissiveSampler))
                {
                    resetLighting();
                    dirty = true;
                }
                widget.tooltip("Selects which light sampler to use for importance sampling of emissive geometry.", true);

                if (mpEmissiveSampler)
                {
                    if (mpEmissiveSampler->renderUI(group)) mOptionsChanged = true;
                }
            }
        }
    }

    if (auto group = widget.group("RTXDI"))
    {
        dirty |= widget.checkbox("Enabled", mStaticParams.useRTXDI);
        widget.tooltip("Use RTXDI for direct illumination.");
        if (mpRTXDI) dirty |= mpRTXDI->renderUI(group);
    }

    if (auto group = widget.group("Material controls"))
    {
        dirty |= widget.checkbox("Alpha test", mStaticParams.useAlphaTest);
        widget.tooltip("Use alpha testing on non-opaque triangles.");

        dirty |= widget.checkbox("Adjust shading normals on secondary hits", mStaticParams.adjustShadingNormals);
        widget.tooltip("Enables adjustment of the shading normals to reduce the risk of black pixels due to back-facing vectors.\nDoes not apply to primary hits which is configured in GBuffer.", true);

        dirty |= widget.var("Max nested materials", mStaticParams.maxNestedMaterials, 2u, 4u);
        widget.tooltip("Maximum supported number of nested materials.");

        dirty |= widget.checkbox("Use lights in dielectric volumes", mStaticParams.useLightsInDielectricVolumes);
        widget.tooltip("Use lights inside of volumes (transmissive materials). We typically don't want this because lights are occluded by the interface.");

        dirty |= widget.checkbox("Disable caustics", mStaticParams.disableCaustics);
        widget.tooltip("Disable sampling of caustic light paths (i.e. specular events after diffuse events).");

        runtimeDirty |= widget.var("Specular roughness threshold", mParams.specularRoughnessThreshold, 0.f, 1.f);
        widget.tooltip("Specular reflection events are only classified as specular if the material's roughness value is equal or smaller than this threshold. Otherwise they are classified diffuse.");

        dirty |= widget.dropdown("Primary LOD Mode", mStaticParams.primaryLodMode);
        widget.tooltip("Texture LOD mode at primary hit");

        runtimeDirty |= widget.var("TexLOD bias", mParams.lodBias, -16.f, 16.f, 0.01f);
    }

    if (auto group = widget.group("Denoiser options"))
    {
        dirty |= widget.checkbox("Use NRD demodulation", mStaticParams.useNRDDemodulation);
        widget.tooltip("Global switch for NRD demodulation");
    }

    if (auto group = widget.group("Scheduling options"))
    {
        dirty |= widget.checkbox("Use SER", mStaticParams.useSER);
        widget.tooltip("Use Shader Execution Reordering (SER) to improve GPU utilization.");
    }

    if (auto group = widget.group("Output options"))
    {
        // Switch to enable/disable path tracer output.
        dirty |= widget.checkbox("Enable output", mEnabled);

        // Controls for output size.
        // When output size requirements change, we'll trigger a graph recompile to update the render pass I/O sizes.
        if (widget.dropdown("Output size", mOutputSizeSelection)) requestRecompile();
        if (mOutputSizeSelection == RenderPassHelpers::IOSize::Fixed)
        {
            if (widget.var("Size in pixels", mFixedOutputSize, 32u, 16384u)) requestRecompile();
        }

        dirty |= widget.dropdown("Color format", mStaticParams.colorFormat);
        widget.tooltip("Selects the color format used for internal per-sample color and denoiser buffers");
    }

    if (dirty) mRecompile = true;
    return dirty || runtimeDirty;
}

bool PathTracer::renderDebugUI(Gui::Widgets& widget)
{
    bool dirty = false;

    if (auto group = widget.group("Debugging"))
    {
        mpPixelDebug->renderUI(group);

        dirty |= group.dropdown("Color format", mStaticParams.colorFormat);

        if (mpPixelStats)
        {
            mpPixelStats->renderUI(group);
        }

        // CIR debugging controls
        if (auto cirGroup = group.group("CIR Debugging"))
        {
            cirGroup.checkbox("Enable CIR debugging", mCIRDebugEnabled);
            cirGroup.tooltip("Enable/disable CIR data collection debugging output");

            dirty |= cirGroup.var("Check interval (frames)", mCIRFrameCheckInterval, 1u, 1000u);
            cirGroup.tooltip("Number of frames between CIR data verification checks");

            if (cirGroup.button("Dump CIR Data"))
            {
                // Note: This will be called during UI rendering, so we need a render context
                // For now, we'll set a flag to trigger the dump in the next frame
                static bool triggerDump = false;
                triggerDump = true;
                // The actual dump will be triggered in the execute method
            }

            if (cirGroup.button("Verify CIR Data"))
            {
                logInfo("CIR: Manual verification triggered from UI");
                mLastCIRCheckFrame = 0; // Force verification on next frame
            }

            if (cirGroup.button("Show CIR Statistics"))
            {
                logCIRBufferStatus();
            }

            // Display current CIR status
            cirGroup.text("CIR Status:");
            cirGroup.text(fmt::format("  Buffer allocated: {}", mpCIRPathBuffer ? "Yes" : "No"));
            cirGroup.text(fmt::format("  Buffer bound: {}", mCIRBufferBound ? "Yes" : "No"));
            cirGroup.text(fmt::format("  Paths collected: {}", mCurrentCIRPathCount));
            cirGroup.text(fmt::format("  Buffer capacity: {}", mMaxCIRPaths));
            
            if (mpCIRPathBuffer && mMaxCIRPaths > 0)
            {
                float usagePercent = static_cast<float>(mCurrentCIRPathCount) / mMaxCIRPaths * 100.0f;
                cirGroup.text(fmt::format("  Usage: {:.2f}%", usagePercent));
            }
        }
    }

    return dirty;
}

void PathTracer::renderStatsUI(Gui::Widgets& widget)
{
    if (auto g = widget.group("Statistics"))
    {
        // Show ray stats
        mpPixelStats->renderUI(g);
    }
}

bool PathTracer::onMouseEvent(const MouseEvent& mouseEvent)
{
    return mpPixelDebug->onMouseEvent(mouseEvent);
}

void PathTracer::reset()
{
    mParams.frameCount = 0;
}

PathTracer::TracePass::TracePass(ref<Device> pDevice, const std::string& name, const std::string& passDefine, const ref<Scene>& pScene, const DefineList& defines, const TypeConformanceList& globalTypeConformances)
    : name(name)
    , passDefine(passDefine)
{
    const uint32_t kRayTypeScatter = 0;
    const uint32_t kMissScatter = 0;

    bool useSER = defines.at("USE_SER") == "1";

    ProgramDesc desc;
    desc.addShaderModules(pScene->getShaderModules());
    desc.addShaderLibrary(kTracePassFilename);
    if (pDevice->getType() == Device::Type::D3D12 && useSER)
        desc.addCompilerArguments({ "-Xdxc", "-enable-lifetime-markers" });
    desc.setMaxPayloadSize(160); // This is conservative but the required minimum is 140 bytes.
    desc.setMaxAttributeSize(pScene->getRaytracingMaxAttributeSize());
    desc.setMaxTraceRecursionDepth(1);
    if (!pScene->hasProceduralGeometry()) desc.setRtPipelineFlags(RtPipelineFlags::SkipProceduralPrimitives);

    // Create ray tracing binding table.
    pBindingTable = RtBindingTable::create(1, 1, pScene->getGeometryCount());

    // Specify entry point for raygen and miss shaders.
    // The raygen shader needs type conformances for *all* materials in the scene.
    // The miss shader doesn't need need any type conformances because it does not use materials.
    pBindingTable->setRayGen(desc.addRayGen("rayGen", globalTypeConformances));
    pBindingTable->setMiss(kMissScatter, desc.addMiss("scatterMiss"));

    // Specify hit group entry points for every combination of geometry and material type.
    // The code for each hit group gets specialized for the actual types it's operating on.
    // First query which material types the scene has.
    auto materialTypes = pScene->getMaterialSystem().getMaterialTypes();

    for (const auto materialType : materialTypes)
    {
        auto typeConformances = pScene->getMaterialSystem().getTypeConformances(materialType);

        // Add hit groups for triangles.
        if (auto geometryIDs = pScene->getGeometryIDs(Scene::GeometryType::TriangleMesh, materialType); !geometryIDs.empty())
        {
            auto shaderID = desc.addHitGroup("scatterTriangleClosestHit", "scatterTriangleAnyHit", "", typeConformances, to_string(materialType));
            pBindingTable->setHitGroup(kRayTypeScatter, geometryIDs, shaderID);
        }

        // Add hit groups for displaced triangle meshes.
        if (auto geometryIDs = pScene->getGeometryIDs(Scene::GeometryType::DisplacedTriangleMesh, materialType); !geometryIDs.empty())
        {
            auto shaderID = desc.addHitGroup("scatterDisplacedTriangleMeshClosestHit", "", "displacedTriangleMeshIntersection", typeConformances, to_string(materialType));
            pBindingTable->setHitGroup(kRayTypeScatter, geometryIDs, shaderID);
        }

        // Add hit groups for curves.
        if (auto geometryIDs = pScene->getGeometryIDs(Scene::GeometryType::Curve, materialType); !geometryIDs.empty())
        {
            auto shaderID = desc.addHitGroup("scatterCurveClosestHit", "", "curveIntersection", typeConformances, to_string(materialType));
            pBindingTable->setHitGroup(kRayTypeScatter, geometryIDs, shaderID);
        }

        // Add hit groups for SDF grids.
        if (auto geometryIDs = pScene->getGeometryIDs(Scene::GeometryType::SDFGrid, materialType); !geometryIDs.empty())
        {
            auto shaderID = desc.addHitGroup("scatterSdfGridClosestHit", "", "sdfGridIntersection", typeConformances, to_string(materialType));
            pBindingTable->setHitGroup(kRayTypeScatter, geometryIDs, shaderID);
        }
    }

    pProgram = Program::create(pDevice, desc, defines);
}


void PathTracer::TracePass::prepareProgram(ref<Device> pDevice, const DefineList& defines)
{
    FALCOR_ASSERT(pProgram != nullptr && pBindingTable != nullptr);
    pProgram->setDefines(defines);
    if (!passDefine.empty()) pProgram->addDefine(passDefine);
    pVars = RtProgramVars::create(pDevice, pProgram, pBindingTable);
}

void PathTracer::resetPrograms()
{
    mpTracePass = nullptr;
    mpTraceDeltaReflectionPass = nullptr;
    mpTraceDeltaTransmissionPass = nullptr;
    mpGeneratePaths = nullptr;
    mpReflectTypes = nullptr;

    mRecompile = true;
}

void PathTracer::updatePrograms()
{
    FALCOR_ASSERT(mpScene);

    if (mRecompile == false) return;

    // If we get here, a change that require recompilation of shader programs has occurred.
    // This may be due to change of scene defines, type conformances, shader modules, or other changes that require recompilation.
    // When type conformances and/or shader modules change, the programs need to be recreated. We assume programs have been reset upon such changes.
    // When only defines have changed, it is sufficient to update the existing programs and recreate the program vars.

    auto defines = mStaticParams.getDefines(*this);
    TypeConformanceList globalTypeConformances;
    mpScene->getTypeConformances(globalTypeConformances);

    // Create trace pass.
    if (!mpTracePass)
        mpTracePass = TracePass::create(mpDevice, "tracePass", "", mpScene, defines, globalTypeConformances);

    mpTracePass->prepareProgram(mpDevice, defines);

    // Create specialized trace passes.
    if (mOutputNRDAdditionalData)
    {
        if (!mpTraceDeltaReflectionPass)
            mpTraceDeltaReflectionPass = TracePass::create(mpDevice, "traceDeltaReflectionPass", "DELTA_REFLECTION_PASS", mpScene, defines, globalTypeConformances);
        if (!mpTraceDeltaTransmissionPass)
            mpTraceDeltaTransmissionPass = TracePass::create(mpDevice, "traceDeltaTransmissionPass", "DELTA_TRANSMISSION_PASS", mpScene, defines, globalTypeConformances);

        mpTraceDeltaReflectionPass->prepareProgram(mpDevice, defines);
        mpTraceDeltaTransmissionPass->prepareProgram(mpDevice, defines);
    }

    // Create compute passes.
    ProgramDesc baseDesc;
    mpScene->getShaderModules(baseDesc.shaderModules);
    baseDesc.addTypeConformances(globalTypeConformances);

    if (!mpGeneratePaths)
    {
        ProgramDesc desc = baseDesc;
        desc.addShaderLibrary(kGeneratePathsFilename).csEntry("main");
        mpGeneratePaths = ComputePass::create(mpDevice, desc, defines, false);
    }
    if (!mpReflectTypes)
    {
        ProgramDesc desc = baseDesc;
        desc.addShaderLibrary(kReflectTypesFile).csEntry("main");
        mpReflectTypes = ComputePass::create(mpDevice, desc, defines, false);
    }

    auto preparePass = [&](ref<ComputePass> pass)
    {
        // Note that we must use set instead of add defines to replace any stale state.
        pass->getProgram()->setDefines(defines);

        // Recreate program vars. This may trigger recompilation if needed.
        // Note that program versions are cached, so switching to a previously used specialization is faster.
        pass->setVars(nullptr);
    };
    preparePass(mpGeneratePaths);
    preparePass(mpResolvePass);
    preparePass(mpReflectTypes);

    mVarsChanged = true;
    mRecompile = false;
}

void PathTracer::prepareResources(RenderContext* pRenderContext, const RenderData& renderData)
{
    // Compute allocation requirements for paths and output samples.
    // Note that the sample buffers are padded to whole tiles, while the max path count depends on actual frame dimension.
    // If we don't have a fixed sample count, assume the worst case.
    uint32_t spp = mFixedSampleCount ? mStaticParams.samplesPerPixel : kMaxSamplesPerPixel;
    uint32_t tileCount = mParams.screenTiles.x * mParams.screenTiles.y;
    const uint32_t sampleCount = tileCount * kScreenTileDim.x * kScreenTileDim.y * spp;
    const uint32_t screenPixelCount = mParams.frameDim.x * mParams.frameDim.y;
    const uint32_t pathCount = screenPixelCount * spp;

    // Allocate output sample offset buffer if needed.
    // This buffer stores the output offset to where the samples for each pixel are stored consecutively.
    // The offsets are local to the current tile, so 16-bit format is sufficient and reduces bandwidth usage.
    if (!mFixedSampleCount)
    {
        if (!mpSampleOffset || mpSampleOffset->getWidth() != mParams.frameDim.x || mpSampleOffset->getHeight() != mParams.frameDim.y)
        {
            FALCOR_ASSERT(kScreenTileDim.x * kScreenTileDim.y * kMaxSamplesPerPixel <= (1u << 16));
            mpSampleOffset = mpDevice->createTexture2D(mParams.frameDim.x, mParams.frameDim.y, ResourceFormat::R16Uint, 1, 1, nullptr, ResourceBindFlags::ShaderResource | ResourceBindFlags::UnorderedAccess);
            mVarsChanged = true;
        }
    }

    auto var = mpReflectTypes->getRootVar();

    // Allocate per-sample buffers.
    // For the special case of fixed 1 spp, the output is written out directly and this buffer is not needed.
    if (!mFixedSampleCount || mStaticParams.samplesPerPixel > 1)
    {
        if (!mpSampleColor || mpSampleColor->getElementCount() < sampleCount || mVarsChanged)
        {
            mpSampleColor = mpDevice->createStructuredBuffer(var["sampleColor"], sampleCount, ResourceBindFlags::ShaderResource | ResourceBindFlags::UnorderedAccess, MemoryType::DeviceLocal, nullptr, false);
            mVarsChanged = true;
        }
    }

    if (mOutputGuideData && (!mpSampleGuideData || mpSampleGuideData->getElementCount() < sampleCount || mVarsChanged))
    {
        mpSampleGuideData = mpDevice->createStructuredBuffer(var["sampleGuideData"], sampleCount, ResourceBindFlags::ShaderResource | ResourceBindFlags::UnorderedAccess, MemoryType::DeviceLocal, nullptr, false);
        mVarsChanged = true;
    }

    if (mOutputNRDData && (!mpSampleNRDRadiance || mpSampleNRDRadiance->getElementCount() < sampleCount || mVarsChanged))
    {
        mpSampleNRDRadiance = mpDevice->createStructuredBuffer(var["sampleNRDRadiance"], sampleCount, ResourceBindFlags::ShaderResource | ResourceBindFlags::UnorderedAccess, MemoryType::DeviceLocal, nullptr, false);
        mpSampleNRDHitDist = mpDevice->createStructuredBuffer(var["sampleNRDHitDist"], sampleCount, ResourceBindFlags::ShaderResource | ResourceBindFlags::UnorderedAccess, MemoryType::DeviceLocal, nullptr, false);
        mpSampleNRDPrimaryHitNeeOnDelta = mpDevice->createStructuredBuffer(var["sampleNRDPrimaryHitNeeOnDelta"], sampleCount, ResourceBindFlags::ShaderResource | ResourceBindFlags::UnorderedAccess, MemoryType::DeviceLocal, nullptr, false);
        mpSampleNRDEmission = mpDevice->createStructuredBuffer(var["sampleNRDEmission"], sampleCount, ResourceBindFlags::ShaderResource | ResourceBindFlags::UnorderedAccess, MemoryType::DeviceLocal, nullptr, false);
        mpSampleNRDReflectance = mpDevice->createStructuredBuffer(var["sampleNRDReflectance"], sampleCount, ResourceBindFlags::ShaderResource | ResourceBindFlags::UnorderedAccess, MemoryType::DeviceLocal, nullptr, false);
        mVarsChanged = true;
    }

    // 为初始光线信息创建缓冲区
    if (mOutputInitialRayInfo && (!mpSampleInitialRayInfo || mpSampleInitialRayInfo->getElementCount() < sampleCount || mVarsChanged))
    {
        mpSampleInitialRayInfo = mpDevice->createStructuredBuffer(var["sampleInitialRayInfo"], sampleCount, ResourceBindFlags::ShaderResource | ResourceBindFlags::UnorderedAccess, MemoryType::DeviceLocal, nullptr, false);
        mVarsChanged = true;
    }

    // CIR path buffer is already allocated in constructor
}

void PathTracer::preparePathTracer(const RenderData& renderData)
{
    // Create path tracer parameter block if needed.
    if (!mpPathTracerBlock || mVarsChanged)
    {
        auto reflector = mpReflectTypes->getProgram()->getReflector()->getParameterBlock("pathTracer");
        mpPathTracerBlock = ParameterBlock::create(mpDevice, reflector);
        FALCOR_ASSERT(mpPathTracerBlock);
        mVarsChanged = true;
    }

    // Bind resources.
    auto var = mpPathTracerBlock->getRootVar();
    bindShaderData(var, renderData);

    // Bind CIR buffer to parameter block
    if (mpCIRPathBuffer)
    {
        bindCIRBufferToParameterBlock(var, "pathTracer");
    }
}

void PathTracer::resetLighting()
{
    // Retain the options for the emissive sampler.
    if (auto lightBVHSampler = dynamic_cast<LightBVHSampler*>(mpEmissiveSampler.get()))
    {
        mLightBVHOptions = lightBVHSampler->getOptions();
    }

    mpEmissiveSampler = nullptr;
    mpEnvMapSampler = nullptr;
    mRecompile = true;
}

void PathTracer::prepareMaterials(RenderContext* pRenderContext)
{
    // This functions checks for scene changes that require shader recompilation.
    // Whenever materials or geometry is added/removed to the scene, we reset the shader programs to trigger
    // recompilation with the correct defines, type conformances, shader modules, and binding table.

    if (is_set(mUpdateFlags, IScene::UpdateFlags::RecompileNeeded) ||
        is_set(mUpdateFlags, IScene::UpdateFlags::GeometryChanged))
    {
        resetPrograms();
    }
}

bool PathTracer::prepareLighting(RenderContext* pRenderContext)
{
    bool lightingChanged = false;

    if (is_set(mUpdateFlags, IScene::UpdateFlags::RenderSettingsChanged))
    {
        lightingChanged = true;
        mRecompile = true;
    }

    if (is_set(mUpdateFlags, IScene::UpdateFlags::SDFGridConfigChanged))
    {
        mRecompile = true;
    }

    if (is_set(mUpdateFlags, IScene::UpdateFlags::EnvMapChanged))
    {
        mpEnvMapSampler = nullptr;
        lightingChanged = true;
        mRecompile = true;
    }

    if (mpScene->useEnvLight())
    {
        if (!mpEnvMapSampler)
        {
            mpEnvMapSampler = std::make_unique<EnvMapSampler>(mpDevice, mpScene->getEnvMap());
            lightingChanged = true;
            mRecompile = true;
        }
    }
    else
    {
        if (mpEnvMapSampler)
        {
            mpEnvMapSampler = nullptr;
            lightingChanged = true;
            mRecompile = true;
        }
    }

    // Request the light collection if emissive lights are enabled.
    if (mpScene->getRenderSettings().useEmissiveLights)
    {
        mpScene->getILightCollection(pRenderContext);
    }

    if (mpScene->useEmissiveLights())
    {
        if (!mpEmissiveSampler)
        {
            const auto& pLights = mpScene->getILightCollection(pRenderContext);
            FALCOR_ASSERT(pLights && pLights->getActiveLightCount(pRenderContext) > 0);
            FALCOR_ASSERT(!mpEmissiveSampler);

            switch (mStaticParams.emissiveSampler)
            {
            case EmissiveLightSamplerType::Uniform:
                mpEmissiveSampler = std::make_unique<EmissiveUniformSampler>(pRenderContext, mpScene->getILightCollection(pRenderContext));
                break;
            case EmissiveLightSamplerType::LightBVH:
                mpEmissiveSampler = std::make_unique<LightBVHSampler>(pRenderContext, mpScene->getILightCollection(pRenderContext), mLightBVHOptions);
                break;
            case EmissiveLightSamplerType::Power:
                mpEmissiveSampler = std::make_unique<EmissivePowerSampler>(pRenderContext, mpScene->getILightCollection(pRenderContext));
                break;
            default:
                FALCOR_THROW("Unknown emissive light sampler type");
            }
            lightingChanged = true;
            mRecompile = true;
        }
    }
    else
    {
        if (mpEmissiveSampler)
        {
            // Retain the options for the emissive sampler.
            if (auto lightBVHSampler = dynamic_cast<LightBVHSampler*>(mpEmissiveSampler.get()))
            {
                mLightBVHOptions = lightBVHSampler->getOptions();
            }

            mpEmissiveSampler = nullptr;
            lightingChanged = true;
            mRecompile = true;
        }
    }

    if (mpEmissiveSampler)
    {
        lightingChanged |= mpEmissiveSampler->update(pRenderContext, mpScene->getILightCollection(pRenderContext));
        auto defines = mpEmissiveSampler->getDefines();
        if (mpTracePass && mpTracePass->pProgram->addDefines(defines)) mRecompile = true;
    }

    return lightingChanged;
}

void PathTracer::prepareRTXDI(RenderContext* pRenderContext)
{
    if (mStaticParams.useRTXDI)
    {
        if (!mpRTXDI) mpRTXDI = std::make_unique<RTXDI>(mpScene, mRTXDIOptions);

        // Emit warning if enabled while using spp != 1.
        if (!mFixedSampleCount || mStaticParams.samplesPerPixel != 1)
        {
            logWarning("Using RTXDI with samples/pixel != 1 will only generate one RTXDI sample reused for all pixel samples.");
        }
    }
    else
    {
        mpRTXDI = nullptr;
    }
}

void PathTracer::setNRDData(const ShaderVar& var, const RenderData& renderData) const
{
    var["sampleRadiance"] = mpSampleNRDRadiance;
    var["sampleHitDist"] = mpSampleNRDHitDist;
    var["samplePrimaryHitNEEOnDelta"] = mpSampleNRDPrimaryHitNeeOnDelta;
    var["sampleEmission"] = mpSampleNRDEmission;
    var["sampleReflectance"] = mpSampleNRDReflectance;
    var["primaryHitEmission"] = renderData.getTexture(kOutputNRDEmission);
    var["primaryHitDiffuseReflectance"] = renderData.getTexture(kOutputNRDDiffuseReflectance);
    var["primaryHitSpecularReflectance"] = renderData.getTexture(kOutputNRDSpecularReflectance);
    var["deltaReflectionReflectance"] = renderData.getTexture(kOutputNRDDeltaReflectionReflectance);
    var["deltaReflectionEmission"] = renderData.getTexture(kOutputNRDDeltaReflectionEmission);
    var["deltaReflectionNormWRoughMaterialID"] = renderData.getTexture(kOutputNRDDeltaReflectionNormWRoughMaterialID);
    var["deltaReflectionPathLength"] = renderData.getTexture(kOutputNRDDeltaReflectionPathLength);
    var["deltaReflectionHitDist"] = renderData.getTexture(kOutputNRDDeltaReflectionHitDist);
    var["deltaTransmissionReflectance"] = renderData.getTexture(kOutputNRDDeltaTransmissionReflectance);
    var["deltaTransmissionEmission"] = renderData.getTexture(kOutputNRDDeltaTransmissionEmission);
    var["deltaTransmissionNormWRoughMaterialID"] = renderData.getTexture(kOutputNRDDeltaTransmissionNormWRoughMaterialID);
    var["deltaTransmissionPathLength"] = renderData.getTexture(kOutputNRDDeltaTransmissionPathLength);
    var["deltaTransmissionPosW"] = renderData.getTexture(kOutputNRDDeltaTransmissionPosW);
}

void PathTracer::bindShaderData(const ShaderVar& var, const RenderData& renderData, bool useLightSampling) const
{
    // Bind static resources that don't change per frame.
    if (mVarsChanged)
    {
        if (useLightSampling && mpEnvMapSampler) mpEnvMapSampler->bindShaderData(var["envMapSampler"]);

        var["sampleOffset"] = mpSampleOffset; // Can be nullptr
        var["sampleColor"] = mpSampleColor;
        var["sampleGuideData"] = mpSampleGuideData;
        var["sampleInitialRayInfo"] = mpSampleInitialRayInfo;

        // Conditionally bind CIR buffer - only for shaders that support it
        if (mpCIRPathBuffer)
        {
            // Check if the shader variable supports gCIRPathBuffer before binding
            try
            {
                // Test if the variable exists by attempting to access it
                auto cirVar = var["gCIRPathBuffer"];
                cirVar = mpCIRPathBuffer;
                logInfo("CIR: Buffer bound to shader variable 'gCIRPathBuffer' - element count: {}", mpCIRPathBuffer->getElementCount());
                logInfo("CIR: Buffer capacity: {} paths, Current count: {}", mMaxCIRPaths, mCurrentCIRPathCount);
            }
            catch (const std::exception&)
            {
                // gCIRPathBuffer not defined in this shader - skip binding
                logInfo("CIR: Shader does not support gCIRPathBuffer - skipping binding (normal for GeneratePaths)");
            }
        }
        else
        {
            logWarning("CIR: Buffer not allocated, CIR data collection will be disabled");
        }

        // Note: CIR buffer binding to parameter blocks is now handled by bindCIRBufferToRootVar
        // for better multi-pass compatibility. This only logs status for debugging.
        if (mpCIRPathBuffer)
        {
            logInfo("CIR: Buffer available for binding - element count: {}", mpCIRPathBuffer->getElementCount());
        }
        else
        {
            logWarning("CIR: Buffer not allocated");
        }
    }

    // Bind runtime data.
    setNRDData(var["outputNRD"], renderData);

    ref<Texture> pViewDir;
    if (mpScene && mpScene->getCamera()->getApertureRadius() > 0.f)
    {
        pViewDir = renderData.getTexture(kInputViewDir);
        if (!pViewDir) logWarning("Depth-of-field requires the '{}' input. Expect incorrect rendering.", kInputViewDir);
    }

    ref<Texture> pSampleCount;
    if (!mFixedSampleCount)
    {
        pSampleCount = renderData.getTexture(kInputSampleCount);
        if (!pSampleCount) FALCOR_THROW("PathTracer: Missing sample count input texture");
    }

    var["params"].setBlob(mParams);
    var["vbuffer"] = renderData.getTexture(kInputVBuffer);
    var["viewDir"] = pViewDir; // Can be nullptr
    var["sampleCount"] = pSampleCount; // Can be nullptr
    var["outputColor"] = renderData.getTexture(kOutputColor);

    if (useLightSampling && mpEmissiveSampler)
    {
        // TODO: Do we have to bind this every frame?
        mpEmissiveSampler->bindShaderData(var["emissiveSampler"]);
    }
}

bool PathTracer::beginFrame(RenderContext* pRenderContext, const RenderData& renderData)
{
    const auto& pOutputColor = renderData.getTexture(kOutputColor);
    FALCOR_ASSERT(pOutputColor);

    // Set output frame dimension.
    setFrameDim(uint2(pOutputColor->getWidth(), pOutputColor->getHeight()));

    // Validate all I/O sizes match the expected size.
    // If not, we'll disable the path tracer to give the user a chance to fix the configuration before re-enabling it.
    bool resolutionMismatch = false;
    auto validateChannels = [&](const auto& channels) {
        for (const auto& channel : channels)
        {
            auto pTexture = renderData.getTexture(channel.name);
            if (pTexture && (pTexture->getWidth() != mParams.frameDim.x || pTexture->getHeight() != mParams.frameDim.y)) resolutionMismatch = true;
        }
    };
    validateChannels(kInputChannels);
    validateChannels(kOutputChannels);

    if (mEnabled && resolutionMismatch)
    {
        logError("PathTracer I/O sizes don't match. The pass will be disabled.");
        mEnabled = false;
    }

    if (mpScene == nullptr || !mEnabled)
    {
        pRenderContext->clearUAV(pOutputColor->getUAV().get(), float4(0.f));

        // Set refresh flag if changes that affect the output have occured.
        // This is needed to ensure other passes get notified when the path tracer is enabled/disabled.
        if (mOptionsChanged)
        {
            auto& dict = renderData.getDictionary();
            auto flags = dict.getValue(kRenderPassRefreshFlags, Falcor::RenderPassRefreshFlags::None);
            if (mOptionsChanged) flags |= Falcor::RenderPassRefreshFlags::RenderOptionsChanged;
            dict[Falcor::kRenderPassRefreshFlags] = flags;
        }

        return false;
    }

    // Update materials.
    prepareMaterials(pRenderContext);

    // Update the env map and emissive sampler to the current frame.
    bool lightingChanged = prepareLighting(pRenderContext);

    // Prepare RTXDI.
    prepareRTXDI(pRenderContext);
    if (mpRTXDI) mpRTXDI->beginFrame(pRenderContext, mParams.frameDim);

    // Update refresh flag if changes that affect the output have occured.
    auto& dict = renderData.getDictionary();
    if (mOptionsChanged || lightingChanged)
    {
        auto flags = dict.getValue(kRenderPassRefreshFlags, Falcor::RenderPassRefreshFlags::None);
        if (mOptionsChanged) flags |= Falcor::RenderPassRefreshFlags::RenderOptionsChanged;
        if (lightingChanged) flags |= Falcor::RenderPassRefreshFlags::LightingChanged;
        dict[Falcor::kRenderPassRefreshFlags] = flags;
        mOptionsChanged = false;
    }

    // Check if GBuffer has adjusted shading normals enabled.
    bool gbufferAdjustShadingNormals = dict.getValue(Falcor::kRenderPassGBufferAdjustShadingNormals, false);
    if (gbufferAdjustShadingNormals != mGBufferAdjustShadingNormals)
    {
        mGBufferAdjustShadingNormals = gbufferAdjustShadingNormals;
        mRecompile = true;
    }

    // Check if fixed sample count should be used. When the sample count input is connected we load the count from there instead.
    mFixedSampleCount = renderData[kInputSampleCount] == nullptr;

    // Check if guide data should be generated.
    mOutputGuideData = renderData[kOutputAlbedo] != nullptr || renderData[kOutputSpecularAlbedo] != nullptr
        || renderData[kOutputIndirectAlbedo] != nullptr || renderData[kOutputGuideNormal] != nullptr
        || renderData[kOutputReflectionPosW] != nullptr;

    // Check if NRD data should be generated.
    mOutputNRDData =
        renderData[kOutputNRDDiffuseRadianceHitDist] != nullptr
        || renderData[kOutputNRDSpecularRadianceHitDist] != nullptr
        || renderData[kOutputNRDResidualRadianceHitDist] != nullptr
        || renderData[kOutputNRDEmission] != nullptr
        || renderData[kOutputNRDDiffuseReflectance] != nullptr
        || renderData[kOutputNRDSpecularReflectance] != nullptr;

    // Check if additional NRD data should be generated.
    bool prevOutputNRDAdditionalData = mOutputNRDAdditionalData;
    mOutputNRDAdditionalData =
        renderData[kOutputNRDDeltaReflectionRadianceHitDist] != nullptr
        || renderData[kOutputNRDDeltaTransmissionRadianceHitDist] != nullptr
        || renderData[kOutputNRDDeltaReflectionReflectance] != nullptr
        || renderData[kOutputNRDDeltaReflectionEmission] != nullptr
        || renderData[kOutputNRDDeltaReflectionNormWRoughMaterialID] != nullptr
        || renderData[kOutputNRDDeltaReflectionPathLength] != nullptr
        || renderData[kOutputNRDDeltaReflectionHitDist] != nullptr
        || renderData[kOutputNRDDeltaTransmissionReflectance] != nullptr
        || renderData[kOutputNRDDeltaTransmissionEmission] != nullptr
        || renderData[kOutputNRDDeltaTransmissionNormWRoughMaterialID] != nullptr
        || renderData[kOutputNRDDeltaTransmissionPathLength] != nullptr
        || renderData[kOutputNRDDeltaTransmissionPosW] != nullptr;
    if (mOutputNRDAdditionalData != prevOutputNRDAdditionalData) mRecompile = true;

    // Check if initial ray info should be generated.
    bool prevOutputInitialRayInfo = mOutputInitialRayInfo;
    mOutputInitialRayInfo = renderData[kOutputInitialRayInfo] != nullptr;
    if (mOutputInitialRayInfo != prevOutputInitialRayInfo) mRecompile = true;

    // Enable pixel stats if rayCount or pathLength outputs are connected.
    if (renderData[kOutputRayCount] != nullptr || renderData[kOutputPathLength] != nullptr)
    {
        mpPixelStats->setEnabled(true);
    }

    mpPixelStats->beginFrame(pRenderContext, mParams.frameDim);
    mpPixelDebug->beginFrame(pRenderContext, mParams.frameDim);

    // Ensure CIR buffer is properly bound
    if (mpCIRPathBuffer)
    {
        // CIR buffer binding is handled in bindShaderData
        mCIRBufferBound = true;
    }

    // Update the random seed.
    mParams.seed = mParams.useFixedSeed ? mParams.fixedSeed : mParams.frameCount;

    // Reset CIR data for new frame (only reset count, keep buffer allocated)
    mCurrentCIRPathCount = 0;

    mUpdateFlags = IScene::UpdateFlags::None;

    return true;
}

void PathTracer::endFrame(RenderContext* pRenderContext, const RenderData& renderData)
{
    mpPixelStats->endFrame(pRenderContext);
    mpPixelDebug->endFrame(pRenderContext);

    auto copyTexture = [pRenderContext](Texture* pDst, const Texture* pSrc)
    {
        if (pDst && pSrc)
        {
            FALCOR_ASSERT(pDst && pSrc);
            FALCOR_ASSERT(pDst->getFormat() == pSrc->getFormat());
            FALCOR_ASSERT(pDst->getWidth() == pSrc->getWidth() && pDst->getHeight() == pSrc->getHeight());
            pRenderContext->copyResource(pDst, pSrc);
        }
        else if (pDst)
        {
            pRenderContext->clearUAV(pDst->getUAV().get(), uint4(0, 0, 0, 0));
        }
    };

    // Copy pixel stats to outputs if available.
    copyTexture(renderData.getTexture(kOutputRayCount).get(), mpPixelStats->getRayCountTexture(pRenderContext).get());
    copyTexture(renderData.getTexture(kOutputPathLength).get(), mpPixelStats->getPathLengthTexture().get());

    if (mpRTXDI) mpRTXDI->endFrame(pRenderContext);

    mVarsChanged = false;
    mParams.frameCount++;
}

void PathTracer::generatePaths(RenderContext* pRenderContext, const RenderData& renderData)
{
    FALCOR_PROFILE(pRenderContext, "generatePaths");

    // Check shader assumptions.
    // We launch one thread group per screen tile, with threads linearly indexed.
    const uint32_t tileSize = kScreenTileDim.x * kScreenTileDim.y;
    FALCOR_ASSERT(kScreenTileDim.x == 16 && kScreenTileDim.y == 16); // TODO: Remove this temporary limitation when Slang bug has been fixed, see comments in shader.
    FALCOR_ASSERT(kScreenTileBits.x <= 4 && kScreenTileBits.y <= 4); // Since we use 8-bit deinterleave.
    FALCOR_ASSERT(mpGeneratePaths->getThreadGroupSize().x == tileSize);
    FALCOR_ASSERT(mpGeneratePaths->getThreadGroupSize().y == 1 && mpGeneratePaths->getThreadGroupSize().z == 1);

    // Additional specialization. This shouldn't change resource declarations.
    mpGeneratePaths->addDefine("USE_VIEW_DIR", (mpScene->getCamera()->getApertureRadius() > 0 && renderData[kInputViewDir] != nullptr) ? "1" : "0");
    mpGeneratePaths->addDefine("OUTPUT_GUIDE_DATA", mOutputGuideData ? "1" : "0");
    mpGeneratePaths->addDefine("OUTPUT_NRD_DATA", mOutputNRDData ? "1" : "0");
    mpGeneratePaths->addDefine("OUTPUT_NRD_ADDITIONAL_DATA", mOutputNRDAdditionalData ? "1" : "0");

    // Bind resources.
    auto var = mpGeneratePaths->getRootVar()["CB"]["gPathGenerator"];
    bindShaderData(var, renderData, false);

    // Note: PathGenerator does not need CIR buffer as it only initializes paths
    // CIR data collection happens in TracePass

    mpScene->bindShaderData(mpGeneratePaths->getRootVar()["gScene"]);

    if (mpRTXDI) mpRTXDI->bindShaderData(mpGeneratePaths->getRootVar());

    // Launch one thread per pixel.
    // The dimensions are padded to whole tiles to allow re-indexing the threads in the shader.
    mpGeneratePaths->execute(pRenderContext, { mParams.screenTiles.x * tileSize, mParams.screenTiles.y, 1u });
}

void PathTracer::tracePass(RenderContext* pRenderContext, const RenderData& renderData, TracePass& tracePass)
{
    FALCOR_PROFILE(pRenderContext, tracePass.name);

    FALCOR_ASSERT(tracePass.pProgram != nullptr && tracePass.pBindingTable != nullptr && tracePass.pVars != nullptr);

    // Additional specialization. This shouldn't change resource declarations.
    tracePass.pProgram->addDefine("USE_VIEW_DIR", (mpScene->getCamera()->getApertureRadius() > 0 && renderData[kInputViewDir] != nullptr) ? "1" : "0");
    tracePass.pProgram->addDefine("OUTPUT_GUIDE_DATA", mOutputGuideData ? "1" : "0");
    tracePass.pProgram->addDefine("OUTPUT_NRD_DATA", mOutputNRDData ? "1" : "0");
    tracePass.pProgram->addDefine("OUTPUT_NRD_ADDITIONAL_DATA", mOutputNRDAdditionalData ? "1" : "0");

    // Bind global resources.
    auto var = tracePass.pVars->getRootVar();

    if (mVarsChanged) mpSampleGenerator->bindShaderData(var);
    if (mpRTXDI) mpRTXDI->bindShaderData(var);

    mpPixelStats->prepareProgram(tracePass.pProgram, var);
    mpPixelDebug->prepareProgram(tracePass.pProgram, var);

    // Bind the path tracer.
    var["gPathTracer"] = mpPathTracerBlock;

    // Bind CIR buffer to PathTracer parameter block
    bindCIRBufferToParameterBlock(var["gPathTracer"], "gPathTracer");

    // Full screen dispatch.
    mpScene->raytrace(pRenderContext, tracePass.pProgram.get(), tracePass.pVars, uint3(mParams.frameDim, 1));
}

void PathTracer::resolvePass(RenderContext* pRenderContext, const RenderData& renderData)
{
    if (!mOutputGuideData && !mOutputNRDData && !mOutputInitialRayInfo && mFixedSampleCount && mStaticParams.samplesPerPixel == 1) return;

    FALCOR_PROFILE(pRenderContext, "resolvePass");

    // This pass is executed when multiple samples per pixel are used.
    // We launch one thread per pixel that computes the resolved color by iterating over the samples.
    // The samples are arranged in tiles with pixels in Morton order, with samples stored consecutively for each pixel.
    // With adaptive sampling, an extra sample offset lookup table computed by the path generation pass is used to
    // locate the samples for each pixel.

    // Additional specialization. This shouldn't change resource declarations.
    mpResolvePass->addDefine("OUTPUT_GUIDE_DATA", mOutputGuideData ? "1" : "0");
    mpResolvePass->addDefine("OUTPUT_NRD_DATA", mOutputNRDData ? "1" : "0");
    mpResolvePass->addDefine("OUTPUT_INITIAL_RAY_INFO", mOutputInitialRayInfo ? "1" : "0");  // Add define for initialRayInfo

    // Bind resources.
    auto var = mpResolvePass->getRootVar()["CB"]["gResolvePass"];
    var["params"].setBlob(mParams);
    var["sampleCount"] = renderData.getTexture(kInputSampleCount); // Can be nullptr
    var["outputColor"] = renderData.getTexture(kOutputColor);
    var["outputAlbedo"] = renderData.getTexture(kOutputAlbedo);
    var["outputSpecularAlbedo"] = renderData.getTexture(kOutputSpecularAlbedo);
    var["outputIndirectAlbedo"] = renderData.getTexture(kOutputIndirectAlbedo);
    var["outputGuideNormal"] = renderData.getTexture(kOutputGuideNormal);
    var["outputReflectionPosW"] = renderData.getTexture(kOutputReflectionPosW);
    var["outputInitialRayInfo"] = renderData.getTexture(kOutputInitialRayInfo);
    var["outputNRDDiffuseRadianceHitDist"] = renderData.getTexture(kOutputNRDDiffuseRadianceHitDist);
    var["outputNRDSpecularRadianceHitDist"] = renderData.getTexture(kOutputNRDSpecularRadianceHitDist);
    var["outputNRDDeltaReflectionRadianceHitDist"] = renderData.getTexture(kOutputNRDDeltaReflectionRadianceHitDist);
    var["outputNRDDeltaTransmissionRadianceHitDist"] = renderData.getTexture(kOutputNRDDeltaTransmissionRadianceHitDist);
    var["outputNRDResidualRadianceHitDist"] = renderData.getTexture(kOutputNRDResidualRadianceHitDist);

    if (mVarsChanged)
    {
        var["sampleOffset"] = mpSampleOffset; // Can be nullptr
        var["sampleColor"] = mpSampleColor;
        var["sampleGuideData"] = mpSampleGuideData;
        var["sampleNRDRadiance"] = mpSampleNRDRadiance;
        var["sampleNRDHitDist"] = mpSampleNRDHitDist;
        var["sampleNRDEmission"] = mpSampleNRDEmission;
        var["sampleNRDReflectance"] = mpSampleNRDReflectance;
        var["sampleInitialRayInfo"] = mpSampleInitialRayInfo; // 新增：绑定初始光线信息样本缓冲区

        var["sampleNRDPrimaryHitNeeOnDelta"] = mpSampleNRDPrimaryHitNeeOnDelta;
        var["primaryHitDiffuseReflectance"] = renderData.getTexture(kOutputNRDDiffuseReflectance);
    }

    // Bind CIR buffer to ResolvePass parameter block
    bindCIRBufferToParameterBlock(var, "gResolvePass");

    // Launch one thread per pixel.
    mpResolvePass->execute(pRenderContext, { mParams.frameDim, 1u });
}

DefineList PathTracer::StaticParams::getDefines(const PathTracer& owner) const
{
    DefineList defines;

    // Path tracer configuration.
    defines.add("SAMPLES_PER_PIXEL", (owner.mFixedSampleCount ? std::to_string(samplesPerPixel) : "0")); // 0 indicates a variable sample count
    defines.add("MAX_SURFACE_BOUNCES", std::to_string(maxSurfaceBounces));
    defines.add("MAX_DIFFUSE_BOUNCES", std::to_string(maxDiffuseBounces));
    defines.add("MAX_SPECULAR_BOUNCES", std::to_string(maxSpecularBounces));
    defines.add("MAX_TRANSMISSON_BOUNCES", std::to_string(maxTransmissionBounces));
    defines.add("ADJUST_SHADING_NORMALS", adjustShadingNormals ? "1" : "0");
    defines.add("USE_BSDF_SAMPLING", useBSDFSampling ? "1" : "0");
    defines.add("USE_NEE", useNEE ? "1" : "0");
    defines.add("USE_MIS", useMIS ? "1" : "0");
    defines.add("USE_RUSSIAN_ROULETTE", useRussianRoulette ? "1" : "0");
    defines.add("USE_RTXDI", useRTXDI ? "1" : "0");
    defines.add("USE_ALPHA_TEST", useAlphaTest ? "1" : "0");
    defines.add("USE_LIGHTS_IN_DIELECTRIC_VOLUMES", useLightsInDielectricVolumes ? "1" : "0");
    defines.add("DISABLE_CAUSTICS", disableCaustics ? "1" : "0");
    defines.add("PRIMARY_LOD_MODE", std::to_string((uint32_t)primaryLodMode));
    defines.add("USE_NRD_DEMODULATION", useNRDDemodulation ? "1" : "0");
    defines.add("USE_SER", useSER ? "1" : "0");
    defines.add("COLOR_FORMAT", std::to_string((uint32_t)colorFormat));
    defines.add("MIS_HEURISTIC", std::to_string((uint32_t)misHeuristic));
    defines.add("MIS_POWER_EXPONENT", std::to_string(misPowerExponent));

    // 输出通道相关宏
    defines.add("OUTPUT_GUIDE_DATA", owner.mOutputGuideData ? "1" : "0");
    defines.add("OUTPUT_NRD_DATA", owner.mOutputNRDData ? "1" : "0");
    defines.add("OUTPUT_NRD_ADDITIONAL_DATA", owner.mOutputNRDAdditionalData ? "1" : "0");
    defines.add("OUTPUT_INITIAL_RAY_INFO", owner.mOutputInitialRayInfo ? "1" : "0"); // 新增宏定义

    // Sampling utilities configuration.
    FALCOR_ASSERT(owner.mpSampleGenerator);
    defines.add(owner.mpSampleGenerator->getDefines());

    if (owner.mpEmissiveSampler) defines.add(owner.mpEmissiveSampler->getDefines());
    if (owner.mpRTXDI) defines.add(owner.mpRTXDI->getDefines());

    defines.add("INTERIOR_LIST_SLOT_COUNT", std::to_string(maxNestedMaterials));

    defines.add("GBUFFER_ADJUST_SHADING_NORMALS", owner.mGBufferAdjustShadingNormals ? "1" : "0");

    // Scene-specific configuration.
    // Set defaults
    defines.add("USE_ENV_LIGHT", "0");
    defines.add("USE_ANALYTIC_LIGHTS", "0");
    defines.add("USE_EMISSIVE_LIGHTS", "0");
    defines.add("USE_CURVES", "0");
    defines.add("USE_SDF_GRIDS", "0");
    defines.add("USE_HAIR_MATERIAL", "0");

    if (auto scene = dynamic_ref_cast<Scene>(owner.mpScene))
    {
        defines.add(scene->getSceneDefines());
        defines.add("USE_ENV_LIGHT", scene->useEnvLight() ? "1" : "0");
        defines.add("USE_ANALYTIC_LIGHTS", scene->useAnalyticLights() ? "1" : "0");
        defines.add("USE_EMISSIVE_LIGHTS", scene->useEmissiveLights() ? "1" : "0");
        defines.add("USE_CURVES", (scene->hasGeometryType(Scene::GeometryType::Curve)) ? "1" : "0");
        defines.add("USE_SDF_GRIDS", scene->hasGeometryType(Scene::GeometryType::SDFGrid) ? "1" : "0");
        defines.add("USE_HAIR_MATERIAL", scene->getMaterialCountByType(MaterialType::Hair) > 0u ? "1" : "0");
    }

    // Set default (off) values for additional features.
    defines.add("USE_VIEW_DIR", "0");
    defines.add("OUTPUT_GUIDE_DATA", "0");
    defines.add("OUTPUT_NRD_DATA", "0");
    defines.add("OUTPUT_NRD_ADDITIONAL_DATA", "0");

    return defines;
}

/*******************************************************************
                     CIR Buffer Management Functions
*******************************************************************/

void PathTracer::allocateCIRBuffers()
{
    // Check if buffer needs reallocation
    if (mpCIRPathBuffer && mpCIRPathBuffer->getElementCount() >= mMaxCIRPaths) return;

    logInfo("CIR: Starting buffer allocation...");
    logInfo("CIR: Requested buffer size - Elements: {}, Element size: {} bytes",
            mMaxCIRPaths, 48u); // Estimated size: 8 fields * 4 bytes + padding
    logInfo("CIR: Total buffer size: {:.2f} MB",
            (mMaxCIRPaths * 48u) / (1024.0f * 1024.0f));

    try
    {
        // Create CIR path data buffer using device API
        // Element size: 48 bytes (6 floats + 1 uint + 1 uint2 = 8*4 + padding)
        mpCIRPathBuffer = mpDevice->createStructuredBuffer(
            48u, // Element size in bytes
            mMaxCIRPaths,
            ResourceBindFlags::ShaderResource | ResourceBindFlags::UnorderedAccess,
            MemoryType::DeviceLocal,
            nullptr,
            false
        );

        if (!mpCIRPathBuffer)
        {
            logError("CIR: Failed to allocate CIR path buffer");
            mCIRBufferBound = false;
            return;
        }

        // Success log
        logInfo("CIR: Buffer allocation successful");
        logInfo("CIR: Buffer element count: {}", mpCIRPathBuffer->getElementCount());
        logInfo("CIR: Buffer total size: {:.2f} MB", mpCIRPathBuffer->getSize() / (1024.0f * 1024.0f));

        mVarsChanged = true;
        mCIRBufferBound = false; // Need to rebind after allocation
    }
    catch (const std::exception& e)
    {
        logError("CIR: Exception during buffer allocation: {}", e.what());
        mpCIRPathBuffer = nullptr;
        mCIRBufferBound = false;
    }
}

bool PathTracer::bindCIRBufferToShader()
{
    if (!mpCIRPathBuffer)
    {
        logError("CIR: Cannot bind buffer - buffer not allocated");
        mCIRBufferBound = false;
        return false;
    }

    if (mCIRBufferBound) return true; // Already bound

    try
    {
        // The buffer binding will be handled in bindShaderData() function
        // This is just a state check function
        mCIRBufferBound = true;
        logInfo("CIR: Buffer ready for binding to shader");
        logInfo("CIR: Buffer element count: {}", mpCIRPathBuffer->getElementCount());
        return true;
    }
    catch (const std::exception& e)
    {
        logError("CIR: Exception during buffer binding preparation: {}", e.what());
        mCIRBufferBound = false;
        return false;
    }
}

bool PathTracer::bindCIRBufferToParameterBlock(const ShaderVar& parameterBlock, const std::string& blockName) const
{
    if (!mpCIRPathBuffer)
    {
        logWarning("CIR: Cannot bind buffer - buffer not allocated");
        return false;
    }

    try
    {
        // Bind CIR buffer to the parameter block member
        parameterBlock["gCIRPathBuffer"] = mpCIRPathBuffer;
        
        // Verify binding was successful using findMember
        auto member = parameterBlock.findMember("gCIRPathBuffer");
        if (member.isValid())
        {
            logInfo("CIR: Buffer successfully bound to parameter block '{}' member 'gCIRPathBuffer'", blockName);
            logInfo("CIR: Bound buffer element count: {}", mpCIRPathBuffer->getElementCount());
            return true;
        }
        else
        {
            logError("CIR: Buffer binding verification failed - member 'gCIRPathBuffer' not found in parameter block '{}'", blockName);
            return false;
        }
    }
    catch (const std::exception& e)
    {
        logError("CIR: Exception during buffer binding to parameter block '{}': {}", blockName, e.what());
        return false;
    }
}

void PathTracer::resetCIRData()
{
    logInfo("CIR: Resetting buffer data...");

    mCurrentCIRPathCount = 0;

    if (!mpCIRPathBuffer)
    {
        logWarning("CIR: Cannot reset - buffer not allocated");
        return;
    }

    try
    {
        // Note: Buffer clearing would need RenderContext which is not available here
        // The buffer will be implicitly cleared when written to
        
        logInfo("CIR: Buffer reset complete - path count: {}", mCurrentCIRPathCount);
        logInfo("CIR: Buffer state - Allocated: {}, Bound: {}",
                mpCIRPathBuffer ? "Yes" : "No",
                mCIRBufferBound ? "Yes" : "No");
    }
    catch (const std::exception& e)
    {
        logError("CIR: Exception during buffer reset: {}", e.what());
    }
}

void PathTracer::logCIRBufferStatus()
{
    logInfo("=== CIR Buffer Status ===");
    logInfo("CIR: Buffer allocated: {}", mpCIRPathBuffer ? "Yes" : "No");
    logInfo("CIR: Buffer bound to shader: {}", mCIRBufferBound ? "Yes" : "No");
    logInfo("CIR: Current path count: {}", mCurrentCIRPathCount);
    logInfo("CIR: Max path capacity: {}", mMaxCIRPaths);

    if (mpCIRPathBuffer)
    {
        float usagePercent = static_cast<float>(mCurrentCIRPathCount) / mMaxCIRPaths * 100.0f;
        logInfo("CIR: Buffer usage: {:.2f}%", usagePercent);

        if (usagePercent > 90.0f)
        {
            logWarning("CIR: Buffer usage exceeds 90% - consider increasing buffer size");
        }
        else if (usagePercent > 80.0f)
        {
            logWarning("CIR: Buffer usage exceeds 80% - monitor closely");
        }

        logInfo("CIR: Buffer details:");
        logInfo("  - Total size: {:.2f} MB", mpCIRPathBuffer->getSize() / (1024.0f * 1024.0f));
    }
    logInfo("========================");
}

/*******************************************************************
                     CIR Data Verification and Debugging Functions
*******************************************************************/

struct CIRPathDataCPU
{
    float pathLength;           
    float emissionAngle;        
    float receptionAngle;       
    float reflectanceProduct;   
    uint32_t reflectionCount;   
    float emittedPower;         
    uint32_t pixelCoordX;       
    uint32_t pixelCoordY;       
};

void PathTracer::dumpCIRDataToFile(RenderContext* pRenderContext)
{
    if (!mpCIRPathBuffer || mCurrentCIRPathCount == 0)
    {
        logWarning("CIR: Cannot dump data - no valid CIR data available");
        return;
    }

    logInfo("CIR: Starting data dump to file...");
    logInfo("CIR: Dumping {} paths from buffer", mCurrentCIRPathCount);

    try
    {
        // Create output directory if it doesn't exist
        std::filesystem::create_directories(mCIROutputDirectory);

        // Read buffer data from GPU
        const CIRPathDataCPU* pData = static_cast<const CIRPathDataCPU*>(
            mpCIRPathBuffer->map()
        );

        if (!pData)
        {
            logError("CIR: Failed to map buffer for reading");
            return;
        }

        // Generate output filename with timestamp
        auto now = std::time(nullptr);
        std::stringstream filename;
        filename << mCIROutputDirectory << "/cir_data_frame_" << std::put_time(std::localtime(&now), "%Y%m%d_%H%M%S") << ".csv";

        std::ofstream file(filename.str());
        if (!file.is_open())
        {
            logError("CIR: Failed to open output file: {}", filename.str());
            mpCIRPathBuffer->unmap();
            return;
        }

        // Write CSV header
        file << "PathIndex,PathLength(m),EmissionAngle(deg),ReceptionAngle(deg),ReflectanceProduct,ReflectionCount,EmittedPower(W),PixelX,PixelY\n";

        // Process and validate data
        uint32_t validPaths = 0;
        uint32_t invalidPaths = 0;
        float totalPathLength = 0.0f;
        float minPathLength = FLT_MAX;
        float maxPathLength = 0.0f;

        for (uint32_t i = 0; i < std::min(mCurrentCIRPathCount, mMaxCIRPaths); ++i)
        {
            const auto& data = pData[i];

            // Validate data integrity
            bool isValid = true;
            if (data.pathLength <= 0.0f || data.pathLength > 1000.0f ||
                data.emissionAngle < 0.0f || data.emissionAngle > float(M_PI) ||
                data.receptionAngle < 0.0f || data.receptionAngle > float(M_PI) ||
                data.reflectanceProduct < 0.0f || data.reflectanceProduct > 1.0f ||
                data.emittedPower < 0.0f || std::isnan(data.emittedPower) || std::isinf(data.emittedPower))
            {
                isValid = false;
                invalidPaths++;
            }
            else
            {
                validPaths++;
                totalPathLength += data.pathLength;
                minPathLength = std::min(minPathLength, data.pathLength);
                maxPathLength = std::max(maxPathLength, data.pathLength);
            }

            // Write to file (include invalid data for analysis)
            file << i << ","
                 << data.pathLength << ","
                 << (data.emissionAngle * 180.0f / float(M_PI)) << ","
                 << (data.receptionAngle * 180.0f / float(M_PI)) << ","
                 << data.reflectanceProduct << ","
                 << data.reflectionCount << ","
                 << data.emittedPower << ","
                 << data.pixelCoordX << ","
                 << data.pixelCoordY << "\n";
        }

        file.close();
        mpCIRPathBuffer->unmap();

        // Log comprehensive statistics
        logInfo("CIR: Data dump completed successfully");
        logInfo("CIR: Output file: {}", filename.str());
        logInfo("CIR: Total paths processed: {}", mCurrentCIRPathCount);
        logInfo("CIR: Valid paths: {}", validPaths);
        logInfo("CIR: Invalid paths: {}", invalidPaths);
        logInfo("CIR: Data integrity: {:.2f}%", (float)validPaths / mCurrentCIRPathCount * 100.0f);

        if (validPaths > 0)
        {
            logInfo("CIR: Path length statistics:");
            logInfo("  - Average: {:.3f}m", totalPathLength / validPaths);
            logInfo("  - Minimum: {:.3f}m", minPathLength);
            logInfo("  - Maximum: {:.3f}m", maxPathLength);
        }
    }
    catch (const std::exception& e)
    {
        logError("CIR: Exception during data dump: {}", e.what());
        // Note: Buffer will be automatically unmapped when function exits
    }
}

void PathTracer::logCIRStatistics(RenderContext* pRenderContext)
{
    if (!mpCIRPathBuffer || mCurrentCIRPathCount == 0)
    {
        logInfo("CIR: No statistics available - no data collected");
        return;
    }

    logInfo("=== CIR Data Statistics ===");
    logInfo("CIR: Collection status:");
    logInfo("  - Buffer allocated: {}", mpCIRPathBuffer ? "Yes" : "No");
    logInfo("  - Buffer bound: {}", mCIRBufferBound ? "Yes" : "No");
    logInfo("  - Paths collected: {}", mCurrentCIRPathCount);
    logInfo("  - Buffer capacity: {}", mMaxCIRPaths);
    logInfo("  - Usage percentage: {:.2f}%", (float)mCurrentCIRPathCount / mMaxCIRPaths * 100.0f);

    if (mpCIRPathBuffer)
    {
        // Calculate element size from total size and element count
        uint32_t elementSize = mpCIRPathBuffer->getElementCount() > 0 ? 
            static_cast<uint32_t>(mpCIRPathBuffer->getSize()) / mpCIRPathBuffer->getElementCount() : 0;
        
        logInfo("CIR: Buffer technical details:");
        logInfo("  - Element size: {} bytes", elementSize);
        logInfo("  - Total size: {:.2f} MB", mpCIRPathBuffer->getSize() / (1024.0f * 1024.0f));
        logInfo("  - Memory used: {:.2f} KB", (mCurrentCIRPathCount * elementSize) / 1024.0f);
    }

    logInfo("==============================");
}

void PathTracer::verifyCIRDataIntegrity(RenderContext* pRenderContext)
{
    if (!mpCIRPathBuffer || mCurrentCIRPathCount == 0)
    {
        logWarning("CIR: Cannot verify data integrity - no data available");
        return;
    }

    logInfo("CIR: Starting data integrity verification...");

    try
    {
        const CIRPathDataCPU* pData = static_cast<const CIRPathDataCPU*>(
            mpCIRPathBuffer->map()
        );

        if (!pData)
        {
            logError("CIR: Failed to map buffer for verification");
            return;
        }

        uint32_t validPaths = 0;
        uint32_t pathLengthErrors = 0;
        uint32_t angleErrors = 0;
        uint32_t reflectanceErrors = 0;
        uint32_t powerErrors = 0;

        // Verify each path
        for (uint32_t i = 0; i < std::min(mCurrentCIRPathCount, mMaxCIRPaths); ++i)
        {
            const auto& data = pData[i];
            bool pathValid = true;

            // Check path length (should be in reasonable range)
            if (data.pathLength <= 0.0f || data.pathLength > 1000.0f)
            {
                pathLengthErrors++;
                pathValid = false;
            }

            // Check emission and reception angles (should be 0 to PI)
            if (data.emissionAngle < 0.0f || data.emissionAngle > float(M_PI) ||
                data.receptionAngle < 0.0f || data.receptionAngle > float(M_PI))
            {
                angleErrors++;
                pathValid = false;
            }

            // Check reflectance product (should be 0 to 1)
            if (data.reflectanceProduct < 0.0f || data.reflectanceProduct > 1.0f)
            {
                reflectanceErrors++;
                pathValid = false;
            }

            // Check emitted power (should be positive and finite)
            if (data.emittedPower < 0.0f || std::isnan(data.emittedPower) || std::isinf(data.emittedPower))
            {
                powerErrors++;
                pathValid = false;
            }

            if (pathValid) validPaths++;
        }

        mpCIRPathBuffer->unmap();

        // Report verification results
        logInfo("CIR: Data integrity verification completed");
        logInfo("CIR: Total paths checked: {}", mCurrentCIRPathCount);
        logInfo("CIR: Valid paths: {}", validPaths);
        logInfo("CIR: Overall integrity: {:.2f}%", (float)validPaths / mCurrentCIRPathCount * 100.0f);

        if (pathLengthErrors > 0 || angleErrors > 0 || reflectanceErrors > 0 || powerErrors > 0)
        {
            logWarning("CIR: Data integrity issues detected:");
            if (pathLengthErrors > 0) logWarning("  - Path length errors: {}", pathLengthErrors);
            if (angleErrors > 0) logWarning("  - Angle errors: {}", angleErrors);
            if (reflectanceErrors > 0) logWarning("  - Reflectance errors: {}", reflectanceErrors);
            if (powerErrors > 0) logWarning("  - Power errors: {}", powerErrors);
        }
        else
        {
            logInfo("CIR: All data passed integrity checks!");
        }
    }
    catch (const std::exception& e)
    {
        logError("CIR: Exception during data verification: {}", e.what());
        // Note: Buffer will be automatically unmapped when function exits
    }
}

void PathTracer::outputCIRSampleData(RenderContext* pRenderContext, uint32_t sampleCount)
{
    if (!mpCIRPathBuffer || mCurrentCIRPathCount == 0)
    {
        logInfo("CIR: No sample data available");
        return;
    }

    sampleCount = std::min(sampleCount, mCurrentCIRPathCount);
    logInfo("CIR: Outputting {} sample data entries:", sampleCount);

    try
    {
        const CIRPathDataCPU* pData = static_cast<const CIRPathDataCPU*>(
            mpCIRPathBuffer->map()
        );

        if (!pData)
        {
            logError("CIR: Failed to map buffer for sample output");
            return;
        }

        for (uint32_t i = 0; i < sampleCount; ++i)
        {
            const auto& data = pData[i];
            
            logInfo("CIR: Sample {}: Length={:.3f}m, EmissionAngle={:.1f}deg, ReceptionAngle={:.1f}deg, Reflectance={:.3f}, Reflections={}, Power={:.6f}W, Pixel=({},{})",
                i,
                data.pathLength,
                data.emissionAngle * 180.0f / float(M_PI),
                data.receptionAngle * 180.0f / float(M_PI),
                data.reflectanceProduct,
                data.reflectionCount,
                data.emittedPower,
                data.pixelCoordX,
                data.pixelCoordY
            );
        }

        mpCIRPathBuffer->unmap();
    }
    catch (const std::exception& e)
    {
        logError("CIR: Exception during sample output: {}", e.what());
        // Note: Buffer will be automatically unmapped when function exits
    }
}

bool PathTracer::hasValidCIRData() const
{
    return mpCIRPathBuffer && mCIRBufferBound && mCurrentCIRPathCount > 0;
}

void PathTracer::triggerCIRDataVerification(RenderContext* pRenderContext)
{
    if (!mCIRDebugEnabled) return;

    // Check if it's time for periodic verification
    static uint32_t frameCounter = 0;
    frameCounter++;

    if (frameCounter - mLastCIRCheckFrame >= mCIRFrameCheckInterval)
    {
        mLastCIRCheckFrame = frameCounter;

        logInfo("CIR: Performing periodic data verification (Frame {})", frameCounter);
        
        // Perform comprehensive verification
        logCIRBufferStatus();
        logCIRStatistics(pRenderContext);
        
        if (hasValidCIRData())
        {
            outputCIRSampleData(pRenderContext, 5); // Show 5 sample entries
            verifyCIRDataIntegrity(pRenderContext);
            
            // Dump data to file every 10 check intervals
            if ((frameCounter / mCIRFrameCheckInterval) % 10 == 0)
            {
                dumpCIRDataToFile(pRenderContext);
            }
        }
        else
        {
            logWarning("CIR: No valid CIR data detected during verification");
        }
    }
}
