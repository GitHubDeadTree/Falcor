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
#include "AccumulatePass.h"
#include "RenderGraph/RenderPassStandardFlags.h"

static void regAccumulatePass(pybind11::module& m)
{
    pybind11::class_<AccumulatePass, RenderPass, ref<AccumulatePass>> pass(m, "AccumulatePass");
    pass.def_property("enabled", &AccumulatePass::isEnabled, &AccumulatePass::setEnabled);
    pass.def("reset", &AccumulatePass::reset);
    pass.def_property_readonly("averageValue", &AccumulatePass::getAverageValue);
}

extern "C" FALCOR_API_EXPORT void registerPlugin(Falcor::PluginRegistry& registry)
{
    registry.registerClass<RenderPass, AccumulatePass>();
    ScriptBindings::registerBinding(regAccumulatePass);
}

namespace
{
const char kShaderFile[] = "RenderPasses/AccumulatePass/Accumulate.cs.slang";

const char kInputChannel[] = "input";
const char kOutputChannel[] = "output";
const char kInputScalarChannel[] = "inputScalar";
const char kOutputScalarChannel[] = "outputScalar";

// Serialized parameters
const char kEnabled[] = "enabled";
const char kOutputFormat[] = "outputFormat";
const char kOutputSize[] = "outputSize";
const char kFixedOutputSize[] = "fixedOutputSize";
const char kAutoReset[] = "autoReset";
const char kPrecisionMode[] = "precisionMode";
const char kMaxFrameCount[] = "maxFrameCount";
const char kOverflowMode[] = "overflowMode";
const char kComputeAverage[] = "computeAverage";
} // namespace

AccumulatePass::AccumulatePass(ref<Device> pDevice, const Properties& props) : RenderPass(pDevice)
{
    // Deserialize pass from dictionary.
    for (const auto& [key, value] : props)
    {
        if (key == kEnabled)
            mEnabled = value;
        else if (key == kOutputFormat)
            mOutputFormat = value;
        else if (key == kOutputSize)
            mOutputSizeSelection = value;
        else if (key == kFixedOutputSize)
            mFixedOutputSize = value;
        else if (key == kAutoReset)
            mAutoReset = value;
        else if (key == kPrecisionMode)
            mPrecisionMode = value;
        else if (key == kMaxFrameCount)
            mMaxFrameCount = value;
        else if (key == kOverflowMode)
            mOverflowMode = value;
        else if (key == kComputeAverage)
            mComputeAverage = value;
        else
            logWarning("Unknown property '{}' in AccumulatePass properties.", key);
    }

    if (props.has("enableAccumulation"))
    {
        logWarning("'enableAccumulation' is deprecated. Use 'enabled' instead.");
        if (!props.has(kEnabled))
            mEnabled = props["enableAccumulation"];
    }

    mpState = ComputeState::create(mpDevice);

    // Create ParallelReduction instance
    mpParallelReduction = std::make_unique<ParallelReduction>(pDevice);

    // Create result buffer (16 bytes, size of float4)
    mpAverageResultBuffer = pDevice->createBuffer(sizeof(float4), ResourceBindFlags::None, MemoryType::ReadBack);
    mpAverageResultBuffer->setName("AccumulatePass::AverageResultBuffer");
}

Properties AccumulatePass::getProperties() const
{
    Properties props;
    props[kEnabled] = mEnabled;
    if (mOutputFormat != ResourceFormat::Unknown)
        props[kOutputFormat] = mOutputFormat;
    props[kOutputSize] = mOutputSizeSelection;
    if (mOutputSizeSelection == RenderPassHelpers::IOSize::Fixed)
        props[kFixedOutputSize] = mFixedOutputSize;
    props[kAutoReset] = mAutoReset;
    props[kPrecisionMode] = mPrecisionMode;
    props[kMaxFrameCount] = mMaxFrameCount;
    props[kOverflowMode] = mOverflowMode;
    props[kComputeAverage] = mComputeAverage;
    return props;
}

RenderPassReflection AccumulatePass::reflect(const CompileData& compileData)
{
    RenderPassReflection reflector;
    const uint2 sz = RenderPassHelpers::calculateIOSize(mOutputSizeSelection, mFixedOutputSize, compileData.defaultTexDims);
    const auto fmt = mOutputFormat != ResourceFormat::Unknown ? mOutputFormat : ResourceFormat::RGBA32Float;
    const auto scalarFmt = ResourceFormat::R32Float;

    reflector.addInput(kInputChannel, "Input data to be temporally accumulated")
        .bindFlags(ResourceBindFlags::ShaderResource)
        .flags(RenderPassReflection::Field::Flags::Optional);
    reflector.addOutput(kOutputChannel, "Output data that is temporally accumulated")
        .bindFlags(ResourceBindFlags::RenderTarget | ResourceBindFlags::UnorderedAccess | ResourceBindFlags::ShaderResource)
        .format(fmt)
        .texture2D(sz.x, sz.y);

    reflector.addInput(kInputScalarChannel, "Single-channel input data to be temporally accumulated")
        .bindFlags(ResourceBindFlags::ShaderResource)
        .flags(RenderPassReflection::Field::Flags::Optional);
    reflector.addOutput(kOutputScalarChannel, "Single-channel output data that is temporally accumulated")
        .bindFlags(ResourceBindFlags::RenderTarget | ResourceBindFlags::UnorderedAccess | ResourceBindFlags::ShaderResource)
        .format(scalarFmt)
        .texture2D(sz.x, sz.y);

    return reflector;
}

void AccumulatePass::execute(RenderContext* pRenderContext, const RenderData& renderData)
{
    if (mAutoReset)
    {
        // Query refresh flags passed down from the application and other passes.
        auto& dict = renderData.getDictionary();
        auto refreshFlags = dict.getValue(kRenderPassRefreshFlags, RenderPassRefreshFlags::None);

        // If any refresh flag is set, we reset frame accumulation.
        if (refreshFlags != RenderPassRefreshFlags::None)
            reset();

        // Reset accumulation upon all scene changes, except camera jitter and history changes.
        // TODO: Add UI options to select which changes should trigger reset
        if (mpScene)
        {
            auto sceneUpdates = mpScene->getUpdates();
            if ((sceneUpdates & ~IScene::UpdateFlags::CameraPropertiesChanged) != IScene::UpdateFlags::None)
            {
                reset();
            }
            if (is_set(sceneUpdates, IScene::UpdateFlags::CameraPropertiesChanged))
            {
                auto excluded = Camera::Changes::Jitter | Camera::Changes::History;
                auto cameraChanges = mpScene->getCamera()->getChanges();
                if ((cameraChanges & ~excluded) != Camera::Changes::None)
                    reset();
            }
        }
    }

    // Check if we reached max number of frames to accumulate and handle overflow.
    if (mMaxFrameCount > 0 && mFrameCount == mMaxFrameCount)
    {
        switch (mOverflowMode)
        {
        case OverflowMode::Stop:
            return;
        case OverflowMode::Reset:
            reset();
            break;
        case OverflowMode::EMA:
            break;
        }
    }

    ref<Texture> pSrc = renderData.getTexture(kInputChannel);
    ref<Texture> pDst = renderData.getTexture(kOutputChannel);
    ref<Texture> pScalarSrc = renderData.getTexture(kInputScalarChannel);
    ref<Texture> pScalarDst = renderData.getTexture(kOutputScalarChannel);

    bool hasStandardData = pSrc && pDst;
    bool hasScalarData = pScalarSrc && pScalarDst;

    if (!hasStandardData && !hasScalarData)
    {
        logWarning("AccumulatePass::execute() - No valid input/output combination found. Pass will be skipped.");
        return;
    }

    // Prepare accumulation resources and shaders.
    uint32_t width = 0;
    uint32_t height = 0;
    if (hasStandardData)
    {
        FALCOR_ASSERT(pSrc->getWidth() == pDst->getWidth() && pSrc->getHeight() == pDst->getHeight());
        width = pSrc->getWidth();
        height = pSrc->getHeight();
        mSrcType = getFormatType(pSrc->getFormat());
    }
    if (hasScalarData)
    {
        FALCOR_ASSERT(pScalarSrc->getWidth() == pScalarDst->getWidth() && pScalarSrc->getHeight() == pScalarDst->getHeight());
        width = std::max(width, pScalarSrc->getWidth());
        height = std::max(height, pScalarSrc->getHeight());
        mScalarSrcType = getFormatType(pScalarSrc->getFormat());
    }
    FALCOR_ASSERT(width > 0 && height > 0);

    prepareAccumulation(pRenderContext, width, height);
    assert(mFrameDim.x == width && mFrameDim.y == height);

    // Perform accumulation of standard data.
    if (hasStandardData)
    {
        accumulate(pRenderContext, pSrc, pDst);
    }

    // Perform accumulation of scalar data.
    if (hasScalarData)
    {
        accumulateScalar(pRenderContext, pScalarSrc, pScalarDst);

        // Compute average value of scalar output if enabled
        if (mComputeAverage)
        {
            computeAverageValue(pRenderContext, pScalarDst);
        }
    }
}

void AccumulatePass::accumulate(RenderContext* pRenderContext, const ref<Texture>& pSrc, const ref<Texture>& pDst)
{
    FALCOR_ASSERT(pSrc && pDst);
    FALCOR_ASSERT(pSrc->getWidth() == mFrameDim.x && pSrc->getHeight() == mFrameDim.y);
    FALCOR_ASSERT(pDst->getWidth() == mFrameDim.x && pDst->getHeight() == mFrameDim.y);
    const FormatType srcType = getFormatType(pSrc->getFormat());

    // If for the first time, or if the input format type has changed, (re)compile the programs.
    if (mpProgram.empty() || srcType != mSrcType)
    {
        DefineList defines;
        switch (srcType)
        {
        case FormatType::Uint:
            defines.add("_INPUT_FORMAT", "INPUT_FORMAT_UINT");
            break;
        case FormatType::Sint:
            defines.add("_INPUT_FORMAT", "INPUT_FORMAT_SINT");
            break;
        default:
            defines.add("_INPUT_FORMAT", "INPUT_FORMAT_FLOAT");
            break;
        }
        // Create accumulation programs.
        // Note only compensated summation needs precise floating-point mode.
        mpProgram[Precision::Double] =
            Program::createCompute(mpDevice, kShaderFile, "accumulateDouble", defines, SlangCompilerFlags::TreatWarningsAsErrors);
        mpProgram[Precision::Single] =
            Program::createCompute(mpDevice, kShaderFile, "accumulateSingle", defines, SlangCompilerFlags::TreatWarningsAsErrors);
        mpProgram[Precision::SingleCompensated] = Program::createCompute(
            mpDevice,
            kShaderFile,
            "accumulateSingleCompensated",
            defines,
            SlangCompilerFlags::FloatingPointModePrecise | SlangCompilerFlags::TreatWarningsAsErrors
        );
        mpVars = ProgramVars::create(mpDevice, mpProgram[mPrecisionMode]->getReflector());

        mSrcType = srcType;
    }

    // Setup accumulation.
    prepareAccumulation(pRenderContext, mFrameDim.x, mFrameDim.y);

    // Set shader parameters.
    auto var = mpVars->getRootVar();
    var["PerFrameCB"]["gResolution"] = mFrameDim;
    var["PerFrameCB"]["gAccumCount"] = mFrameCount;
    var["PerFrameCB"]["gAccumulate"] = mEnabled;
    var["PerFrameCB"]["gMovingAverageMode"] = (mMaxFrameCount > 0);
    var["gCurFrame"] = pSrc;
    var["gOutputFrame"] = pDst;

    // Bind accumulation buffers. Some of these may be nullptr's.
    var["gLastFrameSum"] = mpLastFrameSum;
    var["gLastFrameCorr"] = mpLastFrameCorr;
    var["gLastFrameSumLo"] = mpLastFrameSumLo;
    var["gLastFrameSumHi"] = mpLastFrameSumHi;

    // Update the frame count.
    // The accumulation limit (mMaxFrameCount) has a special value of 0 (no limit) and is not supported in the SingleCompensated mode.
    if (mMaxFrameCount == 0 || mPrecisionMode == Precision::SingleCompensated || mFrameCount < mMaxFrameCount)
    {
        mFrameCount++;
    }

    // Run the accumulation program.
    auto pProgram = mpProgram[mPrecisionMode];
    FALCOR_ASSERT(pProgram);
    uint3 numGroups = div_round_up(uint3(mFrameDim.x, mFrameDim.y, 1u), pProgram->getReflector()->getThreadGroupSize());
    mpState->setProgram(pProgram);
    pRenderContext->dispatch(mpState.get(), mpVars.get(), numGroups);
}

void AccumulatePass::accumulateScalar(RenderContext* pRenderContext, const ref<Texture>& pSrc, const ref<Texture>& pDst)
{
    FALCOR_ASSERT(pSrc && pDst);
    FALCOR_ASSERT(pSrc->getWidth() == mFrameDim.x && pSrc->getHeight() == mFrameDim.y);
    FALCOR_ASSERT(pDst->getWidth() == mFrameDim.x && pDst->getHeight() == mFrameDim.y);
    const FormatType srcType = getFormatType(pSrc->getFormat());

    // If for the first time, or if the input format type has changed, (re)compile the programs.
    if (mpScalarProgram.empty() || srcType != mScalarSrcType)
    {
        DefineList defines;
        switch (srcType)
        {
        case FormatType::Uint:
            defines.add("_INPUT_FORMAT", "INPUT_FORMAT_UINT");
            break;
        case FormatType::Sint:
            defines.add("_INPUT_FORMAT", "INPUT_FORMAT_SINT");
            break;
        default:
            defines.add("_INPUT_FORMAT", "INPUT_FORMAT_FLOAT");
            break;
        }
        // Add single-channel definition
        defines.add("_SCALAR_MODE", "1");

        // Create single-channel accumulation programs.
        mpScalarProgram[Precision::Double] =
            Program::createCompute(mpDevice, kShaderFile, "accumulateScalarDouble", defines, SlangCompilerFlags::TreatWarningsAsErrors);
        mpScalarProgram[Precision::Single] =
            Program::createCompute(mpDevice, kShaderFile, "accumulateScalarSingle", defines, SlangCompilerFlags::TreatWarningsAsErrors);
        mpScalarProgram[Precision::SingleCompensated] = Program::createCompute(
            mpDevice,
            kShaderFile,
            "accumulateScalarSingleCompensated",
            defines,
            SlangCompilerFlags::FloatingPointModePrecise | SlangCompilerFlags::TreatWarningsAsErrors
        );
        mpScalarVars = ProgramVars::create(mpDevice, mpScalarProgram[mPrecisionMode]->getReflector());

        mScalarSrcType = srcType;
    }

    // Setup accumulation.
    prepareAccumulation(pRenderContext, mFrameDim.x, mFrameDim.y);

    // Set shader parameters.
    auto var = mpScalarVars->getRootVar();
    var["PerFrameCB"]["gResolution"] = mFrameDim;
    var["PerFrameCB"]["gAccumCount"] = mFrameCount;
    var["PerFrameCB"]["gAccumulate"] = mEnabled;
    var["PerFrameCB"]["gMovingAverageMode"] = (mMaxFrameCount > 0);
    var["gScalarCurFrame"] = pSrc;
    var["gScalarOutputFrame"] = pDst;

    // Bind accumulation buffers. Some of these may be nullptr's.
    var["gScalarLastFrameSum"] = mpScalarLastFrameSum;
    var["gScalarLastFrameCorr"] = mpScalarLastFrameCorr;
    var["gScalarLastFrameSumLo"] = mpScalarLastFrameSumLo;
    var["gScalarLastFrameSumHi"] = mpScalarLastFrameSumHi;

    // Update the frame count.
    // The accumulation limit (mMaxFrameCount) has a special value of 0 (no limit) and is not supported in the SingleCompensated mode.
    if (mMaxFrameCount == 0 || mPrecisionMode == Precision::SingleCompensated || mFrameCount < mMaxFrameCount)
    {
        // Avoid duplicate counting, only increase frame count if there's no standard RGB channel
        if (mpVars == nullptr)
        {
            mFrameCount++;
        }
    }

    // Run the accumulation program.
    auto pProgram = mpScalarProgram[mPrecisionMode];
    FALCOR_ASSERT(pProgram);
    uint3 numGroups = div_round_up(uint3(mFrameDim.x, mFrameDim.y, 1u), pProgram->getReflector()->getThreadGroupSize());
    mpState->setProgram(pProgram);
    pRenderContext->dispatch(mpState.get(), mpScalarVars.get(), numGroups);
}

void AccumulatePass::renderUI(Gui::Widgets& widget)
{
    widget.checkbox("Enabled", mEnabled);
    widget.tooltip("Enable/disable accumulation.");

    if (widget.checkbox("Auto Reset", mAutoReset))
    {
        if (mAutoReset)
            reset();
    }
    widget.tooltip("Reset accumulation automatically upon scene changes or refresh flags.");

    if (widget.button("Reset", true))
        reset();
    widget.tooltip("Reset accumulation.");

    widget.text("Frame count: " + std::to_string(mFrameCount));

    // Only enable the combo box for selecting precision in modes that use compute shaders.
    if (widget.dropdown("Mode", mPrecisionMode))
    {
        reset();
    }
    widget.tooltip(
        "Precision mode selection:\n\n"
        "Double:\n"
        "Standard summation in double precision. Slow but accurate.\n\n"
        "Single:\n"
        "Standard summation in single precision. Fast but may result in excessive variance on accumulation.\n\n"
        "SingleCompensated:\n"
        "Compensated summation using Kahan summation in single precision. Good balance between speed and precision."
    );

    widget.var("Max Frame Count", mMaxFrameCount, 0u, UINT_MAX, 1u);
    widget.tooltip("Maximum number of frames to accumulate before triggering overflow handler. Set to 0 for unlimited.");

    if (mMaxFrameCount > 0)
    {
        widget.dropdown("Overflow Mode", mOverflowMode);
        widget.tooltip(
            "Overflow handler:\n\n"
            "Stop:\n"
            "Stop accumulation and retain accumulated image when max frame count is reached.\n\n"
            "Reset:\n"
            "Reset accumulation and continue when max frame count is reached.\n\n"
            "EMA:\n"
            "Switch to exponential moving average when max frame count is reached."
        );
    }

    // Add average value calculation controls
    widget.separator();
    widget.text("--- Average Value ---");
    widget.checkbox("Compute Average", mComputeAverage);
    widget.tooltip("When enabled, computes the average value of the scalar output texture.");

    if (mComputeAverage && mFrameCount > 0)
    {
        std::string avgText = "Average Value: " + std::to_string(mAverageValue);
        widget.text(avgText);
    }
    else if (mFrameCount == 0)
    {
        widget.text("Average not available (no frames accumulated)");
    }
    else
    {
        widget.text("Average calculation disabled");
    }
}

void AccumulatePass::setScene(RenderContext* pRenderContext, const ref<Scene>& pScene)
{
    mpScene = pScene;

    // Reset accumulation when the scene changes.
    reset();
}

void AccumulatePass::onHotReload(HotReloadFlags reloaded)
{
    // Reset accumulation if programs changed.
    if (is_set(reloaded, HotReloadFlags::Program))
        reset();
}

void AccumulatePass::setEnabled(bool enabled)
{
    if (enabled != mEnabled)
    {
        mEnabled = enabled;
        reset();
    }
}

void AccumulatePass::reset()
{
    mFrameCount = 0;
}

void AccumulatePass::prepareAccumulation(RenderContext* pRenderContext, uint32_t width, uint32_t height)
{
    // Update frame dimensions
    mFrameDim = { width, height };

    // Allocate/resize/clear buffers for intermedate data. These are different depending on accumulation mode.
    // Buffers that are not used in the current mode are released.
    auto prepareBuffer = [&](ref<Texture>& pBuf, ResourceFormat format, bool bufUsed)
    {
        if (!bufUsed)
        {
            pBuf = nullptr;
            return;
        }
        // (Re-)create buffer if needed.
        if (!pBuf || pBuf->getWidth() != width || pBuf->getHeight() != height)
        {
            pBuf = mpDevice->createTexture2D(
                width, height, format, 1, 1, nullptr, ResourceBindFlags::ShaderResource | ResourceBindFlags::UnorderedAccess
            );
            FALCOR_ASSERT(pBuf);
            reset();
        }
        // Clear data if accumulation has been reset (either above or somewhere else).
        if (mFrameCount == 0)
        {
            if (getFormatType(format) == FormatType::Float)
                pRenderContext->clearUAV(pBuf->getUAV().get(), float4(0.f));
            else
                pRenderContext->clearUAV(pBuf->getUAV().get(), uint4(0));
        }
    };

    // RGB channel buffers
    prepareBuffer(
        mpLastFrameSum, ResourceFormat::RGBA32Float, mPrecisionMode == Precision::Single || mPrecisionMode == Precision::SingleCompensated
    );
    prepareBuffer(mpLastFrameCorr, ResourceFormat::RGBA32Float, mPrecisionMode == Precision::SingleCompensated);
    prepareBuffer(mpLastFrameSumLo, ResourceFormat::RGBA32Uint, mPrecisionMode == Precision::Double);
    prepareBuffer(mpLastFrameSumHi, ResourceFormat::RGBA32Uint, mPrecisionMode == Precision::Double);

    // Single-channel irradiance buffers
    prepareBuffer(
        mpScalarLastFrameSum, ResourceFormat::R32Float, mPrecisionMode == Precision::Single || mPrecisionMode == Precision::SingleCompensated
    );
    prepareBuffer(mpScalarLastFrameCorr, ResourceFormat::R32Float, mPrecisionMode == Precision::SingleCompensated);
    prepareBuffer(mpScalarLastFrameSumLo, ResourceFormat::R32Uint, mPrecisionMode == Precision::Double);
    prepareBuffer(mpScalarLastFrameSumHi, ResourceFormat::R32Uint, mPrecisionMode == Precision::Double);
}

// Implementation of average value calculation
void AccumulatePass::computeAverageValue(RenderContext* pRenderContext, const ref<Texture>& pTexture)
{
    if (!mpParallelReduction || !pTexture)
    {
        logWarning("AccumulatePass::computeAverageValue() - ParallelReduction or texture is missing");
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
            mAverageValue = sum.x / pixelCount;
            logInfo("AccumulatePass::computeAverageValue() - Average value: {}", mAverageValue);
        }
    }
    catch (const std::exception& e)
    {
        logError("AccumulatePass::computeAverageValue() - Error calculating average: {}", e.what());
    }
}
