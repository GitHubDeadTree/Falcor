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

// Scene imports for accessing camera parameters
import Scene.Scene;
import Scene.Camera;

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
}

// Scene data (bound by the host code)
ParameterBlock<Scene> gScene;           ///< Scene data for accessing camera information

// Utility functions

// Check if wavelength is in the specified range
bool isWavelengthInRange(float wavelength)
{
    return (wavelength >= gMinWavelength && wavelength <= gMaxWavelength);
}

// Compute ray direction from pixel position (used if ray direction texture is not available)
float3 computeRayDirection(uint2 pixel, uint2 dimensions)
{
    // Get camera
    Camera camera = gScene.getCamera();

    // Calculate normalized device coordinates
    float2 pixelCenter = float2(pixel) + 0.5f;
    float2 ndc = pixelCenter / float2(dimensions) * 2.0f - 1.0f;

    // Get camera parameters
    float4x4 invViewProj = camera.getInverseViewProj();
    float3 cameraPos = camera.getPosition();

    // Generate world position on the far plane
    float4 worldPos = mul(invViewProj, float4(ndc.x, -ndc.y, 1.0f, 1.0f));
    worldPos /= worldPos.w;

    // Calculate ray direction
    return normalize(float3(worldPos.xyz) - cameraPos);
}

// Compute pixel area on the image sensor
float computePixelArea(uint2 dimensions)
{
    // Get camera
    Camera camera = gScene.getCamera();

    // Get horizontal FOV in radians
    float hFOV = camera.getFocalLength();  // This is already in radians in slang

    // Use normalized distance to image plane
    float distToImagePlane = 1.0f;

    // Calculate sensor dimensions
    float sensorWidth = 2.0f * distToImagePlane * tan(hFOV * 0.5f);
    float aspectRatio = float(dimensions.x) / float(dimensions.y);
    float sensorHeight = sensorWidth / aspectRatio;

    // Calculate pixel dimensions
    float pixelWidth = sensorWidth / float(dimensions.x);
    float pixelHeight = sensorHeight / float(dimensions.y);

    // Return pixel area
    return pixelWidth * pixelHeight;
}

// Compute the cosine term based on ray direction and camera normal
float computeCosTheta(float3 rayDir)
{
    // Get camera
    Camera camera = gScene.getCamera();

    // Get camera forward direction (normal to image plane)
    float3 cameraNormal = camera.getForwardDir();

    // For rays entering the camera, use inverse normal
    float3 invNormal = -cameraNormal;

    // Compute cosine using dot product (clamp to non-negative)
    return max(0.0f, dot(rayDir, invNormal));
}

// Compute the power of incoming light for the given pixel
float4 computeLightPower(uint2 pixel, uint2 dimensions, float3 rayDir, float4 radiance, float wavelength)
{
    // Filter by wavelength range
    if (!isWavelengthInRange(wavelength))
    {
        return float4(0, 0, 0, 0);
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
    uint2 dimensions = uint2(gOutputPower.GetDimensions());

    // Skip if outside texture bounds
    if (any(pixel >= dimensions)) return;

    // Get input radiance
    float4 radiance = gInputRadiance[pixel];

    // Get ray direction (either from texture or compute it)
    float3 rayDir;
    if (gInputRayDirection.GetDimensions().x > 0) // Check if texture is bound
    {
        rayDir = gInputRayDirection[pixel];
    }
    else
    {
        rayDir = computeRayDirection(pixel, dimensions);
    }

    // Get wavelength (default to 550nm if not provided)
    float wavelength = 550.0f; // Default middle of visible spectrum
    if (gInputWavelength.GetDimensions().x > 0) // Check if texture is bound
    {
        wavelength = gInputWavelength[pixel];
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
