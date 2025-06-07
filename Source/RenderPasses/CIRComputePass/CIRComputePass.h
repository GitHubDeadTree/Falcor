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
#include "Core/API/Fence.h"
#include <memory>

using namespace Falcor;

/** CIR (Channel Impulse Response) path data structure.
    This structure stores the essential parameters of each light propagation path
    needed for calculating the Channel Impulse Response in visible light communication systems.
    Each path represents light traveling from an LED transmitter through possible reflections
    to a photodiode receiver.
    
    Note: This structure must match exactly with the PathTracer CPU CIRPathDataCPU structure
    Memory layout fix: Using separate uint32_t fields instead of uint64_t to match GPU uint2
*/
struct CIRPathData
{
    float pathLength;           // d_i: Total propagation distance of the path (meters)
    float emissionAngle;        // φ_i: Emission angle at LED surface (radians)
    float receptionAngle;       // θ_i: Reception angle at photodiode surface (radians)
    float reflectanceProduct;   // r_i product: Product of all surface reflectances along the path [0,1]
    uint32_t reflectionCount;   // K_i: Number of reflections in the path
    float emittedPower;         // P_t: Emitted optical power (watts)
    uint32_t pixelX;            // Pixel X coordinate (separated for better memory alignment)
    uint32_t pixelY;            // Pixel Y coordinate (separated for better memory alignment)
    uint32_t pathIndex;         // Unique index identifier for this path

    // Helper methods for pixel coordinate access (for backward compatibility)
    uint32_t getPixelX() const { return pixelX; }
    uint32_t getPixelY() const { return pixelY; }
    void setPixelCoord(uint32_t x, uint32_t y) { pixelX = x; pixelY = y; }

    /** Validate that all CIR parameters are within expected physical ranges.
        \return True if all parameters are valid, false otherwise.
    */
    bool isValid() const
    {
        // Check path length: reasonable range 0.1m to 1000m for indoor VLC
        if (pathLength < 0.1f || pathLength > 1000.0f) return false;

        // Check angles: must be in [0, π] radians
        if (emissionAngle < 0.0f || emissionAngle > 3.14159265f) return false;
        if (receptionAngle < 0.0f || receptionAngle > 3.14159265f) return false;

        // Check reflectance product: must be in [0, 1]
        if (reflectanceProduct < 0.0f || reflectanceProduct > 1.0f) return false;

        // Check reflection count: reasonable upper limit of 100 bounces
        if (reflectionCount > 100) return false;

        // Check emitted power: must be positive and reasonable (up to 1000W)
        if (emittedPower <= 0.0f || emittedPower > 1000.0f) return false;

        // Check for NaN or infinity in float values
        if (std::isnan(pathLength) || std::isinf(pathLength)) return false;
        if (std::isnan(emissionAngle) || std::isinf(emissionAngle)) return false;
        if (std::isnan(receptionAngle) || std::isinf(receptionAngle)) return false;
        if (std::isnan(reflectanceProduct) || std::isinf(reflectanceProduct)) return false;
        if (std::isnan(emittedPower) || std::isinf(emittedPower)) return false;

        return true;
    }
};

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
    
    // Input data output control
    bool mOutputInputData = false;    // Flag to control per-frame input data output
    
    // GPU-CPU synchronization for data transfer
    ref<Fence> mpSyncFence;           // Fence for GPU-CPU synchronization during buffer readback

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
    
    // Input data output functions
    void outputInputData(RenderContext* pRenderContext, const ref<Buffer>& pInputPathData, uint32_t pathCount);
    void saveInputDataToFile(const std::vector<CIRPathData>& pathData, const std::string& filename, uint32_t frameCount);
}; 