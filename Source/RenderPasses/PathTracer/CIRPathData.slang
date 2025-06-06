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
#pragma once

// CIR calculation constants
static const uint kMaxCIRPaths = 1000000;       ///< Maximum number of CIR paths to collect
static const bool kOutputCIRData = true;        ///< Enable CIR data collection

/** CIR (Channel Impulse Response) path data structure.

    This structure stores the essential parameters of each light propagation path
    needed for calculating the Channel Impulse Response in visible light communication systems.
    Each path represents light traveling from an LED transmitter through possible reflections
    to a photodiode receiver.
*/
struct CIRPathData
{
    float pathLength;           ///< d_i: Total propagation distance of the path (meters)
    float emissionAngle;        ///< φ_i: Emission angle at LED surface (radians)
    float receptionAngle;       ///< θ_i: Reception angle at photodiode surface (radians)
    float reflectanceProduct;   ///< r_i product: Product of all surface reflectances along the path [0,1]
    uint reflectionCount;       ///< K_i: Number of reflections in the path
    float emittedPower;         ///< P_t: Emitted optical power (watts)
    uint2 pixelCoord;          ///< Pixel coordinates where the path terminates
    uint pathIndex;            ///< Unique index identifier for this path

    /** Initialize CIR path data with default values.
        Default values represent a direct line-of-sight path with minimal parameters.
    */
    [mutating] void initDefaults()
    {
        pathLength = 0.0f;          // Will be accumulated during path tracing
        emissionAngle = 0.0f;       // Perpendicular emission (0 radians)
        receptionAngle = 0.0f;      // Perpendicular reception (0 radians)
        reflectanceProduct = 1.0f;  // No reflections initially, so product = 1
        reflectionCount = 0;        // No reflections initially
        emittedPower = 1.0f;        // Default 1 watt emission power
        pixelCoord = uint2(0, 0);   // Origin pixel
        pathIndex = 0;              // Will be set when path is created
    }

    /** Validate that all CIR parameters are within expected physical ranges.
        \return True if all parameters are valid, false otherwise.
    */
    bool isValid()
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
        if (isnan(pathLength) || isinf(pathLength)) return false;
        if (isnan(emissionAngle) || isinf(emissionAngle)) return false;
        if (isnan(receptionAngle) || isinf(receptionAngle)) return false;
        if (isnan(reflectanceProduct) || isinf(reflectanceProduct)) return false;
        if (isnan(emittedPower) || isinf(emittedPower)) return false;

        return true;
    }

    /** Sanitize parameters to valid ranges if they are invalid.
        This provides robust error handling for edge cases.
    */
    [mutating] void sanitize()
    {
        // Clamp path length to reasonable range
        pathLength = clamp(pathLength, 0.1f, 1000.0f);

        // Clamp angles to [0, π] range
        emissionAngle = clamp(emissionAngle, 0.0f, 3.14159265f);
        receptionAngle = clamp(receptionAngle, 0.0f, 3.14159265f);

        // Clamp reflectance product to [0, 1]
        reflectanceProduct = clamp(reflectanceProduct, 0.0f, 1.0f);

        // Clamp reflection count to reasonable range
        reflectionCount = min(reflectionCount, 100u);

        // Clamp emitted power to positive reasonable range
        emittedPower = clamp(emittedPower, 1e-6f, 1000.0f);

        // Handle NaN and infinity cases
        if (isnan(pathLength) || isinf(pathLength)) pathLength = 0.3f; // Default ~1ns light travel
        if (isnan(emissionAngle) || isinf(emissionAngle)) emissionAngle = 0.785398f; // 45 degrees
        if (isnan(receptionAngle) || isinf(receptionAngle)) receptionAngle = 0.785398f; // 45 degrees
        if (isnan(reflectanceProduct) || isinf(reflectanceProduct)) reflectanceProduct = 0.5f;
        if (isnan(emittedPower) || isinf(emittedPower)) emittedPower = 1.0f;
    }
};
