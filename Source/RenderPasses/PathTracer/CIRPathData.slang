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
import Utils.Debug.PixelDebug;

/** CIR (Channel Impulse Response) path data structure.
    This structure stores the key parameters for each light propagation path
    that are required for CIR calculation in visible light communication systems.
*/
struct CIRPathData
{
    float pathLength;           ///< Total path propagation distance (meters)
    float emissionAngle;        ///< Emission angle at LED surface (radians)
    float receptionAngle;       ///< Reception angle at receiver surface (radians)
    float reflectanceProduct;   ///< Product of all surface reflectances along the path
    uint reflectionCount;       ///< Number of reflections in the path
    float emittedPower;         ///< Initial emitted power (watts)
    uint2 pixelCoord;          ///< Pixel coordinates for debugging
    uint pathIndex;            ///< Unique path identifier

    /** Initialize CIR path data with default values.
    */
    __init()
    {
        pathLength = 0.0f;
        emissionAngle = 0.0f;
        receptionAngle = 0.0f;
        reflectanceProduct = 1.0f;
        reflectionCount = 0;
        emittedPower = 0.0f;
        pixelCoord = uint2(0, 0);
        pathIndex = 0;
    }

    /** Initialize CIR path data with specified parameters.
        \param[in] pathIdx Unique path identifier.
        \param[in] pixelPos Pixel coordinates.
    */
    __init(uint pathIdx, uint2 pixelPos)
    {
        pathLength = 0.0f;
        emissionAngle = 0.0f;
        receptionAngle = 0.0f;
        reflectanceProduct = 1.0f;
        reflectionCount = 0;
        emittedPower = 0.0f;
        pixelCoord = pixelPos;
        pathIndex = pathIdx;
    }

    /** Validate CIR path data parameters.
        Checks if all parameters are within reasonable physical ranges.
        \return True if all parameters are valid, false otherwise.
    */
    bool isValid()
    {
        // Check path length (should be positive and less than 1000m)
        if (pathLength <= 0.0f || pathLength > 1000.0f)
            return false;

        // Check angles (should be between 0 and PI radians)
        if (emissionAngle < 0.0f || emissionAngle > M_PI ||
            receptionAngle < 0.0f || receptionAngle > M_PI)
            return false;

        // Check reflectance product (should be between 0 and 1)
        if (reflectanceProduct < 0.0f || reflectanceProduct > 1.0f)
            return false;

        // Check emitted power (should be positive)
        if (emittedPower <= 0.0f || isnan(emittedPower) || isinf(emittedPower))
            return false;

        return true;
    }

    /** Apply corrections to invalid parameter values.
        This function attempts to fix out-of-range values by clamping them
        to reasonable defaults or valid ranges.
    */
    [mutating] void correctInvalidValues()
    {
        // Correct path length
        if (pathLength <= 0.0f || pathLength > 1000.0f)
        {
            pathLength = clamp(pathLength, 0.1f, 1000.0f);
            if (pathLength <= 0.0f)
                pathLength = 0.3f; // Default to light travel time of 1ns
        }

        // Correct emission angle
        if (emissionAngle < 0.0f || emissionAngle > M_PI)
        {
            emissionAngle = clamp(emissionAngle, 0.0f, M_PI);
            if (isnan(emissionAngle))
                emissionAngle = M_PI / 4.0f; // Default to 45 degrees
        }

        // Correct reception angle
        if (receptionAngle < 0.0f || receptionAngle > M_PI)
        {
            receptionAngle = clamp(receptionAngle, 0.0f, M_PI);
            if (isnan(receptionAngle))
                receptionAngle = M_PI / 4.0f; // Default to 45 degrees
        }

        // Correct reflectance product
        if (reflectanceProduct < 0.0f || reflectanceProduct > 1.0f)
        {
            reflectanceProduct = clamp(reflectanceProduct, 0.0f, 1.0f);
            if (isnan(reflectanceProduct))
                reflectanceProduct = 0.5f; // Default reflectance
        }

        // Correct emitted power
        if (emittedPower <= 0.0f || isnan(emittedPower) || isinf(emittedPower))
        {
            emittedPower = 1.0f; // Default to 1 watt
        }
    }

    /** Log detailed parameter information for debugging.
        Outputs comprehensive information about this path's parameters.
    */
    void logDebugInfo()
    {
        logInfo("=== CIR Path Data Debug Info ===");
        logInfo("Path Index: " + std::to_string(pathIndex));
        logInfo("Pixel Coord: (" + std::to_string(pixelCoord.x) + ", " + std::to_string(pixelCoord.y) + ")");
        logInfo("Path Length: " + std::to_string(pathLength) + " m");
        logInfo("Emission Angle: " + std::to_string(emissionAngle) + " rad (" +
                std::to_string(emissionAngle * 180.0f / M_PI) + " deg)");
        logInfo("Reception Angle: " + std::to_string(receptionAngle) + " rad (" +
                std::to_string(receptionAngle * 180.0f / M_PI) + " deg)");
        logInfo("Reflectance Product: " + std::to_string(reflectanceProduct));
        logInfo("Reflection Count: " + std::to_string(reflectionCount));
        logInfo("Emitted Power: " + std::to_string(emittedPower) + " W");
        logInfo("Valid: " + std::string(isValid() ? "Yes" : "No"));
        logInfo("==============================");
    }

    /** Get a brief summary string of the path data.
        \return Formatted string with key parameters.
    */
    std::string getSummaryString()
    {
        return "Path " + std::to_string(pathIndex) +
               ": L=" + std::to_string(pathLength) + "m" +
               ", E=" + std::to_string(emissionAngle * 180.0f / M_PI) + "°" +
               ", R=" + std::to_string(receptionAngle * 180.0f / M_PI) + "°" +
               ", Ref=" + std::to_string(reflectanceProduct) +
               ", Cnt=" + std::to_string(reflectionCount) +
               ", P=" + std::to_string(emittedPower) + "W";
    }
};

/** CIR data collection statistics.
    This structure tracks statistics about the CIR data collection process.
*/
struct CIRCollectionStats
{
    uint totalPaths;            ///< Total number of paths processed
    uint validPaths;            ///< Number of paths with valid data
    uint correctedPaths;        ///< Number of paths that required correction
    float avgPathLength;        ///< Average path length
    float minPathLength;        ///< Minimum path length observed
    float maxPathLength;        ///< Maximum path length observed
    float avgReflectanceProduct; ///< Average reflectance product
    uint maxReflections;        ///< Maximum number of reflections observed

    /** Initialize statistics with default values.
    */
    __init()
    {
        totalPaths = 0;
        validPaths = 0;
        correctedPaths = 0;
        avgPathLength = 0.0f;
        minPathLength = 1e30f;
        maxPathLength = 0.0f;
        avgReflectanceProduct = 0.0f;
        maxReflections = 0;
    }

    /** Update statistics with a new path data sample.
        \param[in] pathData The path data to include in statistics.
        \param[in] wasCorrected Whether the path data required correction.
    */
    [mutating] void updateWithPath(const CIRPathData pathData, bool wasCorrected)
    {
        totalPaths++;

        if (pathData.isValid())
        {
            validPaths++;

            // Update path length statistics
            avgPathLength = (avgPathLength * (validPaths - 1) + pathData.pathLength) / validPaths;
            minPathLength = min(minPathLength, pathData.pathLength);
            maxPathLength = max(maxPathLength, pathData.pathLength);

            // Update reflectance statistics
            avgReflectanceProduct = (avgReflectanceProduct * (validPaths - 1) + pathData.reflectanceProduct) / validPaths;

            // Update reflection count statistics
            maxReflections = max(maxReflections, pathData.reflectionCount);
        }

        if (wasCorrected)
        {
            correctedPaths++;
        }
    }

    /** Log comprehensive statistics summary.
    */
    void logStatistics()
    {
        logInfo("=== CIR Collection Statistics ===");
        logInfo("Total Paths: " + std::to_string(totalPaths));
        logInfo("Valid Paths: " + std::to_string(validPaths) + " (" +
                std::to_string(validPaths * 100.0f / max(1u, totalPaths)) + "%)");
        logInfo("Corrected Paths: " + std::to_string(correctedPaths) + " (" +
                std::to_string(correctedPaths * 100.0f / max(1u, totalPaths)) + "%)");

        if (validPaths > 0)
        {
            logInfo("Path Length - Avg: " + std::to_string(avgPathLength) + "m, " +
                    "Min: " + std::to_string(minPathLength) + "m, " +
                    "Max: " + std::to_string(maxPathLength) + "m");
            logInfo("Avg Reflectance Product: " + std::to_string(avgReflectanceProduct));
            logInfo("Max Reflections: " + std::to_string(maxReflections));
        }
        logInfo("===============================");
    }
};
