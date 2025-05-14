# 辐照度计算的法线修复方案（分步实现）

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
