/***************************************************************************
 # Copyright (c) 2024, NVIDIA CORPORATION. All rights reserved.
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

/** Compute shader for calculating irradiance from ray direction and intensity data.

    Irradiance (E) is calculated as:
    E = L * cos(θ) = L * (N·ω)

    where:
    - L is the radiance (intensity)
    - θ is the angle between the ray direction and the surface normal
    - N is the surface normal (we assume (0,0,1) for simplicity)
    - ω is the ray direction (negated if reverseRayDirection is enabled)

    For Monte Carlo integration, we sum all irradiance contributions across all paths.
*/

// Input data
Texture2D<float4> gInputRayInfo;        ///< Input ray direction (xyz) and intensity (w)

// Output data
RWTexture2D<float4> gOutputIrradiance;  ///< Output irradiance (xyz) and total cos-weighted samples count (w)

// Parameters
cbuffer PerFrameCB
{
    bool gReverseRayDirection;          ///< Whether to reverse ray direction when calculating irradiance
    float gIntensityScale;              ///< Scaling factor for the irradiance
}

[numthreads(16, 16, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint2 pixel = dispatchThreadId.xy;

    // Skip if out of bounds
    uint width, height;
    gInputRayInfo.GetDimensions(width, height);
    if (any(pixel >= uint2(width, height))) return;

    // Read ray direction and intensity from input
    float4 rayInfo = gInputRayInfo[pixel];
    float3 rayDir = rayInfo.xyz;
    float intensity = rayInfo.w;

    // For irradiance calculation, we need the ray direction to be pointing toward the receiving surface
    // In path tracing, ray directions typically point from camera/surface to the light source
    // If reverseRayDirection is true, we flip the direction
    if (gReverseRayDirection)
    {
        rayDir = -rayDir;
    }

    // We assume the receiving surface normal is (0,0,1) for simplicity
    // This can be modified to use an actual normal if needed
    float3 normal = float3(0, 0, 1);

    // Calculate cosine term (N·ω)
    float cosTheta = max(0.0f, dot(normal, rayDir));

    // Calculate irradiance contribution: L * cosθ
    float irradiance = cosTheta * intensity * gIntensityScale;

    // Output irradiance (xyz) and number of samples (w)
    // For Monte Carlo integration, we need to sum all contributions
    // The w component can be used to normalize the result if needed (not used in this implementation)
    gOutputIrradiance[pixel] = float4(irradiance, irradiance, irradiance, 1.0f);
}
