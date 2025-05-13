# 辐照度计算的法线修复方案

## 问题描述

当前的IrradiancePass实现使用了固定法线(0,0,1)进行辐照度计算，这导致在非平面物体（如球体）上的辐照度计算不准确。辐照度计算应该基于每个像素的实际表面法线，而不是假设一个统一的法线方向。

目前的计算在`IrradiancePass.cs.slang`中是这样的：

```cpp
// 我们假设接收表面的法线是(0,0,1)以简化计算
// 如果需要，可以修改为使用实际的法线
float3 normal = float3(0, 0, 1);

// 计算余弦项(N·ω)
float cosTheta = max(0.0f, dot(normal, rayDir));
```

## 修改方案

为了使辐照度计算能够在任意曲面（包括球体）上正确进行，我们需要修改IrradiancePass以使用VBufferRT提供的实际法线数据。修改步骤如下：

### 1. 修改IrradiancePass.h文件

在IrradiancePass类中添加对法线缓冲区的支持：

```cpp
// 在private部分添加：
bool mUseActualNormals = true;  ///< 是否使用实际法线而非固定法线
```

### 2. 修改IrradiancePass.cpp文件

#### 2.1 更新构造函数解析选项

```cpp
IrradiancePass::IrradiancePass(ref<Device> pDevice, const Properties& props) : RenderPass(pDevice)
{
    // 解析属性
    for (const auto& [key, value] : props)
    {
        if (key == "enabled") mEnabled = value;
        else if (key == "reverseRayDirection") mReverseRayDirection = value;
        else if (key == "intensityScale") mIntensityScale = value;
        else if (key == "useActualNormals") mUseActualNormals = value;  // 新增选项
        else logWarning("Unknown property '{}' in IrradiancePass properties.", key);
    }

    // 创建计算通道
    prepareProgram();
}
```

#### 2.2 更新getProperties()方法

```cpp
Properties IrradiancePass::getProperties() const
{
    Properties props;
    props["enabled"] = mEnabled;
    props["reverseRayDirection"] = mReverseRayDirection;
    props["intensityScale"] = mIntensityScale;
    props["useActualNormals"] = mUseActualNormals;  // 新增选项
    return props;
}
```

#### 2.3 更新RenderPassReflection

修改`reflect()`方法，添加对法线输入的支持：

```cpp
RenderPassReflection IrradiancePass::reflect(const CompileData& compileData)
{
    RenderPassReflection reflector;

    // 输入：初始光线方向和强度
    reflector.addInput(kInputRayInfo, "Initial ray direction (xyz) and intensity (w)").bindFlags(ResourceBindFlags::ShaderResource);

    // 新增：表面法线输入
    if (mUseActualNormals)
    {
        reflector.addInput("vbuffer", "Visibility buffer for surface identification").bindFlags(ResourceBindFlags::ShaderResource);
    }

    // 输出：辐照度
    reflector.addOutput(kOutputIrradiance, "Calculated irradiance per pixel").bindFlags(ResourceBindFlags::UnorderedAccess).format(ResourceFormat::RGBA32Float);

    return reflector;
}
```

#### 2.4 更新execute()方法

```cpp
void IrradiancePass::execute(RenderContext* pRenderContext, const RenderData& renderData)
{
    // 获取输入纹理
    const auto& pInputRayInfo = renderData.getTexture(kInputRayInfo);
    if (!pInputRayInfo)
    {
        logWarning("IrradiancePass::execute() - Input ray info texture is missing. Make sure the PathTracer is outputting initialRayInfo data.");
        return;
    }

    // 获取法线输入
    const auto& pVBuffer = mUseActualNormals ? renderData.getTexture("vbuffer") : nullptr;
    if (mUseActualNormals && !pVBuffer)
    {
        logWarning("IrradiancePass::execute() - VBuffer texture is missing but useActualNormals is enabled. Falling back to fixed normal.");
    }

    // 如果禁用，清除输出并返回
    if (!mEnabled)
    {
        pRenderContext->clearUAV(renderData.getTexture(kOutputIrradiance)->getUAV().get(), float4(0.f));
        return;
    }

    // 准备资源并确保着色器程序已更新
    prepareResources(pRenderContext, renderData);

    // 设置着色器常量
    auto var = mpComputePass->getRootVar();

    // 通过cbuffer名称路径访问变量
    var[kPerFrameCB][kReverseRayDirection] = mReverseRayDirection;
    var[kPerFrameCB][kIntensityScale] = mIntensityScale;
    var[kPerFrameCB]["gUseActualNormals"] = mUseActualNormals && pVBuffer != nullptr;

    // 全局纹理资源绑定
    var["gInputRayInfo"] = pInputRayInfo;
    var["gOutputIrradiance"] = renderData.getTexture(kOutputIrradiance);

    // 绑定法线输入（如果可用）
    if (mUseActualNormals && pVBuffer)
    {
        var["gVBuffer"] = pVBuffer;
        var["gScene"] = mpScene->getParameterBlock();
    }

    // 执行计算通道
    uint32_t width = pInputRayInfo->getWidth();
    uint32_t height = pInputRayInfo->getHeight();
    mpComputePass->execute(pRenderContext, { (width + 15) / 16, (height + 15) / 16, 1 });
}
```

#### 2.5 更新renderUI()方法

```cpp
void IrradiancePass::renderUI(Gui::Widgets& widget)
{
    widget.checkbox("Enabled", mEnabled);
    widget.checkbox("Reverse Ray Direction", mReverseRayDirection);
    widget.tooltip("When enabled, inverts the ray direction to calculate irradiance.\n"
                   "This is usually required because ray directions in path tracing typically\n"
                   "point from camera/surface to the light source, but irradiance calculations\n"
                   "need directions pointing toward the receiving surface.");

    widget.checkbox("Use Actual Normals", mUseActualNormals);
    widget.tooltip("When enabled, uses the actual surface normals from VBuffer\n"
                   "instead of assuming a fixed normal (0,0,1).\n"
                   "This is required for accurate irradiance calculation on curved surfaces.");

    widget.var("Intensity Scale", mIntensityScale, 0.0f, 10.0f, 0.1f);
    widget.tooltip("Scaling factor applied to the calculated irradiance value.");
}
```

#### 2.6 更新prepareProgram()方法

```cpp
void IrradiancePass::prepareProgram()
{
    // 创建计算通道
    ProgramDesc desc;
    desc.addShaderLibrary(kShaderFile).csEntry("main");

    // 如果使用实际法线，添加场景定义
    if (mUseActualNormals && mpScene)
    {
        desc.addShaderModules(mpScene->getShaderModules());
        desc.addTypeConformances(mpScene->getTypeConformances());
    }

    mpComputePass = ComputePass::create(mpDevice, desc);
}
```

#### 2.7 添加对场景的支持

确保`setScene()`方法正确处理场景引用：

```cpp
void IrradiancePass::setScene(RenderContext* pRenderContext, const ref<Scene>& pScene)
{
    mpScene = pScene;
    if (mUseActualNormals && pScene)
    {
        // 需要重新创建计算通道以包含场景着色器模块
        prepareProgram();
    }
}
```

### 3. 修改IrradiancePass.cs.slang文件

```cpp
/***************************************************************************
 # Copyright (c) 2024, NVIDIA CORPORATION. All rights reserved.
 #
 # [版权声明...]
 **************************************************************************/

/** 从光线方向和强度数据计算辐照度的计算着色器。

    辐照度(E)计算为:
    E = L * cos(θ) = L * (N·ω)

    其中:
    - L是辐射亮度(强度)
    - θ是光线方向与表面法线之间的角度
    - N是表面法线(可以使用VBuffer提供的实际法线或假设固定法线(0,0,1))
    - ω是光线方向(如果启用reverseRayDirection则取反)

    对于蒙特卡洛积分，我们对所有路径的辐照度贡献求和。
*/

import Scene.Shading;  // 导入场景着色模块，用于法线查询

// 输入数据
Texture2D<float4> gInputRayInfo;        ///< 输入光线方向(xyz)和强度(w)
Texture2D<PackedHitInfo> gVBuffer;      ///< 可视性缓冲区，用于获取表面属性

// 输出数据
RWTexture2D<float4> gOutputIrradiance;  ///< 输出辐照度(xyz)和总余弦加权样本计数(w)

// 参数
cbuffer PerFrameCB
{
    bool gReverseRayDirection;          ///< 是否在计算辐照度时反转光线方向
    float gIntensityScale;              ///< 辐照度的缩放因子
    bool gUseActualNormals;             ///< 是否使用实际法线而非固定法线
}

[numthreads(16, 16, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint2 pixel = dispatchThreadId.xy;

    // 跳过越界像素
    uint width, height;
    gInputRayInfo.GetDimensions(width, height);
    if (any(pixel >= uint2(width, height))) return;

    // 从输入读取光线方向和强度
    float4 rayInfo = gInputRayInfo[pixel];
    float3 rayDir = rayInfo.xyz;
    float intensity = rayInfo.w;

    // 对于辐照度计算，光线方向需要指向接收表面
    // 在路径追踪中，光线方向通常从相机/表面指向光源
    // 如果reverseRayDirection为true，我们翻转方向
    if (gReverseRayDirection)
    {
        rayDir = -rayDir;
    }

    float3 normal;
    if (gUseActualNormals)
    {
        // 从VBuffer获取表面法线
        const HitInfo hit = HitInfo(gVBuffer[pixel]);
        if (hit.isValid())
        {
            // 获取表面着色数据
            VertexData v = {};
            const GeometryInstanceID instanceID = hit.getInstanceID();
            const uint primitiveIndex = hit.getPrimitiveIndex();
            const float2 barycentrics = hit.getBarycentrics();
            v = gScene.getVertexData(instanceID, primitiveIndex, barycentrics);

            // 使用法线作为接收表面的方向
            normal = normalize(v.normalW);
        }
        else
        {
            // 如果命中无效，使用默认法线
            normal = float3(0, 0, 1);
        }
    }
    else
    {
        // 使用固定法线(0,0,1)简化计算
        normal = float3(0, 0, 1);
    }

    // 计算余弦项(N·ω)
    float cosTheta = max(0.0f, dot(normal, rayDir));

    // 计算辐照度贡献: L * cosθ
    float irradiance = cosTheta * intensity * gIntensityScale;

    // 输出辐照度(xyz)和样本数(w)
    // 对于蒙特卡洛积分，我们需要对所有贡献求和
    // w分量可以用于归一化结果(在此实现中未使用)
    gOutputIrradiance[pixel] = float4(irradiance, irradiance, irradiance, 1.0f);
}
```

### 4. 修改PathTracerWithIrradiance.py脚本

更新脚本，添加VBuffer到IrradiancePass的连接：

```python
def render_graph_PathTracerWithIrradiance():
    g = RenderGraph("PathTracerWithIrradiance")

    # 添加VBufferRT通道
    VBufferRT = createPass("VBufferRT", {'samplePattern': 'Stratified', 'sampleCount': 16, 'useAlphaTest': True})
    g.addPass(VBufferRT, "VBufferRT")

    # 添加PathTracer通道
    PathTracer = createPass("PathTracer", {'samplesPerPixel': 1})
    g.addPass(PathTracer, "PathTracer")

    # 添加IrradiancePass通道 - 添加useActualNormals选项
    IrradiancePass = createPass("IrradiancePass", {'enabled': True, 'reverseRayDirection': True, 'intensityScale': 1.0, 'useActualNormals': True})
    g.addPass(IrradiancePass, "IrradiancePass")

    # ... 其他通道配置 ...

    # 连接VBufferRT到PathTracer
    g.addEdge("VBufferRT.vbuffer", "PathTracer.vbuffer")
    g.addEdge("VBufferRT.viewW", "PathTracer.viewW")
    g.addEdge("VBufferRT.mvec", "PathTracer.mvec")

    # 新增: 连接VBufferRT到IrradiancePass以提供法线数据
    g.addEdge("VBufferRT.vbuffer", "IrradiancePass.vbuffer")

    # 连接PathTracer的输出
    g.addEdge("PathTracer.color", "AccumulatePass.input")
    g.addEdge("PathTracer.initialRayInfo", "IrradiancePass.initialRayInfo")

    # ... 其他连接 ...

    return g
```

## 实现效果

通过这些修改，IrradiancePass将能够使用每个像素的实际表面法线进行辐照度计算，而不是假设所有像素都具有相同的(0,0,1)法线。这将使得辐照度计算在曲面（如球体）上更加准确，画面中的所有可见区域都将显示正确的辐照度值，而不仅仅是左上角的一小部分。

## 可能遇到的问题及解决方案

1. **性能影响**: 访问和处理VBuffer数据可能会略微增加计算成本。如果性能是一个问题，可以提供选项切换回使用固定法线。

2. **错误的表面法线**: 如果场景中存在法线贴图或其他修改法线的技术，确保从VBuffer中获取的是最终的着色法线，而不是几何法线。

3. **空白区域**: 如果场景中存在没有有效命中的区域，这些区域将回退到固定法线(0,0,1)，可能与周围区域产生不连续性。

4. **Debug方法**: 如果实现后仍有问题，可以考虑添加一个选项，将法线方向可视化为颜色输出，以便检查法线是否正确提取。
