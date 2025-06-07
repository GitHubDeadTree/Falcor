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

// CIR Computation Shader for Channel Impulse Response calculation
// This shader computes the CIR from path tracer data for visible light communication

#include "RenderPasses/PathTracer/CIRPathData.slang"

cbuffer PerFrameCB
{
    float gTimeResolution;      // Time resolution in seconds
    float gMaxDelay;           // Maximum delay in seconds  
    uint gCIRBins;             // Number of CIR bins
    float gLEDPower;           // LED power in watts
    float gHalfPowerAngle;     // LED half power angle in radians
    float gReceiverArea;       // Receiver area in square meters
    float gFieldOfView;        // Receiver field of view in radians
    float gLambertianOrder;    // Calculated Lambertian order
    uint gPathCount;           // Total number of paths to process
};

// Input buffers
StructuredBuffer<CIRPathData> gPathDataBuffer;  // Path data from PathTracer

// Output buffers  
RWBuffer<uint> gOutputCIR;          // CIR result vector (as uint for atomic operations)
RWBuffer<uint> gOverflowCounter;    // Counter for paths exceeding time range

// Constants
static const float kLightSpeed = 2.998e8f;  // Speed of light in m/s
static const float kPI = 3.14159265359f;
static const float kMinGain = 1e-12f;       // Minimum gain to avoid zero values
static const float kMaxValidDelay = 1e-3f;  // Maximum valid delay (1ms)
static const uint kMaxAttempts = 16;        // Maximum attempts for atomic operations

/** Compute the power gain for a single path using the VLC channel model.
    Based on the formula: H_i = (A / (4π * d_i²)) * ((m+1)/(2π)) * cos^m(φ_i) * cos(θ_i) * ∏r_k
    
    \param[in] pathData CIR path data containing all required parameters
    \param[in] receiverArea Receiver photodiode area in square meters
    \param[in] lambertianOrder Lambertian order m calculated from LED half-power angle
    \return Power gain for this path (dimensionless)
*/
float computePathGain(CIRPathData pathData, float receiverArea, float lambertianOrder)
{
    // Validate input path data
    if (!pathData.isValid())
    {
        return kMinGain; // Return minimum gain for invalid data
    }
    
    // Calculate distance-based area gain: A / (4π * d²)
    float distanceSquared = pathData.pathLength * pathData.pathLength;
    if (distanceSquared < 1e-6f) // Prevent division by very small numbers
    {
        distanceSquared = 1e-6f; // Minimum distance of 1mm
    }
    float areaGain = receiverArea / (4.0f * kPI * distanceSquared);
    
    // Calculate Lambertian emission pattern gain: (m+1)/(2π) * cos^m(φ_i)
    float lambertianCoeff = (lambertianOrder + 1.0f) / (2.0f * kPI);
    float cosEmission = cos(pathData.emissionAngle);
    
    // Ensure cosine is non-negative (should be for valid emission angles)
    cosEmission = max(cosEmission, 0.0f);
    
    float emissionGain = lambertianCoeff * pow(cosEmission, lambertianOrder);
    
    // Calculate reception angle gain: cos(θ_i)
    float cosReception = cos(pathData.receptionAngle);
    float receptionGain = max(cosReception, 0.0f); // Ensure non-negative
    
    // Apply reflection losses: ∏r_k (already computed as product in pathData)
    float reflectionLoss = pathData.reflectanceProduct;
    
    // Compute total path gain
    float totalGain = areaGain * emissionGain * receptionGain * reflectionLoss;
    
    // Validate and clamp result
    if (isnan(totalGain) || isinf(totalGain) || totalGain < 0.0f)
    {
        return kMinGain; // Return minimum valid gain
    }
    
    // Clamp to reasonable range (max gain of 1.0 for sanity check)
    return clamp(totalGain, kMinGain, 1.0f);
}

/** Compute the received power for a single path.
    
    \param[in] pathData CIR path data containing emitted power
    \param[in] pathGain Power gain calculated by computePathGain
    \return Received power in watts
*/
float computePathPower(CIRPathData pathData, float pathGain)
{
    float power = pathData.emittedPower * pathGain;
    
    // Validate power calculation
    if (isnan(power) || isinf(power) || power < 0.0f)
    {
        return kMinGain; // Return minimum power for invalid calculations
    }
    
    // Clamp power to reasonable range
    return clamp(power, kMinGain, pathData.emittedPower); // Power can't exceed emitted power
}

/** Compute propagation delay for a path.
    Task 2.3: Enhanced delay calculation with validation
    
    \param[in] pathLength Total path length in meters
    \return Propagation delay in seconds
*/
float computePathDelay(float pathLength)
{
    // Basic delay calculation: time = distance / speed
    float delay = pathLength / kLightSpeed;
    
    // Validate delay calculation for physical reasonableness
    if (isnan(delay) || isinf(delay) || delay < 0.0f)
    {
        // Invalid delay - return default 1ns delay
        return 1e-9f;
    }
    
    // Check if delay exceeds maximum valid delay (1ms for room-scale scenarios)
    if (delay > kMaxValidDelay)
    {
        // Excessive delay - return default 1ns delay
        return 1e-9f;
    }
    
    // Ensure minimum delay for numerical stability
    return max(delay, 1e-12f); // Minimum 1ps delay
}

/** Convert delay time to discrete bin index.
    Task 2.3: Time discretization for CIR vector
    
    \param[in] delay Propagation delay in seconds
    \param[in] timeResolution Time resolution in seconds per bin
    \return Bin index for CIR vector
*/
uint delayToBinIndex(float delay, float timeResolution)
{
    return uint(delay / timeResolution);
}

/** Atomically add floating point value to CIR buffer using integer operations.
    Task 2.3: Safe atomic accumulation for parallel processing
    
    \param[in] buffer Target buffer for atomic operation
    \param[in] index Buffer index to update
    \param[in] value Floating point value to add
    \return True if operation succeeded within attempt limit
*/
bool atomicAddFloat(RWBuffer<uint> buffer, uint index, float value)
{
    uint valueAsUint = asuint(value);
    
    // Retry loop for atomic float addition using compare-and-swap
    [loop]
    for (uint attempt = 0; attempt < kMaxAttempts; ++attempt)
    {
        uint originalValue = buffer[index];
        float currentValue = asfloat(originalValue);
        
        // Compute new accumulated value
        float newValue = currentValue + value;
        uint newValueAsUint = asuint(newValue);
        
        // Attempt atomic compare-and-swap
        uint actualOriginal;
        InterlockedCompareExchange(buffer[index], originalValue, newValueAsUint, actualOriginal);
        
        // Check if update succeeded
        if (actualOriginal == originalValue)
        {
            return true; // Success
        }
    }
    
    return false; // Failed after maximum attempts
}

/** Main CIR computation kernel.
    Task 2.3: Complete CIR discretization with delay calculation and overflow handling
    Each thread processes one path and accumulates its contribution to the CIR.
*/
[numthreads(256, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    uint pathIndex = id.x;
    
    // Bounds check for path data
    if (pathIndex >= gPathCount)
        return;
    
    // Get path data from buffer
    CIRPathData pathData = gPathDataBuffer[pathIndex];
    
    // Validate path data - skip invalid paths without logging (too expensive in shader)
    if (!pathData.isValid())
    {
        return;
    }
    
    // Task 2.3: Compute path gain using VLC channel model
    float pathGain = computePathGain(pathData, gReceiverArea, gLambertianOrder);
    
    // Task 2.3: Compute received power for this path
    float pathPower = computePathPower(pathData, pathGain);
    
    // Skip paths with negligible power contribution to reduce noise
    if (pathPower < kMinGain)
    {
        return;
    }
    
    // Task 2.3: Compute propagation delay with enhanced validation
    float pathDelay = computePathDelay(pathData.pathLength);
    
    // Task 2.3: Convert continuous delay to discrete bin index for CIR vector
    uint binIndex = delayToBinIndex(pathDelay, gTimeResolution);
    
    // Task 2.3: Check if delay is within CIR time range (overflow handling)
    if (binIndex >= gCIRBins)
    {
        // Count overflow paths for diagnostics - these paths arrive too late
        InterlockedAdd(gOverflowCounter[0], 1);
        return;
    }
    
    // Task 2.3: Atomically accumulate power contribution to discrete CIR bin
    bool success = atomicAddFloat(gOutputCIR, binIndex, pathPower);
    
    // If atomic operation failed after retries, try adjacent bin to avoid data loss
    if (!success && binIndex + 1 < gCIRBins)
    {
        // Fallback: add to next bin if current bin is experiencing contention
        atomicAddFloat(gOutputCIR, binIndex + 1, pathPower);
    }
} 