# IrradiancePass 辐照度平均值计算功能

## 功能介绍

本次修改为IrradiancePass渲染通道添加了计算单通道辐照度平均值的功能。具体实现了以下内容：

1. 使用`ParallelReduction`类实现高效的纹理平均值计算
2. 在UI中显示计算出的平均辐照度值
3. 提供开关控制是否需要计算平均值
4. 实现了对计算结果的异步读取，避免不必要的GPU同步等待
5. 添加了获取平均辐照度值的API接口，方便其他组件使用

## 实现细节

### 修改的文件

1. `Source/RenderPasses/IrradiancePass/IrradiancePass.h`
   - 添加ParallelReduction相关头文件引用
   - 添加成员变量：ParallelReduction实例、结果缓冲区、平均值存储变量
   - 添加控制选项和相关方法

2. `Source/RenderPasses/IrradiancePass/IrradiancePass.cpp`
   - 实现ParallelReduction初始化
   - 在execute方法中添加平均值计算的调用
   - 实现computeAverageIrradiance方法
   - 在UI中添加显示和控制元素

### 关键技术点

1. **并行归约算法**：使用Falcor提供的`ParallelReduction`类来实现高效的GPU并行归约计算。该类可以在GPU上高效地对大型纹理数据执行归约操作（如求和、求最大/最小值）。

2. **异步计算**：通过将结果写入缓冲区而不是直接读取，避免了GPU同步等待，提高性能。只在需要显示结果时才从缓冲区读取。

3. **条件计算**：根据UI设置和调试模式状态有条件地执行平均值计算，避免不必要的计算开销。

4. **接口扩展**：添加了`getAverageIrradiance()`方法，提供了获取计算结果的编程接口。

## 使用方法

### 在UI中查看平均值

在IrradiancePass的UI界面中，新增了"Average Irradiance"部分，包括：

- **Compute Average** 复选框：控制是否计算平均值
- **Average Irradiance** 文本显示：显示当前计算出的平均辐照度值

### 通过代码访问平均值

可以通过以下方式在其他代码中获取平均辐照度值：

```cpp
// 获取IrradiancePass实例
auto pIrradiancePass = renderGraph->getPass("IrradiancePass");
if (auto irradiancePass = std::dynamic_pointer_cast<IrradiancePass>(pIrradiancePass))
{
    // 获取平均辐照度值
    float avgIrradiance = irradiancePass->getAverageIrradiance();
    // 使用avgIrradiance...
}
```

## 遇到的问题与解决方案

### 问题1：ParallelReduction模板参数类型匹配

**问题**：ParallelReduction.execute方法需要使用与纹理格式匹配的模板参数类型。

**解决方案**：根据文档和源码分析，对于单通道的float纹理，仍需使用float4类型作为模板参数。在计算平均值时，我们只使用返回的sum.x值。

### 问题2：计算结果同步

**问题**：直接从ParallelReduction获取结果会导致GPU同步等待，影响性能。

**解决方案**：使用缓冲区方式获取结果，并且只在UI显示需要时才同步读取缓冲区数据。这样可以避免在渲染管线中引入不必要的同步点。

### 问题3：条件执行

**问题**：在某些情况下不应该执行平均值计算（如Debug模式）。

**解决方案**：添加条件检查，只在适当的情况下执行计算，并在UI中提供相应的反馈。

## 注意事项

1. 计算平均值会引入额外的GPU计算和内存开销，如不需要可以通过UI中的"Compute Average"选项关闭。

2. 在Debug模式（显示法线）下，平均值计算会自动禁用，因为此时的输出数据不具有物理意义。

3. 计算结果会随着辐照度计算的更新而更新，受到计算间隔设置的影响。
