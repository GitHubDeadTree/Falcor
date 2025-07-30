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
import Rendering.Lights.EnvMapSampler;
import Rendering.Lights.EmissiveLightSampler;
import Rendering.Lights.EmissiveLightSamplerHelpers;
import Rendering.Lights.LightHelpers;
import Rendering.Materials.IsotropicGGX;
import Rendering.Materials.InteriorListHelpers;
import Rendering.Volumes.HomogeneousVolumeSampler;
import Rendering.Utils.PixelStats;
import RenderPasses.Shared.Denoising.NRDConstants;
import RenderPasses.Shared.Denoising.NRDBuffers;
import RenderPasses.Shared.Denoising.NRDData;
import RenderPasses.Shared.Denoising.NRDHelpers;
import Utils.Color.ColorHelpers;
import Utils.Color.SpectrumUtils;
import Utils.Geometry.GeometryHelpers;
import Utils.Debug.PixelDebug;
import LoadShadingData;
import ColorType;
import NRDHelpers;
__exported import PathState;
__exported import Params;
import CIRPathData;
import Rendering.RTXDI.RTXDI;

/** Interface for querying visibility in the scene.
    This is used in `handleHit`.
*/
interface IVisibilityQuery
{
    /** Trace a visibility ray against the scene.
        \param[in] ray Ray.
        \return Returns true if the ray endpoints are mutually visible (i.e. the ray does NOT intersect the scene).
    */
    [mutating] bool traceVisibilityRay(const Ray ray);
};

/** Path tracer.

    This implements the high-level path tracing logic and is shared among
    different scheduling implementations.
*/
struct PathTracer
{
    PathTracerParams params;                        ///< Runtime parameters.

    // Samplers
    EnvMapSampler envMapSampler;                    ///< Environment map sampler. Only valid when kUseEnvLight == true.
    EmissiveLightSampler emissiveSampler;           ///< Emissive light sampler. Only valid when kUseEmissiveLights == true.

    // Inputs
    Texture2D<PackedHitInfo> vbuffer;               ///< Fullscreen V-buffer for the primary hits.
    Texture2D<float3> viewDir;                      ///< Optional view direction. Only valid when kUseViewDir == true.
    Texture2D<uint> sampleCount;                    ///< Optional input sample count buffer. Only valid when kSamplesPerPixel == 0.
    Texture2D<uint> sampleOffset;                   ///< Output offset into per-sample buffers. Only valid when kSamplesPerPixel == 0.

    // Outputs
    RWStructuredBuffer<ColorType> sampleColor;      ///< Output per-sample color if kSamplesPerPixel != 1.
    RWStructuredBuffer<GuideData> sampleGuideData;  ///< Output per-sample guide data.
    RWStructuredBuffer<float4> sampleInitialRayInfo; ///< Output per-sample initial ray direction and intensity data.
    NRDBuffers outputNRD;                           ///< Output NRD data.

    // === CIR Data Output ===
    RWStructuredBuffer<CIRPathData> sampleCIRData;   ///< CIR path data output buffer

    RWTexture2D<float4> outputColor;                ///< Output color buffer if kSamplesPerPixel == 1.

    /*******************************************************************
                                Static members
    *******************************************************************/

    // Render settings that depend on the scene.
    // TODO: Move into scene defines.
    static const bool kUseEnvLight = USE_ENV_LIGHT;
    static const bool kUseEmissiveLights = USE_EMISSIVE_LIGHTS;
    static const bool kUseAnalyticLights = USE_ANALYTIC_LIGHTS;
    static const bool kUseCurves = USE_CURVES;
    static const bool kUseHairMaterial = USE_HAIR_MATERIAL;

    // Additional specialization.
    static const bool kOutputGuideData = OUTPUT_GUIDE_DATA;
    static const bool kOutputInitialRayInfo = OUTPUT_INITIAL_RAY_INFO;

    /** Types of samplable lights.
    */
    enum class LightType
    {
        EnvMap,
        Emissive,
        Analytic
    };

    /** Describes a light sample.
    */
    struct LightSample
    {
        float3  Li;         ///< Incident radiance at the shading point (unshadowed). This is already divided by the pdf.
        float   pdf;        ///< Pdf with respect to solid angle at the shading point.
        float3  origin;     ///< Ray origin for visibility evaluation (offseted to avoid self-intersection).
        float   distance;   ///< Ray distance for visibility evaluation (shortened to avoid self-intersection).
        float3  dir;        ///< Ray direction for visibility evaluation (normalized).
        uint    lightType;  ///< Light type this sample comes from (LightType casted to uint).

        Ray getVisibilityRay() { return Ray(origin, dir, 0.f, distance); }
    };

    /** Describes a path vertex.
    */
    struct PathVertex
    {
        uint index;         ///< Vertex index (0 = camera, 1 = primary hit, 2 = secondary hit, etc.).
        float3 pos;         ///< Vertex position.
        float3 faceNormal;  ///< Geometry normal at the vertex (zero if not on a surface).
        bool frontFacing;   ///< True if path vertex is on the front-facing side (if on a surface).

        /** Initializes a path vertex.
            \param[in] index Vertex index.
            \param[in] pos Vertex position.
            \param[in] faceNormal Geometry normal.
            \param[in] frontFacing Front-facing flag.
        */
        __init(uint index, float3 pos, float3 faceNormal = float3(0.f), bool frontFacing = true)
        {
            this.index = index;
            this.pos = pos;
            this.faceNormal = faceNormal;
            this.frontFacing = frontFacing;
        }

        /** Get position with offset applied in direction of the geometry normal to avoid self-intersection
            for visibility rays.
            \param[in] rayDir Direction of the visibility ray (does not need to be normalized).
            \return Returns the offseted position.
        */
        float3 getRayOrigin(float3 rayDir)
        {
            return computeRayOrigin(pos, dot(faceNormal, rayDir) >= 0 ? faceNormal : -faceNormal);
        }

        /** Returns the oriented face normal.
            \return Face normal flipped to the same side as the view vector.
        */
        float3 getOrientedFaceNormal()
        {
            return frontFacing ? faceNormal : -faceNormal;
        }
    };

    /** Set guiding data when background is hit.
    */
    static void setBackgroundGuideData(inout GuideData guideData, const float3 dir, const float3 Le)
    {
        if (kOutputGuideData)
        {
            guideData.setGuideNormal(-dir);
            // Compress dynamic range similar to UE4.
            const float3 compressedColor = pow(Le / (Le + 1.f), 0.454545f);
            guideData.setAlbedo(compressedColor);
            guideData.setSpecularAlbedo(float3(0.f));
            guideData.setIndirectAlbedo(float3(0.f));
            guideData.setReflectionPos(float3(0.f));
        }
    }

    /** Updates path wavelength based on emitted light properties.
        This is called when a path segment contributes emitted light.
    */
    static void updatePathWavelength(inout PathState path, const float3 Le)
    {
        // Skip if no emission or path already has a valid wavelength
        if (all(Le <= 0.0f) || path.getWavelength() != 0.0f)
            return;

        // Calculate dominant wavelength from emitted light color
        float wavelength = SpectrumUtils::RGBtoDominantWavelength(Le);

        // Only set wavelength if a valid value was determined
        // (RGBtoDominantWavelength returns 0 if color is too dark or too close to white)
        if (wavelength != 0.0f)
        {
            path.setWavelength(wavelength);
        }
    }

    /** Set guiding data when primary surface is hit.
    */
    static void setPrimarySurfaceGuideData(inout GuideData guideData, const ShadingData sd, const BSDFProperties bsdfProperties)
    {
        if (kOutputGuideData)
        {
            guideData.setGuideNormal(bsdfProperties.guideNormal);
            // Use sum of reflection/transmission albedo as they are denoised together.
            guideData.setAlbedo(bsdfProperties.diffuseReflectionAlbedo + bsdfProperties.diffuseTransmissionAlbedo);
            guideData.setReflectionPos(sd.posW);

            const float NdotV = saturate(dot(bsdfProperties.guideNormal, sd.V));
            const float ggxAlpha = bsdfProperties.roughness * bsdfProperties.roughness;

            // We use an approximate shading model with a single specular lobe.
            // TODO: Generalize this to arbitrary materials.
            if (sd.mtl.getMaterialType() == MaterialType::Hair)
            {
                // Use a simple heuristic for hair material.
                const float3 preintegratedSpecular = approxSpecularIntegralGGX(float3(0.04f), ggxAlpha, NdotV);
                guideData.setSpecularAlbedo(preintegratedSpecular);
            }
            else
            {
                // Use pre-integrated FG term for specular
                const float3 preintegratedSpecular = approxSpecularIntegralGGX(bsdfProperties.specularReflectance, ggxAlpha, NdotV);
                guideData.setSpecularAlbedo(preintegratedSpecular);
            }
        }
    }

    /** Set guiding data when indirect surface is hit.
    */
    static void setIndirectSurfaceGuideData(inout GuideData guideData, const ShadingData sd, const BSDFProperties bsdfProperties)
    {
        if (any(bsdfProperties.emission > 0.f))
        {
            float3 indirectBaseColor = pow(bsdfProperties.emission / (bsdfProperties.emission + 1.0f), 0.454545f);
            guideData.setIndirectAlbedo(indirectBaseColor);
        }
        else
        {
            // Use sum of reflection/transmission albedo as they are denoised together.
            guideData.setIndirectAlbedo(bsdfProperties.diffuseReflectionAlbedo + bsdfProperties.diffuseTransmissionAlbedo);
        }
        guideData.setReflectionPos(sd.posW);
    }

    /*******************************************************************
                              Member functions
    *******************************************************************/

    /** Check if the path has finished all surface bounces and needs to be terminated.
        Note: This is expected to be called after generateScatterRay(), which increments the bounce counters.
        \param[in] path Path state.
        \return Returns true if path has processed all bounces.
    */
    bool hasFinishedSurfaceBounces(const PathState path)
    {
        const uint diffuseBounces = path.getBounces(BounceType::Diffuse);
        const uint specularBounces = path.getBounces(BounceType::Specular);
        const uint transmissionBounces = path.getBounces(BounceType::Transmission);
        const uint surfaceBounces = diffuseBounces + specularBounces + transmissionBounces;
        return
            (surfaceBounces > kMaxSurfaceBounces) ||
            (diffuseBounces > kMaxDiffuseBounces) ||
            (specularBounces > kMaxSpecularBounces) ||
            (transmissionBounces > kMaxTransmissionBounces);
    }

    /** Compute the total length of a terminated path.
        \param[in] path Path state.
        \return Returns the total number of bounces a path took.
    */
    uint getTerminatedPathLength(const PathState path)
    {
        // Account for the fact that we may have counted one bounce too many (scatter ray at the last path vertex).
        uint diffuseBounces = min(kMaxDiffuseBounces, path.getBounces(BounceType::Diffuse));
        uint specularBounces = min(kMaxSpecularBounces, path.getBounces(BounceType::Specular));
        uint transmissionBounces = min(kMaxTransmissionBounces, path.getBounces(BounceType::Transmission));
        uint surfaceBounces = min(kMaxSurfaceBounces, diffuseBounces + specularBounces + transmissionBounces);
        return surfaceBounces;
    }

    /** Generate the path state for a primary hit in screen space.
        This is treated equivalent to subsequent path vertices to reduce code divergence.
        \param[in] pathID Path ID which encodes pixel and sample index.
        \param[out] path Path state for the primary hit.
    */
    void generatePath(const uint pathID, out PathState path)
    {
        path = {};
        path.setActive();
        path.id = pathID;
        path.thp = float3(1.f);

        const uint2 pixel = path.getPixel();

        // Create primary ray.
        Ray cameraRay = gScene.camera.computeRayPinhole(pixel, params.frameDim);
        if (kUseViewDir) cameraRay.dir = -viewDir[pixel];
        path.origin = cameraRay.origin;
        path.dir = cameraRay.dir;

        // Store initial direction for irradiance calculation
        path.initialDir = cameraRay.dir;

        // === Initialize vertex collection ===
        path.initPathVertexCollection(cameraRay.origin);

        // === Initialize CIR data ===
        path.initCIRData();

        // Create sample generator.
        const uint maxSpp = kSamplesPerPixel > 0 ? kSamplesPerPixel : kMaxSamplesPerPixel;
        path.sg = SampleGenerator(pixel, params.seed * maxSpp + path.getSampleIdx());

        // Load the primary hit info from the V-buffer.
        const HitInfo hit = unpackHitInfo(vbuffer[pixel]);

        // If invalid, the path is still active and treated as a miss.
        if (hit.isValid())
        {
            path.setHit(hit);
            path.setVertexIndex(1);

            // Note: Vertex addition for surface hits is handled by closesthit shaders
            // to ensure accurate hit positions are used, avoiding duplicate camera positions
        }
    }

    /** Set up path for logging and debugging.
        \param[in] path Path state.
    */
    void setupPathLogging(const PathState path)
    {
        printSetPixel(path.getPixel());
        logSetPixel(path.getPixel());
    }

    /** Compute a set of coherence hints for a given path state.
        These hints predict the code path taken in the next call to handleHit() with the given path state.
        They are used by reordering schedulers to extract coherence.
        \param[in] path Path state.
        \param[in] isDeltaSpecular Hit material is pure delta specular.
        \param[in] isEmissive Hit material is emissive.
        \return Returns a bit mask of coherence hints. MSB is highest priority hint. Currently 4 bits are used.
    */
    uint getCoherenceHints(const PathState path, const bool isDeltaSpecular, const bool isEmissive)
    {
        bool terminatedByRussianRoulette = {}; ///< handleHit() will be terminated early due to russian roulette.
        bool terminated = {};                  ///< handleHit() will be terminated early.
        bool samplesLight = {};                ///< handleHit() will sample a light.
        bool computesEmissive = {};            ///< handleHit() will compute emission.

        // Determines if handleHit() will return early due to russian roulette.
        if (kUseRussianRoulette)
        {
            // This is under the assumption that the first random number drawn from the sample generator is used for russian roulette.
            // Keep in sync with handleHit().
            PathState tmpPath = path;
            terminatedByRussianRoulette = terminatePathByRussianRoulette(tmpPath, sampleNext1D(tmpPath.sg));
        }

        // Determines if handleHit() will return early.
        terminated = hasFinishedSurfaceBounces(path);
        if (kDisableCaustics && path.getBounces(BounceType::Diffuse) > 0 && isDeltaSpecular)
            terminated = true;

        // Determines if handleHit() will sample a light.
        samplesLight = true;
        if (isDeltaSpecular)
            samplesLight = false;
        if (!kUseLightsInDielectricVolumes && path.isInsideDielectricVolume())
            samplesLight = false;

        // Determines if handleHit() will compute emission.
        computesEmissive = isEmissive && path.isLightSamplable();
        if (kUseRTXDI && path.getVertexIndex() == 2 && !isDeltaSpecular)
            computesEmissive = false;

        // MSB = highest priority hint.
        return
            select(computesEmissive, 0x1, 0x0) |
            select(samplesLight, 0x2, 0x0) |
            select(terminated, 0x4, 0x0) |
            select(terminatedByRussianRoulette, 0x8, 0x0);
    }

    /** Update the path throughouput.
        \param[in,out] path Path state.
        \param[in] weight Vertex throughput.
    */
    void updatePathThroughput(inout PathState path, const float3 weight)
    {
        path.thp *= weight;
    }

    /** Add radiance to the path contribution.
        \param[in,out] path Path state.
        \param[in] radiance Vertex radiance.
    */
    void addToPathContribution(inout PathState path, const float3 radiance)
    {
        // Record initial L value for the first non-zero contribution (used for power calculation)
        if (all(path.L == float3(0.0f)) && any(radiance > 0.0f))
        {
            path.setInitialL(radiance);
        }
        
        path.L += path.thp * radiance;
    }

    /** Update CIR data with material reflectance from BSDF properties.
        Uses the material's actual diffuse and specular reflectance albedo values
        instead of BSDF sampling weights, which contain additional factors.
        \param[in,out] path Path state.
        \param[in] bsdfProperties Material properties containing actual reflectance values.
        \param[in] bsdfSample BSDF sample to determine if this is a reflection event.
    */
    void updateCIRWithMaterialReflectance(inout PathState path, BSDFProperties bsdfProperties, BSDFSample bsdfSample)
    {
        // If already hit emissive surface, do not update reflectance
        if (path.hitEmissiveSurface) return;

        const uint vertexIndex = path.getVertexIndex();
        const bool isPrimaryHit = (vertexIndex == 1);

        // Only accumulate reflectance for actual surface interactions (not primary hit from camera)
        // and only for reflection events
        if (!isPrimaryHit && (bsdfSample.isLobe(LobeType::Reflection) ||
                              bsdfSample.isLobe(LobeType::DiffuseReflection) ||
                              bsdfSample.isLobe(LobeType::SpecularReflection)))
        {
            // Calculate separate diffuse and specular components
            float3 diffuseAlbedo = bsdfProperties.diffuseReflectionAlbedo;
            float3 specularAlbedo = bsdfProperties.specularReflectionAlbedo;

            float diffuseReflectance = dot(diffuseAlbedo, float3(0.333f, 0.333f, 0.333f));
            float specularReflectance = dot(specularAlbedo, float3(0.333f, 0.333f, 0.333f));

            // Use combined diffuse and specular components for total reflectance
            float3 totalAlbedo = diffuseAlbedo + specularAlbedo;
            // Use equal-weight averaging instead of luminance for more physically accurate VLC calculations
            float reflectance = dot(totalAlbedo, float3(0.333f, 0.333f, 0.333f));

            // DEBUG: Add signal values to identify different scenarios
            if (diffuseReflectance <= 0.001f && specularReflectance <= 0.001f)
            {
                // DEBUG: No reflectance at all - signal value 0.11111 (1/9)
                reflectance = 0.11111f;
            }
            else if (diffuseReflectance <= 0.001f && specularReflectance > 0.001f)
            {
                // DEBUG: Only specular, no diffuse - signal value 0.16666 (1/6)
                reflectance = 0.16666f;
            }
            else if (specularReflectance <= 0.001f && diffuseReflectance > 0.001f)
            {
                // DEBUG: Only diffuse, no specular - signal value 0.33333 (1/3)
                reflectance = 0.33333f;
            }
            else if (reflectance > 0.9f)
            {
                // DEBUG: Very high total reflectance - signal value 0.99999 (nearly 1)
                reflectance = 0.99999f;
            }
            else if (reflectance < 0.05f)
            {
                // DEBUG: Very low total reflectance - signal value 0.05555 (1/18)
                reflectance = 0.05555f;
            }
            // else: use normal combined reflectance value

            // Ensure reflectance is within valid range [0,1] for VLC calculations
            reflectance = clamp(reflectance, 0.0f, 1.0f);
            path.updateCIRReflectance(reflectance);
        }
    }

    /** Generates a new scatter ray or terminates the path.
        \param[in] sd Shading data.
        \param[in] mi Material instance at the shading point.
        \param[in,out] path The path state.
        \return True if a ray was generated, false otherwise.
    */
    bool generateScatterRay(const ShadingData sd, const IMaterialInstance mi, inout PathState path)
    {
        BSDFSample result;
        bool valid = mi.sample(sd, path.sg, result, kUseBSDFSampling);

        // Update CIR with real BSDF weight AFTER sampling but BEFORE processing the sample
        updateCIRWithMaterialReflectance(path, mi.getProperties(sd), result);

        if (valid) valid = generateScatterRay(result, sd, mi, path);

        // Ignore valid on purpose for now.
        if (kOutputNRDData)
        {
            const uint lobeTypes = mi.getLobeTypes(sd);
            const bool hasDeltaTransmissionLobe = (lobeTypes & (uint)LobeType::DeltaTransmission) != 0;
            const bool hasNonDeltaLobes = (lobeTypes & (uint)LobeType::NonDelta) != 0;

            if (path.getVertexIndex() == 1)
            {
                path.setDiffusePrimaryHit(result.isLobe(LobeType::Diffuse));
                path.setSpecularPrimaryHit(result.isLobe(LobeType::Specular));

                if (kOutputNRDAdditionalData)
                {
                    // Mark path as delta-only if it followed delta lobe on the primary hit, even though there might have been non-delta lobes.
                    path.setDeltaOnlyPath(result.isLobe(LobeType::DeltaReflection) || result.isLobe(LobeType::DeltaTransmission));

                    path.setDeltaReflectionPrimaryHit(result.isLobe(LobeType::DeltaReflection));
                    path.setDeltaTransmissionPath(result.isLobe(LobeType::DeltaTransmission));
                }
            }

            if (path.getVertexIndex() > 1)
            {
                if (hasNonDeltaLobes) path.setDeltaOnlyPath(false);

                if (kOutputNRDAdditionalData && path.isDeltaTransmissionPath() && path.isDeltaOnlyPath() && hasDeltaTransmissionLobe)
                {
                    if (result.isLobe(LobeType::DeltaReflection) && !isDeltaReflectionAllowedAlongDeltaTransmissionPath(sd))
                    {
                        path.setDeltaTransmissionPath(false);
                    }
                }
            }
        }

        return valid;
    }

    /** Generates a new scatter ray given a valid BSDF sample.
        \param[in] bs BSDF sample (assumed to be valid).
        \param[in] sd Shading data.
        \param[in] mi Material instance at the shading point.
        \param[in,out] path The path state.
        \return True if a ray was generated, false otherwise.
    */
    bool generateScatterRay(const BSDFSample bs, const ShadingData sd, const IMaterialInstance mi, inout PathState path)
    {
        const bool isTriangleHit = path.hit.getType() == HitType::Triangle;
        const bool isCurveHit = kUseCurves && (path.hit.getType() == HitType::Curve);
        const bool isHairMaterial = kUseHairMaterial && (sd.mtl.getMaterialType() == MaterialType::Hair);
        // TODO: Decouple geometry from the material.
        const bool isCurvePolyTubeHit = isTriangleHit && isHairMaterial;

        path.dir = bs.wo;
        updatePathThroughput(path, bs.weight);
        path.pdf = bs.pdf;

        path.clearEventFlags();

        // If already hit emissive surface, do not update bounce counters
        if (path.hitEmissiveSurface)
        {
            // Only update essential path information, skip bounce counting
            path.normal = sd.getOrientedFaceNormal();
            bool valid = any(path.thp > 0.f);
            return valid;
        }

        // Handle reflection events.
        if (bs.isLobe(LobeType::Reflection))
        {
            // We classify specular events as diffuse if the roughness is above some threshold.
            float roughness = mi.getProperties(sd).roughness;
            bool isDiffuse = bs.isLobe(LobeType::DiffuseReflection) || roughness > params.specularRoughnessThreshold;

            if (isDiffuse)
            {
                path.incrementBounces(BounceType::Diffuse);
            }
            else
            {
                path.incrementBounces(BounceType::Specular);
                path.setSpecular();
            }
        }

        // Handle delta events.
        if (bs.isLobe(LobeType::Delta))
        {
            path.setDelta();
        }

        // Handle transmission events.
        if (bs.isLobe(LobeType::Transmission))
        {
            path.incrementBounces(BounceType::Transmission);
            path.setTransmission();

            if (isCurveHit)
            {
                // No need to offset the origin in this case.
            }
            else if (isCurvePolyTubeHit)
            {
                // For curves tessellated into poly-tubes, we make sure that the origin is on the same side as a scatter ray direction
                // so there is no self-intersection.
                path.origin = sd.posW - sd.frame.N * sd.curveRadius * 2.1f;
            }
            else
            {
                // Compute ray origin for next ray segment.
                path.origin = sd.computeRayOrigin(false);

                // Update interior list and inside volume flag.
                if (!sd.mtl.isThinSurface())
                {
                    uint nestedPriority = sd.mtl.getNestedPriority();
                    path.interiorList.handleIntersection(sd.materialID, nestedPriority, sd.frontFacing);
                    path.setInsideDielectricVolume(!path.interiorList.isEmpty());
                }
            }
        }

        // Save the normal used for NEE. This is needed for MIS.
        path.normal = sd.getOrientedFaceNormal();

        // Mark the path as valid only if it has a non-zero throughput.
        bool valid = any(path.thp > 0.f);

        return valid;
    }

    /** Evaluates the currently configured heuristic for multiple importance sampling (MIS).
        \param[in] n0 Number of samples taken from the first sampling strategy.
        \param[in] p0 Pdf for the first sampling strategy.
        \param[in] n1 Number of samples taken from the second sampling strategy.
        \param[in] p1 Pdf for the second sampling strategy.
        \return Weight for the contribution from the first strategy (p0).
    */
    float evalMIS(float n0, float p0, float n1, float p1)
    {
        switch (MISHeuristic(kMISHeuristic))
        {
        case MISHeuristic::Balance:
        {
            // Balance heuristic
            float q0 = n0 * p0;
            float q1 = n1 * p1;
            return q0 / (q0 + q1);
        }
        case MISHeuristic::PowerTwo:
        {
            // Power two heuristic
            float q0 = (n0 * p0) * (n0 * p0);
            float q1 = (n1 * p1) * (n1 * p1);
            return q0 / (q0 + q1);
        }
        case MISHeuristic::PowerExp:
        {
            // Power exp heuristic
            float q0 = pow(n0 * p0, kMISPowerExponent);
            float q1 = pow(n1 * p1, kMISPowerExponent);
            return q0 / (q0 + q1);
        }
        default:
            return 0.f;
        }
    }

    /** Generates a light sample on the environment map.
        \param[in] vertex Path vertex.
        \param[in,out] sg Sample generator.
        \param[out] ls Struct describing valid samples.
        \return True if the sample is valid and has nonzero contribution, false otherwise.
    */
    bool generateEnvMapSample(const PathVertex vertex, inout SampleGenerator sg, out LightSample ls)
    {
        ls = {}; // Default initialization to avoid divergence at returns.

        if (!kUseEnvLight) return false;

        // Sample environment map.
        EnvMapSample lightSample;
        if (!envMapSampler.sample(sampleNext2D(sg), lightSample)) return false;

        // Setup returned sample.
        ls.Li = lightSample.pdf > 0.f ? lightSample.Le / lightSample.pdf : float3(0);
        ls.pdf = lightSample.pdf;
        ls.origin = vertex.getRayOrigin(lightSample.dir);
        ls.distance = kRayTMax;
        ls.dir = lightSample.dir;

        return any(ls.Li > 0.f);
    }

    /** Generates a light sample on the emissive geometry.
        \param[in] vertex Path vertex.
        \param[in] upperHemisphere True if only upper hemisphere should be considered.
        \param[in,out] sg Sample generator.
        \param[out] ls Struct describing valid samples.
        \return True if the sample is valid and has nonzero contribution, false otherwise.
    */
    bool generateEmissiveSample(const PathVertex vertex, const bool upperHemisphere, inout SampleGenerator sg, out LightSample ls)
    {
        ls = {}; // Default initialization to avoid divergence at returns.
        if (!kUseEmissiveLights) return false;

        TriangleLightSample tls;
        if (!emissiveSampler.sampleLight(vertex.pos, vertex.getOrientedFaceNormal(), upperHemisphere, sg, tls)) return false;

        // Setup returned sample.
        ls.Li = tls.pdf > 0.f ? tls.Le / tls.pdf : float3(0);
        ls.pdf = tls.pdf;
        // Offset shading and light position to avoid self-intersection.
        float3 lightPos = computeRayOrigin(tls.posW, tls.normalW);
        ls.origin = vertex.getRayOrigin(lightPos - vertex.pos);
        float3 toLight = lightPos - ls.origin;
        ls.distance = length(toLight);
        ls.dir = normalize(toLight);

        return any(ls.Li > 0.f);
    }

    /** Generates a light sample on the analytic lights.
        \param[in] vertex Path vertex.
        \param[in,out] sg Sample generator.
        \param[out] ls Struct describing valid samples.
        \return True if the sample is valid and has nonzero contribution, false otherwise.
    */
    bool generateAnalyticLightSample(const PathVertex vertex, inout SampleGenerator sg, out LightSample ls)
    {
        ls = {}; // Default initialization to avoid divergence at returns.

        uint lightCount = gScene.getLightCount();
        if (!kUseAnalyticLights || lightCount == 0) return false;

        // Sample analytic light source selected uniformly from the light list.
        // TODO: Sample based on estimated contributions as pdf.
        uint lightIndex = min(uint(sampleNext1D(sg) * lightCount), lightCount - 1);

        // Sample local light source.
        AnalyticLightSample lightSample;
        if (!sampleLight(vertex.pos, gScene.getLight(lightIndex), sg, lightSample)) return false;

        // Setup returned sample.
        ls.pdf = lightSample.pdf / lightCount;
        ls.Li = lightSample.Li * lightCount;
        // Offset shading position to avoid self-intersection.
        ls.origin = vertex.getRayOrigin(lightSample.dir);
        // Analytic lights do not currently have a geometric representation in the scene.
        // Do not worry about adjusting the ray length to avoid self-intersections at the light.
        ls.distance = lightSample.distance;
        ls.dir = lightSample.dir;

        return any(ls.Li > 0.f);
    }

    /** Return the probabilities for selecting different light types.
        \param[out] p Probabilities.
    */
    void getLightTypeSelectionProbabilities(out float p[3])
    {
        // Set relative probabilities of the different sampling techniques.
        // TODO: These should use estimated irradiance from each light type. Using equal probabilities for now.
        p[0] = kUseEnvLight ? 1.f : 0.f;
        p[1] = kUseEmissiveLights ? 1.f : 0.f;
        p[2] = kUseAnalyticLights ? 1.f : 0.f;

        // Normalize probabilities. Early out if zero.
        float sum = p[0] + p[1] + p[2];
        if (sum == 0.f) return;

        float invSum = 1.f / sum;
        p[0] *= invSum;
        p[1] *= invSum;
        p[2] *= invSum;
    }

    float getEnvMapSelectionProbability()   { float p[3]; getLightTypeSelectionProbabilities(p); return p[0]; }
    float getEmissiveSelectionProbability() { float p[3]; getLightTypeSelectionProbabilities(p); return p[1]; }
    float getAnalyicSelectionProbability()  { float p[3]; getLightTypeSelectionProbabilities(p); return p[2]; }

    /** Select a light type for sampling.
        \param[out] lightType Selected light type.
        \param[out] pdf Probability for selected type.
        \param[in,out] sg Sample generator.
        \return Return true if selection is valid.
    */
    bool selectLightType(out uint lightType, out float pdf, inout SampleGenerator sg)
    {
        float p[3];
        getLightTypeSelectionProbabilities(p);

        float u = sampleNext1D(sg);

        [unroll]
        for (lightType = 0; lightType < 3; ++lightType)
        {
            if (u < p[lightType])
            {
                pdf = p[lightType];
                return true;
            }
            u -= p[lightType];
        }

        lightType = {};
        pdf = {};

        return false;
    }

    /** Samples a light source in the scene.
        This function first stochastically selects a type of light source to sample,
        and then calls that the sampling function for the chosen light type.
        \param[in] vertex Path vertex.
        \param[in] sampleUpperHemisphere True if the upper hemisphere should be sampled.
        \param[in] sampleLowerHemisphere True if the lower hemisphere should be sampled.
        \param[in,out] sg Sample generator.
        \param[out] ls Struct describing valid samples.
        \return True if the sample is valid and has nonzero contribution, false otherwise.
    */
    bool generateLightSample(const PathVertex vertex, const bool sampleUpperHemisphere, const bool sampleLowerHemisphere, inout SampleGenerator sg, out LightSample ls)
    {
        ls = {};

        uint lightType;
        float selectionPdf;
        if (!selectLightType(lightType, selectionPdf, sg)) return false;

        bool valid = false;
        if (kUseEnvLight && lightType == (uint)LightType::EnvMap) valid = generateEnvMapSample(vertex, sg, ls);
        if (kUseEmissiveLights && lightType == (uint)LightType::Emissive)
        {
            // Emissive light samplers have an option to exclusively sample the upper hemisphere.
            bool upperHemisphere = sampleUpperHemisphere && !sampleLowerHemisphere;
            valid = generateEmissiveSample(vertex, upperHemisphere, sg, ls);
        }
        if (kUseAnalyticLights && lightType == (uint)LightType::Analytic)
        {
            valid = generateAnalyticLightSample(vertex, sg, ls);
        }
        if (!valid) return false;

        // Reject samples in non-requested hemispheres.
        float NdotL = dot(vertex.getOrientedFaceNormal(), ls.dir);
        if ((!sampleUpperHemisphere && NdotL >= -kMinCosTheta) || (!sampleLowerHemisphere && NdotL <= kMinCosTheta))
            return false;

        // Account for light type selection.
        ls.lightType = lightType;
        ls.pdf *= selectionPdf;
        ls.Li /= selectionPdf;

        return true;
    }

    /** Handle hits on dielectrics.
        \return True if this is an valid intersection, false if it is rejected.
    */
    bool handleNestedDielectrics(inout ShadingData sd, inout PathState path)
    {
        // Check for false intersections.
        uint nestedPriority = sd.mtl.getNestedPriority();
        if (!path.interiorList.isTrueIntersection(nestedPriority))
        {
            // If it is a false intersection, we reject the hit and continue the path
            // on the other side of the interface.
            // If the offset position is not quite large enough, due to self-intersections
            // it is possible we repeatedly hit the same surface and try to reject it.
            // This has happened in a few occasions with large transmissive triangles.
            // As a workaround, count number of rejected hits and terminate the path if too many.
            if (path.rejectedHits < kMaxRejectedHits)
            {
                path.rejectedHits++;
                path.interiorList.handleIntersection(sd.materialID, nestedPriority, sd.frontFacing);
                path.origin = sd.computeRayOrigin(false);
                path.decrementVertexIndex();
            }
            else
            {
                path.terminate();
            }
            return false;
        }

        // Compute index of refraction for medium on the outside.
        sd.IoR = computeOutsideIoR(path.interiorList, sd.materialID, sd.frontFacing);

        return true;
    }

    /** Apply russian roulette to terminate paths early.
        \param[in,out] path Path.
        \param[in] u Uniform random number in [0,1).
        \return Returns true if path was terminated.
    */
    bool terminatePathByRussianRoulette(inout PathState path, float u)
    {
        const float rrVal = luminance(path.thp);
        const float prob = max(0.f, 1.f - rrVal);
        if (u < prob)
        {
            path.terminate();
            return true;
        }
        path.thp /= 1.f - prob;
        return false;
    }

    /** Helper to create a texture sampler instance.
        The method for computing texture level-of-detail depends on the configuration.
        \param[in] path Path state.
        \param[in] isPrimaryTriangleHit True if primary hit on a triangle.
        \return Texture sampler instance.
    */
    ITextureSampler createTextureSampler(const PathState path, bool isPrimaryHit)
    {
        const bool isTriangleHit = (path.hit.getType() == HitType::Triangle);
        if (kPrimaryLodMode == TexLODMode::RayDiffs && isPrimaryHit && isTriangleHit)
        {
            // Filtered lookups at primary hit on triangle.
            float2 ddx, ddy;
            computeDerivativesAtPrimaryTriangleHit(path.hit.getTriangleHit(), path.getPixel(), params.frameDim, ddx, ddy);
            return ExplicitGradientTextureSampler(ddx, ddy);
        }
        else
        {
            float lod = isPrimaryHit ? 0.f : params.lodBias;
            return ExplicitLodTextureSampler(lod);
        }
    }

    /**
     * Handle a hit with simpler interface, used by TracePass.rt.slang.
     * This version handles the creation of required data structures internally.
     */
    void handleHit<V : IVisibilityQuery>(inout PathState path, V visibilityQuery)
    {
        const bool isPrimaryHit = path.getVertexIndex() == 1;
        const HitType hitType = path.hit.getType();
        const bool isTriangleHit = hitType == HitType::Triangle;
        const bool isCurveHit = kUseCurves && (hitType == HitType::Curve);

        // Load shading data. This is a long latency operation.
        ShadingData sd = loadShadingData(path.hit, path.origin, path.dir);

        const bool isHairMaterial = kUseHairMaterial && (sd.mtl.getMaterialType() == MaterialType::Hair);
        // TODO: Decouple geometry from the material.
        const bool isCurvePolyTubeHit = isTriangleHit && isHairMaterial;

        // Account for volume absorption.
        if (!path.interiorList.isEmpty())
        {
            const uint materialID = path.interiorList.getTopMaterialID();
            let material = gScene.materials.getMaterial(materialID);
            let vp = material.getHomogeneousVolumeProperties(gScene.materials, materialID);
            updatePathThroughput(path, HomogeneousVolumeSampler::evalTransmittance(vp, length(sd.posW - path.origin)));
        }

        // Reject false hits in nested dielectrics.
        if (!handleNestedDielectrics(sd, path)) return;

        logPathVertex();

        // Create texture sampler.
        let lod = createTextureSampler(path, isPrimaryHit);

        // Create BSDF instance and query its properties.
        let hints = getMaterialInstanceHints(path.hit, isPrimaryHit);
        const IMaterialInstance mi = gScene.materials.getMaterialInstance(sd, lod, hints);
        BSDFProperties bsdfProperties = mi.getProperties(sd);

        // CRITICAL FIX: Accumulate primary hit distance before CIR processing
        // Primary hits bypass TracePass.rt.slang's handleHit function and go directly here
        // This is essential for ReflectionCount=0 paths to have non-zero path length
        if (isPrimaryHit)
        {
            float primaryHitDist = length(sd.posW - path.origin);
            path.sceneLength += primaryHitDist;

            // FIX: Add primary ray hit vertex collection
            // Primary hits bypass ClosestHit shader vertex collection, so we need to collect here
            path.addPathVertex(sd.posW);
        }

        // === Update CIR data during path tracing ===
        updateCIRDataDuringTracing(path, sd.faceN, bsdfProperties);

        // Disable specular lobes if caustics are disabled and path already contains a diffuse vertex.
        bool isSpecular = bsdfProperties.roughness <= params.specularRoughnessThreshold;
        if (kDisableCaustics && path.getBounces(BounceType::Diffuse) > 0 && isSpecular)
        {
            sd.mtl.setActiveLobes((uint)LobeType::Diffuse);
        }

        // Optionally disable emission inside volumes.
        if (!kUseLightsInDielectricVolumes && path.isInsideDielectricVolume())
        {
            bsdfProperties.emission = float3(0.f);
        }

        // Check if the scatter event is samplable by the light sampling technique.
        const bool isLightSamplable = path.isLightSamplable();

        // Add emitted radiance.
        // The primary hit is always included, secondary hits only if emissive lights are enabled and the full light contribution hasn't been sampled elsewhere.
        bool computeEmissive = isPrimaryHit || kUseEmissiveLights && (!kUseNEE || kUseMIS || !path.isLightSampled() || !isLightSamplable);

        // With RTXDI enabled, we sample the full direct illumination contribution on the primary hit.
        // Skip any additional contribution on the secondary hit unless it comes from a scatter event
        // that RTXDI cannot handle, such as transmission or delta scattering events.
        if (kUseRTXDI && path.getVertexIndex() == 2 && !path.isTransmission() && !path.isDelta()) computeEmissive = false;

        float3 attenuatedEmission = 0.f;

        if (computeEmissive && any(bsdfProperties.emission > 0.f))
        {
            float misWeight = 1.f;
            if (kUseEmissiveLights && kUseNEE && kUseMIS && isTriangleHit && !isPrimaryHit && path.isLightSampled() && isLightSamplable)
            {
                // If NEE and MIS are enabled, and we've already sampled emissive lights,
                // then we need to evaluate the MIS weight here to account for the remaining contribution.
                // Note that MIS is only applied for hits on emissive triangles (other emissive geometry is not supported).

                // Prepare hit point struct with data needed for emissive light PDF evaluation.
                TriangleLightHit hit;
                TriangleHit triangleHit = path.hit.getTriangleHit();
                hit.triangleIndex = gScene.lightCollection.getTriangleIndex(triangleHit.instanceID, triangleHit.primitiveIndex);
                hit.posW = sd.posW;
                hit.normalW = sd.getOrientedFaceNormal();

                // Evaluate PDF at the hit, had it been generated with light sampling.
                // Emissive light samplers have an option to exclusively sample the upper hemisphere.
                bool upperHemisphere = path.isLightSampledUpper() && !path.isLightSampledLower();
                float lightPdf = getEmissiveSelectionProbability() * emissiveSampler.evalPdf(path.origin, path.normal, upperHemisphere, hit);

                // Compute MIS weight by combining this with BSDF sampling.
                // Note we can assume path.pdf > 0.f since we shouldn't have got here otherwise.
                misWeight = evalMIS(1, path.pdf, 1, lightPdf);
            }

            // Accumulate emitted radiance weighted by path throughput and MIS weight.
            float3 emission = misWeight * bsdfProperties.emission;
            addToPathContribution(path, emission);

            attenuatedEmission = path.thp * emission;

            // Update path wavelength based on LED light emission
            // Check if this is an LED light source
            uint lightCount = gScene.getLightCount();
            if (kUseAnalyticLights && lightCount > 0)
            {
                for (uint lightIndex = 0; lightIndex < lightCount; ++lightIndex)
                {
                    LightData light = gScene.getLight(lightIndex);
                    if (light.type == uint(::LightType::LED))
                    {
                        // Update path wavelength from LED emission
                        updatePathWavelengthFromLED(path, light, attenuatedEmission);
                        break; // Assuming one LED per surface
                    }
                }
            }

            // Update path wavelength based on emission color
            updatePathWavelength(path, attenuatedEmission);
        }

        // MODIFIED: Check for early termination after emissive calculation
        // Only terminate if we hit an emissive surface AND have valid CIR data
        // This ensures we collect meaningful path length, power, and reflection data
        if (path.hitEmissiveSurface)
        {
#ifdef _PIXEL_STATS_ENABLED
            // Check if we have sufficient data before terminating
            bool hasValidPathLength = path.sceneLength > 0.0f;
            bool hasValidPower = luminance(path.L) > 0.0f;
            bool hasValidReflectionData = path.getBounces(BounceType::Diffuse) > 0 || path.getBounces(BounceType::Specular) > 0 || isPrimaryHit;

            // Only terminate if we have meaningful data to record
            if (hasValidPathLength && hasValidPower && hasValidReflectionData)
            {
                path.terminate();
                return;
            }
            // If we don't have valid data, continue tracing to collect more information
#endif
        }

        // Terminate after scatter ray on last vertex has been processed.
        if (hasFinishedSurfaceBounces(path))
        {
            path.terminate();
            return;
        }

        PathVertex pathVertex = PathVertex(path.getVertexIndex(), sd.posW, sd.faceN, sd.frontFacing);

        // Compute origin for rays traced from this path vertex.
        if (isCurveHit)
        {
            // For curves, we set the new origin at the sphere center.
            path.origin = sd.posW - sd.frame.N * sd.curveRadius;
        }
        else if (isCurvePolyTubeHit)
        {
            // For curves tessellated into poly-tubes, we offset the new origin away from the curve center.
            path.origin = sd.posW + sd.frame.N * sd.curveRadius * 0.1f;
        }
        else
        {
            path.origin = sd.computeRayOrigin();
        }

        // Determine if BSDF has non-delta lobes.
        const uint lobeTypes = mi.getLobeTypes(sd);
        const bool hasNonDeltaLobes = (lobeTypes & (uint)LobeType::NonDelta) != 0;

        // Check if we should apply NEE.
        const bool applyNEE = kUseNEE && hasNonDeltaLobes;

        // Check if sample from RTXDI should be applied instead of NEE.
        const bool applyRTXDI = kUseRTXDI && isPrimaryHit && hasNonDeltaLobes;

        // TODO: Support multiple shadow rays.
        path.setLightSampled(false, false);
        if (applyNEE || applyRTXDI)
        {
            LightSample ls = {};
            bool validSample = false;

            if (applyRTXDI)
            {
                // Query final sample from RTXDI.
                validSample = gRTXDI.getFinalSample(path.getPixel(), ls.dir, ls.distance, ls.Li);
                ls.origin = path.origin;
            }
            else
            {
                const bool canNeeSampleSphere = isCurveHit || isCurvePolyTubeHit;
                // Determine if upper/lower hemispheres need to be sampled.
                bool sampleUpperHemisphere = canNeeSampleSphere || ((lobeTypes & (uint)LobeType::NonDeltaReflection) != 0);
                bool sampleLowerHemisphere = canNeeSampleSphere || ((lobeTypes & (uint)LobeType::NonDeltaTransmission) != 0);
                if (!kUseLightsInDielectricVolumes && path.isInsideDielectricVolume()) sampleUpperHemisphere = false;

                // Sample a light.
                validSample = generateLightSample(pathVertex, sampleUpperHemisphere, sampleLowerHemisphere, path.sg, ls);
                path.setLightSampled(sampleUpperHemisphere, sampleLowerHemisphere);

                // Record initial L value from light sample for power calculation
                if (validSample && all(path.cirInitialL == float3(0.0f)) && any(ls.Li > 0.0f))
                {
                    path.setInitialL(ls.Li);
                }

                // Additional LED spectral sampling if needed
                if (validSample && ls.lightType == (uint)LightType::Analytic)
                {
                    // Try to handle LED spectral sampling
                    handleLEDLightSampling(path, path.sg);
                }
            }

            if (validSample)
            {
                // Apply MIS weight.
                if (kUseMIS && !applyRTXDI && ls.lightType != (uint)LightType::Analytic)
                {
                    float scatterPdf = mi.evalPdf(sd, ls.dir, kUseBSDFSampling);
                    ls.Li *= evalMIS(1, ls.pdf, 1, scatterPdf);
                }

                float3 weight = mi.eval(sd, ls.dir, path.sg);
                float3 Lr = weight * ls.Li;
                if (any(Lr > 0.f))
                {
                    Ray ray = ls.getVisibilityRay();

                    if (isCurvePolyTubeHit)
                    {
                        // For curves tessellated into poly-tubes, we make sure that the origin is on the same side as light
                        // so there is no self-shadowing (transmission lobe of hair BSDF takes care of that).
                        if (dot(sd.frame.N, ray.dir) < 0.f)
                        {
                            ray.origin = ray.origin - sd.frame.N * sd.curveRadius * 2.1f;
                        }
                    }

                    logTraceRay(PixelStatsRayType::Visibility);
                    bool visible = visibilityQuery.traceVisibilityRay(ray);
                    if (visible) addToPathContribution(path, Lr);
                }
            }
        }

        // Russian roulette to terminate paths early.
        if (kUseRussianRoulette)
        {
            if (terminatePathByRussianRoulette(path, sampleNext1D(path.sg))) return;
        }

        const bool wasDeltaOnlyPathBeforeScattering = path.isDeltaOnlyPath();

        // Generate the next path segment or terminate.
        bool valid = generateScatterRay(sd, mi, path);

        // Output guide data.
        if (path.getVertexIndex() == 1)
        {
            setPrimarySurfaceGuideData(path.guideData, sd, bsdfProperties);
        }
        if (path.getVertexIndex() == 2 && (path.getBounces(BounceType::Specular) == 1 || path.getBounces(BounceType::Transmission) == 1))
        {
            setIndirectSurfaceGuideData(path.guideData, sd, bsdfProperties);
        }

        if (kOutputNRDData)
        {
            const uint2 pixel = path.getPixel();
            const uint outSampleIdx = params.getSampleOffset(pixel, sampleOffset) + path.getSampleIdx();

            setNRDPrimaryHitEmission(outputNRD, kUseNRDDemodulation, path, pixel, isPrimaryHit, attenuatedEmission);
            setNRDPrimaryHitReflectance(outputNRD, kUseNRDDemodulation, path, pixel, isPrimaryHit, sd, bsdfProperties);

            setNRDSampleHitDist(outputNRD, path, outSampleIdx);
            setNRDSampleEmission(outputNRD, kUseNRDDemodulation, path, outSampleIdx, isPrimaryHit, attenuatedEmission, wasDeltaOnlyPathBeforeScattering);
            setNRDSampleReflectance(outputNRD, kUseNRDDemodulation, path, outSampleIdx, isPrimaryHit, sd, bsdfProperties, lobeTypes, wasDeltaOnlyPathBeforeScattering);
            setNRDPrimaryHitSeparatedRadiance(outputNRD, kUseNRDDemodulation, path, isPrimaryHit, outSampleIdx);
        }

        // Check if this is the last path vertex.
        const bool isLastVertex = hasFinishedSurfaceBounces(path);

        // Terminate if this is the last path vertex and light sampling already completely sampled incident radiance.
        if (kUseNEE && !kUseMIS && isLastVertex && path.isLightSamplable()) valid = false;

        // Terminate caustics paths.
        if (kDisableCaustics && path.getBounces(BounceType::Diffuse) > 0 && path.isSpecular()) valid = false;

        if (!valid)
        {
            path.terminate();
        }
    }

    /** Handle the case when a scatter ray misses the scene.
        \param[in,out] path The path state.
    */
    void handleMiss(inout PathState path)
    {
        // Upon miss:
        // - Compute MIS weight if previous path vertex sampled a light
        // - Evaluate environment map
        // - Write guiding data
        // - Terminate the path

        // Check if the scatter event is samplable by the light sampling technique.
        const bool isLightSamplable = path.isLightSamplable();

        // Add env radiance.
        bool computeEnv = kUseEnvLight && (!kUseNEE || kUseMIS || !path.isLightSampled() || !isLightSamplable);

        // With RTXDI enabled, we sample the full direct illumination contribution on the primary hit.
        // Skip any additional contribution on the secondary hit unless it comes from a scatter event
        // that RTXDI cannot handle, such as transmission, delta or volume scattering events.
        if (kUseRTXDI && path.getVertexIndex() == 2 && !path.isTransmission() && !path.isDelta()) computeEnv = false;

        float3 emitterRadiance = 0.f;

        if (computeEnv)
        {
            logPathVertex();

            float misWeight = 1.f;
            if (kUseNEE && kUseMIS && path.isLightSampled() && isLightSamplable)
            {
                // If NEE and MIS are enabled, and we've already sampled the env map,
                // then we need to evaluate the MIS weight here to account for the remaining contribution.

                // Evaluate PDF, had it been generated with light sampling.
                float lightPdf = getEnvMapSelectionProbability() * envMapSampler.evalPdf(path.dir);

                // Compute MIS weight by combining this with BSDF sampling.
                // Note we can assume path.pdf > 0.f since we shouldn't have got here otherwise.
                misWeight = evalMIS(1, path.pdf, 1, lightPdf);
            }

            float3 Le = envMapSampler.eval(path.dir);
            emitterRadiance = misWeight * Le;
            float3 contribution = path.thp * emitterRadiance;
            addToPathContribution(path, emitterRadiance);

            // Update path wavelength based on environment light
            if (path.getWavelength() == 0.0f && any(contribution > 0.0f))
            {
                float wavelength = SpectrumUtils::RGBtoDominantWavelength(contribution);
                if (wavelength != 0.0f)
                {
                    path.setWavelength(wavelength);
                }
            }

            if (kOutputGuideData && path.getVertexIndex() == 2
                && (path.getBounces(BounceType::Specular) == 1
                || path.getBounces(BounceType::Transmission) == 1))
            {
                // Compress dynamic range similar to UE4.
                float3 compressedColor = pow(Le / (Le + 1.0f), 0.454545f);
                path.guideData.setIndirectAlbedo(compressedColor);
                path.guideData.setReflectionPos(path.dir * kEnvMapDepth);
            }
        }

        if (kOutputGuideData && path.getVertexIndex() == 1)
        {
            path.guideData.setGuideNormal(-path.dir);
        }

        if (kOutputNRDData)
        {
            const uint outSampleIdx = params.getSampleOffset(path.getPixel(), sampleOffset) + path.getSampleIdx();
            setNRDSampleHitDist(outputNRD, path, outSampleIdx);
        }

#if defined(DELTA_REFLECTION_PASS)
        if (path.isDeltaReflectionPrimaryHit())
        {
            writeNRDDeltaReflectionGuideBuffers(outputNRD, kUseNRDDemodulation, path.getPixel(), 0.f, path.thp * emitterRadiance, -path.dir, 0.f, kNRDInvalidPathLength, kNRDInvalidPathLength);
        }
        else
        {
            writeNRDDeltaReflectionGuideBuffers(outputNRD, kUseNRDDemodulation, path.getPixel(), 0.f, 0.f, -path.dir, 0.f, kNRDInvalidPathLength, kNRDInvalidPathLength);
        }
#elif defined(DELTA_TRANSMISSION_PASS)
        if (path.isDeltaTransmissionPath())
        {
            writeNRDDeltaTransmissionGuideBuffers(outputNRD, kUseNRDDemodulation, path.getPixel(), 0.f, path.thp * emitterRadiance, -path.dir, 0.f, kNRDInvalidPathLength, path.origin + path.dir * kNRDInvalidPathLength);
        }
        else
        {
            writeNRDDeltaTransmissionGuideBuffers(outputNRD, kUseNRDDemodulation, path.getPixel(), 0.f, 0.f, -path.dir, 0.f, kNRDInvalidPathLength, 0.f);
        }
#endif

        path.terminate();
    }

    /** Write path contribution to output buffer.
    */
    void writeOutput(const PathState path)
    {
        assert(!any(isnan(path.L)));

        // Log path length.
        logPathLength(getTerminatedPathLength(path));

        // Log ray wavelength for statistics
        float wavelength = path.getWavelength();
        if (wavelength > 0.0f)
        {
            logRayWavelength(wavelength);
        }

        const uint2 pixel = path.getPixel();
        const uint outIdx = params.getSampleOffset(pixel, sampleOffset) + path.getSampleIdx();

        if (kSamplesPerPixel == 1)
        {
            // Write color directly to frame buffer.
            outputColor[pixel] = float4(path.L, 1.f);
        }
        else
        {
            // Write color to per-sample buffer.
            sampleColor[outIdx].set(path.L);
        }

        if (kOutputGuideData)
        {
            sampleGuideData[outIdx] = path.guideData;
        }

        // 保存初始光线方向和强度到输出缓冲区
        if (kOutputInitialRayInfo)
        {
            sampleInitialRayInfo[outIdx] = float4(path.initialDir, luminance(path.L));
        }

        if (kOutputNRDData)
        {
            // TODO: Optimize this for 1 SPP. It doesn't have to go through resolve pass like the color above.
            NRDRadiance data = {};

            if (path.isDiffusePrimaryHit()) data.setPathType(NRDPathType::Diffuse);
            else if (path.isSpecularPrimaryHit()) data.setPathType(NRDPathType::Specular);
            else if (path.isDeltaReflectionPrimaryHit()) data.setPathType(NRDPathType::DeltaReflection);
            else if (path.isDeltaTransmissionPath()) data.setPathType(NRDPathType::DeltaTransmission);
            else data.setPathType(NRDPathType::Residual);

            data.setRadiance(path.L);

            outputNRD.sampleRadiance[outIdx] = data;
        }
    }

    /** Update CIR data during path tracing.
        Calculates emission angle based on material properties and surface geometry.
        \param[in,out] path Path state.
        \param[in] surfaceNormal Surface normal at hit point.
        \param[in] bsdfProperties BSDF properties for emission angle calculation.
    */
        void updateCIRDataDuringTracing(inout PathState path, float3 surfaceNormal, BSDFProperties bsdfProperties)
    {
        const uint vertexIndex = path.getVertexIndex();
        const bool isPrimaryHit = (vertexIndex == 1);

                // Strategy 1: Check for emissive materials (LED modeled as emissive surfaces)
        if (any(bsdfProperties.emission > 0.0f))
        {
            path.hitEmissiveSurface = true;  // Set flag when hitting emissive surface
            path.updateCIREmissionAngle(surfaceNormal);

            // Calculate single path effective emitted power from emission
            float emissionPower = calculateEmissionPower(bsdfProperties.emission, path);
            path.setOriginalLEDPower(emissionPower);
        }
        // Strategy 2: For VLC systems, calculate emission angle at primary hit
        else if (isPrimaryHit && path.cirEmissionAngle == 0.0f)
        {
            float3 emissionNormal = normalize(surfaceNormal);
            float3 lightDirection = normalize(-path.initialDir);
            float cosAngle = abs(dot(lightDirection, emissionNormal));

            if (cosAngle > 0.001f && !isnan(cosAngle) && !isinf(cosAngle))
            {
                path.cirEmissionAngle = acos(clamp(cosAngle, 0.0f, 1.0f));
            }
            else
            {
                path.cirEmissionAngle = 0.785398f; // 45 degrees fallback
            }
        }
        // Strategy 3: Final fallback for primary hits without calculated angle
        else if (isPrimaryHit && path.cirEmissionAngle == 0.0f)
        {
            float3 surfaceUp = float3(0, 1, 0);
            float3 normal = normalize(surfaceNormal);
            float cosAngle = abs(dot(normal, surfaceUp));

            if (cosAngle > 0.001f)
            {
                path.cirEmissionAngle = acos(clamp(cosAngle, 0.0f, 1.0f));
            }
            else
            {
                path.cirEmissionAngle = 0.523599f; // 30 degrees fallback
            }
        }

        // Update reflectance using material properties
        // This primarily affects pathways with surface interactions
        if (!isPrimaryHit && vertexIndex > 1)
        {
            // COMMENTED OUT: This was causing double accumulation of reflectance values
            // The high-level function updateCIRWithMaterialReflectance() already handles this
            /*
            // Calculate combined reflectance from diffuse and specular components
            float3 totalAlbedo = bsdfProperties.diffuseReflectionAlbedo + bsdfProperties.specularReflectionAlbedo;
            float avgReflectance = dot(totalAlbedo, float3(0.33333f, 0.33333f, 0.33333f));

            // Apply reflectance only if valid
            if (avgReflectance > 0.0f && avgReflectance <= 1.0f)
            {
                path.updateCIRReflectance(avgReflectance);
            }
            */
        }
    }

    /** Output CIR data on path completion using PixelStats system.
        MODIFIED: Uses unified validation via logCIRPathComplete to ensure data consistency.
        \param[in] path Path state.
    */
    void outputCIRDataOnPathCompletion(PathState path)
    {
        // Update reception angle when path is about to terminate
        if (path.isHit() && length(path.normal) > 0.1f)
        {
            path.updateCIRReceptionAngle(path.dir, path.normal);
        }

        // Dynamically generate complete CIR data (utilizing reused fields)
        CIRPathData cirData = path.getCIRData();

        // UNIFIED VALIDATION: Use single validation point for both statistics and raw data
        // This ensures consistency between statistical aggregation and file output
        logCIRPathComplete(cirData);
    }

    /** Convert radiance to single path power share.
        Converts the sampled radiance from light sampling to the effective power
        share that this single path represents from the LED source.
        \param[in] radiance Sampled radiance from light sampling (Li).
        \param[in] light LED light data structure.
        \param[in] samplingPdf PDF used for light sampling.
        \return Effective power share for this single path (watts).
    */
    float convertRadianceToPower(float3 radiance, LightData light, float samplingPdf)
    {
        // Calculate radiance magnitude
        float radianceValue = luminance(radiance);

        // Error checking for invalid inputs
        if (radianceValue <= 0.0f || samplingPdf <= 0.0f || isnan(radianceValue) || isinf(radianceValue))
        {
            return 0.666f; // Error indicator value
        }

        // Calculate sampling weight (inverse of PDF)
        float samplingWeight = 1.0f / samplingPdf;

        // Estimate solid angle to power conversion factor
        // This is a simplified conversion based on LED characteristics
        // In a real implementation, this would use the actual LED surface area and emission pattern
        float solidAngleToPower = 1.0f; // Simplified factor

        // For LED lights, we can estimate the surface area from light properties
        // This is a heuristic based on typical LED configurations
        if (light.surfaceArea > 0.0f)
        {
            // Use actual surface area if available
            solidAngleToPower = light.surfaceArea / (4.0f * 3.14159265359f);
        }
        else
        {
            // Fallback: estimate based on light intensity
            // Typical LED surface area ranges from 1mm² to 100mm²
            solidAngleToPower = 0.001f; // Default to 10mm² equivalent
        }

        // Calculate effective power share for this path
        // This represents the portion of LED power that this single ray carries
        float pathPower = radianceValue * samplingWeight * solidAngleToPower;

        // Clamp to reasonable range for LED power shares
        // Single path should be a small fraction of total LED power
        pathPower = clamp(pathPower, 0.001f, 10.0f); // 1mW to 10W range

        return pathPower;
    }

    /** Calculate emission power from emissive surface properties.
        Estimates the effective power share for a single path based on the
        emissive surface's emission properties.
        \param[in] emission Emission radiance from BSDF properties.
        \param[in] path Current path state for context.
        \return Effective power share for this single path (watts).
    */
    float calculateEmissionPower(float3 emission, PathState path)
    {
        // Calculate emission magnitude
        float emissionValue = luminance(emission);

        // Error checking
        if (emissionValue <= 0.0f || isnan(emissionValue) || isinf(emissionValue))
        {
            return 0.666f; // Error indicator value
        }

        // Estimate power conversion based on path throughput and sampling
        // This is a heuristic conversion from radiance to power
        float pathThroughput = luminance(path.thp);

        // Simple conversion factor based on typical LED characteristics
        // In practice, this would be calibrated based on actual LED specifications
        float conversionFactor = 0.1f; // Heuristic factor

        // Calculate effective power share
        float pathPower = emissionValue * conversionFactor * max(pathThroughput, 0.001f);

        // Clamp to reasonable range for single path power shares
        pathPower = clamp(pathPower, 0.001f, 5.0f); // 1mW to 5W range

        return pathPower;
    }

    /** Calculate path power from light sample.
        Estimates the effective power share for a single path based on the
        light sampling result.
        \param[in] ls Light sample containing radiance and PDF information.
        \return Effective power share for this single path (watts).
    */
    float calculatePathPowerFromLightSample(LightSample ls)
    {
        // Calculate radiance magnitude
        float radianceValue = luminance(ls.Li);

        // Error checking
        if (radianceValue <= 0.0f || ls.pdf <= 0.0f || isnan(radianceValue) || isinf(radianceValue))
        {
            return 0.666f; // Error indicator value
        }

        // Calculate sampling weight (inverse of PDF)
        float samplingWeight = 1.0f / ls.pdf;

        // Different conversion factors based on light type
        float conversionFactor = 0.1f; // Default factor

        if (ls.lightType == (uint)LightType::Emissive)
        {
            // Emissive surfaces: use area-based conversion
            conversionFactor = 0.05f; // Smaller factor for emissive surfaces
        }
        else if (ls.lightType == (uint)LightType::Analytic)
        {
            // Analytic lights (including LEDs): use intensity-based conversion
            conversionFactor = 0.2f; // Larger factor for point/directional lights
        }
        else if (ls.lightType == (uint)LightType::EnvMap)
        {
            // Environment map: very small contribution per path
            conversionFactor = 0.01f;
        }

        // Calculate effective power share
        float pathPower = radianceValue * samplingWeight * conversionFactor;

        // Clamp to reasonable range for single path power shares
        pathPower = clamp(pathPower, 0.001f, 8.0f); // 1mW to 8W range

        return pathPower;
    }

    /** Handle LED light sampling with spectral sampling support.
        This function performs spectral-aware sampling of LED lights, updating the path wavelength
        based on the LED's spectral distribution.
        \param[in,out] path Path state.
        \param[in,out] sg Sample generator.
        \return True if LED light was successfully sampled, false otherwise.
    */
    bool handleLEDLightSampling(inout PathState path, inout SampleGenerator sg)
    {
        // Check if we have a valid hit and can access light data
        if (!path.isHit()) return false;

        // Get light data from the hit instance
        const uint materialID = gScene.getMaterialID(path.hit.getInstanceID());
        const MaterialHeader materialHeader = gScene.materials.getMaterialHeader(materialID);
        if (!materialHeader.isEmissive()) return false;

        // Check for LED light type through analytic lights
        uint lightCount = gScene.getLightCount();
        if (!kUseAnalyticLights || lightCount == 0) return false;

        // Find LED light matching this surface
        for (uint lightIndex = 0; lightIndex < lightCount; ++lightIndex)
        {
            LightData light = gScene.getLight(lightIndex);
            if (light.type == uint(::LightType::LED))
            {
                // Sample LED light with spectrum
                AnalyticLightSample ls;
                float sampledWavelength = 0.0f;

                // Use LightHelpers function to sample LED with spectrum
                bool validSample = sampleLEDLightWithSpectrum(
                    path.origin,
                    light,
                    sampleNext3D(sg),
                    ls,
                    sampledWavelength
                );

                if (validSample)
                {
                    // Update path wavelength if not already set
                    if (path.getWavelength() == 0.0f)
                    {
                        // Validate wavelength range
                        if (sampledWavelength >= 350.0f && sampledWavelength <= 800.0f)
                        {
                            path.setWavelength(sampledWavelength);
                        }
                        else
                        {
                            path.setWavelength(550.0f); // Default green wavelength
                        }
                    }

                    // Original LED power is now handled by initial L value recording in addToPathContribution

                    // Add light contribution to path
                    // Note: BSDF evaluation would be handled by the calling code
                    return true;
                }
                else
                {
                    // Set default wavelength if sampling failed
                    if (path.getWavelength() == 0.0f)
                    {
                        path.setWavelength(550.0f);
                    }
                }
            }
        }

        return false;
    }

    /** Update path wavelength from LED light emission.
        This function is called when a path hits an LED light source to update
        the wavelength based on the LED's spectral distribution.
        \param[in,out] path Path state.
        \param[in] lightData LED light data structure.
        \param[in] Le Emitted radiance.
    */
    static void updatePathWavelengthFromLED(inout PathState path, const LightData lightData, const float3 Le)
    {
        // Skip if path already has wavelength or no emission
        if (path.getWavelength() != 0.0f || all(Le <= 0.0f))
            return;

        // Only process LED lights
        if (lightData.type != uint(::LightType::LED))
            return;

        // Sample wavelength from LED spectrum if available
        if (lightData.hasCustomSpectrum != 0)
        {
            float u = sampleNext1D(path.sg);
            float wavelength = sampleLEDWavelength(lightData, u);

            // Validate wavelength range
            if (wavelength >= 350.0f && wavelength <= 800.0f)
            {
                path.setWavelength(wavelength);
            }
            else
            {
                path.setWavelength(550.0f); // Default fallback
            }
        }
        else
        {
            // Use default wavelength for LED without custom spectrum
            path.setWavelength(550.0f);
        }
    }
};
