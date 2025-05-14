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

// Scene imports for accessing geometry data
// Only import HitInfo without requiring full Scene module
// In a future version, we'll add proper scene geometry support
import Scene.HitInfo;

// Input data
Texture2D<float4> gInputRayInfo;        ///< Input ray direction (xyz) and intensity (w)
Texture2D<uint> gVBuffer;               ///< Visibility buffer for surface identification (optional)

// Output data
RWTexture2D<float4> gOutputIrradiance;  ///< Output irradiance (xyz) and total cos-weighted samples count (w)

// Parameters
cbuffer PerFrameCB
{
    bool gReverseRayDirection;          ///< Whether to reverse ray direction when calculating irradiance
    float gIntensityScale;              ///< Scaling factor for the irradiance
    bool gDebugNormalView;              ///< When enabled, visualizes normal directions as colors
    bool gUseActualNormals;             ///< Whether to use actual normals from VBuffer
    float3 gFixedNormal;                ///< Fixed normal direction when not using actual normals
}

// Scene data (will be bound by the host code)
// 在任务5中，我们不直接声明和使用场景变量
// 在任务6中，我们会正确配置场景访问

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

    // Determine the normal to use (actual or fixed)
    float3 normal;
    if (gUseActualNormals)
    {
        // Start with a default value
        normal = float3(0, 0, 1);

        // Get VBuffer data
        uint hitInfo = gVBuffer[pixel];

        // Check if we have valid hit info
        bool hasValidHit = (hitInfo != 0);

        // In Task 5, we're just setting up dependencies
        // No actual normal extraction yet, will be implemented in Task 6
        // The real normal access requires these scene dependencies to be properly configured

        // Note: In Task 6, we'll add code here to extract real normals from the scene
        // using HitInfo, VertexData, and the scene interface
    }
    else
    {
        // Use the configurable fixed normal
        normal = normalize(gFixedNormal);
    }

    // In Debug Normal View mode, use the selected normal
    // For all other modes, we'll still use the fixed normal for calculation in this stage
    float3 calculationNormal = gUseActualNormals ? normal : normalize(gFixedNormal);

    // Calculate cosine term (N·ω)
    float cosTheta = max(0.0f, dot(calculationNormal, rayDir));

    // Calculate irradiance contribution: L * cosθ
    float irradiance = cosTheta * intensity * gIntensityScale;

    // Output irradiance (xyz) and number of samples (w)
    if (gDebugNormalView)
    {
        // Map normal from [-1,1] range to [0,1] range for visualization
        float3 normalColor = (normal + 1.0f) * 0.5f;
        gOutputIrradiance[pixel] = float4(normalColor, 1.0f);
    }
    else
    {
        // Regular irradiance output
        gOutputIrradiance[pixel] = float4(irradiance, irradiance, irradiance, 1.0f);
    }
}
