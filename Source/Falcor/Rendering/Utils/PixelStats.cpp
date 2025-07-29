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
#include "PixelStats.h"
#include "Core/API/RenderContext.h"
#include "Scene/Scene.h"
#include "Scene/Camera/Camera.h"
#include "Scene/Lights/Light.h"
#include "Utils/Math/FalcorMath.h"
#include "Utils/Logger.h"
#include "Utils/Scripting/ScriptBindings.h"
#include <sstream>
#include <iomanip>
#include <fstream>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <algorithm>

// Additional headers for enhanced CIR export functionality
#include <chrono>
#include <filesystem>
#include <iomanip>
#include <sstream>

namespace Falcor
{
    namespace
    {
        const char kComputeRayCountFilename[] = "Rendering/Utils/PixelStats.cs.slang";

        pybind11::dict toPython(const PixelStats::Stats& stats)
        {
            pybind11::dict d;
            d["visibilityRays"] = stats.visibilityRays;
            d["closestHitRays"] = stats.closestHitRays;
            d["totalRays"] = stats.totalRays;
            d["pathVertices"] = stats.pathVertices;
            d["volumeLookups"] = stats.volumeLookups;
            d["avgVisibilityRays"] = stats.avgVisibilityRays;
            d["avgClosestHitRays"] = stats.avgClosestHitRays;
            d["avgTotalRays"] = stats.avgTotalRays;
            d["avgPathLength"] = stats.avgPathLength;
            d["avgPathVertices"] = stats.avgPathVertices;
            d["avgVolumeLookups"] = stats.avgVolumeLookups;
            d["avgRayWavelength"] = stats.avgRayWavelength;
            return d;
        }
    }

    PixelStats::PixelStats(ref<Device> pDevice)
        : mpDevice(pDevice)
    {
        mpComputeRayCount = ComputePass::create(mpDevice, kComputeRayCountFilename, "main");
    }

    void PixelStats::beginFrame(RenderContext* pRenderContext, const uint2& frameDim)
    {
        // Prepare state.
        FALCOR_ASSERT(!mRunning);
        mRunning = true;
        mWaitingForData = false;
        mFrameDim = frameDim;

        // Mark previously stored data as invalid. The config may have changed, so this is the safe bet.
        mStats = Stats();
        mStatsValid = false;
        mStatsBuffersValid = false;
        mRayCountTextureValid = false;
        mCIRRawDataValid = false;

        if (mEnabled)
        {
            // Create parallel reduction helper.
            if (!mpParallelReduction)
            {
                mpParallelReduction = std::make_unique<ParallelReduction>(mpDevice);
                // Extend reduction result buffer to include CIR data (6 CIR types + 1 valid sample count)
                mpReductionResult = mpDevice->createBuffer((kRayTypeCount + 3 + kCIRTypeCount + 1) * sizeof(uint4), ResourceBindFlags::None, MemoryType::ReadBack);
            }

            // Prepare stats buffers.
            if (!mpStatsPathLength || mpStatsPathLength->getWidth() != frameDim.x || mpStatsPathLength->getHeight() != frameDim.y)
            {
                for (uint32_t i = 0; i < kRayTypeCount; i++)
                {
                    mpStatsRayCount[i] = mpDevice->createTexture2D(frameDim.x, frameDim.y, ResourceFormat::R32Uint, 1, 1, nullptr, ResourceBindFlags::ShaderResource | ResourceBindFlags::UnorderedAccess);
                }
                mpStatsPathLength = mpDevice->createTexture2D(frameDim.x, frameDim.y, ResourceFormat::R32Uint, 1, 1, nullptr, ResourceBindFlags::ShaderResource | ResourceBindFlags::UnorderedAccess);
                mpStatsPathVertexCount = mpDevice->createTexture2D(frameDim.x, frameDim.y, ResourceFormat::R32Uint, 1, 1, nullptr, ResourceBindFlags::ShaderResource | ResourceBindFlags::UnorderedAccess);
                mpStatsVolumeLookupCount = mpDevice->createTexture2D(frameDim.x, frameDim.y, ResourceFormat::R32Uint, 1, 1, nullptr, ResourceBindFlags::ShaderResource | ResourceBindFlags::UnorderedAccess);

                // Create CIR statistics buffers
                for (uint32_t i = 0; i < kCIRTypeCount; i++)
                {
                    mpStatsCIRData[i] = mpDevice->createTexture2D(frameDim.x, frameDim.y, ResourceFormat::R32Float, 1, 1, nullptr, ResourceBindFlags::ShaderResource | ResourceBindFlags::UnorderedAccess);
                }
                mpStatsCIRValidSamples = mpDevice->createTexture2D(frameDim.x, frameDim.y, ResourceFormat::R32Uint, 1, 1, nullptr, ResourceBindFlags::ShaderResource | ResourceBindFlags::UnorderedAccess);
            }

            // Clear CIR raw data buffers if they exist (creation moved to prepareProgram)
            if ((mCollectionMode == PixelStatsCollectionMode::RawData || mCollectionMode == PixelStatsCollectionMode::Both) && mpCIRCounterBuffer)
            {
                // Clear the counter buffer for new frame
                pRenderContext->clearUAV(mpCIRCounterBuffer->getUAV().get(), uint4(0, 0, 0, 0));
            }

            for (uint32_t i = 0; i < kRayTypeCount; i++)
            {
                pRenderContext->clearUAV(mpStatsRayCount[i]->getUAV().get(), uint4(0, 0, 0, 0));
            }
            pRenderContext->clearUAV(mpStatsPathLength->getUAV().get(), uint4(0, 0, 0, 0));
            pRenderContext->clearUAV(mpStatsPathVertexCount->getUAV().get(), uint4(0, 0, 0, 0));
            pRenderContext->clearUAV(mpStatsVolumeLookupCount->getUAV().get(), uint4(0, 0, 0, 0));

            // Clear CIR statistics buffers
            for (uint32_t i = 0; i < kCIRTypeCount; i++)
            {
                pRenderContext->clearUAV(mpStatsCIRData[i]->getUAV().get(), float4(0.0f, 0.0f, 0.0f, 0.0f));
            }
            pRenderContext->clearUAV(mpStatsCIRValidSamples->getUAV().get(), uint4(0, 0, 0, 0));
        }
    }

    void PixelStats::endFrame(RenderContext* pRenderContext)
    {
        FALCOR_ASSERT(mRunning);
        mRunning = false;

        if (mEnabled)
        {
            // Create fence first time we need it.
            if (!mpFence) mpFence = mpDevice->createFence();

            // Process statistics collection if enabled
            if (mCollectionMode == PixelStatsCollectionMode::Statistics || mCollectionMode == PixelStatsCollectionMode::Both)
            {
                // Sum of the per-pixel counters. The results are copied to a GPU buffer.
                for (uint32_t i = 0; i < kRayTypeCount; i++)
                {
                    mpParallelReduction->execute<uint4>(pRenderContext, mpStatsRayCount[i], ParallelReduction::Type::Sum, nullptr, mpReductionResult, i * sizeof(uint4));
                }
                mpParallelReduction->execute<uint4>(pRenderContext, mpStatsPathLength, ParallelReduction::Type::Sum, nullptr, mpReductionResult, kRayTypeCount * sizeof(uint4));
                mpParallelReduction->execute<uint4>(pRenderContext, mpStatsPathVertexCount, ParallelReduction::Type::Sum, nullptr, mpReductionResult, (kRayTypeCount + 1) * sizeof(uint4));
                mpParallelReduction->execute<uint4>(pRenderContext, mpStatsVolumeLookupCount, ParallelReduction::Type::Sum, nullptr, mpReductionResult, (kRayTypeCount + 2) * sizeof(uint4));

                // Add CIR statistics reduction
                for (uint32_t i = 0; i < kCIRTypeCount; i++)
                {
                    mpParallelReduction->execute<float4>(pRenderContext, mpStatsCIRData[i], ParallelReduction::Type::Sum, nullptr, mpReductionResult, (kRayTypeCount + 3 + i) * sizeof(uint4));
                }
                mpParallelReduction->execute<uint4>(pRenderContext, mpStatsCIRValidSamples, ParallelReduction::Type::Sum, nullptr, mpReductionResult, (kRayTypeCount + 3 + kCIRTypeCount) * sizeof(uint4));
            }

            // Process raw CIR data collection if enabled
            if (mCollectionMode == PixelStatsCollectionMode::RawData || mCollectionMode == PixelStatsCollectionMode::Both)
            {
                // Copy counter to readback buffer
                pRenderContext->copyBufferRegion(mpCIRCounterReadback.get(), 0, mpCIRCounterBuffer.get(), 0, sizeof(uint32_t));

                // Copy raw data to readback buffer
                pRenderContext->copyBufferRegion(mpCIRRawDataReadback.get(), 0, mpCIRRawDataBuffer.get(), 0, mMaxCIRPathsPerFrame * sizeof(CIRPathData));
            }

            // Submit command list and insert signal.
            pRenderContext->submit(false);
            pRenderContext->signal(mpFence.get());

            mStatsBuffersValid = true;
            mWaitingForData = true;
        }
    }

    void PixelStats::prepareProgram(const ref<Program>& pProgram, const ShaderVar& var)
    {
        FALCOR_ASSERT(mRunning);

        if (mEnabled)
        {
            pProgram->addDefine("_PIXEL_STATS_ENABLED");

            // Bind statistics buffers if statistics collection is enabled
            if (mCollectionMode == PixelStatsCollectionMode::Statistics || mCollectionMode == PixelStatsCollectionMode::Both)
            {
                for (uint32_t i = 0; i < kRayTypeCount; i++)
                {
                    var["gStatsRayCount"][i] = mpStatsRayCount[i];
                }
                var["gStatsPathLength"] = mpStatsPathLength;
                var["gStatsPathVertexCount"] = mpStatsPathVertexCount;
                var["gStatsVolumeLookupCount"] = mpStatsVolumeLookupCount;

                // Bind CIR statistics buffers
                for (uint32_t i = 0; i < kCIRTypeCount; i++)
                {
                    var["gStatsCIRData"][i] = mpStatsCIRData[i];
                }
                var["gStatsCIRValidSamples"] = mpStatsCIRValidSamples;
            }

            // Bind raw CIR data buffers if raw data collection is enabled
            if (mCollectionMode == PixelStatsCollectionMode::RawData || mCollectionMode == PixelStatsCollectionMode::Both)
            {
                pProgram->addDefine("_PIXEL_STATS_RAW_DATA_ENABLED");

                // Create CIR buffers using PixelInspectorPass pattern - reflector-based creation
                if (!mpCIRRawDataBuffer || mpCIRRawDataBuffer->getElementCount() < mMaxCIRPathsPerFrame)
                {
                    // Use program reflector to create buffer with correct type matching
                    mpCIRRawDataBuffer = mpDevice->createStructuredBuffer(
                        var["gCIRRawDataBuffer"], mMaxCIRPathsPerFrame,
                        ResourceBindFlags::ShaderResource | ResourceBindFlags::UnorderedAccess,
                        MemoryType::DeviceLocal, nullptr, false
                    );
                    mpCIRRawDataReadback = mpDevice->createBuffer(
                        mMaxCIRPathsPerFrame * sizeof(CIRPathData),
                        ResourceBindFlags::None,
                        MemoryType::ReadBack
                    );
                    logInfo("Created CIR raw data buffer using reflector: {} elements", mMaxCIRPathsPerFrame);
                }

                if (!mpCIRCounterBuffer)
                {
                    mpCIRCounterBuffer = mpDevice->createBuffer(
                        sizeof(uint32_t),
                        ResourceBindFlags::ShaderResource | ResourceBindFlags::UnorderedAccess,
                        MemoryType::DeviceLocal
                    );
                    mpCIRCounterReadback = mpDevice->createBuffer(
                        sizeof(uint32_t),
                        ResourceBindFlags::None,
                        MemoryType::ReadBack
                    );
                    logInfo("Created CIR counter buffer: {} bytes", sizeof(uint32_t));
                }

                // Direct binding following PixelInspectorPass pattern - variables always exist now
                var["gCIRRawDataBuffer"] = mpCIRRawDataBuffer;
                var["gCIRCounterBuffer"] = mpCIRCounterBuffer;
                var["PerFrameCB"]["gMaxCIRPaths"] = mMaxCIRPathsPerFrame;

                logDebug("Successfully bound CIR raw data buffers to shader variables");
            }
            else
            {
                pProgram->removeDefine("_PIXEL_STATS_RAW_DATA_ENABLED");
            }
        }
        else
        {
            pProgram->removeDefine("_PIXEL_STATS_ENABLED");
            pProgram->removeDefine("_PIXEL_STATS_RAW_DATA_ENABLED");
        }
    }

    void PixelStats::renderUI(Gui::Widgets& widget)
    {
        // Configuration.
        widget.checkbox("Ray stats", mEnabled);
        widget.tooltip("Collects ray tracing traversal stats on the GPU.\nNote that this option slows down the performance.");

        // Collection mode selection
        if (mEnabled)
        {
            widget.text("Collection Mode:");

            // Create dropdown list for collection modes
            const Gui::DropdownList kCollectionModeList = {
                {(uint32_t)PixelStatsCollectionMode::Statistics, "Statistics"},
                {(uint32_t)PixelStatsCollectionMode::RawData, "Raw Data"},
                {(uint32_t)PixelStatsCollectionMode::Both, "Both"}
            };

            uint32_t mode = (uint32_t)mCollectionMode;
            if (widget.dropdown("Mode", kCollectionModeList, mode))
            {
                mCollectionMode = (PixelStatsCollectionMode)mode;
            }

            if (mCollectionMode == PixelStatsCollectionMode::RawData || mCollectionMode == PixelStatsCollectionMode::Both)
            {
                widget.var("Max CIR paths per frame", mMaxCIRPathsPerFrame, 1000u, 10000000u);

                // CIR export format selection
                const Gui::DropdownList kExportFormatList = {
                    {(uint32_t)CIRExportFormat::CSV, "CSV (Excel compatible)"},
                    {(uint32_t)CIRExportFormat::JSONL, "JSONL (JSON Lines)"},
                    {(uint32_t)CIRExportFormat::TXT, "TXT (Original format)"}
                };

                uint32_t format = (uint32_t)mCIRExportFormat;
                if (widget.dropdown("Export format", kExportFormatList, format))
                {
                    mCIRExportFormat = (CIRExportFormat)format;
                }

                // CIR raw data controls
                copyCIRRawDataToCPU();

                // Display filtered CIR paths count with original count for reference
                uint32_t filteredCount = mCIRRawDataValid ? static_cast<uint32_t>(mCIRRawData.size()) : 0;
                if (mCIRFilteringEnabled) {
                    widget.text(fmt::format("CIR paths: {} filtered / {} collected", filteredCount, mCollectedCIRPaths));
                    widget.tooltip("Shows filtered CIR paths count vs total collected paths");
                } else {
                    widget.text(fmt::format("CIR paths: {} collected (filtering disabled)", filteredCount));
                    widget.tooltip("Shows collected CIR paths count (no filtering applied)");
                }

                if (widget.button("Export CIR Data (Auto-timestamped)"))
                {
                    // Export with automatic timestamp and format selection
                    exportCIRDataWithTimestamp(mCIRExportFormat, mpScene);
                }

                if (widget.button("Export CIR Data (Original)"))
                {
                    // Legacy export for compatibility
                    exportCIRData("cir_data.txt", mpScene);
                }

                // Add CIR filtering parameters UI
                if (auto group = widget.group("CIR Filtering Parameters")) {
                    try {
                        // Filtering Enable/Disable Switch
                        group.checkbox("Enable CIR Filtering", mCIRFilteringEnabled);
                        group.tooltip("Enable or disable CIR data filtering. When disabled, all collected paths are included.");

                        if (mCIRFilteringEnabled) {
                            // Path Length Filtering
                            group.text("Path Length Filtering:");
                            if (group.var("Min Path Length (m)", mCIRMinPathLength, 0.1f, 500.0f, 0.1f)) {
                                // Ensure min <= max
                                if (mCIRMinPathLength > mCIRMaxPathLength) {
                                    mCIRMaxPathLength = mCIRMinPathLength;
                                    logWarning("CIR UI: Adjusted max path length to match min value");
                                }
                            }
                            group.tooltip("Minimum path length for CIR data filtering (meters)");

                            if (group.var("Max Path Length (m)", mCIRMaxPathLength, 1.0f, 10000.0f, 1.0f)) {
                                // Ensure max >= min
                                if (mCIRMaxPathLength < mCIRMinPathLength) {
                                    mCIRMinPathLength = mCIRMaxPathLength;
                                    logWarning("CIR UI: Adjusted min path length to match max value");
                                }
                            }
                            group.tooltip("Maximum path length for CIR data filtering (meters)");

                            // Emitted Power Filtering
                            group.text("Emitted Power Filtering:");
                            if (group.var("Min Emitted Power (W)", mCIRMinEmittedPower, 0.0f, 100.0f, 0.01f)) {
                                if (mCIRMinEmittedPower > mCIRMaxEmittedPower) {
                                    mCIRMaxEmittedPower = mCIRMinEmittedPower;
                                    logWarning("CIR UI: Adjusted max emitted power to match min value");
                                }
                            }
                            group.tooltip("Minimum emitted power for CIR data filtering (watts)");

                            if (group.var("Max Emitted Power (W)", mCIRMaxEmittedPower, 1.0f, 50000.0f, 1.0f)) {
                                if (mCIRMaxEmittedPower < mCIRMinEmittedPower) {
                                    mCIRMinEmittedPower = mCIRMaxEmittedPower;
                                    logWarning("CIR UI: Adjusted min emitted power to match max value");
                                }
                            }
                            group.tooltip("Maximum emitted power for CIR data filtering (watts)");

                            // Angle Filtering
                            group.text("Angle Filtering:");
                            if (group.var("Min Angle (rad)", mCIRMinAngle, 0.0f, 3.14159f, 0.01f)) {
                                if (mCIRMinAngle > mCIRMaxAngle) {
                                    mCIRMaxAngle = mCIRMinAngle;
                                    logWarning("CIR UI: Adjusted max angle to match min value");
                                }
                            }
                            group.tooltip("Minimum angle for emission/reception filtering (radians)");

                            if (group.var("Max Angle (rad)", mCIRMaxAngle, 0.0f, 3.14159f, 0.01f)) {
                                if (mCIRMaxAngle < mCIRMinAngle) {
                                    mCIRMinAngle = mCIRMaxAngle;
                                    logWarning("CIR UI: Adjusted min angle to match max value");
                                }
                            }
                            group.tooltip("Maximum angle for emission/reception filtering (radians)");

                            // Reflectance Filtering
                            group.text("Reflectance Filtering:");
                            if (group.var("Min Reflectance", mCIRMinReflectance, 0.0f, 1.0f, 0.01f)) {
                                if (mCIRMinReflectance > mCIRMaxReflectance) {
                                    mCIRMaxReflectance = mCIRMinReflectance;
                                    logWarning("CIR UI: Adjusted max reflectance to match min value");
                                }
                            }
                            group.tooltip("Minimum reflectance product for CIR data filtering");

                            if (group.var("Max Reflectance", mCIRMaxReflectance, 0.0f, 1.0f, 0.01f)) {
                                if (mCIRMaxReflectance < mCIRMinReflectance) {
                                    mCIRMinReflectance = mCIRMaxReflectance;
                                    logWarning("CIR UI: Adjusted min reflectance to match max value");
                                }
                            }
                            group.tooltip("Maximum reflectance product for CIR data filtering");

                                                    // Reset Button
                        if (group.button("Reset to Defaults")) {
                            mCIRFilteringEnabled = true;
                            mCIRMinPathLength = 0.1f;
                            mCIRMaxPathLength = 1000.0f;
                            mCIRMinEmittedPower = 0.0f;
                            mCIRMaxEmittedPower = 100000.0f;
                            mCIRMinAngle = 0.0f;
                            mCIRMaxAngle = 3.14159f;
                            mCIRMinReflectance = 0.0f;
                            mCIRMaxReflectance = 1.0f;
                        }

                        // Logging Control Section
                        group.text("Logging Control:");
                        group.checkbox("Enable Detailed CIR Logging", mCIRDetailedLogging);
                        group.tooltip("Enable detailed CIR filtering logs with frequency control");

                        if (mCIRDetailedLogging) {
                            if (group.var("Log Interval (frames)", mCIRLogInterval, 1u, 100u, 1u)) {
                                // Ensure reasonable interval
                                if (mCIRLogInterval < 1) mCIRLogInterval = 1;
                                if (mCIRLogInterval > 100) mCIRLogInterval = 100;
                            }
                            group.tooltip("How often to output detailed CIR filtering logs (in frames)");
                        }
                        }
                    } catch (const std::exception& e) {
                        logError("CIR UI rendering error: " + std::string(e.what()));
                        // UI continues to function with current values
                    }
                }
            }
        }

        // Fetch data and show stats if available.
        copyStatsToCPU();
        if (mStatsValid)
        {
            widget.text("Stats:");
            widget.tooltip("All averages are per pixel on screen.\n"
                "\n"
                "The path vertex count includes:\n"
                " - Primary hits\n"
                " - Secondary hits on geometry\n"
                " - Secondary misses on envmap\n"
                "\n"
                "Note that the camera/sensor is not included, nor misses when there is no envmap (no-op miss shader).");

            std::ostringstream oss;
            oss << "Path length (avg): " << std::fixed << std::setprecision(3) << mStats.avgPathLength << "\n"
                << "Path vertices (avg): " << std::fixed << std::setprecision(3) << mStats.avgPathVertices << "\n"
                << "Total rays (avg): " << std::fixed << std::setprecision(3) << mStats.avgTotalRays << "\n"
                << "Visibility rays (avg): " << std::fixed << std::setprecision(3) << mStats.avgVisibilityRays << "\n"
                << "ClosestHit rays (avg): " << std::fixed << std::setprecision(3) << mStats.avgClosestHitRays << "\n"
                << "Path vertices: " << mStats.pathVertices << "\n"
                << "Total rays: " << mStats.totalRays << "\n"
                << "Visibility rays: " << mStats.visibilityRays << "\n"
                << "ClosestHit rays: " << mStats.closestHitRays << "\n"
                << "Volume lookups: " << mStats.volumeLookups << "\n"
                << "Volume lookups (avg): " << mStats.avgVolumeLookups << "\n";

            // Add CIR statistics to display
            if (mStats.validCIRSamples > 0)
            {
                oss << "\n=== CIR Statistics ===\n"
                    << "Valid CIR samples: " << mStats.validCIRSamples << "\n"
                    << "CIR Path length (avg): " << std::fixed << std::setprecision(3) << mStats.avgCIRPathLength << "\n"
                    << "CIR Emission angle (avg): " << std::fixed << std::setprecision(3) << mStats.avgCIREmissionAngle << " rad\n"
                    << "CIR Reception angle (avg): " << std::fixed << std::setprecision(3) << mStats.avgCIRReceptionAngle << " rad\n"
                    << "CIR Reflectance product (avg): " << std::fixed << std::setprecision(3) << mStats.avgCIRReflectanceProduct << "\n"
                    << "CIR Emitted power (avg): " << std::fixed << std::setprecision(3) << mStats.avgCIREmittedPower << "\n"
                    << "CIR Reflection count (avg): " << std::fixed << std::setprecision(3) << mStats.avgCIRReflectionCount << "\n"
                    << "Ray wavelength (avg): " << std::fixed << std::setprecision(1) << mStats.avgRayWavelength << " nm\n";
            }
            else
            {
                oss << "\n=== CIR Statistics ===\n"
                    << "No valid CIR samples found\n";
            }

            widget.checkbox("Enable logging", mEnableLogging);
            widget.text(oss.str());

            if (mEnableLogging) logInfo("\n" + oss.str());
        }
    }

    bool PixelStats::getStats(PixelStats::Stats& stats)
    {
        copyStatsToCPU();
        if (!mStatsValid)
        {
            logWarning("PixelStats::getStats() - Stats are not valid. Ignoring.");
            return false;
        }
        stats = mStats;
        return true;
    }

    const ref<Texture> PixelStats::getRayCountTexture(RenderContext* pRenderContext)
    {
        FALCOR_ASSERT(!mRunning);
        if (!mStatsBuffersValid) return nullptr;

        if (!mRayCountTextureValid)
        {
            computeRayCountTexture(pRenderContext);
        }

        FALCOR_ASSERT(mRayCountTextureValid);
        return mpStatsRayCountTotal;
    }

    void PixelStats::computeRayCountTexture(RenderContext* pRenderContext)
    {
        FALCOR_ASSERT(mStatsBuffersValid);
        if (!mpStatsRayCountTotal || mpStatsRayCountTotal->getWidth() != mFrameDim.x || mpStatsRayCountTotal->getHeight() != mFrameDim.y)
        {
            mpStatsRayCountTotal = mpDevice->createTexture2D(mFrameDim.x, mFrameDim.y, ResourceFormat::R32Uint, 1, 1, nullptr, ResourceBindFlags::ShaderResource | ResourceBindFlags::UnorderedAccess);
        }

        auto var = mpComputeRayCount->getRootVar();
        for (uint32_t i = 0; i < kRayTypeCount; i++)
        {
            var["gStatsRayCount"][i] = mpStatsRayCount[i];
        }
        var["gStatsRayCountTotal"] = mpStatsRayCountTotal;
        var["CB"]["gFrameDim"] = mFrameDim;

        mpComputeRayCount->execute(pRenderContext, mFrameDim.x, mFrameDim.y);
        mRayCountTextureValid = true;
    }

    const ref<Texture> PixelStats::getPathLengthTexture() const
    {
        FALCOR_ASSERT(!mRunning);
        return mStatsBuffersValid ? mpStatsPathLength : nullptr;
    }

    const ref<Texture> PixelStats::getPathVertexCountTexture() const
    {
        FALCOR_ASSERT(!mRunning);
        return mStatsBuffersValid ? mpStatsPathVertexCount : nullptr;
    }

    const ref<Texture> PixelStats::getVolumeLookupCountTexture() const
    {
        FALCOR_ASSERT(!mRunning);
        return mStatsBuffersValid ? mpStatsVolumeLookupCount : nullptr;
    }

    void PixelStats::copyStatsToCPU()
    {
        FALCOR_ASSERT(!mRunning);
        if (mWaitingForData)
        {
            // Wait for signal.
            mpFence->wait();
            mWaitingForData = false;

            if (mEnabled)
            {
                // Map the stats buffer.
                const uint4* result = static_cast<const uint4*>(mpReductionResult->map());
                FALCOR_ASSERT(result);

                const uint32_t totalPathLength = result[kRayTypeCount].x;
                const uint32_t totalPathVertices = result[kRayTypeCount + 1].x;
                const uint32_t totalVolumeLookups = result[kRayTypeCount + 2].x;
                const uint32_t numPixels = mFrameDim.x * mFrameDim.y;
                FALCOR_ASSERT(numPixels > 0);

                mStats.visibilityRays = result[(uint32_t)PixelStatsRayType::Visibility].x;
                mStats.closestHitRays = result[(uint32_t)PixelStatsRayType::ClosestHit].x;
                mStats.totalRays = mStats.visibilityRays + mStats.closestHitRays;
                mStats.pathVertices = totalPathVertices;
                mStats.volumeLookups = totalVolumeLookups;
                mStats.avgVisibilityRays = (float)mStats.visibilityRays / numPixels;
                mStats.avgClosestHitRays = (float)mStats.closestHitRays / numPixels;
                mStats.avgTotalRays = (float)mStats.totalRays / numPixels;
                mStats.avgPathLength = (float)totalPathLength / numPixels;
                mStats.avgPathVertices = (float)totalPathVertices / numPixels;
                mStats.avgVolumeLookups = (float)totalVolumeLookups / numPixels;

                // Process CIR statistics
                const uint32_t validCIRSamples = result[kRayTypeCount + 3 + kCIRTypeCount].x;
                mStats.validCIRSamples = validCIRSamples;

                if (validCIRSamples > 0)
                {
                    // Cast to float4 for CIR data which are stored as floats
                    const float4* cirResult = reinterpret_cast<const float4*>(result);

                    mStats.avgCIRPathLength = cirResult[kRayTypeCount + 3 + (uint32_t)PixelStatsCIRType::PathLength].x / validCIRSamples;
                    mStats.avgCIREmissionAngle = cirResult[kRayTypeCount + 3 + (uint32_t)PixelStatsCIRType::EmissionAngle].x / validCIRSamples;
                    mStats.avgCIRReceptionAngle = cirResult[kRayTypeCount + 3 + (uint32_t)PixelStatsCIRType::ReceptionAngle].x / validCIRSamples;
                    mStats.avgCIRReflectanceProduct = cirResult[kRayTypeCount + 3 + (uint32_t)PixelStatsCIRType::ReflectanceProduct].x / validCIRSamples;
                    mStats.avgCIREmittedPower = cirResult[kRayTypeCount + 3 + (uint32_t)PixelStatsCIRType::EmittedPower].x / validCIRSamples;
                    mStats.avgCIRReflectionCount = cirResult[kRayTypeCount + 3 + (uint32_t)PixelStatsCIRType::ReflectionCount].x / validCIRSamples;
                    mStats.avgRayWavelength = cirResult[kRayTypeCount + 3 + (uint32_t)PixelStatsCIRType::Wavelength].x / validCIRSamples;
                }
                else
                {
                    // Reset CIR averages to zero if no valid samples
                    mStats.avgCIRPathLength = 0.0f;
                    mStats.avgCIREmissionAngle = 0.0f;
                    mStats.avgCIRReceptionAngle = 0.0f;
                    mStats.avgCIRReflectanceProduct = 0.0f;
                    mStats.avgCIREmittedPower = 0.0f;
                    mStats.avgCIRReflectionCount = 0.0f;
                    mStats.avgRayWavelength = 0.0f;
                }

                mpReductionResult->unmap();
                mStatsValid = true;
            }
        }
    }

    void PixelStats::copyCIRRawDataToCPU()
    {
        FALCOR_ASSERT(!mRunning);
        if (mWaitingForData && (mCollectionMode == PixelStatsCollectionMode::RawData || mCollectionMode == PixelStatsCollectionMode::Both))
        {
            // Wait for signal.
            mpFence->wait();

            try
            {
                // Map the counter buffer to get actual path count
                const uint32_t* counterData = static_cast<const uint32_t*>(mpCIRCounterReadback->map());
                if (counterData)
                {
                    mCollectedCIRPaths = std::min(*counterData, mMaxCIRPathsPerFrame);
                    mpCIRCounterReadback->unmap();

                    if (mCollectedCIRPaths > 0)
                    {
                        // Map the raw data buffer
                        const CIRPathData* rawData = static_cast<const CIRPathData*>(mpCIRRawDataReadback->map());
                        if (rawData)
                        {
                            mCIRRawData.clear();
                            mCIRRawData.reserve(mCollectedCIRPaths);

                            // NEW FILTERING LOGIC: Apply CPU-side filtering once
                            // Data that passes this filter goes directly to both statistics and raw data
                            // without additional validation
                            uint32_t filteredCount = 0;
                            uint32_t totalCount = static_cast<uint32_t>(mCollectedCIRPaths);

                            for (uint32_t i = 0; i < mCollectedCIRPaths; i++)
                            {
                                try {
                                    // Apply CPU-side filtering with configurable criteria
                                    bool shouldInclude = true;
                                    if (mCIRFilteringEnabled)
                                    {
                                        shouldInclude = rawData[i].isValid(mCIRMinPathLength, mCIRMaxPathLength,
                                                                          mCIRMinEmittedPower, mCIRMaxEmittedPower,
                                                                          mCIRMinAngle, mCIRMaxAngle,
                                                                          mCIRMinReflectance, mCIRMaxReflectance);
                                    }

                                    if (shouldInclude)
                                    {
                                        mCIRRawData.push_back(rawData[i]);
                                        filteredCount++;
                                    }
                                } catch (const std::exception& e) {
                                    logError("CIR filtering error at index " + std::to_string(i) + ": " + std::string(e.what()));
                                    // Continue processing other data points
                                    continue;
                                }
                            }

                            // Log filtering statistics with frequency control
                            if (totalCount > 0) {
                                float filterRatio = static_cast<float>(filteredCount) / totalCount;

                                // Increment frame counter
                                mCIRLogFrameCounter++;

                                // Check if we should log this frame (based on interval and change detection)
                                bool shouldLog = mCIRDetailedLogging &&
                                               (mCIRLogFrameCounter % mCIRLogInterval == 0 ||
                                                filteredCount != mLastCIRFilteredCount);

                                if (shouldLog) {
                                    // Log detailed filtering information
                                    logInfo("CIR filtering details:");
                                    logInfo("  - Filtering enabled: {}", mCIRFilteringEnabled ? "Yes" : "No");
                                    logInfo("  - Path length range: [{:.2f}, {:.2f}] m", mCIRMinPathLength, mCIRMaxPathLength);
                                    logInfo("  - Emitted power range: [{:.2e}, {:.2e}] W", mCIRMinEmittedPower, mCIRMaxEmittedPower);
                                    logInfo("  - Angle range: [{:.3f}, {:.3f}] rad", mCIRMinAngle, mCIRMaxAngle);
                                    logInfo("  - Reflectance range: [{:.3f}, {:.3f}]", mCIRMinReflectance, mCIRMaxReflectance);
                                    logInfo("  - Total paths collected: {}", totalCount);
                                    logInfo("  - Paths after filtering: {}", filteredCount);

                                    if (filterRatio < 0.1f) {
                                        logWarning("CIR filtering: Only {:.1f}% of data passed filters ({}/{})",
                                                  filterRatio * 100.0f, filteredCount, totalCount);
                                    }

                                    // Update last filtered count for change detection
                                    mLastCIRFilteredCount = filteredCount;
                                }
                            }

                            mpCIRRawDataReadback->unmap();
                            mCIRRawDataValid = true;

                            // Only log summary if detailed logging is enabled and it's time to log
                            if (mCIRDetailedLogging && (mCIRLogFrameCounter % mCIRLogInterval == 0 || filteredCount != mLastCIRFilteredCount))
                            {
                                if (mCIRFilteringEnabled)
                                {
                                    logInfo("PixelStats: CPU-filtered {} valid CIR paths out of {} total (configurable criteria)",
                                           filteredCount, totalCount);
                                }
                                else
                                {
                                    logInfo("PixelStats: Collected {} CIR paths (filtering disabled)",
                                           filteredCount);
                                }
                            }
                        }
                    }
                    else
                    {
                        mCIRRawData.clear();
                        mCIRRawDataValid = false;
                    }
                }
            }
            catch (const std::exception& e)
            {
                logError("PixelStats: Error reading CIR raw data: " + std::string(e.what()));
                mCIRRawData.clear();
                mCollectedCIRPaths = 0;
            }
        }
    }

    bool PixelStats::getCIRRawData(std::vector<CIRPathData>& outData)
    {
        copyCIRRawDataToCPU();
        if (!mCIRRawDataValid)
        {
            return false;
        }
        outData = mCIRRawData;
        return true;
    }

    uint32_t PixelStats::getCIRPathCount()
    {
        copyCIRRawDataToCPU();
        return mCIRRawDataValid ? static_cast<uint32_t>(mCIRRawData.size()) : 0;
    }

    bool PixelStats::exportCIRData(const std::string& filename, const ref<Scene>& pScene)
    {
        // Copy and filter CIR data using CPU-side configurable criteria
        copyCIRRawDataToCPU();
        if (!mCIRRawDataValid || mCIRRawData.empty())
        {
            logWarning("PixelStats::exportCIRData() - No valid CIR data to export after CPU filtering.");
            return false;
        }

        try
        {
            std::ofstream file(filename);
            if (!file.is_open())
            {
                logError("PixelStats::exportCIRData() - Failed to open file: {}", filename);
                return false;
            }

            // Compute static parameters if scene is provided
            CIRStaticParameters staticParams;
            ref<Scene> sceneToUse = pScene ? pScene : mpScene;
            if (sceneToUse)
            {
                staticParams = computeCIRStaticParameters(sceneToUse, mFrameDim);
            }

            // Write header with static parameters and filtering information
            file << "# CIR Path Data Export with Static Parameters\n";
            file << "# Data filtered with CPU-side configurable criteria\n";
            file << "# Static Parameters for VLC Channel Impulse Response Calculation:\n";
            file << "# A_receiver_area_m2=" << std::scientific << std::setprecision(6) << staticParams.receiverArea << "\n";
            file << "# m_led_lambertian_order=" << std::fixed << std::setprecision(3) << staticParams.ledLambertianOrder << "\n";
            file << "# c_light_speed_ms=" << std::scientific << std::setprecision(3) << staticParams.lightSpeed << "\n";
            file << "# FOV_receiver_rad=" << std::fixed << std::setprecision(3) << staticParams.receiverFOV << "\n";
            file << "# T_s_optical_filter_gain=" << std::fixed << std::setprecision(1) << staticParams.opticalFilterGain << "\n";
            file << "# g_optical_concentration=" << std::fixed << std::setprecision(1) << staticParams.opticalConcentration << "\n";
            file << "#\n";
            file << "# Path Data Format: PathIndex,PixelX,PixelY,PathLength(m),EmissionAngle(rad),ReceptionAngle(rad),ReflectanceProduct,ReflectionCount,EmittedPower(W)\n";
            file << std::fixed << std::setprecision(6);

            // Write filtered path data (no additional validation needed)
            for (size_t i = 0; i < mCIRRawData.size(); i++)
            {
                const auto& data = mCIRRawData[i];
                file << i << ","
                     << data.pixelX << ","
                     << data.pixelY << ","
                     << data.pathLength << ","
                     << data.emissionAngle << ","
                     << data.receptionAngle << ","
                     << data.reflectanceProduct << ","
                     << data.reflectionCount << ","
                     << data.emittedPower << "\n";
            }

            file.close();
            logInfo("PixelStats: Exported {} CPU-filtered CIR paths to {}", mCIRRawData.size(), filename);
            return true;
        }
        catch (const std::exception& e)
        {
            logError("PixelStats::exportCIRData() - Error writing file: " + std::string(e.what()));
            return false;
        }
    }

    float PixelStats::computeReceiverArea(const ref<Camera>& pCamera, const uint2& frameDim)
    {
        if (!pCamera) return 1e-4f; // Default 1 cm²

        try
        {
            // Get camera parameters for calculating sensor area
            float focalLength = pCamera->getFocalLength(); // mm
            float frameHeight = pCamera->getFrameHeight(); // mm
            float aspectRatio = pCamera->getAspectRatio();

            // Calculate FOV from focal length and frame height
            float fovY = focalLengthToFovY(focalLength, frameHeight); // radians

            // Calculate physical sensor dimensions in meters
            float sensorHeightM = frameHeight * 1e-3f; // Convert mm to meters
            float sensorWidthM = sensorHeightM * aspectRatio;

            // Calculate total sensor area
            float totalSensorArea = sensorWidthM * sensorHeightM; // m²

            // Calculate pixel area (total sensor area divided by number of pixels)
            uint32_t totalPixels = frameDim.x * frameDim.y;
            float pixelArea = totalSensorArea / totalPixels;

            logInfo("PixelStats: Computed receiver area = {:.6e} m² (total sensor: {:.6e} m², pixels: {})",
                   pixelArea, totalSensorArea, totalPixels);

            return pixelArea;
        }
        catch (const std::exception& e)
        {
            logError("PixelStats: Error computing receiver area: " + std::string(e.what()));
            return 1e-4f; // Default fallback
        }
    }

    float PixelStats::computeLEDLambertianOrder(const ref<Scene>& pScene)
    {
        if (!pScene) return 1.0f; // Default Lambertian order

        try
        {
            const auto& lights = pScene->getLights();
            if (lights.empty())
            {
                logWarning("PixelStats: No lights found in scene, using default Lambertian order = 1.0");
                return 1.0f;
            }

            // Find first point light and calculate Lambertian order
            for (const auto& pLight : lights)
            {
                if (pLight->getType() == LightType::Point)
                {
                    const auto* pPointLight = static_cast<const PointLight*>(pLight.get());
                    float openingAngle = pPointLight->getOpeningAngle(); // radians

                    // Calculate Lambertian order using m = -ln(2)/ln(cos(θ_1/2))
                    if (openingAngle >= (float)M_PI)
                    {
                        // Isotropic light source: m = 1 (Lambertian)
                        logInfo("PixelStats: Found isotropic point light, Lambertian order = 1.0");
                        return 1.0f;
                    }
                    else
                    {
                        float halfAngle = openingAngle * 0.5f;
                        float cosHalfAngle = std::cos(halfAngle);

                        if (cosHalfAngle > 0.0f && cosHalfAngle < 1.0f)
                        {
                            float lambertianOrder = -std::log(2.0f) / std::log(cosHalfAngle);
                            logInfo("PixelStats: Computed LED Lambertian order = {:.3f} (half-angle = {:.3f} rad)",
                                   lambertianOrder, halfAngle);
                            return std::max(0.1f, lambertianOrder); // Ensure positive value
                        }
                    }
                }
            }

            logWarning("PixelStats: No suitable point light found, using default Lambertian order = 1.0");
            return 1.0f;
        }
        catch (const std::exception& e)
        {
            logError("PixelStats: Error computing LED Lambertian order: " + std::string(e.what()));
            return 1.0f; // Default fallback
        }
    }

    float PixelStats::computeReceiverFOV(const ref<Camera>& pCamera)
    {
        if (!pCamera) return (float)M_PI; // Default wide FOV

        try
        {
            float focalLength = pCamera->getFocalLength(); // mm
            float frameHeight = pCamera->getFrameHeight(); // mm

            // Calculate vertical FOV
            float fovY = focalLengthToFovY(focalLength, frameHeight); // radians

            logInfo("PixelStats: Computed receiver FOV = {:.3f} rad ({:.1f} degrees)",
                   fovY, fovY * 180.0f / (float)M_PI);

            return fovY;
        }
        catch (const std::exception& e)
        {
            logError("PixelStats: Error computing receiver FOV: " + std::string(e.what()));
            return (float)M_PI; // Default fallback
        }
    }

    CIRStaticParameters PixelStats::computeCIRStaticParameters(const ref<Scene>& pScene, const uint2& frameDim)
    {
        CIRStaticParameters params;

        if (!pScene)
        {
            logWarning("PixelStats: No scene provided, using default CIR static parameters");
            return params;
        }

        try
        {
            // Get camera for receiver calculations
            ref<Camera> pCamera = pScene->getCamera();

            if (pCamera)
            {
                // 1. Compute receiver effective area (A)
                params.receiverArea = computeReceiverArea(pCamera, frameDim);

                // 4. Compute receiver field of view (FOV)
                params.receiverFOV = computeReceiverFOV(pCamera);
            }
            else
            {
                logWarning("PixelStats: No camera found, using default receiver parameters");
                params.receiverArea = 1e-4f; // 1 cm²
                params.receiverFOV = (float)M_PI; // 180 degrees
            }

            // 2. Compute LED Lambertian order (m)
            params.ledLambertianOrder = computeLEDLambertianOrder(pScene);

            // 3. Light speed (c) - physical constant
            params.lightSpeed = 3.0e8f; // m/s

            // 5. Optical filter transmittance (T_s) - set to 1.0 (no filter)
            params.opticalFilterGain = 1.0f;

            // 6. Optical concentration gain (g) - set to 1.0 (no concentration)
            params.opticalConcentration = 1.0f;

            logInfo("PixelStats: Computed CIR static parameters:");
            logInfo("  Receiver area: {:.6e} m²", params.receiverArea);
            logInfo("  LED Lambertian order: {:.3f}", params.ledLambertianOrder);
            logInfo("  Light speed: {:.3e} m/s", params.lightSpeed);
            logInfo("  Receiver FOV: {:.3f} rad", params.receiverFOV);
            logInfo("  Optical filter gain: {:.1f}", params.opticalFilterGain);
            logInfo("  Optical concentration: {:.1f}", params.opticalConcentration);

            return params;
        }
        catch (const std::exception& e)
        {
            logError("PixelStats: Error computing CIR static parameters: " + std::string(e.what()));
            return CIRStaticParameters(); // Return default parameters
        }
    }

    std::string PixelStats::generateTimestampedFilename(CIRExportFormat format)
    {
        // Get current time
        auto now = std::chrono::system_clock::now();
        auto time_t_now = std::chrono::system_clock::to_time_t(now);
        auto tm_now = *std::localtime(&time_t_now);

        // Generate timestamp string
        std::ostringstream oss;
        oss << std::put_time(&tm_now, "%Y%m%d_%H%M%S");
        std::string timestamp = oss.str();

        // Get file extension based on format
        std::string extension;
        switch (format)
        {
            case CIRExportFormat::CSV:
                extension = ".csv";
                break;
            case CIRExportFormat::JSONL:
                extension = ".jsonl";
                break;
            case CIRExportFormat::TXT:
            default:
                extension = ".txt";
                break;
        }

        return "CIRData_" + timestamp + extension;
    }

    bool PixelStats::ensureCIRDataDirectory()
    {
        try
        {
            const std::string dirPath = "CIRData";
            if (!std::filesystem::exists(dirPath))
            {
                if (std::filesystem::create_directories(dirPath))
                {
                    logInfo("PixelStats: Created CIRData directory");
                }
                else
                {
                    logError("PixelStats: Failed to create CIRData directory");
                    return false;
                }
            }
            return true;
        }
        catch (const std::exception& e)
        {
            logError("PixelStats: Error creating CIRData directory: " + std::string(e.what()));
            return false;
        }
    }

    bool PixelStats::exportCIRDataWithTimestamp(CIRExportFormat format, const ref<Scene>& pScene)
    {
        if (!ensureCIRDataDirectory())
        {
            return false;
        }

        std::string filename = "CIRData/" + generateTimestampedFilename(format);
        return exportCIRDataWithFormat(filename, format, pScene);
    }

    bool PixelStats::exportCIRDataWithFormat(const std::string& filename, CIRExportFormat format, const ref<Scene>& pScene)
    {
        copyCIRRawDataToCPU();
        if (!mCIRRawDataValid || mCIRRawData.empty())
        {
            logWarning("PixelStats::exportCIRDataWithFormat() - No valid CIR data to export.");
            return false;
        }

        try
        {
            // Compute static parameters if scene is provided
            CIRStaticParameters staticParams;
            ref<Scene> sceneToUse = pScene ? pScene : mpScene;
            if (sceneToUse)
            {
                staticParams = computeCIRStaticParameters(sceneToUse, mFrameDim);
            }

            bool success = false;
            switch (format)
            {
                case CIRExportFormat::CSV:
                    success = exportCIRDataCSV(filename, staticParams);
                    break;
                case CIRExportFormat::JSONL:
                    success = exportCIRDataJSONL(filename, staticParams);
                    break;
                case CIRExportFormat::TXT:
                default:
                    success = exportCIRDataTXT(filename, staticParams);
                    break;
            }

            if (success)
            {
                logInfo("PixelStats: Exported {} CIR paths in {} format to {}",
                       mCIRRawData.size(),
                       format == CIRExportFormat::CSV ? "CSV" :
                       format == CIRExportFormat::JSONL ? "JSONL" : "TXT",
                       filename);
            }

            return success;
        }
        catch (const std::exception& e)
        {
            logError("PixelStats::exportCIRDataWithFormat() - Error: " + std::string(e.what()));
            return false;
        }
    }

    bool PixelStats::exportCIRDataCSV(const std::string& filename, const CIRStaticParameters& staticParams)
    {
        std::ofstream file(filename);
        if (!file.is_open())
        {
            logError("PixelStats: Failed to open CSV file: {}", filename);
            return false;
        }

        // Write header with static parameters as comments
        file << "# CIR Path Data Export (CSV Format)\n";
        file << "# Static Parameters for VLC Channel Impulse Response Calculation:\n";
        file << "# A_receiver_area_m2," << std::scientific << std::setprecision(6) << staticParams.receiverArea << "\n";
        file << "# m_led_lambertian_order," << std::fixed << std::setprecision(3) << staticParams.ledLambertianOrder << "\n";
        file << "# c_light_speed_ms," << std::scientific << std::setprecision(3) << staticParams.lightSpeed << "\n";
        file << "# FOV_receiver_rad," << std::fixed << std::setprecision(3) << staticParams.receiverFOV << "\n";
        file << "# T_s_optical_filter_gain," << std::fixed << std::setprecision(1) << staticParams.opticalFilterGain << "\n";
        file << "# g_optical_concentration," << std::fixed << std::setprecision(1) << staticParams.opticalConcentration << "\n";
        file << "#\n";

        // Write CSV header with vertex data support
        file << "PathIndex,PixelX,PixelY,PathLength_m,EmissionAngle_rad,ReceptionAngle_rad,ReflectanceProduct,ReflectionCount,EmittedPower_W,HitEmissiveSurface,";
        file << "VertexCount,BasePosition_X,BasePosition_Y,BasePosition_Z,";
        file << "Vertex1_X,Vertex1_Y,Vertex1_Z,Vertex2_X,Vertex2_Y,Vertex2_Z,Vertex3_X,Vertex3_Y,Vertex3_Z,";
        file << "Vertex4_X,Vertex4_Y,Vertex4_Z,Vertex5_X,Vertex5_Y,Vertex5_Z,Vertex6_X,Vertex6_Y,Vertex6_Z,Vertex7_X,Vertex7_Y,Vertex7_Z\n";

        // Write data rows with vertex information
        file << std::fixed << std::setprecision(6);
        for (size_t i = 0; i < mCIRRawData.size(); i++)
        {
            // Create a copy for potential modification (backward compatibility)
            CIRPathData data = mCIRRawData[i];

            // Handle legacy data if needed
            handleLegacyData(data);

            // Validate vertex data integrity (log warnings for invalid data but continue export)
            if (!validateCIRVertexData(data))
            {
                logWarning("PixelStats: Invalid vertex data in path {}, using default values", i);
                handleLegacyData(data); // Force default values for invalid data
            }

            try
            {
                // Write basic path data
                file << i << ","
                     << data.pixelX << ","
                     << data.pixelY << ","
                     << data.pathLength << ","
                     << data.emissionAngle << ","
                     << data.receptionAngle << ","
                     << data.reflectanceProduct << ","
                     << data.reflectionCount << ","
                     << data.emittedPower << ","
                     << (data.hitEmissiveSurface ? 1 : 0) << ",";

                // Write vertex data
                file << data.vertexCount << ","
                     << data.basePosition.x << ","
                     << data.basePosition.y << ","
                     << data.basePosition.z << ",";

                // Decompress and write vertices (up to 7 vertices)
                std::vector<float3> vertices = decompressPathVertices(data);
                for (uint32_t v = 0; v < 7; v++)
                {
                    if (v < vertices.size())
                    {
                        file << vertices[v].x << "," << vertices[v].y << "," << vertices[v].z;
                    }
                    else
                    {
                        file << "0,0,0"; // Empty vertex placeholder
                    }
                    if (v < 6) file << ","; // Add comma separator except for last vertex
                }
                file << "\n";
            }
            catch (const std::exception& e)
            {
                logError("PixelStats: Error writing path {} to CSV: {}", i, e.what());
                // Write error marker row to maintain file structure
                file << i << ",ERROR,ERROR,ERROR,ERROR,ERROR,ERROR,ERROR,ERROR,ERROR,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0\n";
            }
        }

        file.close();
        return true;
    }

    bool PixelStats::exportCIRDataJSONL(const std::string& filename, const CIRStaticParameters& staticParams)
    {
        std::ofstream file(filename);
        if (!file.is_open())
        {
            logError("PixelStats: Failed to open JSONL file: {}", filename);
            return false;
        }

        // Write static parameters as first JSON object
        file << "{\"type\":\"static_parameters\",\"data\":{";
        file << "\"receiver_area_m2\":" << std::scientific << std::setprecision(6) << staticParams.receiverArea << ",";
        file << "\"led_lambertian_order\":" << std::fixed << std::setprecision(3) << staticParams.ledLambertianOrder << ",";
        file << "\"light_speed_ms\":" << std::scientific << std::setprecision(3) << staticParams.lightSpeed << ",";
        file << "\"receiver_fov_rad\":" << std::fixed << std::setprecision(3) << staticParams.receiverFOV << ",";
        file << "\"optical_filter_gain\":" << std::fixed << std::setprecision(1) << staticParams.opticalFilterGain << ",";
        file << "\"optical_concentration\":" << std::fixed << std::setprecision(1) << staticParams.opticalConcentration;
        file << "}}\n";

        // Write path data as JSON objects with vertex information
        file << std::fixed << std::setprecision(6);
        for (size_t i = 0; i < mCIRRawData.size(); i++)
        {
            // Create a copy for potential modification (backward compatibility)
            CIRPathData data = mCIRRawData[i];

            // Handle legacy data if needed
            handleLegacyData(data);

            // Validate vertex data integrity (log warnings for invalid data but continue export)
            if (!validateCIRVertexData(data))
            {
                logWarning("PixelStats: Invalid vertex data in path {}, using default values", i);
                handleLegacyData(data); // Force default values for invalid data
            }

            try
            {
                file << "{\"type\":\"path_data\",\"data\":{";

                // Basic path data
                file << "\"path_index\":" << i << ",";
                file << "\"pixel_x\":" << data.pixelX << ",";
                file << "\"pixel_y\":" << data.pixelY << ",";
                file << "\"path_length_m\":" << data.pathLength << ",";
                file << "\"emission_angle_rad\":" << data.emissionAngle << ",";
                file << "\"reception_angle_rad\":" << data.receptionAngle << ",";
                file << "\"reflectance_product\":" << data.reflectanceProduct << ",";
                file << "\"reflection_count\":" << data.reflectionCount << ",";
                file << "\"emitted_power_w\":" << data.emittedPower << ",";
                file << "\"hit_emissive_surface\":" << (data.hitEmissiveSurface ? "true" : "false") << ",";

                // Vertex data
                file << "\"vertex_data\":{";
                file << "\"vertex_count\":" << data.vertexCount << ",";
                file << "\"base_position\":[" << data.basePosition.x << "," << data.basePosition.y << "," << data.basePosition.z << "],";
                file << "\"vertices\":[";

                // Decompress and write vertices
                std::vector<float3> vertices = decompressPathVertices(data);
                for (uint32_t v = 0; v < vertices.size(); v++)
                {
                    file << "{\"index\":" << v << ",\"position\":["
                         << vertices[v].x << "," << vertices[v].y << "," << vertices[v].z << "]}";
                    if (v < vertices.size() - 1) file << ","; // Add comma separator except for last vertex
                }
                file << "]}"; // Close vertices array and vertex_data object
                file << "}}\n"; // Close data object and main object
            }
            catch (const std::exception& e)
            {
                logError("PixelStats: Error writing path {} to JSONL: {}", i, e.what());
                // Write error marker object to maintain file structure
                file << "{\"type\":\"path_data\",\"error\":\"Failed to serialize path " << i << "\",\"data\":{}}\n";
            }
        }

        file.close();
        return true;
    }

    bool PixelStats::exportCIRDataTXT(const std::string& filename, const CIRStaticParameters& staticParams)
    {
        std::ofstream file(filename);
        if (!file.is_open())
        {
            logError("PixelStats: Failed to open TXT file: {}", filename);
            return false;
        }

        // Write header with static parameters (original format)
        file << "# CIR Path Data Export with Static Parameters\n";
        file << "# Static Parameters for VLC Channel Impulse Response Calculation:\n";
        file << "# A_receiver_area_m2=" << std::scientific << std::setprecision(6) << staticParams.receiverArea << "\n";
        file << "# m_led_lambertian_order=" << std::fixed << std::setprecision(3) << staticParams.ledLambertianOrder << "\n";
        file << "# c_light_speed_ms=" << std::scientific << std::setprecision(3) << staticParams.lightSpeed << "\n";
        file << "# FOV_receiver_rad=" << std::fixed << std::setprecision(3) << staticParams.receiverFOV << "\n";
        file << "# T_s_optical_filter_gain=" << std::fixed << std::setprecision(1) << staticParams.opticalFilterGain << "\n";
        file << "# g_optical_concentration=" << std::fixed << std::setprecision(1) << staticParams.opticalConcentration << "\n";
        file << "#\n";
        file << "# Path Data Format Extended with Vertex Collection:\n";
        file << "# PathIndex,PixelX,PixelY,PathLength(m),EmissionAngle(rad),ReceptionAngle(rad),ReflectanceProduct,ReflectionCount,EmittedPower(W),HitEmissiveSurface,\n";
        file << "# VertexCount,BasePosition(X,Y,Z),Vertices(X,Y,Z for each vertex up to 7)\n";
        file << "#\n";
        file << "# Vertex Collection Feature: Each path contains up to 7 collected vertices representing the light path trajectory\n";
        file << "# Base position is typically the camera position, vertices are stored as absolute world coordinates\n";
        file << "#\n";
        file << std::fixed << std::setprecision(6);

        // Write path data with vertex information
        for (size_t i = 0; i < mCIRRawData.size(); i++)
        {
            // Create a copy for potential modification (backward compatibility)
            CIRPathData data = mCIRRawData[i];

            // Handle legacy data if needed
            handleLegacyData(data);

            // Validate vertex data integrity (log warnings for invalid data but continue export)
            if (!validateCIRVertexData(data))
            {
                logWarning("PixelStats: Invalid vertex data in path {}, using default values", i);
                handleLegacyData(data); // Force default values for invalid data
            }

            try
            {
                // Basic path data
                file << i << ","
                     << data.pixelX << ","
                     << data.pixelY << ","
                     << data.pathLength << ","
                     << data.emissionAngle << ","
                     << data.receptionAngle << ","
                     << data.reflectanceProduct << ","
                     << data.reflectionCount << ","
                     << data.emittedPower << ","
                     << (data.hitEmissiveSurface ? 1 : 0) << ",";

                // Vertex data
                file << data.vertexCount << ","
                     << data.basePosition.x << ","
                     << data.basePosition.y << ","
                     << data.basePosition.z;

                // Decompress and write vertices
                std::vector<float3> vertices = decompressPathVertices(data);
                for (uint32_t v = 0; v < vertices.size(); v++)
                {
                    file << "," << vertices[v].x << "," << vertices[v].y << "," << vertices[v].z;
                }
                file << "\n";
            }
            catch (const std::exception& e)
            {
                logError("PixelStats: Error writing path {} to TXT: {}", i, e.what());
                // Write error marker row to maintain file structure
                file << i << ",ERROR,ERROR,ERROR,ERROR,ERROR,ERROR,ERROR,ERROR,ERROR,0,0,0\n";
            }
        }

        file.close();
        return true;
    }

    // === Vertex Processing Functions Implementation ===

    float3 PixelStats::decompressVertex(const CIRPathData::CompressedVertex& compressed, const float3& basePosition) const
    {
        // Extract relative coordinates from compressed format (matching GPU implementation)
        // Convert from f16 back to f32 using Falcor's utility functions
        float x = f16tof32(compressed.x & 0xFFFF);        // Extract lower 16 bits
        float y = f16tof32(compressed.x >> 16);           // Extract upper 16 bits
        float z = f16tof32(compressed.y & 0xFFFF);        // Extract z from second uint

        // Validate decompression results for NaN or infinite values
        if (std::isnan(x) || std::isnan(y) || std::isnan(z) ||
            std::isinf(x) || std::isinf(y) || std::isinf(z))
        {
            // Return error indicator position for invalid data (matching GPU behavior)
            return float3(0.666f, 0.666f, 0.666f);
        }

        // Additional validation: check for reasonable coordinate values
        const float MAX_REASONABLE_DISTANCE = 100000.0f; // 100km maximum distance
        if (abs(x) > MAX_REASONABLE_DISTANCE || abs(y) > MAX_REASONABLE_DISTANCE || abs(z) > MAX_REASONABLE_DISTANCE)
        {
            logWarning("PixelStats: Decompressed vertex has unreasonable coordinates, using error marker");
            return float3(0.666f, 0.666f, 0.666f);
        }

        // Convert relative coordinates back to world space
        float3 relative = float3(x, y, z);
        return basePosition + relative;
    }

    std::vector<float3> PixelStats::decompressPathVertices(const CIRPathData& cirData) const
    {
        std::vector<float3> vertices;
        vertices.reserve(cirData.vertexCount);

        // Decompress each valid vertex
        for (uint32_t i = 0; i < cirData.vertexCount && i < 7; i++)
        {
            float3 vertex = decompressVertex(cirData.compressedVertices[i], cirData.basePosition);
            vertices.push_back(vertex);
        }

        return vertices;
    }

    bool PixelStats::validateCIRVertexData(const CIRPathData& cirData) const
    {
        // Check vertex count range
        if (cirData.vertexCount > 7)
        {
            logError("PixelStats: Invalid vertex count: {} (maximum 7)", cirData.vertexCount);
            return false;
        }

        // Check base position validity
        if (std::isnan(cirData.basePosition.x) || std::isnan(cirData.basePosition.y) || std::isnan(cirData.basePosition.z) ||
            std::isinf(cirData.basePosition.x) || std::isinf(cirData.basePosition.y) || std::isinf(cirData.basePosition.z))
        {
            logError("PixelStats: Invalid base position with NaN/infinite values");
            return false;
        }

        // Additional validation: check for reasonable base position values
        const float MAX_WORLD_COORDINATE = 1000000.0f; // 1000km maximum world size
        if (abs(cirData.basePosition.x) > MAX_WORLD_COORDINATE ||
            abs(cirData.basePosition.y) > MAX_WORLD_COORDINATE ||
            abs(cirData.basePosition.z) > MAX_WORLD_COORDINATE)
        {
            logError("PixelStats: Base position coordinates exceed reasonable world bounds");
            return false;
        }

        // Verify compressed vertices can be decompressed without errors
        for (uint32_t i = 0; i < cirData.vertexCount; i++)
        {
            float3 decompressed = decompressVertex(cirData.compressedVertices[i], cirData.basePosition);

            // Check if decompression returned error indicator
            const float3 errorMarker(0.666f, 0.666f, 0.666f);
            if (length(decompressed - errorMarker) < 0.001f) // Allow small floating point tolerance
            {
                logError("PixelStats: Vertex decompression failed at index {}", i);
                return false;
            }

            // Additional validation: ensure decompressed vertex is reasonable
            const float MAX_VERTEX_DISTANCE = 1000000.0f; // 1000km maximum distance from base
            float3 relative = decompressed - cirData.basePosition;
            if (length(relative) > MAX_VERTEX_DISTANCE)
            {
                logError("PixelStats: Vertex {} is too far from base position", i);
                return false;
            }
        }

        return true;
    }

    // === Backward Compatibility Support ===

    /** Check if CIR data version supports vertex collection feature.
        Legacy data may not have vertex fields properly initialized.
        \param cirData CIR path data to check
        \return True if vertex data is available, false for legacy data
    */
    bool PixelStats::supportsVertexData(const CIRPathData& cirData) const
    {
        // Simple heuristic: if vertexCount is 0 and basePosition is zero vector,
        // likely legacy data without vertex collection
        return cirData.vertexCount > 0 ||
               (cirData.basePosition.x != 0.0f || cirData.basePosition.y != 0.0f || cirData.basePosition.z != 0.0f);
    }

    /** Handle legacy CIR data by providing default vertex information.
        \param cirData CIR path data to update (will be modified)
    */
    void PixelStats::handleLegacyData(CIRPathData& cirData) const
    {
        if (!supportsVertexData(cirData))
        {
            // Set default vertex information for legacy data
            cirData.vertexCount = 1;
            cirData.basePosition = float3(0.0f, 0.0f, 0.0f);

            // Set first vertex as default (empty/zero compression)
            cirData.compressedVertices[0].x = 0;
            cirData.compressedVertices[0].y = 0;

            // Clear remaining vertices
            for (uint32_t i = 1; i < 7; i++)
            {
                cirData.compressedVertices[i].x = 0;
                cirData.compressedVertices[i].y = 0;
            }

            logInfo("PixelStats: Legacy CIR data detected, using default vertex information");
        }
        else if (!validateCIRVertexData(cirData))
        {
            // Data claims to support vertices but validation failed - fix it
            logWarning("PixelStats: CIR vertex data failed validation, applying corrections");

            // Clamp vertex count to valid range
            if (cirData.vertexCount > 7)
            {
                cirData.vertexCount = 7;
            }

            // Fix invalid base position
            if (std::isnan(cirData.basePosition.x) || std::isinf(cirData.basePosition.x) ||
                std::isnan(cirData.basePosition.y) || std::isinf(cirData.basePosition.y) ||
                std::isnan(cirData.basePosition.z) || std::isinf(cirData.basePosition.z))
            {
                cirData.basePosition = float3(0.0f, 0.0f, 0.0f);
                logWarning("PixelStats: Fixed invalid base position to (0,0,0)");
            }

            // Validate and fix each vertex
            for (uint32_t i = 0; i < cirData.vertexCount; i++)
            {
                float3 vertex = decompressVertex(cirData.compressedVertices[i], cirData.basePosition);
                const float3 errorMarker(0.666f, 0.666f, 0.666f);

                if (length(vertex - errorMarker) < 0.001f)
                {
                    // This vertex failed decompression, set to zero
                    cirData.compressedVertices[i].x = 0;
                    cirData.compressedVertices[i].y = 0;
                    logWarning("PixelStats: Fixed corrupted vertex {} by setting to origin", i);
                }
            }

            // Clear unused vertex slots
            for (uint32_t i = cirData.vertexCount; i < 7; i++)
            {
                cirData.compressedVertices[i].x = 0;
                cirData.compressedVertices[i].y = 0;
            }
        }
    }

    FALCOR_SCRIPT_BINDING(PixelStats)
    {
        pybind11::class_<PixelStats> pixelStats(m, "PixelStats");
        pixelStats.def_property("enabled", &PixelStats::isEnabled, &PixelStats::setEnabled);
        pixelStats.def_property_readonly("stats", [](PixelStats* pPixelStats) {
            PixelStats::Stats stats;
            pPixelStats->getStats(stats);
            return toPython(stats);
        });
    }
}
