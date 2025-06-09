from falcor import *

def render_graph_VLCAnalysis():
    """
    VLC (Visible Light Communication) Analysis Render Graph
    
    This render graph combines PathTracer with CIRComputePass to analyze
    visible light communication channels using ray tracing.
    
    Pipeline:
    VBufferRT -> PathTracer -> CIRComputePass
                    |
                    v
              AccumulatePass -> ToneMapper (Visual Output)
    """
    g = RenderGraph("VLCAnalysis")
    
    # ===== GEOMETRY PROCESSING =====
    VBufferRT = createPass("VBufferRT", {
        'samplePattern': 'Stratified',
        'sampleCount': 16,
        'useAlphaTest': True,
        'cullingMode': 'CullBack'
    })
    g.addPass(VBufferRT, "VBufferRT")
    
    # ===== PATH TRACING WITH CIR DATA COLLECTION =====
    PathTracer = createPass("PathTracer", {
        # Ray tracing parameters
        'samplesPerPixel': 1,
        'maxBounces': 10,
        'maxNonSpecularBounces': 8,
        'useImportanceSampling': True,
        'useRussianRoulette': True,
        'probabilityAbsorption': 0.2,
        
        # CIR specific parameters
        'enableCIRCollection': True,
        'maxCIRPaths': 1000000,
        
        # Light transport settings
        'useBSDFSampling': True,
        'useNestedDielectrics': True,
        'useLightsInDielectricMedia': False,
        
        # Denoising support
        'outputNRDData': False,
        'outputNRDAdditionalData': False
    })
    g.addPass(PathTracer, "PathTracer")
    
    # ===== CIR COMPUTATION AND ANALYSIS =====
    CIRComputePass = createPass("CIRComputePass", {
        # Time domain parameters
        'timeResolution': 1e-9,         # 1 ns time resolution for high precision
        'maxDelay': 1e-6,               # 1 μs maximum delay (typical indoor range)
        'cirBins': 1000,                # 1000 time bins for analysis
        
        # LED transmitter parameters  
        'ledPower': 1.0,                # 1 W LED power
        'halfPowerAngle': 0.5236,       # 30° half-power angle (typical LED)
        
        # Photodiode receiver parameters
        'receiverArea': 1e-4,           # 1 cm² receiver area (typical PD)
        'fieldOfView': 1.047,           # 60° field of view
        
        # Advanced analysis options
        'enableStatistics': True,
        'enableVisualization': True,
        'outputFrequency': 5000         # Output results every 5000 frames
    })
    g.addPass(CIRComputePass, "CIRComputePass")
    
    # ===== VISUAL RENDERING PIPELINE =====
    AccumulatePass = createPass("AccumulatePass", {
        'enabled': True,
        'precisionMode': 'Single',
        'autoReset': True,
        'maxFrameCount': 0              # Unlimited accumulation
    })
    g.addPass(AccumulatePass, "AccumulatePass")
    
    ToneMapper = createPass("ToneMapper", {
        'operator': 'Aces',             # ACES tone mapping for realistic rendering
        'autoExposure': False,
        'exposureCompensation': 0.0,
        'whiteBalance': False,
        'whitePoint': 6500.0
    })
    g.addPass(ToneMapper, "ToneMapper")
    
    # ===== RENDER GRAPH CONNECTIONS =====
    
    # Geometry data flow: VBufferRT -> PathTracer
    g.addEdge("VBufferRT.vbuffer", "PathTracer.vbuffer")
    g.addEdge("VBufferRT.viewW", "PathTracer.viewW") 
    g.addEdge("VBufferRT.mvec", "PathTracer.mvec")
    
    # CIR data flow: PathTracer -> CIRComputePass
    g.addEdge("PathTracer.cirData", "CIRComputePass.cirData")
    
    # Visual rendering data flow: PathTracer -> AccumulatePass -> ToneMapper
    g.addEdge("PathTracer.color", "AccumulatePass.input")
    g.addEdge("AccumulatePass.output", "ToneMapper.src")
    
    # ===== OUTPUT CHANNELS =====
    
    # Primary visual output
    g.markOutput("ToneMapper.dst")
    
    # CIR analysis output
    g.markOutput("CIRComputePass.cir")
    
    # Optional debugging outputs
    g.markOutput("PathTracer.color")        # Raw path tracer output
    g.markOutput("AccumulatePass.output")   # Accumulated result before tone mapping
    g.markOutput("VBufferRT.vbuffer")       # Geometry buffer for debugging
    
    return g

def render_graph_SimpleVLC():
    """
    Simplified VLC Analysis for quick testing
    """
    g = RenderGraph("SimpleVLC")
    
    # Minimal VBuffer setup
    VBufferRT = createPass("VBufferRT")
    g.addPass(VBufferRT, "VBufferRT")
    
    # Basic PathTracer with CIR collection
    PathTracer = createPass("PathTracer", {
        'samplesPerPixel': 1,
        'maxBounces': 5
    })
    g.addPass(PathTracer, "PathTracer")
    
    # CIR computation with default parameters
    CIRComputePass = createPass("CIRComputePass")
    g.addPass(CIRComputePass, "CIRComputePass")
    
    # Simple tone mapping
    ToneMapper = createPass("ToneMapper")
    g.addPass(ToneMapper, "ToneMapper")
    
    # Basic connections
    g.addEdge("VBufferRT.vbuffer", "PathTracer.vbuffer")
    g.addEdge("VBufferRT.viewW", "PathTracer.viewW")
    g.addEdge("PathTracer.cirData", "CIRComputePass.cirData")
    g.addEdge("PathTracer.color", "ToneMapper.src")
    
    # Outputs
    g.markOutput("ToneMapper.dst")
    g.markOutput("CIRComputePass.cir")
    
    return g

# ===== RENDER GRAPH REGISTRATION =====

# Create advanced VLC analysis graph
VLCAnalysis = render_graph_VLCAnalysis()

# Create simple VLC analysis graph  
SimpleVLC = render_graph_SimpleVLC()

try:
    # Register both graphs
    m.addGraph(VLCAnalysis)
    m.addGraph(SimpleVLC)
    
    # Set the advanced graph as default
    m.loadRenderGraph(VLCAnalysis)
    
except NameError:
    # Handle case where 'm' (Mogwai) is not available
    pass

# ===== USAGE NOTES =====
"""
Usage Instructions:

1. Advanced VLC Analysis (VLCAnalysis):
   - High-precision CIR calculation with detailed parameters
   - Suitable for research and detailed channel analysis
   - Outputs comprehensive debugging information
   - Recommended for publication-quality results

2. Simple VLC Analysis (SimpleVLC):
   - Quick setup for basic CIR analysis
   - Uses default parameters for rapid prototyping
   - Minimal configuration required
   - Good for initial testing and verification

Output Files:
- CIR data will be saved as CSV files: cir_frame_XXXXX.csv
- Visual rendering appears in the main viewport
- Console output provides detailed statistics and debugging info

Parameter Tuning:
- Adjust 'timeResolution' for temporal precision vs. memory usage
- Modify 'maxDelay' based on your scene size
- Change 'cirBins' to balance resolution and performance
- Update LED/receiver parameters to match your VLC system
""" 