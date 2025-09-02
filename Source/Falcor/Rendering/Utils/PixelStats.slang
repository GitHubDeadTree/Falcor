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

// P1 optimization: Path type counters
RWByteAddressBuffer gNEEPathCounter;                // Counter for NEE paths
RWByteAddressBuffer gRegularPathCounter;            // Counter for regular paths

// Constant buffer for CIR parameters following Falcor standard pattern
cbuffer PerFrameCB
{
    uint gMaxCIRPaths;                              // Maximum number of paths that can be stored

    // GPU-side CIR filtering parameters for real-time filtering
    bool gCIRFilteringEnabled;                      // Enable/disable CIR filtering on GPU
    float gCIRMinPathLength;                        // Minimum path length for filtering (meters)
    float gCIRMaxPathLength;                        // Maximum path length for filtering (meters)
    float gCIRMinEmittedPower;                      // Minimum emitted power for filtering (watts)
    float gCIRMaxEmittedPower;                      // Maximum emitted power for filtering (watts)
    float gCIRMinAngle;                             // Minimum angle for filtering (radians)
    float gCIRMaxAngle;                             // Maximum angle for filtering (radians)
    float gCIRMinReflectance;                       // Minimum reflectance for filtering
    float gCIRMaxReflectance;                       // Maximum reflectance for filtering
    
    // P0 optimization: NEE path filtering parameters
    bool gCIRCollectNEEOnly;                        // Collect only NEE paths (default: true for VLC)
    bool gCIRCollectRegularPaths;                   // Collect regular (non-NEE) paths
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

void logRayWavelength(float wavelength)
{
#ifdef _PIXEL_STATS_ENABLED
    gStatsCIRData[(uint)PixelStatsCIRType::Wavelength][gPixelStatsPixel] = wavelength;
#endif
}

// Internal function for raw CIR data collection - MODIFIED: No validation, direct storage
void logCIRRawPathInternal(CIRPathData pathData)
{
#ifdef _PIXEL_STATS_RAW_DATA_ENABLED
    // P1 optimization: Track path type statistics
    if (pathData.getIsNEEPath())
    {
        uint dummy;
        gNEEPathCounter.InterlockedAdd(0, 1, dummy);
    }
    else
    {
        uint dummy;
        gRegularPathCounter.InterlockedAdd(0, 1, dummy);
    }
    
    // Atomically increment the counter to get a unique index
    uint index = 0;
    gCIRCounterBuffer.InterlockedAdd(0, 1, index);

    // Check if we have space in the buffer
    if (index < gMaxCIRPaths)
    {
        gCIRRawDataBuffer[index] = pathData;
    }
    else
    {
        // Buffer overflow detected - GPU shaders cannot use logWarning
        // Overflow is detected but not logged to avoid compilation errors
        // CPU side monitoring through PixelStats handles overflow reporting
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
    // Copy to local variable to avoid mutation warning on 'in' parameter
    CIRPathData localPathData = pathData;
    localPathData.sanitize();
    if (!localPathData.isValid()) return;
    pathData = localPathData;

    logCIRRawPathInternal(pathData);
#endif
}

// GPU-side CIR data filtering function for real-time filtering
// This function applies user-configured filtering parameters on GPU to ensure only valid data is stored
bool isValidCIRDataGPU(CIRPathData data)
{
    // If filtering is disabled, accept all data (except NaN/inf checks in logCIRPathComplete)
    if (!gCIRFilteringEnabled) return true;

    // Enhanced VLC-specific data validity checks
    if (data.pathLength < 0.0f || data.emittedPower < 0.0f ||
        data.reflectanceProduct < 0.0f || data.reflectanceProduct > 1.0f)
    {
        return false;
    }

    // VLC angle validity: angles should be in range [0, π/2] for physical systems
    // Reject angles that are clearly unphysical (negative or > π)
    if (data.emissionAngle < 0.0f || data.emissionAngle > 3.14159f ||
        data.receptionAngle < 0.0f || data.receptionAngle > 3.14159f)
    {
        return false;
    }

    // Apply user-configured filtering criteria
    bool validPathLength = data.pathLength >= gCIRMinPathLength && data.pathLength <= gCIRMaxPathLength;
    bool validEmittedPower = data.emittedPower >= gCIRMinEmittedPower && data.emittedPower <= gCIRMaxEmittedPower;
    bool validEmissionAngle = data.emissionAngle >= gCIRMinAngle && data.emissionAngle <= gCIRMaxAngle;
    bool validReceptionAngle = data.receptionAngle >= gCIRMinAngle && data.receptionAngle <= gCIRMaxAngle;
    bool validReflectance = data.reflectanceProduct >= gCIRMinReflectance && data.reflectanceProduct <= gCIRMaxReflectance;

    return validPathLength && validEmittedPower && validEmissionAngle && validReceptionAngle && validReflectance;
}

// Unified CIR logging function - MODIFIED: Added GPU-side real-time filtering
void logCIRPathComplete(CIRPathData pathData)
{
    // P0 optimization: NEE path filtering
    // Check if we should filter based on path type (NEE vs regular)
    if (gCIRCollectNEEOnly && !pathData.getIsNEEPath())
    {
        // We're in NEE-only mode but this is not an NEE path - skip it
        return;
    }
    
    // Check if we should collect regular paths
    if (!gCIRCollectNEEOnly && !gCIRCollectRegularPaths && !pathData.getIsNEEPath())
    {
        // We're not in NEE-only mode, but regular path collection is disabled 
        // and this is not an NEE path - skip it
        return;
    }
    
    // Enhanced sanity checks - reject NaN/infinite values in critical fields
    if (isnan(pathData.pathLength) || isinf(pathData.pathLength) ||
        isnan(pathData.emissionAngle) || isinf(pathData.emissionAngle) ||
        isnan(pathData.receptionAngle) || isinf(pathData.receptionAngle))
    {
        // Discard invalid data immediately - do not store in buffer
        return;
    }

    // Apply GPU-side real-time filtering using user-configured parameters
    if (!isValidCIRDataGPU(pathData))
    {
        // Path does not meet user filtering criteria - discard to save buffer space
        return;
    }

    // Only store data that passes both sanity checks and user filtering criteria
    // This ensures buffer contains only data the user actually wants
    logCIRStatisticsInternal(pathData);
    logCIRRawPathInternal(pathData);
}
