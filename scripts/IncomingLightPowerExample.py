from falcor import *

def render_graph_IncomingLightPowerExample():
    g = RenderGraph("Incoming Light Power Example")

    # Create passes
    VBufferRT = createPass("VBufferRT", {"samplePattern": "Stratified", "sampleCount": 16, "useAlphaTest": True})
    PathTracer = createPass("PathTracer", {"samplesPerPixel": 1})
    IncomingLightPower = createPass("IncomingLightPowerPass", {
        "minWavelength": 450.0,  # Blue light range (450-495nm)
        "maxWavelength": 495.0,
        "filterMode": 0,         # Range mode
        "useVisibleSpectrumOnly": True,
        "enableWavelengthFilter": False  # 禁用波长过滤
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
    g.addEdge("PathTracer.initialRayInfo", "IncomingLightPower.rayDirection")

    # Direct connection from IncomingLightPower to ToneMapper (removed AccumulatePass)
    g.addEdge("IncomingLightPower.lightPower", "ToneMapper.src")

    # Mark outputs to be displayed
    g.markOutput("ToneMapper.dst")
    g.markOutput("IncomingLightPower.lightPower")
    g.markOutput("IncomingLightPower.lightWavelength")
    g.markOutput("PathTracer.color")

    return g

# Create instance of the render graph and add it to Mogwai
IncomingLightPowerExample = render_graph_IncomingLightPowerExample()

# Try to add the graph to Mogwai (required for UI display)
try:
    m.addGraph(IncomingLightPowerExample)
except NameError:
    None
