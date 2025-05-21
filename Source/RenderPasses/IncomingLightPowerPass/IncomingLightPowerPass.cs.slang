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
    // Calculate camera normal (forward direction)
    float3 cameraNormal = normalize(gCameraTarget - gCameraPosition);

    // For rays entering the camera, use inverse normal
    float3 invNormal = -cameraNormal;

    // Compute cosine using dot product (clamp to non-negative)
    return max(0.0f, dot(rayDir, invNormal));
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
    // Apply wavelength filtering if enabled
    // Only check wavelength filtering if the global control is enabled
    if (gEnableWavelengthFilter)
    {
        // Check if the wavelength passes the filter
        if (!isWavelengthAllowed(wavelength))
        {
            return float4(0, 0, 0, 0);
        }
    }

    // Calculate pixel area
    float pixelArea = computePixelArea(dimensions);

    // Calculate cosine term
    float cosTheta = computeCosTheta(rayDir);

    // Calculate power using the formula: Power = Radiance * Area * cos(θ)
    float3 power = radiance.rgb * pixelArea * cosTheta;

    return float4(power, wavelength);
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

    // Get input radiance from path tracer
    float4 radiance = gInputRadiance[pixel];

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
