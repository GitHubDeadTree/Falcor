# IncomingLightPowerPass Force Large Output Values Modifications

This document explains the modifications made to force the `IncomingLightPowerPass` to output large values when `enable=true`, and provides instructions on how to revert these changes to restore the normal calculation logic.

## Overview of Modifications

To ensure that `IncomingLightPowerPass` outputs large non-zero values when enabled, we made changes to both the C++ implementation and the HLSL shader code. These modifications:

1. Force normalized colors to have minimum component values
2. Apply a high multiplier (20.0) to all output values
3. Ensure a minimum threshold (5.0) for all output components
4. Add debug code to verify the output values

## Detailed Modifications

### 1. In HLSL Shader (IncomingLightPowerPass.cs.slang)

#### Current Code (Modified to Force Large Values):

```hlsl
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

    // Get original radiance color components but normalize them
    float3 normalizedColor = float3(0.0f, 0.0f, 0.0f);

    // If we have any input radiance, normalize it to maintain color but force brightness
    float maxComponent = max(max(radiance.r, radiance.g), radiance.b);
    if (maxComponent > 0.0f)
    {
        normalizedColor = radiance.rgb / maxComponent;
    }
    else
    {
        // If no input radiance, create default white light
        normalizedColor = float3(1.0f, 1.0f, 1.0f);
    }

    // FORCE HIGH POWER VALUE - Always output a large value (20.0)
    const float forcedPowerValue = 20.0f;
    float3 power = normalizedColor * forcedPowerValue;

    // Ensure power is never too small
    power = max(power, float3(5.0f, 5.0f, 5.0f));

    // Return power with the wavelength
    return float4(power, wavelength > 0.0f ? wavelength : 550.0f);
}
```

#### Original Logic (How to Restore):

To restore the original calculation logic, replace the `computeLightPower` function with the following:

```hlsl
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

    // Return power with the wavelength
    return float4(power, wavelength > 0.0f ? wavelength : 550.0f);
}
```

### 2. In C++ Implementation (IncomingLightPowerPass.cpp)

#### Current Code (Modified to Force Large Values):

```cpp
float4 IncomingLightPowerPass::CameraIncidentPower::compute(
    const uint2& pixel,
    const float3& rayDir,
    const float4& radiance,
    float wavelength,
    float minWavelength,
    float maxWavelength,
    IncomingLightPowerPass::FilterMode filterMode,
    bool useVisibleSpectrumOnly,
    bool invertFilter,
    const std::vector<float>& bandWavelengths,
    const std::vector<float>& bandTolerances,
    bool enableFilter) const
{
    // FORCE HIGH POWER VALUE: Override normal calculation to ensure visibility
    // Only apply wavelength filtering if explicitly enabled
    if (enableFilter && !isWavelengthAllowed(wavelength, minWavelength, maxWavelength,
        filterMode, useVisibleSpectrumOnly, invertFilter,
        bandWavelengths, bandTolerances, enableFilter))
    {
        return float4(0.f, 0.f, 0.f, 0.f);
    }

    // Get original radiance color components but normalize them
    float3 normalizedColor = float3(1.0f, 1.0f, 1.0f);  // Default to white

    // If we have any input radiance, normalize it to maintain color but force brightness
    float maxComponent = std::max(std::max(radiance.r, radiance.g), radiance.b);
    if (maxComponent > 0.0f)
    {
        // Normalize but ensure each component is at least 0.25
        normalizedColor = float3(
            std::max(0.25f, radiance.r / maxComponent),
            std::max(0.25f, radiance.g / maxComponent),
            std::max(0.25f, radiance.b / maxComponent)
        );
    }

    // Force high power value while maintaining color balance
    const float forcedPowerValue = 20.0f;  // Increased from 10.0f to 20.0f
    float3 power = normalizedColor * forcedPowerValue;

    // Ensure power is never too small
    power.x = std::max(5.0f, power.x);
    power.y = std::max(5.0f, power.y);
    power.z = std::max(5.0f, power.z);

    // Return power with the wavelength
    return float4(power.x, power.y, power.z, wavelength > 0.0f ? wavelength : 550.0f);
}
```

#### Original Logic (How to Restore):

To restore the original calculation logic, replace the `compute` method with the following:

```cpp
float4 IncomingLightPowerPass::CameraIncidentPower::compute(
    const uint2& pixel,
    const float3& rayDir,
    const float4& radiance,
    float wavelength,
    float minWavelength,
    float maxWavelength,
    IncomingLightPowerPass::FilterMode filterMode,
    bool useVisibleSpectrumOnly,
    bool invertFilter,
    const std::vector<float>& bandWavelengths,
    const std::vector<float>& bandTolerances,
    bool enableFilter) const
{
    // Apply wavelength filtering if explicitly enabled
    if (enableFilter && !isWavelengthAllowed(wavelength, minWavelength, maxWavelength,
        filterMode, useVisibleSpectrumOnly, invertFilter,
        bandWavelengths, bandTolerances, enableFilter))
    {
        return float4(0.f, 0.f, 0.f, 0.f);
    }

    // Calculate pixel area
    float pixelArea = computePixelArea();

    // Calculate cosine term
    float cosTheta = computeCosTheta(rayDir);

    // Calculate power using the formula: Power = Radiance * Area * cos(θ)
    float3 power = float3(radiance.r, radiance.g, radiance.b) * pixelArea * cosTheta;

    // Return power with the wavelength
    return float4(power.x, power.y, power.z, wavelength > 0.0f ? wavelength : 550.0f);
}
```

### 3. Debug Code in Main Shader Function

#### Current Code (Added Debug Pixel):

```hlsl
// Debug code: Record the value of the first pixel
if (pixel.x == 0 && pixel.y == 0)
{
    // Can't print directly here, but we can set the first pixel to a special value for detection in C++ code
    // Modify to a very obvious value for observation
    power = float4(50.0, 50.0, 50.0, wavelength);
}
```

#### Restoration:

Remove this debug code block to restore normal operation for all pixels:

```hlsl
// Compute power
float4 power = computeLightPower(pixel, dimensions, rayDir, radiance, wavelength);

// Write output
gOutputPower[pixel] = power;
```

### 4. Log Message in execute Method

#### Current Code:

```cpp
// Important: Log that we're using forced high power values
logInfo("NOTICE: Using VERY HIGH FORCED power values (20.0) with minimum threshold (5.0) to ensure visibility regardless of input.");
```

#### Restoration:

Remove or update this log message:

```cpp
// Log execution settings
logInfo(fmt::format("IncomingLightPowerPass executing - settings: enabled={0}, wavelength_filter_enabled={1}, filter_mode={2}, min_wavelength={3}, max_wavelength={4}",
                   mEnabled, mEnableWavelengthFilter, static_cast<int>(mFilterMode), mMinWavelength, mMaxWavelength));
```

## Summary of Changes to Revert

To restore the normal calculation logic:

1. Replace the `computeLightPower` function in the shader with the original version that uses the physical calculation formula
2. Replace the `compute` method in the C++ code with the original version
3. Remove the debug code that forces the first pixel to have a value of 50.0
4. Update or remove the log message that mentions forced high values

After these changes, the IncomingLightPowerPass will use the proper physical formula to calculate light power:
```
Power = Radiance * Effective Area * cos(θ)
```

Rather than forcing artificially high values as it does in the current modified version.

## Expected Impact of Reverting

After reverting these changes:

1. The light power calculation will be physically based and proportional to the input radiance.
2. Outputs may be darker for scenes with low radiance values.
3. Wavelength filtering will still function as before.
4. The main difference will be that the artificial brightness boost is removed.

If after reverting, the output is too dark, consider adjusting the exposure settings in the ToneMapper pass instead of forcing artificial values in the IncomingLightPowerPass.
