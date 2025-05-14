# 辐照度计算区域不正常的解决方案

## 辐照度显示UI问题分析与修复方案

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


## 原理

让我详细解释为什么输入纹理尺寸小于预期会导致IrradiancePass只计算和写入输出纹理左上角区域的问题。

核心原因在于计算着色器的**线程调度方式**和**像素处理逻辑**：

### 1. 计算着色器的线程调度

在IrradiancePass.cpp的execute方法中，计算着色器的调度是基于输入纹理的尺寸进行的：

```cpp
// 获取输入纹理尺寸
uint32_t width = pInputRayInfo->getWidth();
uint32_t height = pInputRayInfo->getHeight();
// 使用输入纹理尺寸执行计算着色器
mpComputePass->execute(pRenderContext, { (width + 15) / 16, (height + 15) / 16, 1 });
```

这段代码告诉GPU要启动多少个线程组来执行计算着色器。每个线程组包含16×16个线程（这由着色器的[numthreads(16, 16, 1)]定义决定），每个线程处理一个像素。

如果输入纹理尺寸为512×512，那么计算得到的线程组数量为(512+15)/16×(512+15)/16 = 32×32 = 1024个线程组，总共有1024×16×16 = 262,144个线程，刚好对应512×512个像素。

但如果输入纹理尺寸只有128×128，那么计算得到的线程组数量为(128+15)/16×(128+15)/16 = 9×9 = 81个线程组，总共只有81×16×16 = 20,736个线程，只能处理128×128个像素。

### 2. 像素坐标和边界检查

在IrradiancePass.cs.slang着色器中，每个线程处理的像素坐标由线程ID决定：

```cpp
[numthreads(16, 16, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint2 pixel = dispatchThreadId.xy;

    // 跳过超出边界的像素
    uint width, height;
    gInputRayInfo.GetDimensions(width, height);
    if (any(pixel >= uint2(width, height))) return;

    // ... 处理当前像素 ...
}
```

这里的关键点是边界检查使用了**输入纹理**的尺寸 `gInputRayInfo.GetDimensions(width, height)`，而不是输出纹理的尺寸。当线程的 `pixel`坐标超出输入纹理范围时，线程会直接返回，不处理该像素。

### 3. 尺寸不匹配导致的具体问题

假设场景如下：

- 输入纹理（PathTracer.initialRayInfo）尺寸为128×128
- 输出纹理（IrradiancePass.irradiance）尺寸为1024×1024
- 调试窗口期望显示完整的1024×1024图像

以下是问题发生的具体步骤：

1. IrradiancePass根据输入纹理尺寸128×128启动9×9个线程组
2. 这些线程组只能覆盖输出纹理左上角的128×128区域
3. 在这128×128区域内，每个线程读取输入纹理对应位置的数据，计算辐照度，并写入输出纹理
4. 剩余的输出纹理区域（从(128,0)到(1023,1023)）没有线程处理，保持未初始化状态
5. 当IrradianceToneMapper处理这个输出纹理时，只有左上角128×128区域有有效数据
6. 调试窗口显示整个纹理，但只有左上角小区域可见，其余部分为空（或显示为底纹）

### 4. 为什么是左上角？

在计算机图形学中，纹理坐标系的原点(0,0)通常位于左上角，x轴向右，y轴向下。当线程ID从0开始递增时，最先处理的就是左上角区域的像素，所以有效内容总是出现在左上角。

### 5. 为什么其他通道没问题？

其他渲染通道可能：

1. 正确处理了输入/输出尺寸不匹配的情况
2. 在计算着色器调度时使用了输出纹理的尺寸
3. 或者确保了整个渲染管线使用一致的纹理尺寸

例如，ToneMapper通道会检查输入输出尺寸是否匹配，并发出警告：

```cpp
if (pSrc->getWidth() != pDst->getWidth() || pSrc->getHeight() != pDst->getHeight())
{
    logWarning("ToneMapper pass I/O has different dimensions. The image will be resampled.");
}
```

总结来说，IrradiancePass只处理了与输入纹理尺寸相匹配的输出区域，由于图形API中的坐标系原点在左上角，因此只有输出纹理的左上角区域显示有效内容，其余部分保持未处理状态，导致在调试窗口中只能看到左上角的一小块区域。
