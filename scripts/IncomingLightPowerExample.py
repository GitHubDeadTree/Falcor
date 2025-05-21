from falcor import *

def render_graph_IncomingLightPowerExample():
    """
    This example demonstrates how to use the IncomingLightPowerPass with a path tracer.
    It shows how to filter specific wavelengths of light to analyze lighting in a scene.
    """
    g = RenderGraph("Incoming Light Power Example")

    # Create passes
    VBufferRT = createPass("VBufferRT", {"samplePattern": "Stratified", "sampleCount": 16, "useAlphaTest": True})
    PathTracer = createPass("PathTracer", {"samplesPerPixel": 1})
    IncomingLightPower = createPass("IncomingLightPowerPass", {
        "minWavelength": 450.0,  # Blue light range (450-495nm)
        "maxWavelength": 495.0,
        "filterMode": 0,         # Range mode
        "useVisibleSpectrumOnly": True
    })
    AccumulatePass = createPass("AccumulatePass", {"enabled": True, "precisionMode": "Single"})
    ToneMapper = createPass("ToneMapper", {"autoExposure": False, "exposureCompensation": 0.0})

    # Add passes to graph
    g.addPass(VBufferRT, "VBufferRT")
    g.addPass(PathTracer, "PathTracer")
    g.addPass(IncomingLightPower, "IncomingLightPower")
    g.addPass(AccumulatePass, "AccumulatePass")
    g.addPass(ToneMapper, "ToneMapper")

    # Connect passes
    g.addEdge("VBufferRT.vbuffer", "PathTracer.vbuffer")
    g.addEdge("VBufferRT.viewW", "PathTracer.viewW")

    # Connect PathTracer to IncomingLightPower
    g.addEdge("PathTracer.color", "IncomingLightPower.radiance")
    # Optional connections if available in your PathTracer version
    if hasattr(PathTracer, "initialRayInfo"):
        g.addEdge("PathTracer.initialRayInfo", "IncomingLightPower.rayDirection")

    # Connect for accumulation and tone mapping
    g.addEdge("IncomingLightPower.lightPower", "AccumulatePass.input")
    g.addEdge("AccumulatePass.output", "ToneMapper.src")

    # Mark outputs to be displayed
    g.markOutput("ToneMapper.dst", "Filtered Light Power")
    g.markOutput("IncomingLightPower.lightPower", "Raw Light Power")
    g.markOutput("IncomingLightPower.lightWavelength", "Wavelength")
    g.markOutput("PathTracer.color", "Path Tracer Output")

    return g

def render_graph_IncomingLightPower_RedFilter():
    """Example that filters for red light wavelengths"""
    g = RenderGraph("Red Light Filter")

    # Create passes
    VBufferRT = createPass("VBufferRT", {"samplePattern": "Stratified", "sampleCount": 16})
    PathTracer = createPass("PathTracer", {"samplesPerPixel": 1})
    IncomingLightPower = createPass("IncomingLightPowerPass", {
        "minWavelength": 620.0,  # Red light range (620-750nm)
        "maxWavelength": 750.0,
        "filterMode": 0,         # Range mode
        "useVisibleSpectrumOnly": True
    })
    ToneMapper = createPass("ToneMapper", {"autoExposure": False, "exposureCompensation": 0.0})

    # Setup graph similar to the main example
    g.addPass(VBufferRT, "VBufferRT")
    g.addPass(PathTracer, "PathTracer")
    g.addPass(IncomingLightPower, "IncomingLightPower")
    g.addPass(ToneMapper, "ToneMapper")

    # Connect passes
    g.addEdge("VBufferRT.vbuffer", "PathTracer.vbuffer")
    g.addEdge("PathTracer.color", "IncomingLightPower.radiance")
    g.addEdge("IncomingLightPower.lightPower", "ToneMapper.src")

    g.markOutput("ToneMapper.dst")

    return g

def render_graph_IncomingLightPower_SpecificBands():
    """Example using specific wavelength bands (e.g., Mercury lamp spectrum)"""
    g = RenderGraph("Spectral Bands Filter")

    # Create passes
    VBufferRT = createPass("VBufferRT", {"samplePattern": "Stratified", "sampleCount": 16})
    PathTracer = createPass("PathTracer", {"samplesPerPixel": 1})
    IncomingLightPower = createPass("IncomingLightPowerPass", {
        "filterMode": 1,         # Specific Bands mode
        "useVisibleSpectrumOnly": True
        # Bands will use the default Mercury lamp wavelengths: 405nm, 436nm, 546nm, 578nm
    })
    ToneMapper = createPass("ToneMapper", {"autoExposure": False, "exposureCompensation": 0.0})

    # Setup graph similar to the main example
    g.addPass(VBufferRT, "VBufferRT")
    g.addPass(PathTracer, "PathTracer")
    g.addPass(IncomingLightPower, "IncomingLightPower")
    g.addPass(ToneMapper, "ToneMapper")

    # Connect passes
    g.addEdge("VBufferRT.vbuffer", "PathTracer.vbuffer")
    g.addEdge("PathTracer.color", "IncomingLightPower.radiance")
    g.addEdge("IncomingLightPower.lightPower", "ToneMapper.src")

    g.markOutput("ToneMapper.dst")
    g.markOutput("IncomingLightPower.lightWavelength")

    return g

def render_graph_IncomingLightPower_InvertedFilter():
    """Example using an inverted filter (showing light outside the specified range)"""
    g = RenderGraph("Inverted Wavelength Filter")

    # Create passes
    VBufferRT = createPass("VBufferRT", {"samplePattern": "Stratified", "sampleCount": 16})
    PathTracer = createPass("PathTracer", {"samplesPerPixel": 1})
    IncomingLightPower = createPass("IncomingLightPowerPass", {
        "minWavelength": 500.0,  # Green light range (500-565nm)
        "maxWavelength": 565.0,
        "filterMode": 0,         # Range mode
        "useVisibleSpectrumOnly": True,
        "invertFilter": True     # Invert the filter to show everything EXCEPT green light
    })
    ToneMapper = createPass("ToneMapper", {"autoExposure": False, "exposureCompensation": 0.0})

    # Setup graph similar to the main example
    g.addPass(VBufferRT, "VBufferRT")
    g.addPass(PathTracer, "PathTracer")
    g.addPass(IncomingLightPower, "IncomingLightPower")
    g.addPass(ToneMapper, "ToneMapper")

    # Connect passes
    g.addEdge("VBufferRT.vbuffer", "PathTracer.vbuffer")
    g.addEdge("PathTracer.color", "IncomingLightPower.radiance")
    g.addEdge("IncomingLightPower.lightPower", "ToneMapper.src")

    g.markOutput("ToneMapper.dst")
    g.markOutput("PathTracer.color", "Original")

    return g

# Create instances of the render graphs and add them to Mogwai
IncomingLightPowerExample = render_graph_IncomingLightPowerExample()
IncomingLightPower_RedFilter = render_graph_IncomingLightPower_RedFilter()
IncomingLightPower_SpecificBands = render_graph_IncomingLightPower_SpecificBands()
IncomingLightPower_InvertedFilter = render_graph_IncomingLightPower_InvertedFilter()

# Try to add the graphs to Mogwai (required for UI display)
try:
    m.addGraph(IncomingLightPowerExample)
    m.addGraph(IncomingLightPower_RedFilter)
    m.addGraph(IncomingLightPower_SpecificBands)
    m.addGraph(IncomingLightPower_InvertedFilter)
except NameError:
    None
