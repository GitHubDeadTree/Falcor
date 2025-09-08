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

/** CIR (Channel Impulse Response) path data structure for VLC analysis.
    This structure stores the essential parameters of each light propagation path
    needed for calculating the Channel Impulse Response in visible light communication systems.
    Each path represents light traveling from an LED transmitter through possible reflections
    to a photodiode receiver.

    Note: This structure must match exactly with the corresponding CPU structure
    for proper GPU-CPU data transfer.
*/
struct CIRPathData
{
    float pathLength;           ///< d_i: Total propagation distance of the path (meters)
    float emissionAngle;        ///< φ_i: Emission angle at LED surface (radians, range [0, π])
    float receptionAngle;       ///< θ_i: Reception angle at photodiode surface (radians, range [0, π])
    float reflectanceProduct;   ///< r_i product: Product of all surface reflectances along the path [0,1]
    uint reflectionCount;       ///< K_i: Number of reflections in the path
    float emittedPower;         ///< @deprecated P_t: Emitted optical power after attenuation (watts) - USE radianceRGB instead
    float originalEmittedPower; ///< @deprecated P_t_orig: Original LED emitted power before attenuation (watts) - USE radianceRGB instead
    uint pixelX;                ///< Pixel X coordinate
    uint pixelY;                ///< Pixel Y coordinate
    uint pathIndex;             ///< Unique index identifier for this path
    uint flags;                 ///< Bit 0: hitEmissiveSurface, Bit 1: isNEEPath, Bits 2-31: reserved

    // Helper functions for flag access
    bool getHitEmissiveSurface() { return (flags & 0x1) != 0; }
    bool getIsNEEPath() { return (flags & 0x2) != 0; }
    [mutating] void setHitEmissiveSurface(bool value) { flags = value ? (flags | 0x1) : (flags & ~0x1); }
    [mutating] void setIsNEEPath(bool value) { flags = value ? (flags | 0x2) : (flags & ~0x2); }

    // Vertex-related fields for path vertex collection feature
    uint2 compressedVertices[7];  ///< Compressed vertex coordinates, each vertex uses 6 bytes
    uint vertexCount;             ///< Actual number of vertices in the path
    float3 basePosition;          ///< Base position (camera position) for relative coordinate calculation
    
    // NEE-specific fields
    float4 lightSourcePosition;  ///< World space position of light source (for NEE paths only), w component unused
    
    // NEW FIELDS - Added at end to maintain memory layout compatibility
    float primaryRayPdfW;       ///< p_det(ω): Direction probability density at receiver/PD for solid angle (1/sr)
    float4 radianceRGBA;        ///< L(ω): MIS-combined radiance at camera position (W/(m²·sr)), A component unused
    
    // Light source position accessor functions (float4 with unused w component for alignment)
    float3 getLightSourcePosition() { return lightSourcePosition.xyz; }
    [mutating] void setLightSourcePosition(float3 position) { lightSourcePosition = float4(position, 0.0f); }

    /** Validate that all CIR parameters are within expected physical ranges.
        MODIFIED: Relaxed emittedPower validation to allow zero values for better data preservation.
        \return True if all parameters are valid, false otherwise.
    */
    bool isValid()
    {
        // GPU端只检查路径长度的NaN/无穷大值
        if (isnan(pathLength) || isinf(pathLength)) return false;

        // 其他所有验证都移到CPU端
        return true;
    }

    /** Sanitize CIR data by fixing only critical data integrity issues.
        MODIFIED: Removed path length clamping to allow proper filtering of invalid paths.
        This method only fixes NaN/infinity values but preserves original data for filtering.
    */
    [mutating] void sanitize()
    {
        // GPU端只处理路径长度的NaN/无穷大值
        // 其他所有验证和处理都交给CPU端
        if (isnan(pathLength) || isinf(pathLength))
        {
            pathLength = 1.0f; // 设置默认值，但实际上这种情况下整条路径会被丢弃
        }

        // 移除所有其他字段的处理逻辑
    }
};

/** Compress a 3D vertex coordinate using 16-bit float precision and relative positioning.
    Uses relative coordinates from a base position to maximize precision within 16-bit float range.

    \param vertex World space vertex position to compress
    \param basePosition Base position (typically camera position) for relative coordinate calculation
    \return Compressed vertex data packed into uint2 (6 bytes total)
*/
uint2 compressVertex(float3 vertex, float3 basePosition)
{
    // Calculate relative coordinates to maximize 16-bit float precision
    float3 relative = vertex - basePosition;

    // Check coordinate range to prevent 16-bit float overflow
    const float MAX_RANGE = 65000.0f; // Safe range for 16-bit floating point numbers
    if (length(relative) > MAX_RANGE)
    {
        // If exceeds range, scale to fit within safe bounds
        relative = normalize(relative) * MAX_RANGE;
    }

    // Compress using 16-bit floating point representation
    uint x16 = f32tof16(relative.x);
    uint y16 = f32tof16(relative.y);
    uint z16 = f32tof16(relative.z);

    // Pack coordinates into uint2 structure (6 bytes total)
    uint2 compressed;
    compressed.x = (y16 << 16) | x16;  // Pack x and y into first uint
    compressed.y = z16;                // Store z in second uint (only lower 16 bits used)
    return compressed;
}

/** Decompress a vertex coordinate from compressed uint2 format back to world space.

    \param compressed Compressed vertex data from compressVertex function
    \param basePosition Base position used during compression
    \return Decompressed world space vertex position, or error indicator (0.666, 0.666, 0.666) if invalid
*/
float3 decompressVertex(uint2 compressed, float3 basePosition)
{
    // Extract relative coordinates from compressed format
    float x = f16tof32(compressed.x & 0xFFFF);      // Extract lower 16 bits
    float y = f16tof32(compressed.x >> 16);         // Extract upper 16 bits
    float z = f16tof32(compressed.y & 0xFFFF);      // Extract z from second uint

    // Validate decompression results for NaN or infinite values
    if (isnan(x) || isnan(y) || isnan(z) || isinf(x) || isinf(y) || isinf(z))
    {
        // Return error indicator position for invalid data
        return float3(0.666, 0.666, 0.666);
    }

    // Convert relative coordinates back to world space
    float3 relative = float3(x, y, z);
    return basePosition + relative;
}
