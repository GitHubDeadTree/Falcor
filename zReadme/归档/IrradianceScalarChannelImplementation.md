# IrradianceAccumulatePass单通道辐照度实现报告

## 1. 概述

本文档记录了我对Falcor引擎IrradianceAccumulatePass渲染通道的修改，增加了单通道辐照度输出的支持。通过这一实现，渲染系统现在能够单独处理和可视化辐照度的标量值，而不仅仅是RGB三通道的辐照度数据。

## 2. 实现背景

在原始的实现中，IrradianceAccumulatePass是基于AccumulatePass创建的，专门用于累积IrradiancePass通道输出的辐照度数据。该数据以RGBA格式的纹理表示，其中RGB三个通道存储相同的辐照度值，而A通道用于记录采样计数。然而，对于某些应用场景，使用单通道表示辐照度可以提高存储效率，并简化后续处理。

## 3. 修改内容

### 3.1 修改的文件

- **Source/RenderPasses/AccumulatePass/AccumulatePass.h**
  - 添加了单通道辐照度数据处理相关的成员变量和方法

- **Source/RenderPasses/AccumulatePass/AccumulatePass.cpp**
  - 添加了单通道输入/输出通道的声明和处理
  - 增加了accumulateScalar方法用于处理单通道数据的累积
  - 修改了texture资源的分配和管理逻辑以适应单通道数据

- **Source/RenderPasses/AccumulatePass/Accumulate.cs.slang**
  - 添加了单通道辐照度的输入/输出纹理声明
  - 实现了三种精度模式下（Single、SingleCompensated、Double）的单通道累积着色器函数

- **scripts/PathTracerWithIrradiance.py**
  - 添加了新的IrradianceScalarAccumulatePass实例用于处理单通道辐照度数据
  - 增加了相应的通道连接和输出配置

### 3.2 核心功能实现

#### 3.2.1 单通道输入/输出支持

在AccumulatePass.cpp中，我添加了新的通道定义：

```cpp
const char kInputScalarChannel[] = "inputScalar";
const char kOutputScalarChannel[] = "outputScalar";
```

并在reflect方法中为其配置了资源：

```cpp
reflector.addInput(kInputScalarChannel, "Single-channel input data to be temporally accumulated")
    .bindFlags(ResourceBindFlags::ShaderResource)
    .flags(RenderPassReflection::Field::Flags::Optional);
reflector.addOutput(kOutputScalarChannel, "Single-channel output data that is temporally accumulated")
    .bindFlags(ResourceBindFlags::RenderTarget | ResourceBindFlags::UnorderedAccess | ResourceBindFlags::ShaderResource)
    .format(scalarFmt)
    .texture2D(sz.x, sz.y);
```

#### 3.2.2 单通道累积实现

在Accumulate.cs.slang中，为三种精度模式实现了对应的单通道累积功能：

- **accumulateScalarSingle**: 使用单精度浮点数进行标准累积
- **accumulateScalarSingleCompensated**: 使用补偿求和方法（Kahan Summation）提高累积精度
- **accumulateScalarDouble**: 使用双精度浮点数进行高精度累积

每个方法都支持指数移动平均（EMA）模式和无帧限制的高精度累积模式。

#### 3.2.3 渲染图配置

在PathTracerWithIrradiance.py中，添加了新的单通道辐照度处理流程：

```python
# 新增：单通道辐照度累积通道
IrradianceScalarAccumulatePass = createPass("AccumulatePass", {'enabled': True, 'precisionMode': 'Single'})
g.addPass(IrradianceScalarAccumulatePass, "IrradianceScalarAccumulatePass")

# 连接单通道辐照度输出到单通道累积通道
g.addEdge("IrradiancePass.irradianceScalar", "IrradianceScalarAccumulatePass.inputScalar")

# 标记单通道辐照度输出
g.markOutput("IrradianceScalarToneMapper.dst")
```

## 4. 使用方法

要使用单通道辐照度功能，只需加载修改后的PathTracerWithIrradiance渲染图，系统将自动处理和显示三种不同的输出：

1. **标准颜色输出**: PathTracer的主输出
2. **RGB辐照度输出**: 以三通道形式表示的辐照度数据
3. **单通道辐照度输出**: 使用单通道表示的辐照度数据

## 5. 实现过程中的挑战与解决方案

### 5.1 资源共享与同步

在实现过程中，最大的挑战是如何在同一个通道中支持两种不同类型的输入/输出（标准RGBA和单通道），同时保持帧计数器的一致性。解决方案是：

1. 将两种输入/输出类型设置为可选（Optional），允许渲染通道处理一种或两种类型的数据
2. 在处理单通道数据时，检测标准RGBA通道是否也在处理数据，如果是，则避免重复增加帧计数器

```cpp
// 避免重复计数，只有在没有标准RGB通道的情况下才增加帧计数
if (mpVars == nullptr)
{
    mFrameCount++;
}
```

### 5.2 资源管理

为了有效管理单通道辐照度的中间数据，我实现了一组新的纹理资源：

```cpp
ref<Texture> mpScalarLastFrameSum;     // 单通道数据的累积和
ref<Texture> mpScalarLastFrameCorr;    // 单通道数据的累积误差校正项
ref<Texture> mpScalarLastFrameSumLo;   // 双精度模式下的低位部分
ref<Texture> mpScalarLastFrameSumHi;   // 双精度模式下的高位部分
```

这些资源根据精度模式的需要动态创建和管理，确保资源使用的高效性。

### 5.3 遇到的错误与修复

#### 5.3.1 markOutput参数错误

**错误描述**:
在实现过程中，我们遇到了`PathTracerWithIrradiance.py`脚本中的一个错误：

```
TypeError: markOutput(): incompatible function arguments. The following argument types are supported:
    1. (self: falcor.falcor_ext.RenderGraph, name: str, mask: falcor.falcor_ext.TextureChannelFlags = TextureChannelFlags.RGB) -> None

Invoked with: <falcor.falcor_ext.RenderGraph object at 0x000001A4B5E37E30>, 'IrradianceScalarToneMapper.dst', 'irradianceScalar'
```

**原因分析**:
`markOutput()`函数的第二个参数应该是一个`TextureChannelFlags`类型，而不是字符串。我错误地将输出的自定义名称作为第二个参数传递。

**修复方法**:
修改`PathTracerWithIrradiance.py`文件，删除第二个参数：

```python
# 修改前：
g.markOutput("IrradianceScalarToneMapper.dst", "irradianceScalar")

# 修改后：
g.markOutput("IrradianceScalarToneMapper.dst")
```

#### 5.3.2 着色器中的中文注释

**错误描述**:
虽然没有直接报错，但是着色器文件`Accumulate.cs.slang`中使用了中文注释，这可能在某些编译环境或字符编码设置下导致问题。

**修复方法**:
将所有中文注释替换为英文注释，保持原有注释的意义：

```glsl
// 修改前：
// 单通道辐照度的单精度标准累积

// 修改后：
// Single precision standard summation for scalar irradiance.
```

这样可以确保在所有环境中都能正确编译着色器代码。

## 6. 总结

通过这次实现，我成功地为IrradianceAccumulatePass添加了单通道辐照度输出支持，丰富了渲染系统的功能。这一新功能使得渲染系统能够更灵活地表示和处理辐照度数据，特别是在需要高效存储和传输辐照度信息的场景中。

在实现过程中，我遇到了一些API使用和国际化方面的问题，通过适当的调整成功解决了这些问题。最终的实现保持了与原有系统的兼容性，同时提供了更高效的单通道辐照度处理功能。

单通道辐照度输出与原有的RGB辐照度输出相比，具有更低的内存占用和更高的处理效率，同时保持了相同的精度控制选项，包括单精度、补偿累积和双精度模式。
