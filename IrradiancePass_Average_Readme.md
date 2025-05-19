# IrradianceAccumulatePass 辐照度平均值计算功能

## 功能介绍

本次修改为AccumulatePass添加了计算单通道辐照度平均值的功能，这使得IrradianceAccumulatePass实例（使用AccumulatePass创建）能够计算并显示时间累积后的单通道辐照度平均值。具体实现了以下内容：

1. 使用`ParallelReduction`类实现高效的纹理平均值计算
2. 在UI中显示计算出的平均辐照度值
3. 提供开关控制是否需要计算平均值
4. 实现了对计算结果的异步读取，避免不必要的GPU同步等待
5. 添加了获取平均辐照度值的API接口，方便其他组件使用

## 实现细节

### 修改的文件

1. `Source/RenderPasses/AccumulatePass/AccumulatePass.h`
   - 添加ParallelReduction相关头文件引用
   - 添加成员变量：ParallelReduction实例、结果缓冲区、平均值存储变量
   - 添加计算平均值的方法声明和API接口

2. `Source/RenderPasses/AccumulatePass/AccumulatePass.cpp`
   - 实现ParallelReduction初始化
   - 在execute方法中添加平均值计算的调用
   - 实现computeAverageValue方法
   - 在UI中添加显示和控制元素
   - 添加对Python绑定的支持

### 关键技术点

1. **针对AccumulatePass的修改**：由于IrradianceAccumulatePass是基于AccumulatePass实现的，我们通过修改AccumulatePass基类来为所有使用该类创建的实例添加平均值计算功能。

2. **并行归约算法**：使用Falcor提供的`ParallelReduction`类来实现高效的GPU并行归约计算。该类可以在GPU上高效地对大型纹理数据执行归约操作（如求和、求最大/最小值）。

3. **异步计算**：通过将结果写入缓冲区而不是直接读取，避免了GPU同步等待，提高性能。只在需要显示结果时才从缓冲区读取。

4. **条件计算**：根据UI设置有条件地执行平均值计算，避免不必要的计算开销。

5. **接口扩展**：添加了`getAverageValue()`方法，提供了获取计算结果的编程接口。

## 使用方法

### 在UI中查看平均值

在AccumulatePass（包括IrradianceAccumulatePass）的UI界面中，新增了"Average Value"部分，包括：

- **Compute Average** 复选框：控制是否计算平均值
- **Average Value** 文本显示：显示当前计算出的平均辐照度值

### 通过代码访问平均值

可以通过以下方式在Python脚本或C++代码中获取平均辐照度值：

#### Python示例
```python
# 获取IrradianceScalarAccumulatePass实例
accumPass = graph.getPass("IrradianceScalarAccumulatePass")
# 获取平均辐照度值
avgValue = accumPass.averageValue
# 使用avgValue...
```

#### C++示例
```cpp
// 获取IrradianceScalarAccumulatePass实例
auto pAccumPass = renderGraph->getPass("IrradianceScalarAccumulatePass");
if (auto accumPass = std::dynamic_pointer_cast<AccumulatePass>(pAccumPass))
{
    // 获取平均辐照度值
    float avgValue = accumPass->getAverageValue();
    // 使用avgValue...
}
```

## 与IrradiancePass的区别

IrradiancePass和IrradianceAccumulatePass都添加了平均值计算功能，但它们计算的是不同阶段的辐照度值：

1. **IrradiancePass**：计算的是单帧辐照度的平均值，反映的是当前帧的平均光照强度。
2. **IrradianceAccumulatePass**：计算的是时间累积后的辐照度平均值，由于累积了多帧数据，噪声更低，更能反映场景的稳定光照强度。

## 实现中遇到的问题与解决方案

### 问题1：AccumulatePass类重构

**问题**：AccumulatePass类的实现比较复杂，有多种精度模式和溢出处理机制。

**解决方案**：保持核心功能不变，只在合适的位置添加平均值计算功能。特别是在处理单通道（scalar）数据的部分添加了平均值计算的调用。

### 问题2：非Object类的处理

**问题**：ParallelReduction类不继承自Falcor的Object基类，不能使用Falcor的ref<T>智能指针。

**解决方案**：使用std::unique_ptr来管理ParallelReduction实例，确保正确的内存管理。

### 问题3：Python绑定

**问题**：需要让Python脚本能够访问平均值属性。

**解决方案**：在注册Python绑定时添加了averageValue属性绑定，使得可以在Python脚本中访问该属性。

## 注意事项

1. 平均值计算仅针对单通道（scalar）输出进行，不对RGB数据计算平均值。

2. 计算平均值会引入额外的GPU计算和内存开销，如不需要可以通过UI中的"Compute Average"选项关闭。

3. 平均值计算结果会随着累积帧数的增加而更加稳定，在刚开始累积时（帧数少）可能波动较大。

4. 当累积帧数为0时（如刚重置后），平均值计算无法提供有效结果。

## Falcor3 IrradiancePass and AccumulatePass Average Value Calculation

### Issue Analysis

We encountered an assertion failure in `AccumulatePass.cpp` at line 233:

```
Assertion failed: mFrameDim.x == width && mFrameDim.y == height
```

The error occurred during the execution of the `AccumulatePass::accumulate` method, where the method expected the `mFrameDim` value to match the dimensions of the input texture. However, the `prepareAccumulation` method wasn't setting the `mFrameDim` variable, leading to a dimension mismatch.

### Solution

The problem was resolved by modifying the `prepareAccumulation` method in `AccumulatePass.cpp`. We added code to explicitly set the `mFrameDim` variable to match the input width and height:

```cpp
void AccumulatePass::prepareAccumulation(RenderContext* pRenderContext, uint32_t width, uint32_t height)
{
    // Update frame dimensions
    mFrameDim = { width, height };

    // Original code follows...
}
```

This ensures that the `mFrameDim` is always in sync with the current texture dimensions before any operations that rely on this value.

### Implementation Overview

The project involves adding average value calculation functionality to two render passes:

1. **IrradiancePass:** For calculating the average value of single-frame irradiance textures.
2. **AccumulatePass:** For calculating the average value of temporally accumulated irradiance textures.

Both implementations use Falcor's `ParallelReduction` utility for efficient GPU-based reduction operations.

### Key Components of the Implementation

#### 1. ParallelReduction Usage

Both passes use the `ParallelReduction` class to efficiently compute averages on the GPU:

```cpp
mpParallelReduction->execute<float4>(
    pRenderContext,
    pTexture,
    ParallelReduction::Type::Sum,
    nullptr,  // Don't directly read results to avoid GPU sync wait
    mpAverageResultBuffer,
    0
);
```

#### 2. Memory Management

In both IrradiancePass and AccumulatePass, we used `std::unique_ptr` to manage the `ParallelReduction` instance since it's not a subclass of Falcor's `Object` and can't be managed with `ref<T>`:

```cpp
std::unique_ptr<ParallelReduction> mpParallelReduction;
```

#### 3. Asynchronous Computation

To avoid GPU stalls, we implemented an asynchronous computation approach:
- Results are written to a buffer rather than directly read
- The buffer is only read when the UI needs to display the value

### Implementation Details

#### AccumulatePass Modifications

1. **Header Changes (AccumulatePass.h):**
   - Added ParallelReduction header inclusion
   - Added member variables for average calculation
   - Added getAverageValue() method for external access

2. **Implementation Changes (AccumulatePass.cpp):**
   - Fixed the frame dimension initialization in prepareAccumulation
   - Implemented the computeAverageValue method
   - Added UI controls for the average calculation
   - Added Python bindings for the averageValue property

#### IrradiancePass Modifications

1. **Header Changes (IrradiancePass.h):**
   - Added ParallelReduction header inclusion
   - Added member variables for average calculation
   - Added getAverageIrradiance() method for external access

2. **Implementation Changes (IrradiancePass.cpp):**
   - Implemented the computeAverageIrradiance method
   - Added UI controls for the average calculation
   - Added conditional execution based on debug mode

### Usage Differences

The two implementations have different purposes:

- **IrradiancePass Average:** Calculates the average value of a single frame's irradiance.
- **AccumulatePass Average:** Calculates the average value of the temporally accumulated irradiance, which typically has less noise as it combines data from multiple frames.

### API Access

Both implementations provide a consistent API for retrieving the average value:

```cpp
// For IrradiancePass
float avgIrradiance = irradiancePass->getAverageIrradiance();

// For AccumulatePass
float avgValue = accumPass->getAverageValue();
```

### Encountered Issues and Solutions

#### Issue 1: Frame Dimension Mismatch

**Problem:** The assertion failure `mFrameDim.x == width && mFrameDim.y == height` indicated that the frame dimensions weren't being properly initialized.

**Solution:** Modified `prepareAccumulation` to set `mFrameDim` at the beginning of the method.

#### Issue 2: Memory Management

**Problem:** ParallelReduction is not an Object subclass in Falcor, so it can't use ref<T>.

**Solution:** Used std::unique_ptr instead for proper lifetime management.

#### Issue 3: Buffer Synchronization

**Problem:** Direct reading of GPU computation results can cause stalls.

**Solution:** Implemented asynchronous buffer reading pattern, only synchronizing when the UI needs to display values.

### Performance Considerations

1. The average calculation is optional and can be disabled through the UI.
2. The calculation result reading involves a GPU synchronization point, which could impact performance if used excessively.
3. For IrradiancePass, the calculation is only performed when the pass actually computes irradiance (not on skipped frames).

### Conclusion

The implementation successfully adds average value calculation capabilities to both IrradiancePass and AccumulatePass, allowing users to monitor the average irradiance levels in the scene. The bug in AccumulatePass was fixed by properly initializing the frame dimensions.
