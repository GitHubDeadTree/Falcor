# Irradiance Calculation Implementation for Falcor PathTracer

This document outlines a plan to implement irradiance calculation functionality in Falcor's PathTracer. The implementation will consist of three main components:

1. Modifying the `PathState` structure to preserve initial ray direction
2. Adding a new output channel to PathTracer for ray data
3. Creating a new render pass to compute irradiance from the ray data

## 1. PathState Modification

### What changes need to be made to PathState?

The `PathState` structure (defined in `PathState.slang`) needs to be extended with a new field to store the initial ray direction. The current `PathState` structure tracks the current ray direction in the `dir` field, but this gets overwritten during bounces. We need to add:

```cpp
struct PathState
{
    // Existing fields
    float3      origin;     ///< Origin of scattered ray
    float3      dir;        ///< Direction of scattered ray (unit vector)
    float       pdf;        ///< PDF for generating the scattered ray
    float3      normal;     ///< Shading normal at ray origin
    HitInfo     hit;        ///< Hit information for the scattered ray, filled when hitting a triangle

    float3      thp;        ///< Path throughput
    float3      L;          ///< Accumulated path contribution (radiance)

    // NEW FIELD
    float3      initialDir; ///< Initial ray direction from camera

    // Other existing fields and methods
    // ...
};
```

This new `initialDir` field will store the original ray direction from the camera and remain unchanged throughout the path tracing process, even as the `dir` field gets updated with each bounce.

## 2. Accessing Initial Ray Direction

The initial ray direction can be captured when the path is first generated. In `GeneratePaths.cs.slang`, when the camera ray is computed:

```cpp
void generatePath(const uint pathID, out PathState path)
{
    // ... existing code ...

    // Compute primary ray
    Ray cameraRay = gScene.camera.computeRayPinhole(pixel, params.frameDim);
    if (kUseViewDir) cameraRay.dir = -viewDir[pixel];

    // Initialize path state
    path.origin = cameraRay.origin;
    path.dir = cameraRay.dir;

    // Store initial direction - NEW ADDITION
    path.initialDir = cameraRay.dir;

    // ... existing code ...
}
```

This ensures that for each path, we store both the current ray direction (which changes with each bounce) and the initial ray direction (which remains constant).

## 3. New Output Channel in PathTracer

### Output Format

PathTracer will need a new output channel to export both the final radiance (L) and the initial ray direction. This will be defined in `PathTracer.cpp` alongside the existing output channels:

```cpp
const std::string kOutputRayData = "rayData";

const Falcor::ChannelList kOutputChannels =
{
    // Existing output channels...
    { kOutputRayData, "", "Ray data (radiance and initial direction)", true /* optional */, ResourceFormat::RGBA32Float },
    // Other existing channels...
};
```

For the ray data output, we recommend using:
- `ResourceFormat::RGBA32Float` for the radiance component (xyz) and an additional value (w)
- `ResourceFormat::RGBA16Float` for the direction component (xyz) and an additional value (w)

This follows Falcor's existing patterns for similar data types.

### How to Ensure Continuous Output of Ray Information

To ensure the PathTracer continuously outputs ray information for all paths:

1. **Persistent Buffer Creation**: The output textures for ray data will be created along with other output channels when the render pass is initialized.

2. **Per-Frame Updates**: In the `execute()` method of the PathTracer, the ray data will be written to the output channels at each frame.

3. **Shader Implementation**: Modify the `writeOutput()` function in PathTracer's shaders to write the initial ray direction and radiance to the new output channel:

```cpp
void writeOutput(const PathState path)
{
    // ... existing output writes ...

    // Write ray data to new output channel
    if (kOutputRayData)
    {
        uint2 pixel = DispatchRaysIndex().xy;
        gOutputRayRadiance[pixel] = float4(path.L, 0.0);
        gOutputRayDirection[pixel] = float4(path.initialDir, 0.0);
    }

    // ... other existing output writes ...
}
```

4. **Buffer Management**: Because PathTracer already handles the parallel processing of multiple rays on the GPU, the new output channels will naturally contain data for all processed rays, with each pixel corresponding to a different ray.

## 4. Ensuring Continuous Output

The PathTracer in Falcor runs in a massively parallel manner on the GPU, with each pixel/ray processed by a different GPU thread. To ensure continuous output of ray information:

1. **GPU-Side Storage**: The ray data (radiance and initial direction) will be written to GPU textures, which can store data for all rays simultaneously.

2. **Render Graph Integration**: By properly integrating the new output channels into Falcor's render graph, the data will be available to subsequent render passes.

3. **Frame-to-Frame Persistence**: If accumulation across frames is needed, we can use Falcor's built-in accumulation mechanisms to combine data from multiple frames.

4. **Resource Management**: Falcor's resource management system will handle the allocation and management of the GPU memory needed for these output channels.

## 5. Ray Direction for Irradiance Calculation

For calculating the irradiance on a receiver, **we do need to consider the direction of the ray**. The irradiance formula requires the cosine of the angle between the incoming ray and the receiver's normal:

$$E = \int_{\Omega} L_i(\omega) \cos\theta \, d\omega$$

When implementing this in code:

1. **Direction Matters**: The direction vector stored in `initialDir` represents the ray's direction from the camera into the scene.

2. **No Reversal Needed**: For irradiance calculation, we use this direction as-is. We don't need to reverse it because:
   - The cosine term in the irradiance integral already accounts for the angle between the incoming light direction and the receiver normal.
   - The radiance value (L) is associated with this specific direction.

3. **Cosine Weighting**: We'll multiply the incoming radiance by the cosine of the angle between the ray direction and the receiver normal:
   ```cpp
   float cosTheta = max(0.0f, dot(rayDirection, receiverNormal));
   float3 irradianceContribution = rayRadiance * cosTheta;
   ```

## 6. New Render Pass for Irradiance Calculation

### Computation Method

The new render pass will implement irradiance calculation using the Monte Carlo estimation formula from the document:

$$E \approx \frac{2\pi}{n} \sum_{i=1}^{n} L_i \cdot \cos\theta_i$$

The implementation will:

1. Take as input the ray data (radiance and initial direction) from the PathTracer.
2. Define a receiver surface with a position and normal.
3. For each pixel/ray:
   - Extract the ray's radiance (L) and initial direction.
   - Check if the ray intersects with the receiver.
   - If it does, calculate the cosine term and add the weighted contribution to the total irradiance.
4. Apply proper normalization to account for the number of samples and the solid angle.

The core computation in the shader will look like:

```cpp
float3 computeIrradiance(Texture2D rayRadiance, Texture2D rayDirection, float3 receiverPos, float3 receiverNormal)
{
    float3 totalIrradiance = float3(0.0f);
    uint sampleCount = 0;

    // Process all rays/pixels
    for (uint y = 0; y < gFrameDim.y; y++)
    {
        for (uint x = 0; x < gFrameDim.x; x++)
        {
            uint2 pixel = uint2(x, y);
            float3 L = rayRadiance[pixel].xyz;
            float3 dir = rayDirection[pixel].xyz;

            // Check if ray intersects receiver
            // This is a simplified test; actual implementation would be more complex
            if (isRayHittingReceiver(dir, receiverPos))
            {
                // Calculate cosine term
                float cosTheta = max(0.0f, dot(dir, receiverNormal));

                // Add weighted contribution
                totalIrradiance += L * cosTheta;
                sampleCount++;
            }
        }
    }

    // Apply Monte Carlo normalization
    // The factor 2π comes from the hemisphere integral normalization
    if (sampleCount > 0)
    {
        totalIrradiance *= (2.0f * PI) / float(sampleCount);
    }

    return totalIrradiance;
}
```

### Integration with Falcor

The new render pass will:

1. Declare inputs for the ray data from PathTracer.
2. Define parameters for the receiver position and normal.
3. Have an output for the calculated irradiance.
4. Implement the Execute method to perform the calculation.

## 7. Irradiance Output

### Output Format

The irradiance calculation render pass will output:

1. **Total Irradiance**: A single float3 value representing the total irradiance on the receiver.
2. **Irradiance Map**: Optionally, a texture showing the spatial distribution of irradiance on the receiver surface.

### Storage Location

The irradiance output will be stored in:

1. **GPU Texture**: As a standard Falcor output texture that can be displayed or used by other render passes.
2. **CPU Accessible Buffer**: For analysis or export to file, the irradiance can be copied to a buffer that's accessible from the CPU.
3. **File Output**: Optionally, the irradiance values can be exported to CSV or image files for offline analysis.

The specific output location can be configured via the render pass's UI, allowing flexibility in how the data is used.

## Additional Considerations

### Performance Optimization

1. **Receiver Culling**: Implement early culling of rays that won't hit the receiver to avoid unnecessary computations.
2. **Spatial Acceleration**: Use a spatial data structure to quickly identify which rays might hit the receiver.
3. **Multi-Frame Accumulation**: Allow accumulating irradiance over multiple frames to reduce noise.

### Validation and Visualization

1. **Debug Visualization**: Add visualization modes to debug the irradiance calculation.
2. **Ground Truth Comparison**: Compare calculated irradiance with analytical solutions for simple scenes.
3. **Interactive Parameters**: Allow real-time adjustment of receiver parameters to explore different configurations.

### Integration with VLC Systems

For visible light communication (VLC) applications:

1. **Temporal Analysis**: Track irradiance changes over time for temporal signal analysis.
2. **Spectral Response**: Consider the spectral response of the receiver for more accurate VLC modeling.
3. **Signal Processing**: Add signal processing capabilities to extract communication data from the irradiance patterns.

## Implementation Phases

1. **Phase 1**: Modify PathState to store initial ray direction.
2. **Phase 2**: Add new output channels to PathTracer for ray data.
3. **Phase 3**: Create the irradiance calculation render pass.
4. **Phase 4**: Add visualization and analysis tools.
5. **Phase 5**: Optimize performance for large scenes.

By following this implementation plan, you'll be able to accurately calculate the irradiance at a receiver surface based on the path-traced scene, which is valuable for VLC system simulation, lighting design, and other applications requiring accurate light transport modeling.
