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
#include "PixelStatsShared.slang"
#include "Core/Macros.h"
#include "Core/API/Buffer.h"
#include "Core/API/Texture.h"
#include "Core/API/Fence.h"
#include "Core/Pass/ComputePass.h"
#include "Utils/UI/Gui.h"
#include "Utils/Algorithm/ParallelReduction.h"
#include "Utils/Logger.h"
#include <memory>
#include <vector>
#include <string>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace Falcor
{
    // Forward declarations
    class Scene;
    class Camera;
    class Light;

    // Forward declaration for CIR path data structure
    struct CIRPathData
    {
        float pathLength;
        float emissionAngle;
        float receptionAngle;
        float reflectanceProduct;
        uint32_t reflectionCount;
        float emittedPower;
        float originalEmittedPower;  // TASK 3: Original LED emitted power before attenuation (watts)
        uint32_t pixelX;
        uint32_t pixelY;
        uint32_t pathIndex;
        uint32_t flags;                  // Bit 0: hitEmissiveSurface, Bit 1: isNEEPath, Bits 2-31: reserved

        // Helper functions for flag access
        bool getHitEmissiveSurface() const { return (flags & 0x1) != 0; }
        bool getIsNEEPath() const { return (flags & 0x2) != 0; }
        void setHitEmissiveSurface(bool value) { flags = value ? (flags | 0x1) : (flags & ~0x1); }
        void setIsNEEPath(bool value) { flags = value ? (flags | 0x2) : (flags & ~0x2); }

        // Light source position accessor functions (must match GPU interface)
        float3 getLightSourcePosition() const { return float3(lightSourcePosition.x, lightSourcePosition.y, lightSourcePosition.z); }
        void setLightSourcePosition(const float3& position) { lightSourcePosition = float4(position.x, position.y, position.z, 0.0f); }

        // New vertex-related fields for path vertex collection feature (must match GPU definition)
        struct CompressedVertex
        {
            uint32_t x; // Contains packed x(16 bits) and y(16 bits) coordinates
            uint32_t y; // Contains z coordinate in lower 16 bits
        } compressedVertices[7];  // Compressed vertex coordinates, each vertex uses 6 bytes
        uint32_t vertexCount;     // Actual number of vertices in the path
        float3 basePosition;      // Base position (camera position) for relative coordinate calculation

        // NEE-specific fields - must match GPU structure exactly
        float4 lightSourcePosition;  // World space position of light source (for NEE paths only), w component unused

        // NEW FIELDS - Must match GPU CIRPathData exactly (order and alignment)
        float primaryRayPdfW;       // Direction probability density at receiver (1/sr)
        float4 radianceRGBA;        // MIS-combined radiance at camera position; A component unused

        bool isValid(float minPathLength, float maxPathLength,
                    float minEmittedPower, float maxEmittedPower,
                    float minAngle, float maxAngle,
                    float minReflectance, float maxReflectance) const
        {
            // Input validation - ensure parameters are in valid ranges
            if (minPathLength < 0.0f || maxPathLength < minPathLength ||
                minEmittedPower < 0.0f || maxEmittedPower < minEmittedPower ||
                minAngle < 0.0f || maxAngle < minAngle ||
                minReflectance < 0.0f || maxReflectance < minReflectance) {
                logError("CIRPathData::isValid: Invalid parameter ranges");
                return false;  // Default: reject invalid data
            }

            // Data validation - ensure all data values are physically reasonable
            if (pathLength < 0.0f || emittedPower < 0.0f ||
                emissionAngle < 0.0f || receptionAngle < 0.0f ||
                reflectanceProduct < 0.0f) {
                logWarning("CIRPathData::isValid: Invalid data values detected");
                return false;  // Default: reject invalid data
            }

            // Apply filtering criteria with configurable parameters
            bool valid = pathLength >= minPathLength && pathLength <= maxPathLength &&
                        emissionAngle >= minAngle && emissionAngle <= maxAngle &&
                        receptionAngle >= minAngle && receptionAngle <= maxAngle &&
                        reflectanceProduct >= minReflectance && reflectanceProduct <= maxReflectance &&
                        emittedPower >= minEmittedPower && emittedPower <= maxEmittedPower;

            return valid;
        }

        /** Validate vertex data integrity for path vertex collection feature.
            Checks vertex count range and base position validity.
            \return True if vertex data is valid, false otherwise.
        */
        bool isVertexDataValid() const
        {
            // Check vertex count is within valid range
            if (vertexCount > 7)
            {
                return false;
            }

            // Check base position for NaN or infinite values
            if (std::isnan(basePosition.x) || std::isnan(basePosition.y) || std::isnan(basePosition.z) ||
                std::isinf(basePosition.x) || std::isinf(basePosition.y) || std::isinf(basePosition.z))
            {
                return false;
            }

            return true;
        }
    };

    // CIR export format enumeration
    enum class CIRExportFormat
    {
        CSV,        // Comma-separated values (default, compatible with Excel)
        JSONL,      // JSON Lines format
        TXT         // Original text format
    };

    // CIR static parameters structure for VLC analysis
    struct CIRStaticParameters
    {
        float receiverArea;         // A: Receiver effective area (m²)
        float ledLambertianOrder;   // m: LED Lambertian order
        float lightSpeed;           // c: Light propagation speed (m/s)
        float receiverFOV;          // FOV: Receiver field of view (radians)
        float opticalFilterGain;    // T_s(θ): Optical filter transmittance
        float opticalConcentration; // g(θ): Optical concentration gain

        CIRStaticParameters()
            : receiverArea(1e-4f)
            , ledLambertianOrder(1.0f)
            , lightSpeed(3.0e8f)
            , receiverFOV((float)M_PI)
            , opticalFilterGain(1.0f)
            , opticalConcentration(1.0f)
        {}
    };

    /** Helper class for collecting runtime stats in the path tracer.

        Per-pixel stats are logged in buffers on the GPU, which are immediately ready for consumption
        after end() is called. These stats are summarized in a reduction pass, which are
        available in getStats() or printStats() after async readback to the CPU.

        Extended to support both statistical aggregation and raw CIR path data collection.
    */
    class FALCOR_API PixelStats
    {
    public:
        using CollectionMode = PixelStatsCollectionMode;

        struct Stats
        {
            uint32_t visibilityRays = 0;
            uint32_t closestHitRays = 0;
            uint32_t totalRays = 0;
            uint32_t pathVertices = 0;
            uint32_t volumeLookups = 0;
            float    avgVisibilityRays = 0.f;
            float    avgClosestHitRays = 0.f;
            float    avgTotalRays = 0.f;
            float    avgPathLength = 0.f;
            float    avgPathVertices = 0.f;
            float    avgVolumeLookups = 0.f;

            // CIR statistics
            uint32_t validCIRSamples = 0;
            float    avgCIRPathLength = 0.f;
            float    avgCIREmissionAngle = 0.f;
            float    avgCIRReceptionAngle = 0.f;
            float    avgCIRReflectanceProduct = 0.f;
            float    avgCIREmittedPower = 0.f;
            float    avgCIRReflectionCount = 0.f;
            float    avgRayWavelength = 0.f;

            // NEE-CIR statistics (for monitoring NEE-based light path collection)
            uint32_t neeAttempts = 0;           ///< Total NEE visibility queries attempted
            uint32_t neeSuccessful = 0;         ///< Successful NEE visibility queries
            uint32_t neeCircRecords = 0;        ///< NEE-CIR paths recorded
            uint32_t neeCircErrors = 0;         ///< NEE-CIR recording errors
            float    neeSuccessRate = 0.f;      ///< NEE success rate (successful/attempts)
            float    avgNEEPathLength = 0.f;    ///< Average NEE path length
            float    avgNEEEmissionAngle = 0.f; ///< Average NEE emission angle

            // P1 optimization: Path type statistics
            uint32_t neePathsCollected = 0;     ///< Number of NEE paths collected
            uint32_t regularPathsCollected = 0; ///< Number of regular (non-NEE) paths collected
            uint32_t totalPathsAttempted = 0;   ///< Total paths attempted for collection
            float    neePathRatio = 0.f;        ///< Ratio of NEE paths to total collected
        };

        PixelStats(ref<Device> pDevice);

        void setEnabled(bool enabled) { mEnabled = enabled; }
        bool isEnabled() const { return mEnabled; }

        // Scene reference for CIR parameter calculation
        void setScene(const ref<Scene>& pScene) { mpScene = pScene; }

        // Collection mode configuration
        void setCollectionMode(CollectionMode mode) { mCollectionMode = mode; }
        CollectionMode getCollectionMode() const { return mCollectionMode; }

        // CIR raw data configuration
        void setMaxCIRPathsPerFrame(uint32_t maxPaths) { mMaxCIRPathsPerFrame = maxPaths; }
        uint32_t getMaxCIRPathsPerFrame() const { return mMaxCIRPathsPerFrame; }

        void beginFrame(RenderContext* pRenderContext, const uint2& frameDim);
        void endFrame(RenderContext* pRenderContext);

        /** Perform program specialization and bind resources.
            This call doesn't change any resource declarations in the program.
        */
        void prepareProgram(const ref<Program>& pProgram, const ShaderVar& var);

        void renderUI(Gui::Widgets& widget);

        /** Fetches the latest stats generated by begin()/end().
            \param[out] stats The stats are copied here.
            \return True if stats are available, false otherwise.
        */
        bool getStats(PixelStats::Stats& stats);

        /** Returns the per-pixel ray count texture or nullptr if not available.
            \param[in] pRenderContext The render context.
            \return Texture in R32Uint format containing per-pixel ray counts, or nullptr if not available.
        */
        const ref<Texture> getRayCountTexture(RenderContext* pRenderContext);

        /** Returns the per-pixel path length texture or nullptr if not available.
            \return Texture in R32Uint format containing per-pixel path length, or nullptr if not available.
        */
        const ref<Texture> getPathLengthTexture() const;

        /** Returns the per-pixel path vertex count texture or nullptr if not available.
            \return Texture in R32Uint format containing per-pixel path vertex counts, or nullptr if not available.
        */
        const ref<Texture> getPathVertexCountTexture() const;

        /** Returns the per-pixel volume lookup count texture or nullptr if not available.
            \return Texture in R32Uint format containing per-pixel volume lookup counts, or nullptr if not available.
        */
        const ref<Texture> getVolumeLookupCountTexture() const;

        // CIR raw data access methods
        /** Get raw CIR path data collected in the last frame.
            Only available if collection mode includes RawData.
            \param[out] outData Vector to receive the CIR path data.
            \return True if data is available, false otherwise.
        */
        bool getCIRRawData(std::vector<CIRPathData>& outData);

        /** Get the number of CIR paths collected in the last frame.
            \return Number of paths collected, or 0 if no data available.
        */
        uint32_t getCIRPathCount();

        /** Export CIR raw data to a file with static parameters.
            \param[in] filename Output filename for the CIR data.
            \param[in] pScene Scene pointer for parameter calculation (optional, will use stored scene if null).
            \return True if export was successful, false otherwise.
        */
        bool exportCIRData(const std::string& filename, const ref<Scene>& pScene = nullptr);

        /** Export CIR raw data with automatic timestamped filename and format selection.
            Data is saved to ./CIRData/ directory with timestamp suffix.
            \param[in] format Export format (CSV, JSONL, or TXT).
            \param[in] pScene Scene pointer for parameter calculation (optional, will use stored scene if null).
            \return True if export was successful, false otherwise.
        */
        bool exportCIRDataWithTimestamp(CIRExportFormat format = CIRExportFormat::CSV, const ref<Scene>& pScene = nullptr);

        /** Export CIR raw data to specified file with format selection.
            \param[in] filename Output filename for the CIR data.
            \param[in] format Export format (CSV, JSONL, or TXT).
            \param[in] pScene Scene pointer for parameter calculation (optional, will use stored scene if null).
            \return True if export was successful, false otherwise.
        */
        bool exportCIRDataWithFormat(const std::string& filename, CIRExportFormat format, const ref<Scene>& pScene = nullptr);

        /** Compute CIR static parameters from scene information.
            \param[in] pScene Scene containing camera and light information.
            \param[in] frameDim Frame dimensions for area calculation.
            \return Computed static parameters structure.
        */
        CIRStaticParameters computeCIRStaticParameters(const ref<Scene>& pScene, const uint2& frameDim);

    protected:
        void copyStatsToCPU();
        void copyCIRRawDataToCPU();
        void computeRayCountTexture(RenderContext* pRenderContext);

        // Helper methods for static parameter computation
        float computeReceiverArea(const ref<Camera>& pCamera, const uint2& frameDim);
        float computeLEDLambertianOrder(const ref<Scene>& pScene);
        float computeReceiverFOV(const ref<Camera>& pCamera);

        // Helper methods for CIR data export
        std::string generateTimestampedFilename(CIRExportFormat format);
        bool ensureCIRDataDirectory();
        bool exportCIRDataCSV(const std::string& filename, const CIRStaticParameters& staticParams);
        bool exportCIRDataJSONL(const std::string& filename, const CIRStaticParameters& staticParams);
        bool exportCIRDataTXT(const std::string& filename, const CIRStaticParameters& staticParams);

        // Vertex processing functions for path vertex collection feature
        /** Decompress a vertex coordinate from compressed format back to world space.
            CPU-side implementation matching GPU decompressVertex function.
            \param compressed Compressed vertex data
            \param basePosition Base position used during compression
            \return Decompressed world space vertex position, or error indicator if invalid
        */
        float3 decompressVertex(const CIRPathData::CompressedVertex& compressed, const float3& basePosition) const;

        /** Decompress all vertices in a CIRPathData structure.
            \param cirData CIR path data containing compressed vertices
            \return Vector of decompressed world space vertex positions
        */
        std::vector<float3> decompressPathVertices(const CIRPathData& cirData) const;

        /** Validate CIR vertex data integrity with detailed error reporting.
            \param cirData CIR path data to validate
            \return True if vertex data is valid, false with error logging if invalid
        */
        bool validateCIRVertexData(const CIRPathData& cirData) const;

        /** TASK 3: Validate CIR data with enhanced originalEmittedPower validation.
            Validates CIR data fields including the new originalEmittedPower field,
            checking for logical consistency and reasonable value ranges.
            \param[in,out] data CIR path data to validate (may be modified for error correction)
            \param[in] pathIndex Path index for error reporting
        */
        void validateCIRDataForExport(CIRPathData& data, size_t pathIndex) const;

        // Backward compatibility support functions
        /** Check if CIR data version supports vertex collection feature.
            \param cirData CIR path data to check
            \return True if vertex data is available, false for legacy data
        */
        bool supportsVertexData(const CIRPathData& cirData) const;

        /** Handle legacy CIR data by providing default vertex information.
            \param cirData CIR path data to update (will be modified)
        */
        void handleLegacyData(CIRPathData& cirData) const;

        static const uint32_t kRayTypeCount = (uint32_t)PixelStatsRayType::Count;
        static const uint32_t kCIRTypeCount = (uint32_t)PixelStatsCIRType::Count;

        ref<Device>                         mpDevice;

        // Internal state
        std::unique_ptr<ParallelReduction>  mpParallelReduction;            ///< Helper for parallel reduction on the GPU.
        ref<Buffer>                         mpReductionResult;              ///< Results buffer for stats readback (CPU mappable).
        ref<Fence>                          mpFence;                        ///< GPU fence for sychronizing readback.

        // Configuration
        bool                                mEnabled = false;               ///< Enable pixel statistics.
        bool                                mEnableLogging = false;         ///< Enable printing to logfile.
        CollectionMode                      mCollectionMode = CollectionMode::Both;  ///< Data collection mode.
        uint32_t                            mMaxCIRPathsPerFrame = 50000;   ///< Maximum CIR paths to collect per frame (increased for NEE-CIR).

        // CIR export configuration
        CIRExportFormat                     mCIRExportFormat = CIRExportFormat::CSV; ///< Selected CIR export format.

        // CIR filtering parameters (configurable via UI)
        bool                                mCIRFilteringEnabled = true;     ///< Enable CIR data filtering
        float                               mCIRMinPathLength = 0.1f;        ///< Minimum path length for CIR filtering (meters)
        float                               mCIRMaxPathLength = 80.0f;       ///< Maximum path length for CIR filtering (meters)
        float                               mCIRMinEmittedPower = 1e-14f;    ///< Minimum emitted power for CIR filtering (watts)
        float                               mCIRMaxEmittedPower = 100000.0f; ///< Maximum emitted power for CIR filtering (watts)
        float                               mCIRMinAngle = 0.0f;             ///< Minimum angle for CIR filtering (radians)
        float                               mCIRMaxAngle = 3.14159f;         ///< Maximum angle for CIR filtering (radians)
        float                               mCIRMinReflectance = 0.0f;       ///< Minimum reflectance for CIR filtering
        float                               mCIRMaxReflectance = 1.0f;       ///< Maximum reflectance for CIR filtering

        // NEE path filtering parameters (P0 optimization)
        bool                                mCIRCollectNEEOnly = true;       ///< Collect only NEE (direct illumination) paths (default: true for VLC)
        bool                                mCIRCollectRegularPaths = false; ///< Collect regular (non-NEE) paths when NEE-only is disabled

        // CIR logging control parameters
        bool                                mCIRDetailedLogging = false;     ///< Enable detailed CIR filtering logs
        uint32_t                            mCIRLogFrameCounter = 0;         ///< Frame counter for limiting log frequency
        uint32_t                            mCIRLogInterval = 10;            ///< Log output interval (frames)
        uint32_t                            mLastCIRFilteredCount = 0;       ///< Last filtered count for change detection

        // Scene reference for CIR parameter computation
        ref<Scene>                          mpScene;                        ///< Scene reference for static parameter calculation.

        // Runtime data
        bool                                mRunning = false;               ///< True inbetween begin() / end() calls.
        bool                                mWaitingForData = false;        ///< True if we are waiting for data to become available on the GPU.
        uint2                               mFrameDim = { 0, 0 };           ///< Frame dimensions at last call to begin().

        bool                                mStatsValid = false;            ///< True if stats have been read back and are valid.
        bool                                mRayCountTextureValid = false;  ///< True if total ray count texture is valid.
        Stats                               mStats;                         ///< Traversal stats.

        ref<Texture>                        mpStatsRayCount[kRayTypeCount]; ///< Buffers for per-pixel ray count stats.
        ref<Texture>                        mpStatsRayCountTotal;           ///< Buffer for per-pixel total ray count. Only generated if getRayCountTexture() is called.
        ref<Texture>                        mpStatsPathLength;              ///< Buffer for per-pixel path length stats.
        ref<Texture>                        mpStatsPathVertexCount;         ///< Buffer for per-pixel path vertex count.
        ref<Texture>                        mpStatsVolumeLookupCount;       ///< Buffer for per-pixel volume lookup count.
        bool                                mStatsBuffersValid = false;     ///< True if per-pixel stats buffers contain valid data.

        // CIR statistics buffers
        ref<Texture>                        mpStatsCIRData[kCIRTypeCount];  ///< Buffers for per-pixel CIR data stats.
        ref<Texture>                        mpStatsCIRValidSamples;         ///< Buffer for per-pixel valid CIR sample count.

        // CIR raw data collection buffers
        ref<Buffer>                         mpCIRRawDataBuffer;             ///< GPU buffer for raw CIR path data.
        ref<Buffer>                         mpCIRCounterBuffer;             ///< GPU buffer for path counter.
        ref<Buffer>                         mpCIRRawDataReadback;           ///< CPU-readable buffer for CIR raw data.
        ref<Buffer>                         mpCIRCounterReadback;           ///< CPU-readable buffer for path counter.

        // P1 optimization: Path type counter buffers
        ref<Buffer>                         mpNEEPathCounterBuffer;         ///< GPU buffer for NEE path counter.
        ref<Buffer>                         mpRegularPathCounterBuffer;     ///< GPU buffer for regular path counter.
        ref<Buffer>                         mpNEEPathCounterReadback;       ///< CPU-readable buffer for NEE path counter.
        ref<Buffer>                         mpRegularPathCounterReadback;   ///< CPU-readable buffer for regular path counter.

        bool                                mCIRRawDataValid = false;       ///< True if raw CIR data is valid.
        uint32_t                            mCollectedCIRPaths = 0;         ///< Number of CIR paths collected in last frame.
        std::vector<CIRPathData>            mCIRRawData;                    ///< CPU copy of raw CIR data.

        ref<ComputePass>                    mpComputeRayCount;              ///< Pass for computing per-pixel total ray count.
    };
}
