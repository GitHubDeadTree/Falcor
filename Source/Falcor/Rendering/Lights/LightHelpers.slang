/***************************************************************************
 # Copyright (c) 2015-22, NVIDIA CORPORATION. All rights reserved.
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

/** This file contains helper functions for analytic light source sampling.
    The code supports Falcor's analytic point, directional, and area lights,
    which are all defined in the scene description.

    Mesh lights (emissive geometry) and light probes are handled separately.

    This is work in progress. The code is not very well-tested.
*/

#include "Utils/Math/MathConstants.slangh"

import Scene.Scene;
import Scene.Lights.LightData;
import Utils.Math.MathHelpers;
import Utils.Sampling.SampleGeneratorInterface;

static const float kMinLightDistSqr = 1e-9f;
static const float kMaxLightDistance = FLT_MAX;

/** Interpolates intensity from custom light field data based on angle.
    \param[in] dataOffset Offset to the start of light field data for this LED.
    \param[in] dataSize Number of data points in the light field.
    \param[in] angle The angle in radians (0 to π).
    \return Interpolated intensity value.
*/
float interpolateLightField(uint32_t dataOffset, uint32_t dataSize, float angle)
{
    // Safety checks
    if (dataSize == 0) return 0.0f;
    if (dataSize == 1) return gScene.gLightFieldData[dataOffset].y;

    // Clamp angle to valid range [0, π]
    angle = clamp(angle, 0.0f, M_PI);

    // Handle edge cases
    float2 firstPoint = gScene.gLightFieldData[dataOffset];
    float2 lastPoint = gScene.gLightFieldData[dataOffset + dataSize - 1];

    // If angle is before first data point
    if (angle <= firstPoint.x) return firstPoint.y;

    // If angle is after last data point
    if (angle >= lastPoint.x) return lastPoint.y;

    // Binary search for the correct interval
    uint32_t left = 0;
    uint32_t right = dataSize - 1;

    while (right - left > 1)
    {
        uint32_t mid = (left + right) / 2;
        float midAngle = gScene.gLightFieldData[dataOffset + mid].x;

        if (angle <= midAngle)
            right = mid;
        else
            left = mid;
    }

    // Linear interpolation between left and right points
    float2 leftPoint = gScene.gLightFieldData[dataOffset + left];
    float2 rightPoint = gScene.gLightFieldData[dataOffset + right];

    float deltaAngle = rightPoint.x - leftPoint.x;

    // Handle degenerate case
    if (deltaAngle < 1e-6f) return leftPoint.y;

    float t = (angle - leftPoint.x) / deltaAngle;
    return lerp(leftPoint.y, rightPoint.y, t);
}

/** Describes a light sample for Falcor's analytic light sources.

    The struct contains a superset of what is normally needed for evaluating
    lighting integrals. Be careful not to access fields that are not needed,
    to make sure dead code elimination removes the computations.
*/
struct AnalyticLightSample
{
    float3  posW;           ///< Sampled point on the light source in world space (for local lights only).
    float3  normalW;        ///< Normal of the sampled point on the light source in world space (normalized).
    float3  dir;            ///< Direction from the shading point to the sampled point on the light in world space (normalized).
    float   distance;       ///< Distance from the shading point to sampled point on the light.
    float3  Li;             ///< Incident radiance at the shading point (unshadowed). Note: Already divided by the pdf.
    float   pdf;            ///< Probability density function with respect to solid angle at the shading point.
};

/** Internal helper function to finalize the shared computations for area light samples.
    The input sample must already have posW and normalW computed.
    \return True if the sample is valid, false otherwise.
*/
bool finalizeAreaLightSample(const float3 shadingPosW, const LightData light, inout AnalyticLightSample ls)
{
    // Compute direction and distance to light.
    // The distance is clamped to a small epsilon to avoid div-by-zero below.
    float3 toLight = ls.posW - shadingPosW;
    float distSqr = max(dot(toLight, toLight), kMinLightDistSqr);
    ls.distance = sqrt(distSqr);
    ls.dir = toLight / ls.distance;

    // Compute incident radiance at shading point.
    // The area lights are single-sided by default, so radiance is zero when seen from the back-facing side.
    float cosTheta = dot(ls.normalW, -ls.dir);
    if (cosTheta <= 0.f) return false;
    ls.Li = light.intensity * (light.surfaceArea * cosTheta / distSqr);

    // Compute the PDF with respect to solid angle. Note this may be +inf.
    ls.pdf = distSqr / (cosTheta * light.surfaceArea);
    return true;
}

/** Samples a rectangular area light source.
    \param[in] shadingPosW Shading point in world space.
    \param[in] light Light data.
    \param[in] u Uniform random numbers.
    \param[out] ls Light sample struct.
    \return True if a sample was generated, false otherwise.
*/
bool sampleRectAreaLight(const float3 shadingPosW, const LightData light, const float2 u, out AnalyticLightSample ls)
{
    // Pick a random sample on the quad.
    // The quad is from (-1,-1,0) to (1,1,0) in object space, but may be scaled by its transform matrix.
    float3 pos = float3(u.x * 2.f - 1.f, u.y * 2.f - 1.f, 0.f);

    // Apply model to world transformation matrix.
    ls.posW = mul(light.transMat, float4(pos, 1.f)).xyz;

    // Setup world space normal.
    // TODO: normalW is not correctly oriented for mesh instances that have flipped triangle winding.
    ls.normalW = normalize(mul(light.transMatIT, float4(0.f, 0.f, 1.f, 0.f)).xyz);

    return finalizeAreaLightSample(shadingPosW, light, ls);
}

/** Samples a spherical area light source.
    \param[in] shadingPosW Shading point in world space.
    \param[in] light Light data.
    \param[in] u Uniform random numbers.
    \param[out] ls Light sample struct.
    \return True if a sample was generated, false otherwise.
*/
bool sampleSphereAreaLight(const float3 shadingPosW, const LightData light, const float2 u, out AnalyticLightSample ls)
{
    // Sample a random point on the sphere.
    // TODO: We should pick a random point on the hemisphere facing the shading point.
    float3 pos = sample_sphere(u);

    // Apply model to world transformation matrix.
    ls.posW = mul(light.transMat, float4(pos, 1.f)).xyz;

    // Setup world space normal.
    ls.normalW = normalize(mul(light.transMatIT, float4(pos, 0.f)).xyz);

    return finalizeAreaLightSample(shadingPosW, light, ls);
}

/** Samples disc area light source.
    \param[in] shadingPosW Shading point in world space.
    \param[in] light Light data.
    \param[in] Uniform random numbers.
    \param[out] ls Light sample struct.
    \return True if a sample was generated, false otherwise.
*/
bool sampleDiscAreaLight(const float3 shadingPosW, const LightData light, const float2 u, out AnalyticLightSample ls)
{
    // Sample a random point on the disk.
    // TODO: Fix spelling disagreement between disc vs disk.
    float3 pos = float3(sample_disk(u), 0.f);

    // Transform to world space.
    ls.posW = mul(light.transMat, float4(pos, 1.f)).xyz;

    // Setup world space normal.
    ls.normalW = normalize(mul(light.transMatIT, float4(0.f, 0.f, 1.f, 0.f)).xyz);

    return finalizeAreaLightSample(shadingPosW, light, ls);
}

/** Samples a distant light source.
    \param[in] shadingPosW Shading point in world space.
    \param[in] light Light data.
    \param[in] u Uniform random numbers.
    \param[out] ls Light sample struct.
    \return True if a sample was generated, false otherwise.
*/
bool sampleDistantLight(const float3 shadingPosW, const LightData light, const float2 u, out AnalyticLightSample ls)
{
    // A distant light doesn't have a position. Just clear to zero.
    ls.posW = float3(0.f, 0.f, 0.f);

    // Sample direction.
    float3 dir = sample_cone(u, light.cosSubtendedAngle);

    // Transform sampled direction to world space
    ls.dir = normalize(mul((float3x3)light.transMat, dir));
    ls.normalW = -ls.dir;
    ls.distance = kMaxLightDistance;

    // Compute incident radiance at shading point.
    // A DistantLight's angle defines the solid angle it subtends.
    // But because the angle is intended to affect penumbra size, but not
    // perceived brigthness, we treat intensity as radiance.
    float solidAngle = (float)M_2PI * (1.f - light.cosSubtendedAngle);
    ls.Li = light.intensity;
    ls.pdf = 1.f / solidAngle;
    return true;
}

/** Samples a directional light source.
    \param[in] shadingPosW Shading point in world space.
    \param[in] light Light data.
    \param[out] ls Light sample struct.
    \return True if a sample was generated, false otherwise.
*/
bool sampleDirectionalLight(const float3 shadingPosW, const LightData light, out AnalyticLightSample ls)
{
    // A directional light doesn't have a position. Just clear to zero.
    ls.posW = float3(0, 0, 0);

    // For a directional light, the normal is always along its light direction.
    ls.normalW = light.dirW;

    // Setup direction and distance to light.
    ls.distance = kMaxLightDistance;
    ls.dir = -light.dirW;

    // Setup incident radiance. For directional lights there is no falloff or cosine term.
    ls.Li = light.intensity;

    // For a directional light, the PDF with respect to solid angle is a Dirac function. Set to zero.
    ls.pdf = 0.f;

    return true;
}

/** Samples a point (spot) light.
    \param[in] shadingPosW Shading point in world space.
    \param[in] light Light data.
    \param[out] ls Light sample struct.
    \return True if a sample was generated, false otherwise.
*/
bool samplePointLight(const float3 shadingPosW, const LightData light, out AnalyticLightSample ls)
{
    // Get the position and normal.
    ls.posW = light.posW;
    ls.normalW = light.dirW;

    // Compute direction and distance to light.
    // The distance is clamped to a small epsilon to avoid div-by-zero below.
    float3 toLight = ls.posW - shadingPosW;
    float distSqr = max(dot(toLight, toLight), kMinLightDistSqr);
    ls.distance = sqrt(distSqr);
    ls.dir = toLight / ls.distance;

    // Calculate the falloff for spot-lights.
    float cosTheta = -dot(ls.dir, light.dirW);
    float falloff = 1.f;
    if (cosTheta < light.cosOpeningAngle)
    {
        falloff = 0.f;
    }
    else if (light.penumbraAngle > 0.f)
    {
        float deltaAngle = light.openingAngle - acos(cosTheta);
        falloff = smoothstep(0.f, light.penumbraAngle, deltaAngle);
    }

    // Compute incident radiance at shading point.
    ls.Li = light.intensity * falloff / distSqr;

    // For a point light, the PDF with respect to solid angle is a Dirac function. Set to zero.
    ls.pdf = 0.f;

    return true;
}

/** Samples an LED light source.
    \param[in] shadingPosW Shading point in world space.
    \param[in] light Light data.
    \param[in] u Uniform random numbers.
    \param[out] ls Light sample struct.
    \return True if a sample was generated, false otherwise.
*/
bool sampleLEDLight(const float3 shadingPosW, const LightData light, const float2 u, out AnalyticLightSample ls)
{
    // Sample local position based on LED shape type
    float3 localPos = float3(0.0f);

    // Safety check for shape type to avoid invalid values
    uint32_t shapeType = clamp(light.shapeType, 0u, 2u);

    switch (shapeType)
    {
    case 0: // Sphere
        // Uniform sphere sampling
        localPos = sample_sphere(u);
        break;

    case 1: // Ellipsoid
        // Sample unit sphere then transform via scaling matrix
        localPos = sample_sphere(u);
        break;

    case 2: // Rectangle
        // Sample rectangle surface in [-1,1] range
        localPos = float3(u.x * 2.0f - 1.0f, u.y * 2.0f - 1.0f, 0.0f);
        break;

    default:
        // Fallback to sphere for unknown shape types
        localPos = sample_sphere(u);
        break;
    }

    // Transform to world space
    // light.posW is the center position, light.transMat only contains rotation and scale
    float3 transformedLocalPos = mul(light.transMat, float4(localPos, 1.0f)).xyz;
    ls.posW = light.posW + transformedLocalPos;

    // Calculate world space normal based on shape
    float3 localNormal = localPos;
    if (shapeType == 2) // Rectangle
    {
        localNormal = float3(0.0f, 0.0f, 1.0f);
    }
    ls.normalW = normalize(mul(light.transMatIT, float4(localNormal, 0.0f)).xyz);

    // Compute direction and distance to light
    float3 toLight = ls.posW - shadingPosW;
    float distSqr = max(dot(toLight, toLight), kMinLightDistSqr);
    ls.distance = sqrt(distSqr);
    ls.dir = toLight / ls.distance;

    // Check if point is within light cone
    float cosTheta = dot(-ls.dir, light.dirW);
    if (cosTheta < light.cosOpeningAngle) return false;

    // Calculate angular attenuation
    float angleFalloff = 1.0f;

    if (light.hasCustomLightField > 0 && light.lightFieldDataSize > 0)
    {
        // Custom light field distribution with proper interpolation
        float angle = acos(clamp(cosTheta, 0.0f, 1.0f)); // Convert cosine to angle
        // Get normalized distribution value (0-1 range representing relative intensity)
        float normalizedDistribution = interpolateLightField(light.lightFieldDataOffset, light.lightFieldDataSize, angle);

        // Apply additional safety checks for interpolated values
        normalizedDistribution = max(normalizedDistribution, 0.0f); // Ensure non-negative intensity

        // The normalized distribution is multiplied by the total power scaling factor
        // This ensures totalPower controls the absolute intensity
        angleFalloff = normalizedDistribution;
    }
    else
    {
        // Lambert model with safety checks
        float lambertN = max(light.lambertN, 0.1f); // Prevent negative/zero exponents
        float normalizationFactor = max(lambertN + 1.0f, 0.1f); // Prevent division by zero
        angleFalloff = pow(max(cosTheta, 0.0f), lambertN) / normalizationFactor;
    }

    // Surface area check to prevent division by zero
    float surfaceArea = max(light.surfaceArea, 1e-9f);

    // Calculate surface normal dot product for area lights
    float cosNormalTheta = dot(ls.normalW, -ls.dir);
    if (cosNormalTheta <= 0.0f) return false;

    // Compute incident radiance
    ls.Li = light.intensity * angleFalloff * (surfaceArea * cosNormalTheta / distSqr);

    // Compute PDF with safety check
    ls.pdf = distSqr / (cosNormalTheta * surfaceArea);

    return true;
}

/** Samples an analytic light source.
    This function calls the correct sampling function depending on the type of light.
    \param[in] shadingPosW Shading point in world space.
    \param[in] light Light data.
    \param[in,out] sg Sample generator.
    \param[out] ls Sampled point on the light and associated sample data, only valid if true is returned.
    \return True if a sample was generated, false otherwise.
*/
bool sampleLight<S : ISampleGenerator>(const float3 shadingPosW, const LightData light, inout S sg, out AnalyticLightSample ls)
{
    // Sample the light based on its type: point, directional, or area.
    switch (light.type)
    {
    case LightType::Point:
        return samplePointLight(shadingPosW, light, ls);
    case LightType::Directional:
        return sampleDirectionalLight(shadingPosW, light, ls);
    case LightType::Rect:
        return sampleRectAreaLight(shadingPosW, light, sampleNext2D(sg), ls);
    case LightType::Sphere:
        return sampleSphereAreaLight(shadingPosW, light, sampleNext2D(sg), ls);
    case LightType::Disc:
        return sampleDiscAreaLight(shadingPosW, light, sampleNext2D(sg), ls);
    case LightType::Distant:
        return sampleDistantLight(shadingPosW, light, sampleNext2D(sg), ls);
    case LightType::LED:
        return sampleLEDLight(shadingPosW, light, sampleNext2D(sg), ls);
    default:
        ls = {};
        return false; // Should not happen
    }
}

/** Evaluates a light approximately. This is useful for raster passes that don't use stochastic integration.
    For now only point and directional light sources are supported.
    \param[in] shadingPosW Shading point in world space.
    \param[in] light Light data.
    \param[out] ls Sampled point on the light and associated sample data (zero initialized if no sample is generated).
    \return True if a sample was generated, false otherwise.
*/
bool evalLightApproximate(const float3 shadingPosW, const LightData light, out AnalyticLightSample ls)
{
    ls = {};

    switch (light.type)
    {
    case LightType::Point:
        return samplePointLight(shadingPosW, light, ls);
    case LightType::Directional:
        return sampleDirectionalLight(shadingPosW, light, ls);
    case LightType::LED:
        // For approximate evaluation, use center sampling
        return sampleLEDLight(shadingPosW, light, float2(0.5f, 0.5f), ls);
    }

    // All other light types are not supported.
    return false;
}
