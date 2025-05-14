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
// Only import Scene.HitInfo when not using actual normals to avoid preprocessor errors
#if USE_ACTUAL_NORMALS
import Scene.Scene;
import Scene.Shading;
import Scene.HitInfo;
#else
import Scene.HitInfo;  // HitInfo is always needed for basic VBuffer access
#endif

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
    bool gPassthrough;                  ///< When enabled, directly outputs the input rayinfo without calculations
}

// Scene data (bound by the host code)
#if USE_ACTUAL_NORMALS
ParameterBlock<Scene> gScene;           ///< Scene data for accessing geometry information
#endif

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

    // Passthrough mode - directly output the input rayinfo
    if (gPassthrough)
    {
        gOutputIrradiance[pixel] = rayInfo;
        return;
    }

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
        uint packedHitInfo = gVBuffer[pixel];

        #if USE_ACTUAL_NORMALS
        // Initialize HitInfo from packed data
        HitInfo hit = HitInfo(packedHitInfo);

        if (hit.isValid())
        {
            // Process based on hit type
            HitType hitType = hit.getType();

            if (hitType == HitType::Triangle)
            {
                // Get triangle hit data
                TriangleHit triangleHit = hit.getTriangleHit();

                // Extract vertex data from the triangle
                GeometryInstanceID instanceID = triangleHit.instanceID;
                uint primitiveIndex = triangleHit.primitiveIndex;

                // Get vertex data from the scene
                VertexData vertexData = gScene.getVertexData(triangleHit);

                // Use the normal as the receiving surface direction
                normal = normalize(vertexData.normalW);
            }
            // Can add support for other hit types (DisplacedTriangle, Curve, etc.) if needed
        }
        #endif
    }
    else
    {
        // Use the configurable fixed normal
        normal = normalize(gFixedNormal);
    }

    // Calculate cosine term (N·ω)
    float cosTheta = max(0.0f, dot(normal, rayDir));

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
