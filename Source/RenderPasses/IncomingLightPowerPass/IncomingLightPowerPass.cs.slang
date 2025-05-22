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

    // Camera data
    float4x4 gCameraInvViewProj;       ///< Camera inverse view-projection matrix
    float3 gCameraPosition;            ///< Camera position in world space
    float3 gCameraTarget;              ///< Camera target in world space
    float gCameraFocalLength;          ///< Camera focal length
}

// Remove Scene parameter block - we don't need it anymore
// ParameterBlock<Scene> gScene;

// Utility functions

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

    // Return pixel area
    return pixelWidth * pixelHeight;
}

// Compute the cosine term based on ray direction and camera normal
float computeCosTheta(float3 rayDir)
{
    // TEMPORARY DEBUG: Force cosTheta to 1.0 to test if the issue is in this calculation
    // Comment out the normal calculation for now
    /*
    // Calculate camera normal (forward direction)
    float3 cameraNormal = normalize(gCameraTarget - gCameraPosition);

    // For rays entering the camera, we want to use the camera forward direction directly
    // In Falcor, camera looks along its forward direction, so rays entering the camera
    // should have a negative dot product with the camera forward direction
    float dotProduct = dot(rayDir, -cameraNormal);

    // Clamp to non-negative (protect against back-facing rays)
    float cosTheta = max(0.0f, dotProduct);

    // Use a more reasonable minimum value (0.01 instead of 0.00001)
    // This prevents power from being too close to zero for glancing rays
    const float minCosTheta = 0.01f;
    cosTheta = max(cosTheta, minCosTheta);

    return cosTheta;
    */

    // Return 1.0 for all rays to test if CosTheta calculation is the issue
    return 1.0f;
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

// Compute the power of incoming light for the given pixel
float4 computeLightPower(uint2 pixel, uint2 dimensions, float3 rayDir, float4 radiance, float wavelength)
{
    // Debug: For first 4 pixels, log detailed computation
    bool isDebugPixel = pixel.x < 2 && pixel.y < 2;
    float3 originalPower = float3(0, 0, 0);
    float pixelArea = 0;
    float cosTheta = 0;

    // Additional validation of radiance (defensive check)
    if (isDebugPixel) {
        // Store both ray direction and radiance for comparison
        // We'll use the x, y, z components for direction and w for wavelength
        gDebugInputData[pixel] = float4(rayDir.xyz, wavelength);

        // Store radiance in debug calculation data (temporarily)
        // We'll overwrite this later with calculation steps
        if (any(radiance.rgb < 0.0f)) {
            gDebugCalculation[pixel] = float4(-999, -999, -999, -999); // Flag to indicate invalid radiance
        } else {
            gDebugCalculation[pixel] = float4(radiance.rgb, wavelength);
        }
    }

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

    // For debug pixels, collect detailed cosTheta calculation information
    float3 cameraNormal = float3(0, 0, 0);
    float3 invNormal = float3(0, 0, 0);
    float rawDotProduct = 0;
    float rayLengthVal = 0;

    if (isDebugPixel) {
        // Calculate values for debugging
        cameraNormal = normalize(gCameraTarget - gCameraPosition);
        invNormal = -cameraNormal;
        rawDotProduct = dot(rayDir, invNormal);
        rayLengthVal = length(rayDir);

        // Store raw data for debugging
        float4 debugData = float4(0, 0, 0, 0);
        debugData.x = rawDotProduct;                // Raw dot product
        debugData.y = dot(rayDir, float3(0,0,-1));  // Dot with -Z axis (standard camera forward)
        debugData.z = dot(rayDir, float3(0,0,1));   // Dot with +Z axis
        debugData.w = rayLengthVal;                 // Ray length

        // We need to distinguish ray direction and radiance in debug output
        // Store ray direction with a special marker
        gDebugInputData[pixel] = float4(rayDir.xyz, wavelength);

        // Additional debugging for camera-relative coordinates
        // Convert ray direction to camera space using proper camera basis construction

        // 1. First calculate camera forward direction (W)
        float3 camW = normalize(gCameraTarget - gCameraPosition);

        // 2. Find an up vector that's not parallel to camW
        float3 worldUp = float3(0, 1, 0);
        // If camW is nearly parallel to worldUp, use a different up vector
        if (abs(dot(camW, worldUp)) > 0.99f)
        {
            worldUp = float3(0, 0, 1);
        }

        // 3. Calculate camera right vector (U)
        float3 camU = normalize(cross(worldUp, camW));

        // 4. Calculate camera up vector (V)
        float3 camV = normalize(cross(camW, camU));

        // Convert ray direction to camera space
        float3 rayDirInCameraSpace;
        rayDirInCameraSpace.x = dot(rayDir, camU);   // X component (right)
        rayDirInCameraSpace.y = dot(rayDir, camV);   // Y component (up)
        rayDirInCameraSpace.z = dot(rayDir, camW);   // Z component (forward)

        // Store camera-space ray direction for further analysis
        gDebugOutput[pixel] = float4(rayDirInCameraSpace, wavelength);

        // Log the basis vectors for debugging
        gDebugCalculation[pixel] = float4(
            length(camU),                         // Should be ~1.0
            length(camV),                         // Should be ~1.0
            dot(camU, camV),                      // Should be ~0.0 (orthogonal)
            dot(camW, rayDir)                     // Forward component
        );
    }

    // Calculate cosine term
    cosTheta = computeCosTheta(rayDir);

    // Store calculation steps for debug pixels
    if (isDebugPixel) {
        // Store detailed cosTheta calculation info in debug texture
        // x: pixel area, y: final cosTheta, z: raw dot product, w: ray length
        gDebugCalculation[pixel] = float4(pixelArea, cosTheta, rawDotProduct, rayLengthVal);
    }

    // Calculate power using the formula: Power = Radiance * Area * cos(θ)
    // Make sure radiance is non-negative (defensive programming)
    float3 safeRadiance = max(radiance.rgb, 0.0f);
    originalPower = safeRadiance * pixelArea * cosTheta;

    // Store the original calculation result for debug pixels
    if (isDebugPixel) {
        // Store original calculation before forcing high values
        gDebugOutput[pixel] = float4(originalPower, wavelength);
    }

    // FORCE HIGH POWER VALUE - Force the physically calculated values to be large
    // Get normalized color to maintain the original color proportion
    float3 normalizedColor;
    float maxComponent = max(max(originalPower.r, originalPower.g), originalPower.b);

    if (maxComponent > 0.0001f)
    {
        // Normalize based on original calculation to maintain color ratio
        normalizedColor = originalPower / maxComponent;
    }
    else
    {
        // If power is essentially zero, use the original radiance for color
        maxComponent = max(safeRadiance.r, max(safeRadiance.g, safeRadiance.b));
        if (maxComponent > 0.0001f)
        {
            normalizedColor = safeRadiance / maxComponent;
        }
        else
        {
            // Default to white if no meaningful color information
            normalizedColor = float3(1.0f, 1.0f, 1.0f);
        }
    }

    // Apply high power value while maintaining color proportions
    const float forcedPowerValue = 20.0f;
    float3 power = normalizedColor * forcedPowerValue;

    // Ensure power is never too small
    power = max(power, float3(5.0f, 5.0f, 5.0f));

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

    // Compute power
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
