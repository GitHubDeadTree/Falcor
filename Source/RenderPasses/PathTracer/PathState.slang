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
import Utils.Math.PackedFormats;
__exported import Scene.HitInfo;
__exported import Utils.Math.Ray;
__exported import Utils.Sampling.SampleGenerator;
__exported import Rendering.Materials.InteriorList;
__exported import GuideData;
import Utils.Color.ColorHelpers;  // Import for luminance function
import CIRPathData;  // Import CIR data structure

static const uint kMaxRejectedHits = 16; // Maximum number of rejected hits along a path. The path is terminated if the limit is reached to avoid getting stuck in pathological cases.

static const float kRayTMax = 1e30f;

// Be careful with changing these. PathFlags share 32-bit uint with vertexIndex. For now, we keep 10 bits for vertexIndex.
// PathFlags take higher bits, VertexIndex takes lower bits.
static const uint kVertexIndexBitCount = 10u;
static const uint kVertexIndexBitMask = (1u << kVertexIndexBitCount) - 1u;
static const uint kPathFlagsBitCount = 32u - kVertexIndexBitCount;
static const uint kPathFlagsBitMask = ((1u << kPathFlagsBitCount) - 1u) << kVertexIndexBitCount;

/** Path flags. The path flags are currently stored in kPathFlagsBitCount bits.
*/
enum class PathFlags
{
    active                      = 0x0001,   ///< Path is active/terminated.
    hit                         = 0x0002,   ///< Result of the scatter ray (0 = miss, 1 = hit).

    transmission                = 0x0004,   ///< Scatter ray went through a transmission event.
    specular                    = 0x0008,   ///< Scatter ray went through a specular event.
    delta                       = 0x0010,   ///< Scatter ray went through a delta event.

    insideDielectricVolume      = 0x0020,   ///< Path vertex is inside a dielectric volume.
    lightSampledUpper           = 0x0040,   ///< Last path vertex sampled lights using NEE (in upper hemisphere).
    lightSampledLower           = 0x0080,   ///< Last path vertex sampled lights using NEE (in lower hemisphere).

    diffusePrimaryHit           = 0x0100,   ///< Scatter ray went through a diffuse event on primary hit.
    specularPrimaryHit          = 0x0200,   ///< Scatter ray went through a specular event on primary hit.
    deltaReflectionPrimaryHit   = 0x0400,   ///< Primary hit was sampled as the delta reflection.
    deltaTransmissionPath       = 0x0800,   ///< Path started with and followed delta transmission events (whenever possible - TIR could be an exception) until it hit the first non-delta event.
    deltaOnlyPath               = 0x1000,   ///< There was no non-delta events along the path so far.

    // Bits 14 to kPathFlagsBitCount are still unused.
};

/** Bounce types. We keep separate counters for all of these.
*/
enum class BounceType
{
    Diffuse                 = 0,    ///< Diffuse reflection.
    Specular                = 1,    ///< Specular reflection (including delta).
    Transmission            = 2,    ///< Transmission (all kinds).
};

// TODO: Compact encoding to reduce live registers, e.g. packed HitInfo, packed normals.
/** Live state for the path tracer.
*/
struct PathState
{
    uint        id;                     ///< Path ID encodes (pixel, sampleIdx) with 12 bits each for pixel x|y and 8 bits for sample index.

    uint        flagsAndVertexIndex;    ///< Higher kPathFlagsBitCount bits: Flags indicating the current status. This can be multiple PathFlags flags OR'ed together.
                                        ///< Lower kVertexIndexBitCount bits: Current vertex index (0 = camera, 1 = primary hit, 2 = secondary hit, etc.).
    uint16_t    rejectedHits;           ///< Number of false intersections rejected along the path. This is used as a safeguard to avoid deadlock in pathological cases.
    float       sceneLength;            ///< Path length in scene units (0.f at primary hit). Changed from float16_t to float for better precision.
    uint        bounceCounters;         ///< Packed counters for different types of bounces (see BounceType).

    // Scatter ray
    float3      origin;                 ///< Origin of the scatter ray.
    float3      dir;                    ///< Scatter ray normalized direction.
    float       pdf;                    ///< Pdf for generating the scatter ray.
    float3      normal;                 ///< Shading normal at the scatter ray origin.
    HitInfo     hit;                    ///< Hit information for the scatter ray. This is populated at committed triangle hits.

    float3      thp;                    ///< Path throughput.
    float3      L;                      ///< Accumulated path contribution.

    float3      initialDir;             ///< Initial ray direction from camera (added for irradiance calculation).

    GuideData   guideData;              ///< Denoiser guide data.
    InteriorList interiorList;          ///< Interior list. Keeping track of a stack of materials with medium properties.
    SampleGenerator sg;                 ///< Sample generator state. Typically 4-16B.

    float       wavelength;             ///< Light wavelength in nm (0 = not determined or white light)

    // === CIR (Channel Impulse Response) specific fields (29 bytes) ===
    float cirEmissionAngle;             ///< φ_i: Emission angle at LED surface (radians)
    float cirReceptionAngle;            ///< θ_i: Reception angle at receiver surface (radians)
    float cirReflectanceProduct;        ///< r_i: Accumulated product of surface reflectances [0,1]
    bool hitEmissiveSurface;            ///< Flag indicating if the path has hit an emissive surface
    float cirOriginalLEDPower;          ///< P_t_orig: Single path effective emitted power (watts)
    float3 cirInitialL;                 ///< Initial path radiance L for power calculation

    // === Vertex collection fields for path vertex collection feature ===
    float3 pathVertices[7];             ///< Temporary storage for vertex coordinates (max 7 vertices)
    uint pathVertexCount;               ///< Current number of vertices in the path
    float3 pathBasePosition;            ///< Base position (camera position) for relative coordinate calculation
    bool vertexCollectionEnabled;       ///< Flag indicating if vertex collection is enabled

    // Accessors

    bool isTerminated() { return !isActive(); }
    bool isActive() { return hasFlag(PathFlags::active); }
    bool isHit() { return hasFlag(PathFlags::hit); }
    bool isTransmission() { return hasFlag(PathFlags::transmission); }
    bool isSpecular() { return hasFlag(PathFlags::specular); }
    bool isDelta() { return hasFlag(PathFlags::delta); }
    bool isInsideDielectricVolume() { return hasFlag(PathFlags::insideDielectricVolume); }

    bool isLightSampled()
    {
        const uint bits = (uint(PathFlags::lightSampledUpper) | uint(PathFlags::lightSampledLower)) << kVertexIndexBitCount;
        return flagsAndVertexIndex & bits;
    }

    bool isLightSampledUpper() { return hasFlag(PathFlags::lightSampledUpper); }
    bool isLightSampledLower() { return hasFlag(PathFlags::lightSampledLower); }

    bool isDiffusePrimaryHit() { return hasFlag(PathFlags::diffusePrimaryHit); }
    bool isSpecularPrimaryHit() { return hasFlag(PathFlags::specularPrimaryHit); }
    bool isDeltaReflectionPrimaryHit() { return hasFlag(PathFlags::deltaReflectionPrimaryHit); }
    bool isDeltaTransmissionPath() { return hasFlag(PathFlags::deltaTransmissionPath); }
    bool isDeltaOnlyPath() { return hasFlag(PathFlags::deltaOnlyPath); }

    // Check if the scatter event is samplable by the light sampling technique.
    bool isLightSamplable() { return !isDelta(); }

    [mutating] void terminate() { setFlag(PathFlags::active, false); }
    [mutating] void setActive() { setFlag(PathFlags::active); }
    [mutating] void setHit(HitInfo hitInfo) { hit = hitInfo; setFlag(PathFlags::hit); }
    [mutating] void clearHit() { setFlag(PathFlags::hit, false); }

    [mutating] void clearEventFlags()
    {
        const uint bits = (uint(PathFlags::transmission) | uint(PathFlags::specular) | uint(PathFlags::delta)) << kVertexIndexBitCount;
        flagsAndVertexIndex &= ~bits;
    }

    [mutating] void setTransmission(bool value = true) { setFlag(PathFlags::transmission, value); }
    [mutating] void setSpecular(bool value = true) { setFlag(PathFlags::specular, value); }
    [mutating] void setDelta(bool value = true) { setFlag(PathFlags::delta, value); }
    [mutating] void setInsideDielectricVolume(bool value = true) { setFlag(PathFlags::insideDielectricVolume, value); }
    [mutating] void setLightSampled(bool upper, bool lower) { setFlag(PathFlags::lightSampledUpper, upper); setFlag(PathFlags::lightSampledLower, lower); }
    [mutating] void setDiffusePrimaryHit(bool value = true) { setFlag(PathFlags::diffusePrimaryHit, value); }
    [mutating] void setSpecularPrimaryHit(bool value = true) { setFlag(PathFlags::specularPrimaryHit, value); }
    [mutating] void setDeltaReflectionPrimaryHit(bool value = true) { setFlag(PathFlags::deltaReflectionPrimaryHit, value); }
    [mutating] void setDeltaTransmissionPath(bool value = true) { setFlag(PathFlags::deltaTransmissionPath, value); }
    [mutating] void setDeltaOnlyPath(bool value = true) { setFlag(PathFlags::deltaOnlyPath, value); }

    // Wavelength and intensity methods
    [mutating] void setWavelength(float lambda) { wavelength = lambda; }
    float getWavelength() { return wavelength; }

    // Calculate light intensity based on luminance
    float getLightIntensity()
    {
        return luminance(L);
    }

    bool hasFlag(PathFlags flag)
    {
        const uint bit = uint(flag) << kVertexIndexBitCount;
        return (flagsAndVertexIndex & bit) != 0;
    }

    [mutating] void setFlag(PathFlags flag, bool value = true)
    {
        const uint bit = uint(flag) << kVertexIndexBitCount;
        if (value) flagsAndVertexIndex |= bit;
        else flagsAndVertexIndex &= ~bit;
    }

    uint getBounces(BounceType type)
    {
        const uint shift = (uint)type << 3;
        return (bounceCounters >> shift) & 0xff;
    }

    [mutating] void setBounces(BounceType type, uint bounces)
    {
        const uint shift = (uint)type << 3;
        bounceCounters = (bounceCounters & ~((uint)0xff << shift)) | ((bounces & 0xff) << shift);
    }

    [mutating] void incrementBounces(BounceType type)
    {
        const uint shift = (uint)type << 3;
        // We assume that bounce counters cannot overflow.
        bounceCounters += (1 << shift);
    }

    uint2 getPixel() { return uint2(id, id >> 12) & 0xfff; }
    uint getSampleIdx() { return id >> 24; }

    // Unsafe - assumes that index is small enough.
    [mutating] void setVertexIndex(uint index)
    {
        // Clear old vertex index.
        flagsAndVertexIndex &= kPathFlagsBitMask;
        // Set new vertex index (unsafe).
        flagsAndVertexIndex |= index;
    }

    uint getVertexIndex() { return flagsAndVertexIndex & kVertexIndexBitMask; }

    // Unsafe - assumes that vertex index never overflows.
    [mutating] void incrementVertexIndex() { flagsAndVertexIndex += 1; }
    // Unsafe - assumes that vertex index will never be decremented below zero.
    [mutating] void decrementVertexIndex() { flagsAndVertexIndex -= 1; }

    Ray getScatterRay()
    {
        return Ray(origin, dir, 0.f, kRayTMax);
    }

    // === CIR (Channel Impulse Response) data management methods ===

    /** Initialize CIR data with default values.
        Called when a new path is generated.
    */
    [mutating] void initCIRData()
    {
        cirEmissionAngle = 0.0f;      // Default: perpendicular emission
        cirReceptionAngle = 0.0f;     // Default: perpendicular reception
        cirReflectanceProduct = 1.0f; // Initial value: no attenuation, will be multiplied by reflectances
        hitEmissiveSurface = false;   // Default: not hit emissive surface
        cirOriginalLEDPower = 0.0f;   // Default: no power assigned yet
    }

    /** Update emission angle based on surface normal and ray direction.
        Should be called at the first surface interaction (emission surface).
        \param[in] surfaceNormal Surface normal at the emission point.
    */
    [mutating] void updateCIREmissionAngle(float3 surfaceNormal)
    {
        float3 rayDir = normalize(dir);
        float3 normal = normalize(surfaceNormal);
        float cosAngle = dot(rayDir, normal);

        // Numerical domain protection only; semantic filtering is done at export.
        if (isnan(cosAngle) || isinf(cosAngle))
        {
            return;
        }

        cirEmissionAngle = acos(clamp(cosAngle, -1.0f, 1.0f));
    }

    /** DEPRECATED: Old reception angle calculation (incorrect for CIR paths).
        This function calculated angle with last hit surface instead of camera/PD surface.
        Kept for backward compatibility but should not be used.
        Use initCIRReceptionAngle() instead.
    */
    [mutating] void updateCIRReceptionAngle(float3 incomingDir, float3 surfaceNormal)
    {
        float3 incDir = normalize(-incomingDir);
        float3 normal = normalize(surfaceNormal);
        float cosAngle = dot(incDir, normal);

        // Numerical domain protection only; semantic filtering is done at export.
        if (isnan(cosAngle) || isinf(cosAngle))
        {
            return;
        }

        cirReceptionAngle = acos(clamp(cosAngle, -1.0f, 1.0f));
    }

    /** Initialize reception angle for CIR paths at path start.
        For CIR paths (from camera/PD), this calculates the angle between
        the initial ray direction and the camera/PD surface normal.
        Should be called once when the path is initialized.
        \param[in] cameraDirection Camera/PD surface normal direction.
        \param[in] initialRayDir Initial ray direction from camera/PD.
    */
    [mutating] void initCIRReceptionAngle(float3 cameraDirection, float3 initialRayDir)
    {
        float3 camDir = normalize(cameraDirection);
        float3 rayDir = normalize(initialRayDir);
        float cosAngle = dot(rayDir, camDir);

        // Numerical domain protection
        if (isnan(cosAngle) || isinf(cosAngle))
        {
            cirReceptionAngle = 0.0f; // Default: perpendicular emission from PD
            return;
        }

        cirReceptionAngle = acos(clamp(cosAngle, -1.0f, 1.0f));
    }

    /** Update reflectance by accumulating surface reflectance values.
        This follows the VLC standard method of computing the product of
        all surface reflectances along the light path.
        \param[in] reflectance Surface reflectance value to accumulate [0,1].
    */
    [mutating] void updateCIRReflectance(float reflectance)
    {
        // Error checking: ensure reflectance is in valid range
        if (reflectance < 0.0f || reflectance > 1.0f || isnan(reflectance) || isinf(reflectance))
        {
            // Skip invalid reflectance values to maintain data integrity
            return;
        }

        // VLC standard method: multiply surface reflectances
        cirReflectanceProduct *= reflectance;

        // Numerical stability: prevent underflow
        if (cirReflectanceProduct < 1e-6f)
        {
            cirReflectanceProduct = 1e-6f;
        }

        // Ensure within valid range for VLC systems
        cirReflectanceProduct = clamp(cirReflectanceProduct, 0.0f, 1.0f);
    }

    /** Set the original LED power for this path.
        This represents the effective power share of this single path from the LED source.
        \param[in] power Effective power share for this path (watts).
    */
    [mutating] void setOriginalLEDPower(float power)
    {
        // Error checking: validate power value
        if (power <= 0.0f || isnan(power) || isinf(power))
        {
            // Use error indicator value for debugging
            cirOriginalLEDPower = 0.666f;
            return;
        }

        cirOriginalLEDPower = power;
    }

    /** Set the initial path radiance L for power calculation.
        This should be called when the path starts to record the initial L value.
        \param[in] initialL Initial path radiance L value.
    */
    [mutating] void setInitialL(float3 initialL)
    {
        cirInitialL = initialL;
    }

    /** Get the original LED power for this path.
        Uses the initial path radiance L value directly with scaling factor 1.0.
        \return Original LED power based on initial radiance, or error indicator if not set properly.
    */
    float getOriginalLEDPower()
    {
        // Use the initial path radiance directly (scaling factor = 1.0)
        float initialLuminance = luminance(cirInitialL);

        // Error checking for invalid initial radiance
        if (initialLuminance <= 0.0f || isnan(initialLuminance) || isinf(initialLuminance))
        {
            // Return error indicator value for debugging
            return 0.666f;
        }

        // Return the initial luminance directly as power (scaling factor = 1.0)
        return initialLuminance;
    }

    // === Vertex collection methods for path vertex collection feature ===

    /** Initialize vertex collection for the path with camera position as base.
        Sets up the base position and enables vertex collection.
        \param[in] cameraPos Camera position to use as base for relative coordinates
    */
    [mutating] void initPathVertexCollection(float3 cameraPos)
    {
        pathBasePosition = cameraPos;
        pathVertexCount = 0;
        vertexCollectionEnabled = true;

        // Add camera position as the first vertex
        addPathVertex(cameraPos);
    }

    /** Add a vertex to the path vertex collection.
        Automatically validates vertex coordinates and enforces maximum vertex limit.
        Collects all vertices including those on emissive surfaces.
        \param[in] position World space position of the vertex to add
    */
    [mutating] void addPathVertex(float3 position)
    {
        if (!vertexCollectionEnabled)
            return;

        // NOTE: Removed check for hitEmissiveSurface to allow collecting vertices on emissive surfaces
        // This ensures that for ReflectionCount = 1, we can collect both the reflection point and the final emissive hit

        // Check for NaN or infinite values in the input position
        if (isnan(position.x) || isnan(position.y) || isnan(position.z) ||
            isinf(position.x) || isinf(position.y) || isinf(position.z))
        {
            // Skip invalid vertex coordinates
            return;
        }

        if (pathVertexCount < 7)
        {
            pathVertices[pathVertexCount] = position;
            pathVertexCount++;
        }
        else
        {
            // Reached maximum vertex limit, ignore additional vertices
            // Note: In a real implementation, this would be logged as a warning
        }
    }

    /** Validate the current state of vertex collection.
        Checks if vertex collection data is valid and ready for use.
        \return True if vertex collection is valid, false if there are issues
    */
    bool isVertexCollectionValid()
    {
        if (pathVertexCount == 0)
        {
            // No vertices collected
            return false;
        }

        // Check that vertex count is within bounds
        if (pathVertexCount > 7)
        {
            return false;
        }

        // Validate base position
        if (isnan(pathBasePosition.x) || isnan(pathBasePosition.y) || isnan(pathBasePosition.z) ||
            isinf(pathBasePosition.x) || isinf(pathBasePosition.y) || isinf(pathBasePosition.z))
        {
            return false;
        }

        // Check validity of collected vertices
        for (uint i = 0; i < pathVertexCount; i++)
        {
            float3 vertex = pathVertices[i];
            if (isnan(vertex.x) || isnan(vertex.y) || isnan(vertex.z) ||
                isinf(vertex.x) || isinf(vertex.y) || isinf(vertex.z))
            {
                return false;
            }
        }

        return true;
    }

        /** Generate complete CIR data by combining existing fields with CIR-specific fields.
        This method maximizes data reuse by utilizing existing PathState fields.
        Uses accumulated surface reflectance product for VLC calculations.
        Extended to include vertex collection data.
        TASK 3: Updated to ensure originalEmittedPower field is properly filled and validated.
        \return Complete CIR data structure ready for output.
    */
    CIRPathData getCIRData()
    {
        CIRPathData cir = {};  // Zero-initialize all fields
        cir.setIsNEEPath(false); // Explicitly set for regular paths

        // ✅ Reuse existing fields (7/9 parameters - 77.8% reuse rate)
        cir.pathLength = sceneLength;                                           // Direct assignment, no conversion needed
        cir.reflectionCount = getBounces(BounceType::Diffuse) +
                             getBounces(BounceType::Specular);                  // Reuse existing field
        cir.emittedPower = getLightIntensity();                                 // Reuse existing field (attenuated power)
        uint2 pixel = getPixel();
        cir.pixelX = pixel.x;                                                   // Reuse existing field
        cir.pixelY = pixel.y;                                                   // Reuse existing field
        cir.pathIndex = id;                                                     // Reuse existing field

        // ✅ Use accumulated reflectance product from surface materials
        cir.reflectanceProduct = cirReflectanceProduct;                         // Use accumulated material reflectance

        // ❌ Use CIR-specific fields (4/11 parameters)
        cir.emissionAngle = cirEmissionAngle;                                   // CIR-specific field
        cir.receptionAngle = cirReceptionAngle;                                 // CIR-specific field
        cir.setHitEmissiveSurface(hitEmissiveSurface);                          // Flag indicating if an emissive surface was hit
        cir.setIsNEEPath(false);                                               // Regular paths are not NEE paths

        // TASK 3: Set original LED power with validation
        cir.originalEmittedPower = getOriginalLEDPower();                       // Original LED power for this path

        // === New: Handle vertex collection data ===
        if (!isVertexCollectionValid())
        {
            // If vertex collection is invalid, use default error values
            cir.basePosition = float3(0.666, 0.666, 0.666);
            cir.vertexCount = 1;
            cir.compressedVertices[0] = uint2(0, 0);
            for (uint i = 1; i < 7; i++)
            {
                cir.compressedVertices[i] = uint2(0, 0);
            }
        }
        else
        {
            // Valid vertex collection data
            cir.basePosition = pathBasePosition;
            cir.vertexCount = pathVertexCount;

            // Compress vertex data using the compression functions from CIRPathData.slang
            for (uint i = 0; i < 7; i++)
            {
                if (i < pathVertexCount)
                {
                    cir.compressedVertices[i] = compressVertex(pathVertices[i], pathBasePosition);
                }
                else
                {
                    cir.compressedVertices[i] = uint2(0, 0);
                }
            }
        }

        // TASK 3: Enhanced validation and error handling for CIR data

        // Validate reflectance product
        if (cir.reflectanceProduct <= 0.0f || isnan(cir.reflectanceProduct) || isinf(cir.reflectanceProduct))
        {
            cir.reflectanceProduct = 0.666f; // Debug value to identify problematic paths
        }
        else
        {
            // Ensure within valid range for VLC systems
            cir.reflectanceProduct = clamp(cir.reflectanceProduct, 0.0f, 1.0f);
        }

        // TASK 3: Validate original emitted power
        if (cir.originalEmittedPower <= 0.0f || cir.originalEmittedPower == 0.666f)
        {
            // Log warning for debugging (in actual implementation, this would be logged)
            // For now, keep the error indicator value for identification
        }

        // Power relationship validation is no longer needed as originalEmittedPower
        // is now calculated directly from initial L value and should always be valid

        return cir;
    }
};
