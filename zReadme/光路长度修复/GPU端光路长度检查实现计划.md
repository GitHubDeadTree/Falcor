# GPU端光路长度检查实施计划

## 任务概述

实现"GPU端只检查异常的路径长度（NaN/无穷大），异常的路径直接抛弃，而不是钳制"的功能。

## 具体子任务

### 子任务1：修改CIRPathData.slang中的isValid函数

#### 1. 任务目标
修改isValid函数，使其只检查路径长度是否为NaN或无穷大，而不再检查其他字段。

#### 2. 实现方案
定位到CIRPathData.slang文件中的isValid函数（约在54-78行），修改为：

```hlsl
bool isValid()
{
    // GPU端只检查路径长度的NaN/无穷大值
    if (isnan(pathLength) || isinf(pathLength)) return false;

    // 其他所有验证都移到CPU端
    return true;
}
```

#### 3. 错误处理
这个函数是一个验证函数，返回bool值，不需要特殊的错误处理。

#### 4. 验证方法
通过调试日志查看被标记为无效的路径数量是否合理。正常情况下，只有路径长度为NaN或无穷大的路径会被标记为无效。

### 子任务2：修改CIRPathData.slang中的sanitize函数

#### 1. 任务目标
简化sanitize函数，移除对其他字段的处理，只保留对路径长度的基本处理。

#### 2. 实现方案
定位到CIRPathData.slang文件中的sanitize函数（约在85-113行），修改为：

```hlsl
[mutating] void sanitize()
{
    // GPU端只处理路径长度的NaN/无穷大值
    // 其他所有验证和处理都交给CPU端
    if (isnan(pathLength) || isinf(pathLength))
    {
        pathLength = 1.0f; // 设置默认值，但实际上这种情况下整条路径会被丢弃
    }

    // 移除所有其他字段的clamp和处理逻辑
    // 保留原始数值，让CPU端进行完整验证
}
```

#### 3. 错误处理
此函数内部为异常值设置了默认值(1.0f)，但由于在下一步会直接丢弃这些路径，所以这个默认值不会影响最终结果。

#### 4. 验证方法
确认修改后的函数只处理路径长度字段，且不对其他字段进行修改。这部分验证需要与子任务3结合进行。

### 子任务3：修改PixelStats.slang中的logCIRPathComplete函数

#### 1. 任务目标
修改logCIRPathComplete函数，使其在检测到路径长度为NaN或无穷大时直接丢弃整条路径。

#### 2. 实现方案
定位到PixelStats.slang文件中的logCIRPathComplete函数（约在200-211行），修改为：

```hlsl
void logCIRPathComplete(CIRPathData pathData)
{
    // 只检查路径长度的NaN/无穷大值，如果异常则直接丢弃整条光路
    if (isnan(pathData.pathLength) || isinf(pathData.pathLength))
    {
        // 直接返回，不记录任何数据到缓冲区
        return;
    }

    // 对于路径长度有效的数据，直接记录而不进行任何修改
    // 其他字段的验证交给CPU端处理
    logCIRStatisticsInternal(pathData);
    logCIRRawPathInternal(pathData);
}
```

#### 3. 错误处理
这个函数通过提前返回来处理异常情况，不需要额外的错误处理。可以考虑添加调试计数器来统计丢弃的路径数量，但这不是必需的。

#### 4. 验证方法
1. 检查输出的CIR数据中是否不再有路径长度为NaN或无穷大的记录
2. 可以添加临时调试代码，统计被丢弃的路径数量，确认确实有异常路径被过滤

### 子任务4：验证整体数据流程

#### 1. 任务目标
确保修改后的整体数据流程正常工作：TracePass → 路径长度检查 → 数据记录 → CPU端验证。

#### 2. 实现方案
1. 在TracePass.rt.slang中找到调用CIR数据输出的位置（约在407行）
2. 确认调用链：TracePass.rt.slang → logCIRPathComplete → 路径长度检查 → 数据记录

可以临时添加一些调试代码来验证流程：

```hlsl
// 在TracePass.rt.slang中调用CIR数据输出前添加
uint totalPaths = 0;
uint validPaths = 0;
// 计算总路径数...
// 计算有效路径数...
```

#### 3. 错误处理
添加日志记录来跟踪处理过程中的关键参数：

```hlsl
// 在关键位置添加调试代码
if (totalPaths > 0 && validPaths == 0)
{
    logWarning("Warning: All paths were discarded due to invalid path lengths!");
}
```

#### 4. 验证方法
1. 运行程序并生成CIR数据
2. 检查CPU端接收的数据是否不包含路径长度为NaN或无穷大的记录
3. 通过临时添加的调试信息，确认正常情况下：
   - 总路径数 > 有效路径数（表明有一些路径被过滤）
   - 有效路径数占总路径数的比例在合理范围内（如>80%）

## 总结

这四个子任务完成后，将实现GPU端对路径长度的异常检查和过滤，确保CPU端只接收到路径长度有效的数据。这样既保证了数据质量，又避免了对异常数据的钳制处理，保留了数据的原始特性。
