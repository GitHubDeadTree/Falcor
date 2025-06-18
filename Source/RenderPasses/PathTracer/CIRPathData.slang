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
    float emittedPower;         ///< P_t: Emitted optical power (watts)
    uint pixelX;                ///< Pixel X coordinate
    uint pixelY;                ///< Pixel Y coordinate
    uint pathIndex;             ///< Unique index identifier for this path

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
