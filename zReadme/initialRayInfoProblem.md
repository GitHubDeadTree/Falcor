# PathTracer中initialRayInfo输出通道问题分析

## 问题描述

当PathTracer的`samplesPerPixel`设置为1时，initialRayInfo输出通道没有任何输出。

## 原因分析

通过分析PathTracer.cpp代码，问题出在解析阶段的处理上：

1. 在`resolvePass`函数中（约1380行），程序会判断是否需要执行解析：
```cpp
void PathTracer::resolvePass(RenderContext* pRenderContext, const RenderData& renderData)
{
    if (!mOutputGuideData && !mOutputNRDData && !mOutputInitialRayInfo && mFixedSampleCount && mStaticParams.samplesPerPixel == 1) return;

    // ... 其余解析代码
}
```

2. 这个条件判断会在以下情况直接返回，跳过解析：
   - 不需要输出指导数据（`!mOutputGuideData`）
   - 不需要输出NRD数据（`!mOutputNRDData`）
   - 使用固定样本数（`mFixedSampleCount`）
   - 每像素样本数为1（`mStaticParams.samplesPerPixel == 1`）

3. 关键问题是：`initialRayInfo`输出通道的处理**没有**被包含在这个判断条件中

在这个提前返回的条件中，只检查了`mOutputGuideData`和`mOutputNRDData`这两个标志，但没有检查`mOutputInitialRayInfo`标志。当每像素只有1个样本时，系统认为不需要执行解析步骤，直接将tracePass的结果作为最终输出。

## 解决方案

修改`resolvePass`函数的判断条件，增加对`mOutputInitialRayInfo`的检查：

```cpp
void PathTracer::resolvePass(RenderContext* pRenderContext, const RenderData& renderData)
{
    if (!mOutputGuideData && !mOutputNRDData && !mOutputInitialRayInfo && mFixedSampleCount && mStaticParams.samplesPerPixel == 1) return;

    // ... 其余解析代码
}
```

通过增加`!mOutputInitialRayInfo`条件，可以确保当需要输出initialRayInfo时，即使每像素只有1个样本，也会执行解析步骤，从而正确处理initialRayInfo输出通道。

## 技术细节

在代码中，initialRayInfo的处理流程如下：

1. PathTracer会为每个样本生成初始光线信息，存储在`mpSampleInitialRayInfo`缓冲区中
2. `resolvePass`负责将这些样本数据解析到最终的输出纹理
3. 当每像素只有1个样本时，系统优化跳过了解析步骤，导致`initialRayInfo`通道没有得到正确处理

当解析步骤执行时，`resolvePass`函数中的这段代码会处理initialRayInfo输出：
```cpp
var["outputInitialRayInfo"] = renderData.getTexture(kOutputInitialRayInfo);
var["sampleInitialRayInfo"] = mpSampleInitialRayInfo;
```
