/*
    IncomingLightPowerPass.cs.slang

    This compute shader calculates the power of light rays entering the camera,
    with an option to filter by wavelength range.

    Power calculation is based on the formula:
    Power = Radiance * Effective Area * cos(θ)

    Where:
    - Radiance is the input ray intensity
    - Effective Area is the pixel area on the camera sensor
    - cos(θ) is the angle between the ray and the camera normal
*/

// Remove Scene imports as they are causing compilation issues
// We will rely on our constant buffer for camera data instead

// Define constants for filter modes
#ifndef FILTER_MODE
#define FILTER_MODE 0  // Default to Range mode
#endif

#ifndef USE_VISIBLE_SPECTRUM_ONLY
#define USE_VISIBLE_SPECTRUM_ONLY 0
#endif

#ifndef INVERT_FILTER
#define INVERT_FILTER 0
#endif

#ifndef ENABLE_WAVELENGTH_FILTER
#define ENABLE_WAVELENGTH_FILTER 1
#endif

#ifndef SPECIFIC_BANDS
#define SPECIFIC_BANDS 0
#endif

#ifndef MAX_BANDS
#define MAX_BANDS 16  // Maximum number of specific bands
#endif

// Filter mode enum
static const uint FILTER_MODE_RANGE = 0;
static const uint FILTER_MODE_SPECIFIC_BANDS = 1;
static const uint FILTER_MODE_CUSTOM = 2;

// Input data
Texture2D<float4> gInputRadiance;       ///< Input radiance from path tracer
Texture2D<float3> gInputRayDirection;   ///< Optional input ray direction
Texture2D<float> gInputWavelength;      ///< Optional input wavelength information

// Output data
RWTexture2D<float4> gOutputPower;       ///< Output power value (rgb) and wavelength (a)
RWTexture2D<float> gOutputWavelength;   ///< Output wavelength (for filtered rays)
RWTexture2D<float4> gDebugOutput;       ///< Debug output for original calculation (without forcing large values)
RWTexture2D<float4> gDebugInputData;    ///< Debug output for input data
RWTexture2D<float4> gDebugCalculation;  ///< Debug output for calculation steps

// Photodetector analysis buffer
RWStructuredBuffer<uint4> gClassificationBuffer; ///< Buffer for binning results [wavelengthBin, angleBin, powerBits, validFlag]

// Parameters
cbuffer PerFrameCB
{
    float gMinWavelength;              ///< Minimum wavelength to consider (nm)
    float gMaxWavelength;              ///< Maximum wavelength to consider (nm)
    bool gUseVisibleSpectrumOnly;      ///< Whether to only consider visible light spectrum (380-780nm)
    bool gInvertFilter;                ///< Whether to invert the wavelength filter
    bool gEnableWavelengthFilter;      ///< Whether to enable wavelength filtering at all
    uint gFilterMode;                  ///< Wavelength filtering mode (0=Range, 1=Specific Bands, 2=Custom)
    uint gBandCount;                   ///< Number of specific bands to filter
    float gBandWavelengths[MAX_BANDS]; ///< Center wavelengths for specific bands
    float gBandTolerances[MAX_BANDS];  ///< Tolerances for specific wavelength bands
    float gPixelAreaScale;             ///< Scale factor for pixel area (default: 1000.0)

    // Photodetector analysis parameters
    bool gEnablePhotodetectorAnalysis; ///< Enable photodetector analysis
    float gDetectorArea;               ///< Photodetector effective area in m²
    float gSourceSolidAngle;           ///< Source solid angle in sr
    uint gCurrentNumRays;              ///< Current number of rays

    // Camera data
    float4x4 gCameraInvViewProj;       ///< Camera inverse view-projection matrix
    float3 gCameraPosition;            ///< Camera position in world space
    float3 gCameraTarget;              ///< Camera target in world space
    float gCameraFocalLength;          ///< Camera focal length
}

// Remove Scene parameter block - we don't need it anymore
// ParameterBlock<Scene> gScene;

// Utility functions

// High precision binning functions for photodetector analysis

// Find wavelength bin (380-780nm, 1nm precision, 400 bins total)
uint findWavelengthBin(float wavelength)
{
    if (wavelength < 380.0f || wavelength > 780.0f)
        return 0xFFFFFFFF; // Invalid bin marker

    uint bin = uint(wavelength - 380.0f);  // 1nm precision
    return min(bin, 399); // Clamp to 400 bins (0-399)
}

// Find angle bin (0-90°, 1deg precision, 90 bins total)
uint findAngleBin(float angleDeg)
{
    if (angleDeg < 0.0f || angleDeg > 90.0f)
        return 0xFFFFFFFF; // Invalid bin marker

    uint bin = uint(angleDeg);  // 1deg precision
    return min(bin, 89); // Clamp to 90 bins (0-89)
}

// Calculate incident angle from ray direction (reuses existing computeCosTheta method)
float calculateIncidentAngle(float3 rayDir)
{
    // Use existing computeCosTheta function for consistency
    float cosTheta = computeCosTheta(rayDir);
    return acos(cosTheta) * 180.0f / 3.14159f; // Convert to degrees
}

// Error checking function for binning
bool isValidBin(uint wavelengthBin, uint angleBin)
{
    return (wavelengthBin != 0xFFFFFFFF && angleBin != 0xFFFFFFFF);
}

// Log binning error (only for debug pixels to avoid spam)
void logBinningError(float wavelength, float angle, uint2 pixel)
{
    // Only log errors occasionally to avoid spam
    if ((pixel.x + pixel.y) % 1000 == 0) {
        gDebugOutput[pixel] = float4(0.666f, wavelength, angle, 1.0f);
    }
}

// Check if wavelength is in the visible spectrum range (380-780nm)
bool isInVisibleSpectrum(float wavelength)
{
    return (wavelength >= 380.0f && wavelength <= 780.0f);
}

// Check if wavelength is in a specific band
bool isInSpecificBand(float wavelength, float bandCenter, float tolerance)
{
    return abs(wavelength - bandCenter) <= tolerance;
}

// Check if wavelength is in any of the specified bands
bool isInAnyBand(float wavelength)
{
    for (uint i = 0; i < min(gBandCount, MAX_BANDS); i++)
    {
        if (isInSpecificBand(wavelength, gBandWavelengths[i], gBandTolerances[i]))
        {
            return true;
        }
    }
    return false;
}

// Check if wavelength is in the specified range
bool isInWavelengthRange(float wavelength)
{
    return (wavelength >= gMinWavelength && wavelength <= gMaxWavelength);
}

// Apply custom filter (placeholder for future extension)
bool passesCustomFilter(float wavelength)
{
    // Default implementation falls back to range filter
    return isInWavelengthRange(wavelength);
}

// Check if wavelength is allowed by the filter
bool isWavelengthAllowed(float wavelength)
{
    // If filtering is completely disabled, allow all wavelengths
    if (!gEnableWavelengthFilter)
    {
        return true;
    }

    // First check visible spectrum if that option is enabled
    if (gUseVisibleSpectrumOnly && !isInVisibleSpectrum(wavelength))
    {
        return gInvertFilter; // If inverting, allow wavelengths outside visible spectrum
    }

    bool allowed = false;

    // Apply filter based on mode
    switch (gFilterMode)
    {
    case FILTER_MODE_RANGE:
        allowed = isInWavelengthRange(wavelength);
        break;

    case FILTER_MODE_SPECIFIC_BANDS:
        if (gBandCount > 0)
        {
            allowed = isInAnyBand(wavelength);
        }
        else
        {
            // Fall back to range mode if no bands specified
            allowed = isInWavelengthRange(wavelength);
        }
        break;

    case FILTER_MODE_CUSTOM:
        allowed = passesCustomFilter(wavelength);
        break;

    default:
        // Default to range mode
        allowed = isInWavelengthRange(wavelength);
        break;
    }

    // Apply filter inversion if enabled
    return gInvertFilter ? !allowed : allowed;
}

// Compute ray direction from pixel position (used if ray direction texture is not available)
float3 computeRayDirection(uint2 pixel, uint2 dimensions)
{
    // Calculate normalized device coordinates
    float2 pixelCenter = float2(pixel) + 0.5f;
    float2 ndc = pixelCenter / float2(dimensions) * 2.0f - 1.0f;

    // Generate ray direction using camera view-projection matrix
    float4 viewPos = float4(ndc.x, -ndc.y, 1.0f, 1.0f);
    float4 worldPos = mul(gCameraInvViewProj, viewPos);
    worldPos /= worldPos.w;

    float3 origin = gCameraPosition;
    float3 rayDir = normalize(float3(worldPos.xyz) - origin);

    return rayDir;
}

// Compute pixel area on the image sensor
float computePixelArea(uint2 dimensions)
{
    // Calculate FOV and aspect ratio
    float fovY = gCameraFocalLength;  // Camera focal length in Falcor units
    float aspectRatio = float(dimensions.x) / float(dimensions.y);

    // Use normalized distance to image plane
    float distToImagePlane = 1.0f;

    // Calculate sensor dimensions
    float sensorHeight = 2.0f * distToImagePlane * tan(fovY * 0.5f);
    float sensorWidth = sensorHeight * aspectRatio;

    // Calculate pixel dimensions
    float pixelWidth = sensorWidth / float(dimensions.x);
    float pixelHeight = sensorHeight / float(dimensions.y);

    // Return pixel area with scaling factor
    return pixelWidth * pixelHeight * gPixelAreaScale;
}

// Define a debug structure to store cosTheta computation details
struct CosThetaDebugInfo
{
    float3 cameraNormal;     // Camera normal vector
    float3 rayDirection;     // Ray direction vector
    float dotProduct;        // Dot product between ray direction and inverse camera normal
    float cosTheta;          // Final cosTheta value
    float minValue;          // Minimum cosTheta value used
    float3 invNormal;        // Inverse camera normal
    float rayLength;         // Length of ray direction vector
};

// Compute the cosine term based on ray direction and camera normal
float computeCosTheta(float3 rayDir, out CosThetaDebugInfo debugInfo)
{
    // Calculate camera normal (forward direction)
    float3 cameraNormal = normalize(gCameraTarget - gCameraPosition);

    // For rays entering the camera, we want to use the inverse normal
    float3 invNormal = -cameraNormal;

    // Calculate dot product
    float dotProduct = dot(rayDir, invNormal);

    // Clamp to non-negative (protect against back-facing rays)
    float cosTheta = max(0.0f, dotProduct);

    // Use a more reasonable minimum value (0.01 instead of 0.00001)
    // This prevents power from being too close to zero for glancing rays
    const float minCosTheta = 0.01f;
    float finalCosTheta = max(cosTheta, minCosTheta);

    // Store debug information
    debugInfo.cameraNormal = cameraNormal;
    debugInfo.rayDirection = rayDir;
    debugInfo.dotProduct = dotProduct;
    debugInfo.cosTheta = cosTheta;
    debugInfo.minValue = minCosTheta;
    debugInfo.invNormal = invNormal;
    debugInfo.rayLength = length(rayDir);

    return finalCosTheta;
}

// Overloaded version without debug info for non-debug calls
float computeCosTheta(float3 rayDir)
{
    // Temporary debug info that will be discarded
    CosThetaDebugInfo tempDebugInfo;
    return computeCosTheta(rayDir, tempDebugInfo);
}

// Compute dominant wavelength from RGB color if wavelength data is not provided
// This uses a simple approximation based on the highest RGB component
float estimateWavelengthFromRGB(float3 rgb)
{
    // Find the dominant color component
    if (rgb.r >= rgb.g && rgb.r >= rgb.b)
    {
        // Red dominant - map to 620-700nm
        return 620.0f + 80.0f * (1.0f - min(1.0f, rgb.g / max(0.01f, rgb.r)));
    }
    else if (rgb.g >= rgb.r && rgb.g >= rgb.b)
    {
        // Green dominant - map to 520-560nm
        return 520.0f + 40.0f * (1.0f - min(1.0f, rgb.b / max(0.01f, rgb.g)));
    }
    else
    {
        // Blue dominant - map to 450-490nm
        return 450.0f + 40.0f * (1.0f - min(1.0f, rgb.g / max(0.01f, rgb.b)));
    }
}

// Function to convert vectors to RGB colors for visualization
float4 visualizeVectors(float3 rayDir, float3 cameraNormal, float cosTheta)
{
    // Convert camera normal to a blueish color
    float3 normalColor = float3(0.2, 0.4, 1.0) * (cameraNormal * 0.5 + 0.5);

    // Convert ray direction to a reddish color
    float3 rayColor = float3(1.0, 0.3, 0.2) * (rayDir * 0.5 + 0.5);

    // Mix colors based on cosTheta
    // When cosTheta is close to 1, the ray direction is close to opposite of camera normal
    // so the color should be more intense (good alignment)
    float alignment = saturate(cosTheta);
    float3 finalColor = lerp(normalColor * 0.5, rayColor, alignment);

    // Return color with cosTheta as alpha
    return float4(finalColor, cosTheta);
}

// Compute the power of incoming light for the given pixel
float4 computeLightPower(uint2 pixel, uint2 dimensions, float3 rayDir, float4 radiance, float wavelength)
{
    // Debug: For first 4 pixels, log detailed computation
    bool isDebugPixel = pixel.x < 2 && pixel.y < 2;
    float pixelArea = 0;
    float cosTheta = 0;
    CosThetaDebugInfo cosThetaDebug;

    // Make sure radiance is non-negative (defensive programming)
    float3 safeRadiance = max(radiance.rgb, 0.0f);

    // Apply wavelength filtering if enabled
    // Only check wavelength filtering if the global control is enabled
    if (gEnableWavelengthFilter)
    {
        // Check if the wavelength passes the filter
        if (!isWavelengthAllowed(wavelength))
        {
            if (isDebugPixel) {
                // Store debug info for filtered out wavelength
                gDebugOutput[pixel] = float4(-1, -1, -1, wavelength);
            }
            return float4(0, 0, 0, 0);
        }
    }

    // Calculate pixel area
    pixelArea = computePixelArea(dimensions);

    // Calculate cosine term with debug info for debug pixels
    if (isDebugPixel) {
        cosTheta = computeCosTheta(rayDir, cosThetaDebug);

        // Store detailed cosTheta calculation info in debug textures
        // Each debug texture stores different parts of the calculation

        // Debug Input Data: Store ray direction and its length
        gDebugInputData[pixel] = float4(rayDir.xyz, cosThetaDebug.rayLength);

        // Debug Output: Store camera normal and wavelength
        gDebugOutput[pixel] = float4(cosThetaDebug.cameraNormal, wavelength);

        // Debug Calculation: Store cosTheta calculation details
        gDebugCalculation[pixel] = float4(
            cosThetaDebug.dotProduct,           // Raw dot product
            cosThetaDebug.cosTheta,             // Before min clamp
            cosTheta,                           // Final value (after min clamp)
            pixelArea                           // Pixel area for power calculation
        );

        // Special debug for first pixel (0,0)
        if (pixel.x == 0 && pixel.y == 0) {
            // Calculate power values for the RGB components
            float rPower = safeRadiance.r * pixelArea * cosTheta;
            float gPower = safeRadiance.g * pixelArea * cosTheta;
            float bPower = safeRadiance.b * pixelArea * cosTheta;

            // Store in a special debug texture slot - we'll use gDebugInputData for pixel (1,1)
            // This avoids overwriting the camera normal information
            uint2 specialPixel = uint2(1, 1);
            gDebugInputData[specialPixel] = float4(rPower, gPower, bPower, wavelength);

            // For the first pixel, also store the calculation components
            // in a special debug texture for verification
            gDebugCalculation[pixel] = float4(
                safeRadiance.r,              // R component of radiance
                pixelArea,                   // Pixel area
                cosTheta,                    // Final cosTheta value
                rPower                       // R component of calculated power
            );
        }
    }
    else {
        // Normal calculation without debug info
        cosTheta = computeCosTheta(rayDir);
    }

    // Calculate power using the formula: Power = Radiance * Area * cos(θ)
    float3 power = safeRadiance * pixelArea * cosTheta;

    // Return power with the wavelength
    return float4(power, wavelength > 0.0f ? wavelength : 550.0f);
}

[numthreads(16, 16, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    uint2 pixel = dispatchThreadId.xy;

    // Get dimensions safely
    uint width, height;
    gOutputPower.GetDimensions(width, height);
    uint2 dimensions = uint2(width, height);

    // Skip if outside texture bounds
    if (any(pixel >= dimensions)) return;

    // Debug information for first 4 pixels
    bool isDebugPixel = pixel.x < 2 && pixel.y < 2;

    // Get input radiance from path tracer
    float4 radiance = gInputRadiance[pixel];

    // FIX: Validate radiance input - radiance should never be negative
    // Check if we have negative values, which indicates the input might be a direction vector
    bool hasNegativeValues = any(radiance.rgb < 0.0f);
    bool isLikelyDirection = length(radiance.rgb) > 0.5f && length(radiance.rgb) < 1.5f;

    if (isDebugPixel && (hasNegativeValues || isLikelyDirection)) {
        // Store original values for debugging
        gDebugInputData[pixel] = float4(radiance.rgb, 0); // Store original input
    }

    // IMPORTANT FIX: Ensure radiance values are valid (non-negative)
    // If radiance contains negative values (likely a direction vector was passed incorrectly),
    // replace it with a proper radiance value
    if (hasNegativeValues || isLikelyDirection) {
        // Option 1: Use absolute values if it looks like a valid color
        if (max(abs(radiance.r), max(abs(radiance.g), abs(radiance.b))) < 10.0f) {
            radiance.rgb = abs(radiance.rgb);
        }
        // Option 2: If values look like normalized direction vectors, use a default color
        else {
            // Use white as default radiance
            radiance.rgb = float3(1.0f, 1.0f, 1.0f);
        }
    }

    // Debug: Store corrected radiance values
    if (isDebugPixel) {
        // Store in a second debug slot if we have one
        // Or overwrite the original with corrected value
        gDebugInputData[pixel] = float4(radiance.rgb, 1.0f); // Store corrected input
    }

    // Get ray direction (either from texture or compute it)
    float3 rayDir;

    // Check if ray direction texture is bound
    uint rdWidth, rdHeight;
    gInputRayDirection.GetDimensions(rdWidth, rdHeight);
    if (rdWidth > 0 && rdHeight > 0)
    {
        rayDir = gInputRayDirection[pixel];
    }
    else
    {
        rayDir = computeRayDirection(pixel, dimensions);
    }

    // Debug: Store ray direction
    if (isDebugPixel) {
        // Later we'll combine this with the results
    }

    // Get wavelength (default to estimation from RGB if not provided)
    float wavelength = 550.0f; // Default middle of visible spectrum

    // Check if wavelength texture is bound
    uint wlWidth, wlHeight;
    gInputWavelength.GetDimensions(wlWidth, wlHeight);
    if (wlWidth > 0 && wlHeight > 0)
    {
        wavelength = gInputWavelength[pixel];

        // If wavelength is 0 (not determined in path tracer), estimate from RGB
        if (wavelength == 0.0f && any(radiance.rgb > 0.0f))
        {
            wavelength = estimateWavelengthFromRGB(radiance.rgb);
        }
    }
    else if (any(radiance.rgb > 0.0f))
    {
        // If no wavelength texture provided, estimate from RGB
        wavelength = estimateWavelengthFromRGB(radiance.rgb);
    }

    // Photodetector analysis (if enabled)
    if (gEnablePhotodetectorAnalysis && any(radiance.rgb > 0))
    {
        // Use existing angle calculation method for consistency
        float cosTheta = computeCosTheta(rayDir);
        float incidentAngle = calculateIncidentAngle(rayDir);

        // Key correction: Use PD physical area instead of pixel area
        float deltaOmega = gSourceSolidAngle / float(gCurrentNumRays);
        float3 pdPower = radiance.rgb * gDetectorArea * cosTheta * deltaOmega;

        // Perform high-precision binning
        uint wavelengthBin = findWavelengthBin(wavelength);
        uint angleBin = findAngleBin(incidentAngle);

        if (isValidBin(wavelengthBin, angleBin))
        {
            float totalPower = pdPower.x + pdPower.y + pdPower.z;

            // Store classification data in buffer for CPU processing
            uint index = pixel.y * dimensions.x + pixel.x;
            gClassificationBuffer[index] = uint4(
                wavelengthBin,           // Wavelength bin index (0-399)
                angleBin,                // Angle bin index (0-89)
                asuint(totalPower),      // Total power as uint bits
                1                        // Valid flag
            );
        }
        else
        {
            // Record binning error
            logBinningError(wavelength, incidentAngle, pixel);

            // Mark as invalid in classification buffer
            uint index = pixel.y * dimensions.x + pixel.x;
            gClassificationBuffer[index] = uint4(0xFFFFFFFF, 0xFFFFFFFF, 0, 0);
        }
    }

    // Existing image sensor power calculation (preserved for compatibility)
    float4 power = computeLightPower(pixel, dimensions, rayDir, radiance, wavelength);

    // Write output
    gOutputPower[pixel] = power;

    // Write wavelength output (only for rays that pass the filter)
    if (power.w > 0)
    {
        gOutputWavelength[pixel] = power.w; // Store wavelength in output
    }
    else
    {
        gOutputWavelength[pixel] = 0; // Zero for filtered-out wavelengths
    }
}
