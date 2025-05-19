# 辐照度相关渲染通道输出实现详解

本文档详细记录了Falcor引擎中与辐照度计算相关的三个渲染通道的输出实现：PathTracer的初始光线信息输出、IrradiancePass的辐照度输出以及IrradianceAccumulatePass的累积输出。

## 1. PathTracer输出通道: initialRayInfo

PathTracer渲染通道输出了初始光线方向和强度信息，这是辐照度计算的重要输入。

### 1.1 输出通道定义 (PathTracer.cpp)

```cpp
// 定义输出通道名称
const std::string kOutputInitialRayInfo = "initialRayInfo";

// 在输出通道列表中添加initialRayInfo通道
const Falcor::ChannelList kOutputChannels =
{
    // 其他输出通道...
    { kOutputInitialRayInfo, "", "Initial ray direction and intensity", true /* optional */, ResourceFormat::RGBA32Float },
    // 其他输出通道...
};
```

### 1.2 输出标志检测 (PathTracer.cpp)

```cpp
// 在beginFrame方法中检测是否需要输出初始光线信息
bool prevOutputInitialRayInfo = mOutputInitialRayInfo;
mOutputInitialRayInfo = renderData[kOutputInitialRayInfo] != nullptr;
if (mOutputInitialRayInfo != prevOutputInitialRayInfo) mRecompile = true;
```

### 1.3 缓冲区创建 (PathTracer.cpp)

```cpp
// 在prepareResources方法中创建初始光线信息缓冲区
if (mOutputInitialRayInfo && (!mpSampleInitialRayInfo || mpSampleInitialRayInfo->getElementCount() < sampleCount || mVarsChanged))
{
    mpSampleInitialRayInfo = mpDevice->createStructuredBuffer(var["sampleInitialRayInfo"], sampleCount, ResourceBindFlags::ShaderResource | ResourceBindFlags::UnorderedAccess, MemoryType::DeviceLocal, nullptr, false);
    mVarsChanged = true;
}
```

### 1.4 着色器宏定义 (PathTracer.cpp)

```cpp
// 在StaticParams::getDefines方法中添加宏定义
defines.add("OUTPUT_INITIAL_RAY_INFO", owner.mOutputInitialRayInfo ? "1" : "0");
```

### 1.5 着色器资源绑定 (PathTracer.cpp)

```cpp
// 在bindShaderData方法中绑定资源
var["sampleInitialRayInfo"] = mpSampleInitialRayInfo;
```

### 1.6 渲染通道解析器中的绑定 (PathTracer.cpp)

```cpp
// 在resolvePass方法中绑定输出纹理
var["outputInitialRayInfo"] = renderData.getTexture(kOutputInitialRayInfo);

// 绑定样本缓冲区
var["sampleInitialRayInfo"] = mpSampleInitialRayInfo;
```

### 1.7 着色器输出实现 (PathTracer.slang)

```cpp
// 在结构体定义中添加输出缓冲区
RWStructuredBuffer<float4> sampleInitialRayInfo; ///< Output per-sample initial ray direction and intensity data.

// 添加静态常量
static const bool kOutputInitialRayInfo = OUTPUT_INITIAL_RAY_INFO;

// 在writeOutput方法中写入数据
void writeOutput(const PathState path)
{
    // 其他输出写入...

    if (kOutputInitialRayInfo)
    {
        sampleInitialRayInfo[outIdx] = float4(path.initialDir, luminance(path.L));
    }

    // 其他输出写入...
}
```

### 1.8 解析通道实现 (ResolvePass.cs.slang)

```cpp
// 在结构体定义中添加输入和输出
StructuredBuffer<float4> sampleInitialRayInfo;     ///< Input per-sample initial ray direction and intensity data.
RWTexture2D<float4> outputInitialRayInfo;          ///< Output resolved initial ray direction and intensity.

// 添加静态常量
static const bool kOutputInitialRayInfo = OUTPUT_INITIAL_RAY_INFO;

// 在execute方法中处理数据
if (kOutputInitialRayInfo)
{
    float4 initialRayInfo = float4(0.f);

    for (uint sampleIdx = 0; sampleIdx < spp; sampleIdx++)
    {
        uint idx = offset + sampleIdx;
        initialRayInfo += sampleInitialRayInfo[idx];
    }

    outputInitialRayInfo[pixel] = float4(invSpp * initialRayInfo.xyz, initialRayInfo.w * invSpp);
}
```

## 2. IrradiancePass输出通道

IrradiancePass渲染通道处理初始光线信息并输出辐照度值，同时提供RGB三通道和单通道输出格式。

### 2.1 输出通道定义 (IrradiancePass.cpp)

```cpp
// 定义输出通道名称
const std::string kOutputIrradiance = "irradiance";
const std::string kOutputIrradianceScalar = "irradianceScalar";

// 在反射方法中添加输出通道
reflector.addOutput(kOutputIrradiance, "Calculated irradiance per pixel")
    .bindFlags(ResourceBindFlags::UnorderedAccess)
    .format(ResourceFormat::RGBA32Float);

reflector.addOutput(kOutputIrradianceScalar, "Calculated scalar irradiance per pixel")
    .bindFlags(ResourceBindFlags::UnorderedAccess)
    .format(ResourceFormat::R32Float);
```

### 2.2 输出资源处理 (IrradiancePass.cpp)

```cpp
// 在execute方法中获取输出纹理
auto pOutputIrradiance = renderData.getTexture(kOutputIrradiance);
auto pOutputScalarIrradiance = renderData.getTexture(kOutputIrradianceScalar);

// 清除输出纹理
if (mEnabled)
{
    pRenderContext->clearUAV(pOutputIrradiance->getUAV().get(), float4(0.f));
    pRenderContext->clearUAV(pOutputScalarIrradiance->getUAV().get(), float4(0.f));
}
else
{
    // 如果未启用，但需要输出，则使用默认值填充
    pRenderContext->clearUAV(pOutputIrradiance->getUAV().get(), float4(0.f, 0.f, 0.f, 1.f));
    pRenderContext->clearUAV(pOutputScalarIrradiance->getUAV().get(), float4(0.f));
    return;
}

// 绑定输出纹理给着色器
var["gOutputIrradiance"] = pOutputIrradiance;
var["gOutputIrradianceScalar"] = pOutputScalarIrradiance;
```

### 2.3 结果缓存处理 (IrradiancePass.cpp)

```cpp
// 创建和管理用于存储上一帧结果的纹理
if (mUseLastResult && !mpLastIrradianceResult)
{
    mpLastIrradianceResult = Texture::create2D(pOutputIrradiance->getWidth(), pOutputIrradiance->getHeight(),
        pOutputIrradiance->getFormat(), 1, 1, nullptr, ResourceBindFlags::ShaderResource | ResourceBindFlags::UnorderedAccess);
}

// 为单通道输出创建缓存
if (mUseLastResult && !mpLastIrradianceScalarResult)
{
    mpLastIrradianceScalarResult = Texture::create2D(pOutputScalarIrradiance->getWidth(), pOutputScalarIrradiance->getHeight(),
        pOutputScalarIrradiance->getFormat(), 1, 1, nullptr, ResourceBindFlags::ShaderResource | ResourceBindFlags::UnorderedAccess);
}

// 如果需要使用上一帧结果但不需要计算，则复制上一帧结果
if (!shouldCompute && mUseLastResult)
{
    copyLastResultToOutput(pRenderContext, pOutputIrradiance);
    copyLastScalarResultToOutput(pRenderContext, pOutputScalarIrradiance);
    return;
}
```

### 2.4 计算结果复制 (IrradiancePass.cpp)

```cpp
// 复制当前结果到上一帧缓存
if (mUseLastResult)
{
    pRenderContext->copyResource(mpLastIrradianceResult.get(), pOutputIrradiance.get());
    pRenderContext->copyResource(mpLastIrradianceScalarResult.get(), pOutputScalarIrradiance.get());
}
```

### 2.5 着色器实现 (IrradiancePass.cs.slang)

```cpp
// 定义输出纹理
RWTexture2D<float4> gOutputIrradiance;
RWTexture2D<float> gOutputIrradianceScalar;

[numthreads(16, 16, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    uint2 pixel = dispatchThreadId.xy;

    // 检查像素是否在范围内
    if (any(pixel >= gFrameDim)) return;

    // 获取光线信息
    float4 rayInfo = gInitialRayInfo[pixel];
    float3 rayDir = rayInfo.xyz;
    float intensity = rayInfo.w;

    if (mPassthroughMode)
    {
        // 直通模式，直接输出原始值
        gOutputIrradiance[pixel] = float4(intensity.xxx, 1.0f);
        gOutputIrradianceScalar[pixel] = intensity;
        return;
    }

    if (mDebugNormalView)
    {
        // 调试模式，显示法线
        float3 normal = mUseActualNormals ? getActualNormal(pixel) : mFixedNormal;
        gOutputIrradiance[pixel] = float4((normal + 1.0f) * 0.5f, 1.0f);
        gOutputIrradianceScalar[pixel] = 0.0f;
        return;
    }

    // 获取法线
    float3 normal = mUseActualNormals ? getActualNormal(pixel) : mFixedNormal;

    // 根据配置反转光线方向
    if (mReverseRayDirection) rayDir = -rayDir;

    // 计算余弦项
    float cosTheta = max(0.0f, dot(normal, rayDir));

    // 计算辐照度
    float irradiance = intensity * cosTheta * mIntensityScale;

    // 输出结果
    gOutputIrradiance[pixel] = float4(irradiance.xxx, 1.0f);
    gOutputIrradianceScalar[pixel] = irradiance;
}
```

## 3. IrradianceAccumulatePass输出通道

IrradianceAccumulatePass通道对每帧的辐照度结果进行累积，以减少噪音并提高准确性。

### 3.1 输出通道定义 (AccumulatePass.cpp)

```cpp
// 定义输入和输出通道名称
const std::string kInputChannel = "input";
const std::string kOutputChannel = "output";

// 在反射方法中添加输入和输出
reflector.addInput(kInputChannel, "Input data to be accumulated");
reflector.addOutput(kOutputChannel, "Accumulated output data");
```

### 3.2 帧缓冲区创建 (AccumulatePass.cpp)

```cpp
// 创建累积帧缓冲区
void AccumulatePass::createFrameResources()
{
    // 根据精度模式创建适当的累积缓冲区
    switch (mPrecisionMode)
    {
    case PrecisionMode::Single:
        // 单精度模式只需要一个缓冲区
        if (!mpAccumBuffer)
        {
            mpAccumBuffer = Texture::create2D(mFrameDim.x, mFrameDim.y, ResourceFormat::RGBA32Float, 1, 1, nullptr,
                                          ResourceBindFlags::ShaderResource | ResourceBindFlags::UnorderedAccess);
        }
        break;

    case PrecisionMode::Double:
        // 双精度模式需要两个缓冲区
        if (!mpAccumBuffer || !mpAccumBuffer2)
        {
            mpAccumBuffer = Texture::create2D(mFrameDim.x, mFrameDim.y, ResourceFormat::RGBA32Float, 1, 1, nullptr,
                                          ResourceBindFlags::ShaderResource | ResourceBindFlags::UnorderedAccess);
            mpAccumBuffer2 = Texture::create2D(mFrameDim.x, mFrameDim.y, ResourceFormat::RGBA32Float, 1, 1, nullptr,
                                           ResourceBindFlags::ShaderResource | ResourceBindFlags::UnorderedAccess);
        }
        break;

    case PrecisionMode::SingleCompensated:
        // 补偿单精度模式需要两个缓冲区
        if (!mpAccumBuffer || !mpCompensationBuffer)
        {
            mpAccumBuffer = Texture::create2D(mFrameDim.x, mFrameDim.y, ResourceFormat::RGBA32Float, 1, 1, nullptr,
                                          ResourceBindFlags::ShaderResource | ResourceBindFlags::UnorderedAccess);
            mpCompensationBuffer = Texture::create2D(mFrameDim.x, mFrameDim.y, ResourceFormat::RGBA32Float, 1, 1, nullptr,
                                                 ResourceBindFlags::ShaderResource | ResourceBindFlags::UnorderedAccess);
        }
        break;
    }

    // 创建累积计数缓冲区
    if (!mpAccumCountBuffer)
    {
        mpAccumCountBuffer = Texture::create2D(mFrameDim.x, mFrameDim.y, ResourceFormat::R32Uint, 1, 1, nullptr,
                                           ResourceBindFlags::ShaderResource | ResourceBindFlags::UnorderedAccess);
    }
}
```

### 3.3 着色器计算实现 (AccumulatePass.cs.slang)

```cpp
// 单精度累积计算
void accumulateSingle(uint2 pixel, float4 input)
{
    if (!gReset)
    {
        // 读取当前累积值和计数
        uint count = gAccumCount[pixel];
        float4 accum = gAccumBuffer[pixel];

        // 更新计数
        count++;

        // 判断是否达到最大帧数并处理溢出
        if (gMaxFrameCount > 0 && count > gMaxFrameCount)
        {
            // 根据溢出模式处理
            switch (gOverflowMode)
            {
            case OverflowMode::Stop:
                gOutput[pixel] = accum;
                return;

            case OverflowMode::Reset:
                accum = input;
                count = 1;
                break;

            case OverflowMode::EMA:
                // 指数移动平均
                float alpha = 2.0f / (gMaxFrameCount + 1);
                accum = lerp(accum, input, alpha);
                break;
            }
        }
        else
        {
            // 正常累积
            accum = (accum * (count - 1) + input) / count;
        }

        // 更新累积缓冲区和计数
        gAccumBuffer[pixel] = accum;
        gAccumCount[pixel] = count;
        gOutput[pixel] = accum;
    }
    else
    {
        // 重置累积
        gAccumBuffer[pixel] = input;
        gAccumCount[pixel] = 1;
        gOutput[pixel] = input;
    }
}

// 双精度累积计算
void accumulateDouble(uint2 pixel, float4 input)
{
    // 类似实现，但使用两个缓冲区存储高低位
    // ...
}

// 补偿单精度累积计算（Kahan求和算法）
void accumulateSingleCompensated(uint2 pixel, float4 input)
{
    // 使用Kahan求和算法减少舍入误差
    // ...
}

[numthreads(16, 16, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    uint2 pixel = dispatchThreadId.xy;

    // 检查像素是否在范围内
    if (any(pixel >= gFrameDim)) return;

    // 获取输入值
    float4 input = gInput[pixel];

    // 根据精度模式选择累积算法
    switch (gPrecisionMode)
    {
    case PrecisionMode::Single:
        accumulateSingle(pixel, input);
        break;

    case PrecisionMode::Double:
        accumulateDouble(pixel, input);
        break;

    case PrecisionMode::SingleCompensated:
        accumulateSingleCompensated(pixel, input);
        break;
    }
}
```

## 4. 总结

这三个渲染通道通过以下数据流实现了完整的辐照度计算流程：

1. **PathTracer** 输出 `initialRayInfo`（RGBA32Float格式）：
   - RGB(xyz)：初始光线方向
   - Alpha(w)：光线强度/辐射亮度

2. **IrradiancePass** 接收 `initialRayInfo` 并输出：
   - `irradiance`（RGBA32Float格式）：三通道辐照度值
   - `irradianceScalar`（R32Float格式）：单通道辐照度值

3. **IrradianceAccumulatePass** 接收 `irradiance` 或 `irradianceScalar` 并输出：
   - `output`：时间累积后的辐照度值，格式与输入相同

通过这些通道的协同工作，可以获得稳定、准确的辐照度计算结果，为光照分析提供可靠的数据支持。
