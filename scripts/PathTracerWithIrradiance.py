from falcor import *

def render_graph_PathTracerWithIrradiance():
    g = RenderGraph("PathTracerWithIrradiance")

    # 添加VBufferRT通道
    VBufferRT = createPass("VBufferRT", {'samplePattern': 'Stratified', 'sampleCount': 16, 'useAlphaTest': True})
    g.addPass(VBufferRT, "VBufferRT")

    # 添加PathTracer通道
    # 注意: PathTracer不需要显式设置outputInitialRayInfo，它会自动检测连接到initialRayInfo输出的通道
    PathTracer = createPass("PathTracer", {'samplesPerPixel': 1})
    g.addPass(PathTracer, "PathTracer")

    # 添加IrradiancePass通道
    IrradiancePass = createPass("IrradiancePass", {'enabled': True, 'reverseRayDirection': True, 'intensityScale': 1.0, 'useActualNormals': False})
    g.addPass(IrradiancePass, "IrradiancePass")

    # 为颜色和辐照度添加累积通道
    AccumulatePass = createPass("AccumulatePass", {'enabled': True, 'precisionMode': 'Single'})
    g.addPass(AccumulatePass, "AccumulatePass")

    IrradianceAccumulatePass = createPass("AccumulatePass", {'enabled': True, 'precisionMode': 'Single'})
    g.addPass(IrradianceAccumulatePass, "IrradianceAccumulatePass")

    # 新增：单通道辐照度累积通道
    IrradianceScalarAccumulatePass = createPass("AccumulatePass", {'enabled': True, 'precisionMode': 'Single'})
    g.addPass(IrradianceScalarAccumulatePass, "IrradianceScalarAccumulatePass")

    # 添加ToneMapper通道
    ToneMapper = createPass("ToneMapper", {'autoExposure': False, 'exposureCompensation': 0.0})
    g.addPass(ToneMapper, "ToneMapper")

    IrradianceToneMapper = createPass("ToneMapper", {'autoExposure': False, 'exposureCompensation': 0.0})
    g.addPass(IrradianceToneMapper, "IrradianceToneMapper")

    # 新增：单通道辐照度色调映射器
    IrradianceScalarToneMapper = createPass("ToneMapper", {'autoExposure': False, 'exposureCompensation': 0.0})
    g.addPass(IrradianceScalarToneMapper, "IrradianceScalarToneMapper")

    # 连接VBufferRT到PathTracer
    g.addEdge("VBufferRT.vbuffer", "PathTracer.vbuffer")
    g.addEdge("VBufferRT.viewW", "PathTracer.viewW")
    g.addEdge("VBufferRT.mvec", "PathTracer.mvec")

    # 新增: 连接VBufferRT到IrradiancePass提供法线数据
    g.addEdge("VBufferRT.vbuffer", "IrradiancePass.vbuffer")

    # 连接PathTracer的输出
    g.addEdge("PathTracer.color", "AccumulatePass.input")
    g.addEdge("PathTracer.initialRayInfo", "IrradiancePass.initialRayInfo")

    # 连接IrradiancePass到累积通道
    g.addEdge("IrradiancePass.irradiance", "IrradianceAccumulatePass.input")
    # 新增：连接单通道辐照度输出到单通道累积通道
    g.addEdge("IrradiancePass.irradianceScalar", "IrradianceScalarAccumulatePass.inputScalar")

    # 连接累积通道到色调映射器
    g.addEdge("AccumulatePass.output", "ToneMapper.src")
    g.addEdge("IrradianceAccumulatePass.output", "IrradianceToneMapper.src")
    # 新增：连接单通道辐照度累积通道到单通道色调映射器
    g.addEdge("IrradianceScalarAccumulatePass.outputScalar", "IrradianceScalarToneMapper.src")

    # 标记输出
    g.markOutput("ToneMapper.dst")
    g.markOutput("IrradianceToneMapper.dst")
    # 新增：标记单通道辐照度输出
    g.markOutput("IrradianceScalarToneMapper.dst")

    return g

PathTracerWithIrradiance = render_graph_PathTracerWithIrradiance()
try: m.addGraph(PathTracerWithIrradiance)
except NameError: None
