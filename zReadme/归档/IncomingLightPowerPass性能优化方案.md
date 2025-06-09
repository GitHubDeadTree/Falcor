# IncomingLightPowerPass性能优化方案

## 任务概述

优化 IncomingLightPowerPass 的性能，解决启动后严重掉帧的问题，同时确保输出结果的正确性。

## 性能问题分析

通过分析代码，可以发现以下可能的性能瓶颈：

1. **过度的调试输出**：代码中包含大量的 `logInfo` 调用，尤其是在 `execute()` 方法中，这会严重影响帧率
2. **冗余的纹理读回操作**：在每一帧都进行纹理数据的读回（从 GPU 到 CPU）操作，这是极其昂贵的
3. **统计计算开销**：每帧都进行大量的统计数据计算
4. **不必要的 GPU 同步**：多次使用 `pRenderContext->submit(true)` 强制 GPU 和 CPU 同步
5. **复杂的条件判断**：在着色器中可能存在复杂的波长过滤条件判断
6. **纹理格式和内存排列**：使用 RGBA32Float 格式可能不是最优的

## 优化子任务

### 任务1：减少调试日志输出

#### 任务目标
减少每帧中过度的日志输出，仅在需要时输出关键信息。

#### 实现方案
1. 在 `execute()` 方法中，移除或条件化所有的 `logInfo` 调用
2. 添加一个 `mDebugMode` 成员变量，只有在调试模式下才输出详细日志
3. 为关键日志添加输出频率限制，例如每 60 帧输出一次
4. 优先移除以下区域的频繁日志：
   - 像素数据读回后的逐像素日志输出
   - 每帧统计信息的详细日志
   - 启动时的冗长配置日志

#### 调试信息
```cpp
// 在类初始化中添加
bool mDebugMode = false;
uint32_t mDebugLogFrequency = 60; // 每60帧输出一次
uint32_t mFrameCount = 0;

// 在execute()方法中添加
mFrameCount++;
bool shouldLogThisFrame = mDebugMode && (mFrameCount % mDebugLogFrequency == 0);

if (shouldLogThisFrame) {
    logInfo("IncomingLightPowerPass executing - Frame: " + std::to_string(mFrameCount));
    // 其他必要的调试信息
}
```

#### 验证方法
1. 帧率应该明显提高
2. 控制台输出量应大幅减少
3. 仍能在必要时查看关键调试信息

### 任务2：优化纹理读回操作

#### 任务目标
减少纹理数据从 GPU 到 CPU 的读回操作，仅在用户请求导出或统计时执行。

#### 实现方案
1. 移除 `execute()` 中的自动纹理读回操作
2. 创建一个 `mNeedReadback` 标志，仅在以下情况设置为 true：
   - 用户请求导出数据
   - 用户请求计算统计信息
   - 用户启用了连续统计更新
3. 优化 `readbackData()` 方法，使用异步读回并避免阻塞渲染线程
4. 实现一个更有效的数据缓冲机制，减少频繁的大内存分配

#### 调试信息
```cpp
// 在readbackData()方法中添加
if (!mNeedReadback) {
    if (mDebugMode) logInfo("Skipping texture readback as it's not requested");
    return false;
}

try {
    // 读回操作...
    if (mDebugMode) logInfo("Texture readback completed - buffer size: " + std::to_string(mPowerReadbackBuffer.size()));
} catch (const std::exception& e) {
    logError("Readback failed: " + std::string(e.what()));
    // 确保缓冲区不为空，使用上一次的有效数据或初始化为默认值
    if (mPowerReadbackBuffer.empty()) {
        mPowerReadbackBuffer.resize(mFrameDim.x * mFrameDim.y, float4(0.0f, 0.0f, 0.0f, 550.0f));
        logWarning("Using default power values due to readback failure");
    }
    return false;
}
```

#### 验证方法
1. 帧率应立即提高，特别是在未请求读回时
2. 导出和统计功能应继续正常工作
3. 日志中应能看到读回操作仅在需要时执行

### 任务3：优化统计计算

#### 任务目标
减少统计计算的频率，优化计算过程，并允许用户控制何时进行统计更新。

#### 实现方案
1. 修改统计计算逻辑，使其完全基于用户请求，而不是每帧自动计算
2. 在 UI 中添加"更新统计"按钮，让用户手动触发统计计算
3. 优化计算过程，减少不必要的分支和内存访问
4. 实现一种轻量级的增量统计更新方法，只处理新的或变化的数据

#### 调试信息
```cpp
// 在calculateStatistics()方法中添加
uint32_t startTime = std::chrono::duration_cast<std::chrono::milliseconds>(
    std::chrono::high_resolution_clock::now().time_since_epoch()).count();

// 执行统计计算...

uint32_t endTime = std::chrono::duration_cast<std::chrono::milliseconds>(
    std::chrono::high_resolution_clock::now().time_since_epoch()).count();
logInfo("Statistics calculation completed in " + std::to_string(endTime - startTime) + " ms");

// 计算结果验证
if (nonZeroPixels > 0 && mPowerStats.pixelCount == 0) {
    logError("Statistics calculation error: found non-zero pixels but pixel count is zero");
    mPowerStats.pixelCount = nonZeroPixels; // 确保有合理的值
}

// 设置默认值以防计算错误
if (mPowerStats.pixelCount > 0 && (mPowerStats.totalPower[0] <= 0 && mPowerStats.totalPower[1] <= 0 && mPowerStats.totalPower[2] <= 0)) {
    logWarning("Power values suspiciously low, using default minimum values");
    // 设置一个小的非零值
    for (int i = 0; i < 3; i++) {
        if (mPowerStats.totalPower[i] <= 0) mPowerStats.totalPower[i] = 0.001f;
    }
}
```

#### 验证方法
1. 统计计算的性能开销应显著减少
2. 手动触发统计计算应该正常工作
3. 统计数据应该与优化前的结果一致
4. 日志中应报告计算时间，通常应小于 100ms

### 任务4：减少 GPU 同步点

#### 任务目标
减少 GPU 和 CPU 之间的强制同步点，优化 GPU 工作流。

#### 实现方案
1. 移除所有不必要的 `pRenderContext->submit(true)` 调用
2. 采用更现代的异步 GPU 工作流模式
3. 使用 GPU 计时器查询替代同步，以获取性能数据
4. 实现 GPU 事件标记以便于在调试器中识别瓶颈

#### 调试信息
```cpp
// 在execute()方法中添加
GpuTimer timer;
if (mEnableProfiling) timer.start(pRenderContext);

// 执行计算...

if (mEnableProfiling) {
    timer.stop(pRenderContext);
    // 注意：这将在未来的帧中获取结果，而不是立即同步
    if (timer.getElapsedTime() > 0) {
        mLastExecutionTime = timer.getElapsedTime();
        if (mDebugMode && shouldLogThisFrame) {
            logInfo("Shader execution time: " + std::to_string(mLastExecutionTime) + " ms");
        }
    }
}
```

#### 验证方法
1. 帧率应立即提高
2. GPU 利用率应增加
3. 渲染应更加平滑，没有明显的卡顿
4. 性能分析工具应显示更少的 GPU 同步点

### 任务5：优化着色器代码

#### 任务目标
优化计算着色器代码，减少分支和提高指令级并行性。

#### 实现方案
1. 优化波长过滤逻辑，减少条件分支
2. 使用更高效的数学函数和操作
3. 确保内存访问模式对 GPU 缓存友好
4. 如果适用，使用纹理采样器而不是 UAV 以获得更好的缓存局部性

#### 调试信息
```cpp
// 在着色器中添加调试输出（使用特定像素位置存储调试值）
if (pixel.x == 0 && pixel.y == 0) {
    gDebugOutput[pixel] = float4(
        filterExecutionTime,  // 过滤计算时间（相对值）
        powerCalculationTime, // 功率计算时间（相对值）
        branch_count,         // 条件分支计数
        is_filter_pass ? 1.0f : 0.0f // 过滤结果
    );
}

// 在CPU代码中添加（execute方法）
if (pDebugOutput) {
    std::vector<uint8_t> debugData = pRenderContext->readTextureSubresource(pDebugOutput.get(), 0);
    if (!debugData.empty()) {
        const float4& debugValues = *reinterpret_cast<const float4*>(debugData.data());
        if (mDebugMode) {
            logInfo(fmt::format("Shader debug: Filter time={0:.2f}, Power time={1:.2f}, Branches={2}, Filter pass={3}",
                debugValues.x, debugValues.y, debugValues.z, debugValues.w));
        }
    }
}
```

#### 验证方法
1. 帧率应有所提高
2. 着色器性能分析应显示更少的分支和更高的指令吞吐量
3. 通过调试像素验证计算结果仍然正确
4. 着色器执行时间应减少

### 任务6：优化纹理格式和内存布局

#### 任务目标
优化纹理格式和内存布局，减少带宽需求和提高内存访问效率。

#### 实现方案
1. 评估是否可以使用更紧凑的纹理格式（如 RGBA16Float 代替 RGBA32Float）
2. 调整纹理大小和 mipmap 策略
3. 优化内存访问模式，确保同一波前中的线程访问相邻内存
4. 确保纹理尺寸为 GPU 纹理缓存行的倍数（通常为 16 或 32 字节对齐）

#### 调试信息
```cpp
// 在prepareResources()方法中添加
ResourceFormat currentFormat = pOutputPower->getFormat();
uint32_t bytesPerPixel = getFormatBytesPerBlock(currentFormat);
uint64_t totalMemory = bytesPerPixel * mFrameDim.x * mFrameDim.y;

if (mDebugMode && shouldLogThisFrame) {
    logInfo(fmt::format("Texture memory: format={0}, size={1}x{2}, bytes/pixel={3}, total={4} KB",
        to_string(currentFormat), mFrameDim.x, mFrameDim.y, bytesPerPixel, totalMemory / 1024));
}

// 验证纹理对齐
bool isWidthAligned = (mFrameDim.x % 8) == 0;  // 假设最佳对齐为8的倍数
bool isHeightAligned = (mFrameDim.y % 8) == 0;
if (!isWidthAligned || !isHeightAligned) {
    if (mDebugMode) {
        logWarning(fmt::format("Texture dimensions not optimally aligned: {0}x{1}", mFrameDim.x, mFrameDim.y));
    }
}
```

#### 验证方法
1. 帧率应有所提高
2. 内存带宽使用应减少
3. 纹理质量和计算结果应保持不变
4. GPU 内存使用量应减少

### 任务7：实现动态细节级别

#### 任务目标
根据当前帧率动态调整计算精度和纹理分辨率，在性能和质量之间取得平衡。

#### 实现方案
1. 实现一个简单的帧率监控系统
2. 根据帧率动态调整以下参数：
   - 计算着色器的线程组大小
   - 输出纹理的分辨率（可能使用下采样）
   - 统计采样率（不需要处理每个像素）
3. 在 UI 中添加控制选项，允许用户设置性能/质量偏好

#### 调试信息
```cpp
// 在类中添加成员变量
float mCurrentFPS = 60.0f;
float mFPSSmoothing = 0.95f; // 平滑因子
int mCurrentLOD = 0; // 0=全分辨率, 1=半分辨率, 2=1/4分辨率

// 在execute()方法开始处添加
// 计算平滑帧率
float frameTime = mClock.getDeltaTime();
float instantFPS = frameTime > 0 ? 1.0f / frameTime : 60.0f;
mCurrentFPS = mFPSSmoothing * mCurrentFPS + (1.0f - mFPSSmoothing) * instantFPS;

// 根据帧率调整LOD
int targetLOD = 0;
if (mCurrentFPS < 30.0f) targetLOD = 2;
else if (mCurrentFPS < 45.0f) targetLOD = 1;

// 平滑LOD变化
if (targetLOD != mCurrentLOD) {
    if (mFrameCount % 30 == 0) { // 避免频繁改变
        mCurrentLOD = targetLOD;
        if (mDebugMode) {
            logInfo(fmt::format("Adjusting LOD to {0}, current FPS: {1:.1f}", mCurrentLOD, mCurrentFPS));
        }
    }
}

// 计算实际使用的分辨率
uint2 effectiveResolution = mFrameDim;
if (mCurrentLOD > 0) {
    effectiveResolution.x >>= mCurrentLOD;
    effectiveResolution.y >>= mCurrentLOD;
    effectiveResolution.x = std::max(effectiveResolution.x, 1u);
    effectiveResolution.y = std::max(effectiveResolution.y, 1u);
}

// 使用调整后的分辨率执行计算
```

#### 验证方法
1. 在低性能设备上，帧率应保持在可接受范围
2. 在高性能设备上，应使用全分辨率以获得最佳质量
3. LOD 调整应平滑而不突兀
4. 日志中应报告 LOD 变化和相关帧率

## 实施优先级和依赖关系

实施子任务的建议顺序：

1. **任务1**：减少调试日志输出（最简单且收益明显）
2. **任务4**：减少 GPU 同步点（无依赖，收益大）
3. **任务2**：优化纹理读回操作（依赖于任务1的日志机制）
4. **任务3**：优化统计计算（依赖于任务2的读回优化）
5. **任务5**：优化着色器代码（复杂度较高但收益显著）
6. **任务6**：优化纹理格式和内存布局（可能需要更广泛的测试）
7. **任务7**：实现动态细节级别（作为最终的性能保障措施）

## 风险评估

1. **结果准确性**：优化过程中需确保计算结果与原始实现一致
2. **兼容性**：需确保优化后的代码在各种硬件上正常工作
3. **代码复杂性**：某些优化可能增加代码复杂性，需在性能和可维护性间取平衡
4. **调试难度**：减少日志输出可能使调试更困难，需实现智能的条件调试机制

## 评估方法

为了验证优化效果，将在每个任务完成后进行以下测量：

1. 平均帧率（FPS）
2. 帧时间分布（最小、最大、平均、95百分位）
3. GPU 内存使用
4. CPU 内存使用
5. 对比优化前后的计算结果差异（应小于可接受的误差范围）
