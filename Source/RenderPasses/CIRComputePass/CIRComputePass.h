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
#pragma once
#include "Falcor.h"
#include "RenderGraph/RenderPass.h"
#include "RenderGraph/RenderPassHelpers.h"
#include <memory>

using namespace Falcor;

/** CIR (Channel Impulse Response) computation render pass.
    This pass takes path data from the PathTracer and computes the Channel Impulse Response
    for visible light communication analysis. It calculates power gain and propagation delay
    for each path, then aggregates them into a discrete-time CIR vector.
*/
class CIRComputePass : public RenderPass
{
public:
    FALCOR_PLUGIN_CLASS(CIRComputePass, "CIRComputePass", "Render pass for computing Channel Impulse Response from path tracer data.");

    static ref<CIRComputePass> create(ref<Device> pDevice, const Properties& props) { return make_ref<CIRComputePass>(pDevice, props); }

    CIRComputePass(ref<Device> pDevice, const Properties& props);

    virtual Properties getProperties() const override;
    virtual RenderPassReflection reflect(const CompileData& compileData) override;
    virtual void compile(RenderContext* pRenderContext, const CompileData& compileData) override;
    virtual void execute(RenderContext* pRenderContext, const RenderData& renderData) override;
    virtual void renderUI(Gui::Widgets& widget) override;

    // Scripting functions for parameter access
    float getTimeResolution() const { return mTimeResolution; }
    void setTimeResolution(float resolution);

    float getMaxDelay() const { return mMaxDelay; }
    void setMaxDelay(float delay);

    uint32_t getCIRBins() const { return mCIRBins; }
    void setCIRBins(uint32_t bins);

    float getLEDPower() const { return mLEDPower; }
    void setLEDPower(float power);

    float getHalfPowerAngle() const { return mHalfPowerAngle; }
    void setHalfPowerAngle(float angle);

    float getReceiverArea() const { return mReceiverArea; }
    void setReceiverArea(float area);

    float getFieldOfView() const { return mFieldOfView; }
    void setFieldOfView(float fov);

private:
    // CIR computation parameters
    float mTimeResolution = 1e-9f;    // Time resolution in seconds (1 nanosecond)
    float mMaxDelay = 1e-6f;          // Maximum delay in seconds (1 microsecond)
    uint32_t mCIRBins = 1000;         // CIR vector length

    // LED parameters
    float mLEDPower = 1.0f;           // LED power in watts
    float mHalfPowerAngle = 0.5236f;  // Half power angle in radians (30 degrees)

    // Receiver parameters
    float mReceiverArea = 1e-4f;      // Receiver area in square meters (1 cm^2)
    float mFieldOfView = 1.047f;      // Field of view in radians (60 degrees)

    // Internal state
    ref<Buffer> mpCIRBuffer;          // CIR result buffer
    ref<Buffer> mpOverflowCounter;    // Overflow counter for diagnostics
    ref<ComputePass> mpComputePass;   // Compute pass for CIR calculation
    bool mNeedRecompile = false;      // Flag indicating if shader needs recompilation
    uint32_t mFrameCount = 0;         // Frame counter for debugging

    // Parameter validation constants
    static constexpr float kMinTimeResolution = 1e-12f;    // Minimum time resolution (1 picosecond)
    static constexpr float kMaxTimeResolution = 1e-6f;     // Maximum time resolution (1 microsecond)
    static constexpr float kMinMaxDelay = 1e-9f;           // Minimum max delay (1 nanosecond)
    static constexpr float kMaxMaxDelay = 1e-3f;           // Maximum max delay (1 millisecond)
    static constexpr uint32_t kMinCIRBins = 10;            // Minimum CIR bins
    static constexpr uint32_t kMaxCIRBins = 1000000;       // Maximum CIR bins
    static constexpr float kMinLEDPower = 1e-6f;           // Minimum LED power (1 microwatt)
    static constexpr float kMaxLEDPower = 1000.0f;         // Maximum LED power (1000 watts)
    static constexpr float kMinHalfPowerAngle = 0.0174f;   // Minimum half power angle (1 degree)
    static constexpr float kMaxHalfPowerAngle = 1.5708f;   // Maximum half power angle (90 degrees)
    static constexpr float kMinReceiverArea = 1e-8f;       // Minimum receiver area (0.01 mm^2)
    static constexpr float kMaxReceiverArea = 1.0f;        // Maximum receiver area (1 m^2)
    static constexpr float kMinFieldOfView = 0.0174f;      // Minimum field of view (1 degree)
    static constexpr float kMaxFieldOfView = 3.1416f;      // Maximum field of view (180 degrees)

    // Helper functions
    void parseProperties(const Properties& props);
    void validateParameters();
    void createCIRBuffer();
    void prepareComputePass();
    bool validateTimeResolution(float resolution);
    bool validateMaxDelay(float delay);
    bool validateCIRBins(uint32_t bins);
    bool validateLEDPower(float power);
    bool validateHalfPowerAngle(float angle);
    bool validateReceiverArea(float area);
    bool validateFieldOfView(float fov);
    void logParameterStatus();
    void logBufferStatus();
    void setDefaultParametersOnError();
    
    // Task 2.3: CIR validation and statistics functions
    void validateCIRResults(RenderContext* pRenderContext, const ref<Buffer>& pCIRBuffer, uint32_t pathCount);
    void readAndLogOverflowCount(RenderContext* pRenderContext, uint32_t pathCount);
    void logCIRStatistics(const std::vector<float>& cirData, uint32_t pathCount);
    
    // Task 2.4: CIR result output and visualization functions
    void outputCIRResults(RenderContext* pRenderContext, const ref<Buffer>& pCIRBuffer, uint32_t pathCount);
    void saveCIRToFile(const std::vector<float>& cirData, const std::string& filename);
    void updateVisualization(const std::vector<float>& cirData);
    std::vector<float> readFullCIRData(RenderContext* pRenderContext, const ref<Buffer>& pCIRBuffer);
    void validateAndCleanCIRData(std::vector<float>& cirData);
    void computeCIRStatistics(const std::vector<float>& cirData, uint32_t pathCount, float& totalPower, uint32_t& nonZeroBins, float& maxPower, uint32_t& peakBin);
}; 