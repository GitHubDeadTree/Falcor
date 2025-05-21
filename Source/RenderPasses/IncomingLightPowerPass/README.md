# Incoming Light Power Pass

This render pass calculates the power of light rays entering the camera, with wavelength filtering capabilities. It can be used for special effects, light analysis, and scientific visualizations where specific wavelengths of light need to be isolated.

## Features

- Calculate incoming light power for each pixel
- Filter light by wavelength ranges
- Filter light by specific spectral bands
- Spectral analysis of incoming light
- Integration with PathTracer for accurate light simulation

## Usage

The Incoming Light Power Pass takes radiance information (typically from a path tracer), optionally with wavelength and ray direction data, and calculates the power of the incoming light for each pixel, with the ability to filter by wavelength.

### Basic Parameters

- **Min/Max Wavelength**: Define the range of wavelengths to consider (in nanometers)
- **Filter Mode**: Choose between Range (min-max), Specific Bands, or Custom filtering
- **Visible Spectrum Only**: Restrict processing to visible light (380nm-780nm)
- **Invert Filter**: Process wavelengths outside the defined range/bands instead

### Example Render Graph

Here's an example of how to use the IncomingLightPowerPass with PathTracer:

```python
from falcor import *

def render_graph_IncomingLightPowerExample():
    g = RenderGraph("Incoming Light Power Example")

    # Create passes
    VBufferRT = createPass("VBufferRT", {"samplePattern": "Stratified", "sampleCount": 16})
    PathTracer = createPass("PathTracer", {"samplesPerPixel": 1})
    IncomingLightPower = createPass("IncomingLightPowerPass", {
        "minWavelength": 450.0,  # Blue light
        "maxWavelength": 495.0,
        "filterMode": 0,         # Range mode
        "useVisibleSpectrumOnly": True
    })
    ToneMapper = createPass("ToneMapper", {"autoExposure": False, "exposureCompensation": 0.0})

    # Add passes to graph
    g.addPass(VBufferRT, "VBufferRT")
    g.addPass(PathTracer, "PathTracer")
    g.addPass(IncomingLightPower, "IncomingLightPower")
    g.addPass(ToneMapper, "ToneMapper")

    # Connect passes
    g.addEdge("VBufferRT.vbuffer", "PathTracer.vbuffer")
    g.addEdge("VBufferRT.viewW", "PathTracer.viewW")

    # Connect PathTracer to IncomingLightPower
    g.addEdge("PathTracer.color", "IncomingLightPower.radiance")
    # Optional connections if available
    g.addEdge("PathTracer.initialRayInfo", "IncomingLightPower.rayDirection")
    g.addEdge("PathTracer.color", "ToneMapper.src")

    # Output connections
    g.addEdge("IncomingLightPower.lightPower", "ToneMapper.src")

    # Mark outputs to be displayed
    g.markOutput("ToneMapper.dst")
    g.markOutput("IncomingLightPower.lightPower")
    g.markOutput("IncomingLightPower.lightWavelength")

    return g
```

## Integration with Path Tracer

The IncomingLightPowerPass is designed to work well with PathTracer. It can accept:

1. **Radiance data** (required): Provides the base color/intensity information
2. **Ray direction** (optional): Path tracer can provide initial ray direction information
3. **Wavelength data** (optional): If available from the path tracer

If wavelength data is not available, the pass will estimate wavelengths from RGB colors.

## Wavelength Filtering

The pass supports three filtering modes:

1. **Range Mode**: Filter wavelengths between a minimum and maximum value
2. **Specific Bands**: Filter multiple wavelength bands, each with a center and tolerance
3. **Custom Mode**: Advanced filtering (placeholder for future extension)

### Presets

The following presets for specific spectral bands are available:

- **Mercury Lamp**: 405nm, 436nm, 546nm, 578nm
- **Hydrogen Lines**: 434nm, 486nm, 656nm
- **Sodium D-lines**: 589nm, 589.6nm

## API Reference

### Properties

| Name | Type | Description |
|------|------|-------------|
| enabled | bool | Enable/disable the pass |
| minWavelength | float | Minimum wavelength in nanometers (used in Range mode) |
| maxWavelength | float | Maximum wavelength in nanometers (used in Range mode) |
| filterMode | uint | Wavelength filtering mode: 0=Range, 1=SpecificBands, 2=Custom |
| useVisibleSpectrumOnly | bool | Whether to only consider visible light (380-780nm) |
| invertFilter | bool | Invert the filter (select wavelengths outside the range) |
| outputPowerTexName | string | Name of the output power texture |
| outputWavelengthTexName | string | Name of the output wavelength texture |

### Inputs

| Name | Type | Description |
|------|------|-------------|
| radiance | Texture | Input radiance values from path tracer |
| rayDirection | Texture | Ray direction vectors (optional) |
| wavelength | Texture | Wavelength information (optional) |
| sampleCount | Texture | Sample count per pixel (optional) |

### Outputs

| Name | Type | Description |
|------|------|-------------|
| lightPower | Texture | Calculated light power per pixel |
| lightWavelength | Texture | Wavelengths of filtered rays |
