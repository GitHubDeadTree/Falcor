/***************************************************************************
 # Copyright (c) 2015-21, NVIDIA CORPORATION. All rights reserved.
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

/** Functionality for collecting runtime stats in the path tracer.

    The host sets the following defines:

    _PIXEL_STATS_ENABLED       Nonzero if stats should be collected.
    _PIXEL_STATS_RAW_DATA_ENABLED  Nonzero if raw CIR data should be collected.

*/
__exported import PixelStatsShared;

// Include CIRPathData structure
__exported import RenderPasses.PathTracer.CIRPathData;

RWTexture2D<uint> gStatsRayCount[(uint)PixelStatsRayType::Count]; // Per-pixel ray count stats.
RWTexture2D<uint> gStatsPathLength;          // Per-pixel path length.
RWTexture2D<uint> gStatsPathVertexCount;     // Per-pixel path vertex count.
RWTexture2D<uint> gStatsVolumeLookupCount;   // Per-pixel volume lookup count.

// CIR statistics buffers
RWTexture2D<float> gStatsCIRData[(uint)PixelStatsCIRType::Count]; // Per-pixel CIR data stats.
RWTexture2D<uint> gStatsCIRValidSamples;     // Per-pixel valid CIR sample count.

// CIR raw data collection buffers - Always declared to avoid binding issues
RWStructuredBuffer<CIRPathData> gCIRRawDataBuffer;  // Raw CIR path data storage
RWByteAddressBuffer gCIRCounterBuffer;              // Counter for number of stored paths

// Constant buffer for CIR parameters following Falcor standard pattern
cbuffer PerFrameCB
{
    uint gMaxCIRPaths;                              // Maximum number of paths that can be stored
}

#ifdef _PIXEL_STATS_ENABLED
static uint2 gPixelStatsPixel;
#endif

void logSetPixel(uint2 pixel)
{
#ifdef _PIXEL_STATS_ENABLED
    gPixelStatsPixel = pixel;
#endif
}

void logTraceRay(PixelStatsRayType rayType)
{
#ifdef _PIXEL_STATS_ENABLED
    InterlockedAdd(gStatsRayCount[(uint)rayType][gPixelStatsPixel], 1);
#endif
}

void logPathLength(uint pathLength)
{
#ifdef _PIXEL_STATS_ENABLED
    gStatsPathLength[gPixelStatsPixel] = pathLength;
#endif
}

void logPathVertex()
{
#ifdef _PIXEL_STATS_ENABLED
    InterlockedAdd(gStatsPathVertexCount[gPixelStatsPixel], 1);
#endif
}

void logVolumeLookup()
{
#ifdef _PIXEL_STATS_ENABLED
    InterlockedAdd(gStatsVolumeLookupCount[gPixelStatsPixel], 1);
#endif
}

// CIR statistics logging functions - MODIFIED: No validation, direct accumulation
// GPU-side accumulates all sanitized data, CPU-side applies final filtering
void logCIRPathLength(float pathLength)
{
#ifdef _PIXEL_STATS_ENABLED
    gStatsCIRData[(uint)PixelStatsCIRType::PathLength][gPixelStatsPixel] = pathLength;
    InterlockedAdd(gStatsCIRValidSamples[gPixelStatsPixel], 1);
#endif
}

void logCIREmissionAngle(float angle)
{
#ifdef _PIXEL_STATS_ENABLED
    gStatsCIRData[(uint)PixelStatsCIRType::EmissionAngle][gPixelStatsPixel] = angle;
#endif
}

void logCIRReceptionAngle(float angle)
{
#ifdef _PIXEL_STATS_ENABLED
    gStatsCIRData[(uint)PixelStatsCIRType::ReceptionAngle][gPixelStatsPixel] = angle;
#endif
}

void logCIRReflectanceProduct(float reflectance)
{
#ifdef _PIXEL_STATS_ENABLED
    gStatsCIRData[(uint)PixelStatsCIRType::ReflectanceProduct][gPixelStatsPixel] = reflectance;
#endif
}

void logCIREmittedPower(float power)
{
#ifdef _PIXEL_STATS_ENABLED
    gStatsCIRData[(uint)PixelStatsCIRType::EmittedPower][gPixelStatsPixel] = power;
#endif
}

void logCIRReflectionCount(uint count)
{
#ifdef _PIXEL_STATS_ENABLED
    gStatsCIRData[(uint)PixelStatsCIRType::ReflectionCount][gPixelStatsPixel] = (float)count;
#endif
}

// Internal function for raw CIR data collection - MODIFIED: No validation, direct storage
void logCIRRawPathInternal(CIRPathData pathData)
{
#ifdef _PIXEL_STATS_RAW_DATA_ENABLED
    // Atomically increment the counter to get a unique index
    uint index = 0;
    gCIRCounterBuffer.InterlockedAdd(0, 1, index);

    // Check if we have space in the buffer
    if (index < gMaxCIRPaths)
    {
        gCIRRawDataBuffer[index] = pathData;
    }
#endif
}

// Internal function for statistics collection - MODIFIED: No validation, direct accumulation
void logCIRStatisticsInternal(CIRPathData pathData)
{
#ifdef _PIXEL_STATS_ENABLED
    logCIRPathLength(pathData.pathLength);
    logCIREmissionAngle(pathData.emissionAngle);
    logCIRReceptionAngle(pathData.receptionAngle);
    logCIRReflectanceProduct(pathData.reflectanceProduct);
    logCIREmittedPower(pathData.emittedPower);
    logCIRReflectionCount(pathData.reflectionCount);
#endif
}

// Unified CIR data validation and sanitization function
CIRPathData validateAndSanitizeCIRData(CIRPathData pathData)
{
    // Create a copy for modification
    CIRPathData validatedData = pathData;

    // First sanitize the data to fix invalid values
    validatedData.sanitize();

    // Return the sanitized data
    return validatedData;
}

// Legacy function for backward compatibility - still includes validation
void logCIRRawPath(CIRPathData pathData)
{
#ifdef _PIXEL_STATS_RAW_DATA_ENABLED
    // Maintain legacy behavior with validation for compatibility
    pathData.sanitize();
    if (!pathData.isValid()) return;

    logCIRRawPathInternal(pathData);
#endif
}

// Unified CIR logging function - MODIFIED: Added path filtering to remove invalid paths
void logCIRPathComplete(CIRPathData pathData)
{
    // 只检查路径长度的NaN/无穷大值，如果异常则直接丢弃整条光路
    if (isnan(pathData.pathLength) || isinf(pathData.pathLength))
    {
        // 直接返回，不记录任何数据到缓冲区
        return;
    }

    // 对于路径长度有效的数据，直接记录而不进行任何修改
    // 其他字段的验证交给CPU端处理
    logCIRStatisticsInternal(pathData);
    logCIRRawPathInternal(pathData);
}
