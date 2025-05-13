# 辐照度计算的法线修复方案（分步实现）

## 问题描述

当前的IrradiancePass实现使用了固定法线(0,0,1)进行辐照度计算，这导致在非平面物体（如球体）上的辐照度计算不准确。辐照度计算应该基于每个像素的实际表面法线，而不是假设一个统一的法线方向。

目前的计算在 `IrradiancePass.cs.slang`中是这样的：

```cpp
// 我们假设接收表面的法线是(0,0,1)以简化计算
// 如果需要，可以修改为使用实际的法线
float3 normal = float3(0, 0, 1);

// 计算余弦项(N·ω)
float cosTheta = max(0.0f, dot(normal, rayDir));
```

## 分步实现方案

为了便于调试和逐步验证，我们将修改分解为以下几个独立任务：

### 任务1：添加法线可视化功能（调试用）

在开始修改之前，先添加一个简单的调试功能，可以可视化法线方向，帮助验证法线数据是否正确。

1. 修改IrradiancePass.h

```cpp
// 在private部分添加：
bool mDebugNormalView = false;  ///< 当启用时，直接输出法线作为颜色进行调试
```

2. 修改IrradiancePass.cpp中的renderUI方法

```cpp
void IrradiancePass::renderUI(Gui::Widgets& widget)
{
    // 现有UI代码...

    // 添加调试选项
    widget.checkbox("Debug Normal View", mDebugNormalView);
    widget.tooltip("When enabled, visualizes the normal directions as colors for debugging.");
}
```

3. 简单修改IrradiancePass.cs.slang（仍使用固定法线）

```cpp
// 在最后的输出部分
if (gDebugNormalView)
{
    // 将法线映射到[0,1]范围用于可视化
    float3 normalColor = (normal + 1.0f) * 0.5f;
    gOutputIrradiance[pixel] = float4(normalColor, 1.0f);
}
else
{
    gOutputIrradiance[pixel] = float4(irradiance, irradiance, irradiance, 1.0f);
}
```

#### 任务1成功标准

- ✓ 在IrradiancePass的UI面板中可以看到新增的"Debug Normal View"复选框
- ✓ 当启用Debug Normal View时，IrradianceToneMapper输出窗口显示的是蓝色平面（因为(0,0,1)法线映射为颜色后是(0.5,0.5,1.0)，呈现为蓝色）
- ✓ 当禁用Debug Normal View时，辐照度显示恢复为原来的计算结果

### 任务2：添加UI开关和固定法线方向选择

先添加UI选项来控制是否使用实际法线，但不改变着色器内部逻辑，只在CPU端预留接口。

1. 修改IrradiancePass.h

```cpp
// 在private部分添加：
bool mUseActualNormals = false;  ///< 是否使用实际法线而非固定法线
float3 mFixedNormal = float3(0, 0, 1);  ///< 当不使用实际法线时的固定法线方向
```

2. 修改IrradiancePass.cpp中的构造函数和getProperties()

```cpp
// 在构造函数解析选项部分
else if (key == "useActualNormals") mUseActualNormals = value;

// 在getProperties()中添加
props["useActualNormals"] = mUseActualNormals;
```

3. 修改renderUI方法

```cpp
void IrradiancePass::renderUI(Gui::Widgets& widget)
{
    // 已有UI代码...

    // 添加法线选择控制
    widget.checkbox("Use Actual Normals", mUseActualNormals);
    widget.tooltip("When enabled, uses the actual surface normals from VBuffer\n"
                  "instead of assuming a fixed normal direction.");

    if (!mUseActualNormals)
    {
        // 只有在使用固定法线时显示方向控制
        widget.var("Fixed Normal", mFixedNormal, -1.0f, 1.0f, 0.01f);
        widget.tooltip("The fixed normal direction to use when not using actual normals.");
    }
}
```

#### 任务2成功标准

- ✓ UI面板中显示了"Use Actual Normals"复选框和可调整的固定法线方向控件
- ✓ 调整固定法线方向（如改为(1,0,0)）后，在Debug Normal View模式下可以看到颜色变化（应变为红色偏向，因为映射到颜色空间后(1,0,0)变为(1,0.5,0.5)）
- ✓ 调整固定法线方向后，辐照度计算结果也相应变化（可能会看到左上角亮区的位置发生移动）
- ✓ 检查PathTracerWithIrradiance.py的渲染图定义中，"IrradiancePass"的创建参数是否能接收"useActualNormals"参数

### 任务3：更新反射API来请求VBuffer输入

修改反射API请求VBuffer输入，但暂时不使用它，只验证连接是否正确。

1. 修改IrradiancePass.cpp中的reflect方法

```cpp
RenderPassReflection IrradiancePass::reflect(const CompileData& compileData)
{
    RenderPassReflection reflector;

    // 输入：初始光线方向和强度
    reflector.addInput(kInputRayInfo, "Initial ray direction (xyz) and intensity (w)")
        .bindFlags(ResourceBindFlags::ShaderResource);

    // 添加VBuffer输入，但标记为可选（暂时不使用）
    reflector.addInput("vbuffer", "Visibility buffer for surface identification")
        .bindFlags(ResourceBindFlags::ShaderResource)
        .flags(RenderPassReflection::Field::Flags::Optional);

    // 输出：辐照度
    reflector.addOutput(kOutputIrradiance, "Calculated irradiance per pixel")
        .bindFlags(ResourceBindFlags::UnorderedAccess)
        .format(ResourceFormat::RGBA32Float);

    return reflector;
}
```

2. 修改PathTracerWithIrradiance.py脚本添加连接

```python
# 新增: 连接VBufferRT到IrradiancePass提供法线数据
g.addEdge("VBufferRT.vbuffer", "IrradiancePass.vbuffer")
```

#### 任务3成功标准

- ✓ 修改渲染图脚本后，打开Falcor应用程序并成功加载PathTracerWithIrradiance渲染图
- ✓ 在渲染图编辑器中可以看到从"VBufferRT"到"IrradiancePass"的"vbuffer"连接线（验证连接正确建立）
- ✓ 程序运行没有报错，特别是没有关于缺少输入资源的警告
- ✓ IrradiancePass的execute()方法中应能成功获取到vbuffer资源（可添加临时日志输出验证）

### 任务4：通过VBuffer访问并可视化实际法线

在这一步，我们实现法线读取功能，并通过调试视图验证法线数据是否正确，但暂不用于辐照度计算。

1. 修改IrradiancePass.cpp中的execute方法

```cpp
void IrradiancePass::execute(RenderContext* pRenderContext, const RenderData& renderData)
{
    // 现有代码...

    // 获取VBuffer输入
    const auto& pVBuffer = renderData.getTexture("vbuffer");
    bool hasVBuffer = pVBuffer != nullptr;

    // 设置着色器常量
    auto var = mpComputePass->getRootVar();
    var[kPerFrameCB][kReverseRayDirection] = mReverseRayDirection;
    var[kPerFrameCB][kIntensityScale] = mIntensityScale;
    var[kPerFrameCB]["gDebugNormalView"] = mDebugNormalView;
    var[kPerFrameCB]["gUseActualNormals"] = mUseActualNormals && hasVBuffer;
    var[kPerFrameCB]["gFixedNormal"] = mFixedNormal;

    // 绑定纹理
    var["gInputRayInfo"] = pInputRayInfo;
    var["gOutputIrradiance"] = renderData.getTexture(kOutputIrradiance);

    // 如果有VBuffer，绑定它
    if (hasVBuffer)
    {
        var["gVBuffer"] = pVBuffer;
    }

    // 执行...
}
```

2. 更新IrradiancePass.cs.slang（只读取法线，暂不用于计算）

```cpp
// 添加输入
Texture2D<PackedHitInfo> gVBuffer;

// 更新参数
cbuffer PerFrameCB
{
    bool gReverseRayDirection;
    float gIntensityScale;
    bool gDebugNormalView;
    bool gUseActualNormals;
    float3 gFixedNormal;
}

// 在主函数中
float3 normal;
if (gUseActualNormals)
{
    // 尝试获取实际法线，但暂时只用于调试视图
    normal = float3(0, 0, 1); // 默认值

    // 这里先简单获取一下，不涉及复杂的场景接口
    // 实际实现时会根据VBuffer数据获取实际法线
}
else
{
    // 使用固定法线
    normal = normalize(gFixedNormal);
}

// 在输出部分
if (gDebugNormalView)
{
    // 将法线方向映射到颜色空间 [-1,1] -> [0,1]
    float3 normalColor = (normal + 1.0f) * 0.5f;
    gOutputIrradiance[pixel] = float4(normalColor, 1.0f);
}
else
{
    // 仍然使用固定法线计算辐照度
    float3 fixedNormal = normalize(gFixedNormal);
    float cosTheta = max(0.0f, dot(fixedNormal, rayDir));
    float irradiance = cosTheta * intensity * gIntensityScale;
    gOutputIrradiance[pixel] = float4(irradiance, irradiance, irradiance, 1.0f);
}
```

#### 任务4成功标准

- ✓ 编译通过，程序正常运行没有着色器编译错误或其他异常
- ✓ 启用Debug Normal View，能够看到颜色显示，与任务2一致（因为我们还没有真正读取VBuffer中的法线数据）
- ✓ 在Falcor的日志窗口没有关于纹理绑定的错误
- ✓ 可以通过勾选/取消勾选Use Actual Normals选项验证UI交互正常工作
- ✓ 对于此阶段，Debug Normal View的输出应当仍然是固定颜色，因为我们只是设置了框架但尚未真正读取法线

### 任务5：添加场景依赖和类型引用

为了能够从VBuffer中正确解析几何数据，需要添加对场景的依赖。

1. 修改IrradiancePass.h添加场景引用

```cpp
private:
    // 添加场景引用
    ref<Scene> mpScene;
    bool mNeedRecompile = false;  // 标记是否需要重新编译着色器
```

2. 实现setScene方法

```cpp
void IrradiancePass::setScene(RenderContext* pRenderContext, const ref<Scene>& pScene)
{
    mpScene = pScene;
    // 标记需要重新准备程序
    mNeedRecompile = true;
}
```

3. 修改prepareProgram方法，添加场景shader模块

```cpp
void IrradiancePass::prepareProgram()
{
    // 如果没有场景且需要访问场景数据，暂时不创建
    if (mUseActualNormals && !mpScene)
        return;

    ProgramDesc desc;
    desc.addShaderLibrary(kShaderFile).csEntry("main");

    // 添加场景相关的shader模块
    if (mpScene)
    {
        desc.addShaderModules(mpScene->getShaderModules());
        desc.addTypeConformances(mpScene->getTypeConformances());
    }

    mpComputePass = ComputePass::create(mpDevice, desc);
    mNeedRecompile = false;
}
```

4. 修改execute方法检查是否需要重新编译

```cpp
void IrradiancePass::execute(RenderContext* pRenderContext, const RenderData& renderData)
{
    // 现有代码...

    // 如果需要重新编译，先准备程序
    if (mNeedRecompile)
    {
        prepareProgram();
    }

    // 后续代码...
}
```

#### 任务5成功标准

- ✓ 成功加载渲染图，没有编译错误
- ✓ mpScene成员被正确设置（可以添加临时日志输出验证，例如在setScene方法中输出"Scene set" + (pScene ? "有效" : "无效")）
- ✓ 调用执行顺序正确：先setScene，然后是reflect/compile，最后是execute
- ✓ 程序能正确处理场景为空的情况（不应崩溃）
- ✓ 性能上没有明显影响，框架能够准备好正确的着色器编译环境

### 任务6：完成法线获取和辐照度计算

最后实现完整的法线获取和辐照度计算。

1. 更新IrradiancePass.cs.slang导入场景相关模块

```cpp
import Scene.Shading;  // 导入场景着色模块
```

2. 实现完整的法线获取逻辑

```cpp
float3 normal;
if (gUseActualNormals)
{
    // 默认值
    normal = float3(0, 0, 1);

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
}
else
{
    // 使用固定法线
    normal = normalize(gFixedNormal);
}

// 计算余弦项和辐照度
float cosTheta = max(0.0f, dot(normal, rayDir));
float irradiance = cosTheta * intensity * gIntensityScale;
```

3. 在execute方法中绑定场景数据

```cpp
// 如果使用实际法线，绑定场景数据
if (mUseActualNormals && hasVBuffer && mpScene)
{
    var["gScene"] = mpScene->getParameterBlock();
}
```

#### 任务6成功标准

- ✓ 编译通过，运行无错误
- ✓ 在Debug Normal View模式下，现在能看到场景中不同法线方向显示为不同的颜色
  - 对于球体：应该能看到球面上法线方向的渐变，顶部偏蓝色，侧面偏绿色或红色
  - 对于平面：应该看到相对统一的颜色，对应平面的法线方向
- ✓ 启用"Use Actual Normals"选项，关闭Debug View，现在IrradianceToneMapper输出应显示整个场景的辐照度，而不再只有左上角一小片
- ✓ 改变观察角度，辐照度显示应该相应变化，特别是曲面上的辐照度分布
- ✓ 性能没有明显下降，渲染速度和之前相近

## 任务7 辐照度显示UI问题分析与修复方案

### 问题描述

在IrradianceToneMapper的debug窗口中，只显示了实际画面中左上角的一小块区域（红框标出的部分），这一区域之外是Debug Windows的底纹，而不是显示的图像。其他渲染通道的画面都没有这个问题，只有IrradianceToneMapper的显示有问题。

### 问题分析

通过对Falcor渲染管线代码的分析，我发现这个问题是由两个因素共同导致的：

1. **图像尺寸不匹配**: IrradiancePass输出的纹理尺寸与Debug窗口期望的尺寸不匹配。在 `IrradiancePass.cpp`的 `execute`方法中，计算着色器的调度维度是基于输入纹理的维度而不是输出纹理的维度：

```cpp
// 获取输入纹理尺寸
uint32_t width = pInputRayInfo->getWidth();
uint32_t height = pInputRayInfo->getHeight();
// 使用输入纹理尺寸执行计算着色器
mpComputePass->execute(pRenderContext, { (width + 15) / 16, (height + 15) / 16, 1 });
```

如果输入纹理（PathTracer.initialRayInfo）的尺寸小于预期，那么IrradiancePass只会计算并写入输出纹理的一小部分区域，导致只有左上角显示有效内容。

2. **纹理尺寸选择**: 在渲染图配置中，各个Pass的输出尺寸由 `RenderPassHelpers::IOSize`控制。不同的Pass可能使用不同的尺寸设置：

```cpp
RenderPassHelpers::IOSize mOutputSizeSelection = RenderPassHelpers::IOSize::Default;
```

`IOSize`有三种选择：Default、From Input、Fixed。如果PathTracer和IrradiancePass使用不同的尺寸选择策略，就会导致不匹配。

此外，调试窗口只显示实际渲染的内容，而不会自动缩放或拉伸图像，因此只有真正有像素数据的区域会显示，导致只能看到左上角的一小部分。

### 解决方案

有三种解决方案：

#### 方案1：修改IrradiancePass计算着色器的调度方式

在 `IrradiancePass.cpp`的 `execute`方法中，使用输出纹理的尺寸而不是输入纹理的尺寸来调度计算着色器：

```cpp
// 获取输出纹理
const auto& pOutputIrradiance = renderData.getTexture(kOutputIrradiance);

// 使用输出纹理尺寸执行计算着色器
uint32_t width = pOutputIrradiance->getWidth();
uint32_t height = pOutputIrradiance->getHeight();
mpComputePass->execute(pRenderContext, { (width + 15) / 16, (height + 15) / 16, 1 });
```

这样可以确保IrradiancePass处理整个输出纹理，而不仅仅是左上角的一小部分。

#### 方案2：在渲染图中明确设置IrradiancePass的输出尺寸

修改 `PathTracerWithIrradiance.py`，明确指定IrradiancePass和IrradianceToneMapper的输出大小：

```python
# 添加IrradiancePass通道，指定输出尺寸
IrradiancePass = createPass("IrradiancePass", {
    'enabled': True,
    'reverseRayDirection': True,
    'intensityScale': 1.0,
    'useActualNormals': False,
    'outputSize': 'Default'  # 明确设置输出尺寸
})

# 添加IrradianceToneMapper，确保使用相同的输出尺寸设置
IrradianceToneMapper = createPass("ToneMapper", {
    'autoExposure': False,
    'exposureCompensation': 0.0,
    'outputSize': 'Default'
})
```

这样可以确保整个渲染管线使用一致的输出尺寸。

#### 方案3：在IrradiancePass的着色器中处理纹理尺寸不匹配

修改 `IrradiancePass.cs.slang`中的计算着色器代码，确保它能处理输入和输出纹理尺寸不匹配的情况：

```cpp
[numthreads(16, 16, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint2 pixel = dispatchThreadId.xy;

    // 获取输入和输出纹理的尺寸
    uint input_width, input_height;
    uint output_width, output_height;
    gInputRayInfo.GetDimensions(input_width, input_height);
    gOutputIrradiance.GetDimensions(output_width, output_height);

    // 检查是否超出输出边界
    if (any(pixel >= uint2(output_width, output_height))) return;

    // 计算对应的输入纹理坐标（使用比例缩放）
    float2 input_uv = float2(pixel) / float2(output_width, output_height);
    uint2 input_pixel = min(uint2(input_uv * float2(input_width, input_height)), uint2(input_width-1, input_height-1));

    // 从输入纹理采样
    float4 rayInfo = gInputRayInfo[input_pixel];

    // ... 后续计算和输出代码 ...
}
```

这种方法允许IrradiancePass在输入和输出纹理尺寸不匹配时进行适当的插值。

### 推荐方案

推荐使用**方案1**，因为它是最简单直接的解决方案，不需要修改渲染图配置或着色器代码的主要逻辑。只需修改IrradiancePass的execute方法，使用输出纹理的尺寸来调度计算着色器，就可以确保整个输出纹理都得到处理。

### 实施步骤（方案1）

1. 修改 `Source/RenderPasses/IrradiancePass/IrradiancePass.cpp`文件中的 `execute`方法：

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

    // 获取输出纹理
    const auto& pOutputIrradiance = renderData.getTexture(kOutputIrradiance);

    // If disabled, clear output and return
    if (!mEnabled)
    {
        pRenderContext->clearUAV(pOutputIrradiance->getUAV().get(), float4(0.f));
        return;
    }

    // Prepare resources and ensure shader program is updated
    prepareResources(pRenderContext, renderData);

    // Set shader constants
    auto var = mpComputePass->getRootVar();

    // 通过cbuffer名称路径访问变量
    var[kPerFrameCB][kReverseRayDirection] = mReverseRayDirection;
    var[kPerFrameCB][kIntensityScale] = mIntensityScale;
    var[kPerFrameCB][kDebugNormalView] = mDebugNormalView;

    // 全局纹理资源绑定保持不变
    var["gInputRayInfo"] = pInputRayInfo;
    var["gOutputIrradiance"] = pOutputIrradiance;

    // 使用输出纹理的尺寸执行计算着色器（而不是输入纹理的尺寸）
    uint32_t width = pOutputIrradiance->getWidth();
    uint32_t height = pOutputIrradiance->getHeight();
    mpComputePass->execute(pRenderContext, { (width + 15) / 16, (height + 15) / 16, 1 });
}
```

2. 另外，还需要在 `IrradiancePass.cs.slang`中确保处理好输入纹理坐标的映射：

```cpp
// 在main函数中修改输入坐标的获取方式
uint input_width, input_height, output_width, output_height;
gInputRayInfo.GetDimensions(input_width, input_height);
gOutputIrradiance.GetDimensions(output_width, output_height);

// 检查是否超出输入边界
if (any(pixel >= uint2(input_width, input_height)))
{
    // 对于超出输入范围的像素，使用边界或进行采样插值
    float2 uv = float2(pixel) / float2(output_width, output_height);
    uint2 input_pixel = min(uint2(uv * float2(input_width, input_height)), uint2(input_width-1, input_height-1));
    rayInfo = gInputRayInfo[input_pixel];
}
else
{
    // 直接读取输入值
    rayInfo = gInputRayInfo[pixel];
}
```

通过以上修改，IrradiancePass将能够处理整个输出纹理，而不仅仅是左上角的一小部分，从而解决只显示小区域的问题。

## 实现顺序和调试建议

按照上述任务顺序实现，每完成一个任务后可以测试和验证：

1. **任务1**: 验证固定法线可视化是否正常工作
2. **任务2**: 验证UI控制是否能改变固定法线方向，并观察辐照度变化
3. **任务3**: 验证VBuffer输入是否正确连接（通过日志检查）
4. **任务4**: 验证是否能读取VBuffer数据，并通过法线可视化查看是否正确
5. **任务5**: 验证场景引用是否正确设置
6. **任务6**: 验证完整功能，比较使用实际法线和固定法线的辐照度差异

## 调试提示

1. **法线可视化**: 开启法线可视化功能，看是否显示多彩的法线图，验证法线数据读取是否正确。
2. **渐进式启用**: 先在简单场景（如单个平面或球体）上测试，确认基本功能正常后再尝试复杂场景。
3. **对比测试**: 添加一个快捷键或UI选项，快速切换使用固定法线和实际法线，观察差异。
4. **检查连接**: 如果遇到问题，首先检查渲染图中的连接是否正确，VBuffer是否正确传递给IrradiancePass。
5. **增量修改**: 对于每项修改，先检查是否能编译通过，再检查是否能运行，最后检查结果是否符合预期。

通过分步实现和验证，可以更容易地找出和修复问题，确保最终解决方案的正确性。
