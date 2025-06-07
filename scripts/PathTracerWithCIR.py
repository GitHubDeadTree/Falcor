from falcor import *

def render_graph_PathTracerWithCIR():
    g = RenderGraph("PathTracerWithCIR")
    
    # VBuffer pass for geometry processing
    VBufferRT = createPass("VBufferRT", {'samplePattern': 'Stratified', 'sampleCount': 16, 'useAlphaTest': True})
    g.addPass(VBufferRT, "VBufferRT")
    
    # PathTracer pass - collects path data for CIR calculation
    PathTracer = createPass("PathTracer", {
        'samplesPerPixel': 1,
        'maxBounces': 10,
        'useImportanceSampling': True
    })
    g.addPass(PathTracer, "PathTracer")
    
    # CIR computation pass - calculates Channel Impulse Response
    CIRComputePass = createPass("CIRComputePass", {
        'timeResolution': 1e-9,    # 1 nanosecond time resolution
        'maxDelay': 1e-6,          # 1 microsecond max delay
        'cirBins': 1000,           # 1000 time bins
        'ledPower': 1.0,           # 1 watt LED power
        'halfPowerAngle': 0.5236,  # 30 degrees in radians
        'receiverArea': 1e-4,      # 1 cm^2 receiver area
        'fieldOfView': 1.047       # 60 degrees in radians
    })
    g.addPass(CIRComputePass, "CIRComputePass")
    
    # Standard accumulation and tone mapping for visual output
    AccumulatePass = createPass("AccumulatePass", {'enabled': True, 'precisionMode': 'Single'})
    g.addPass(AccumulatePass, "AccumulatePass")
    
    ToneMapper = createPass("ToneMapper", {'autoExposure': False, 'exposureCompensation': 0.0})
    g.addPass(ToneMapper, "ToneMapper")
    
    # Connect VBuffer to PathTracer
    g.addEdge("VBufferRT.vbuffer", "PathTracer.vbuffer")
    g.addEdge("VBufferRT.viewW", "PathTracer.viewW")
    g.addEdge("VBufferRT.mvec", "PathTracer.mvec")
    
    # Connect PathTracer CIR data to CIRComputePass
    g.addEdge("PathTracer.cirData", "CIRComputePass.cirData")
    
    # Connect PathTracer color output to standard visualization pipeline
    g.addEdge("PathTracer.color", "AccumulatePass.input")
    g.addEdge("AccumulatePass.output", "ToneMapper.src")
    
    # Mark outputs
    g.markOutput("ToneMapper.dst")        # Visual rendering output
    g.markOutput("CIRComputePass.cir")    # CIR data output for analysis
    
    return g

# Create and register the render graph
PathTracerWithCIR = render_graph_PathTracerWithCIR()
try: 
    m.addGraph(PathTracerWithCIR)
except NameError: 
    None 