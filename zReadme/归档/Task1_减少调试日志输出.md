# 任务1：减少调试日志输出

## 任务目标

减少IncomingLightPowerPass中过度的日志输出，仅在需要时输出关键信息，以提高性能。

## 实现方案

1. 添加调试模式控制变量，允许在需要时启用详细日志
2. 添加帧计数器和日志频率控制
3. 条件化所有的日志输出，特别是在execute()方法中的大量logInfo调用
4. 在UI中添加调试模式开关，让用户可以控制日志输出

## 具体实现

### 1. 添加调试相关成员变量

在`IncomingLightPowerPass.h`中添加了以下成员变量：

```cpp
// Debug related members
bool mDebugMode = false;             ///< Enable/disable debug mode with detailed logging
uint32_t mDebugLogFrequency = 60;    ///< How often to log debug info (in frames)
uint32_t mFrameCount = 0;            ///< Frame counter for logging frequency control
bool mEnableProfiling = false;       ///< Enable performance profiling
float mLastExecutionTime = 0.0f;     ///< Last recorded execution time in ms
```

### 2. 修改execute方法

在`execute`方法中，添加了帧计数器和条件化日志输出：

```cpp
// Increment frame counter
mFrameCount++;
bool shouldLogThisFrame = mDebugMode && (mFrameCount % mDebugLogFrequency == 0);

// Print debug info - current settings (only in debug mode)
if (shouldLogThisFrame)
{
    logInfo(fmt::format("IncomingLightPowerPass executing - Frame: {0}, settings: enabled={1}, wavelength_filter_enabled={2}, filter_mode={3}, min_wavelength={4}, max_wavelength={5}",
                      mFrameCount, mEnabled, mEnableWavelengthFilter, static_cast<int>(mFilterMode), mMinWavelength, mMaxWavelength));
}
```

### 3. 添加性能分析功能

添加了CPU时间测量功能来跟踪着色器执行时间：

```cpp
// Track performance if profiling is enabled
uint64_t startTime = 0;
if (mEnableProfiling)
{
    startTime = getTimeInMicroseconds();
}

// ... 执行着色器 ...

// Track performance if profiling is enabled
if (mEnableProfiling && startTime > 0)
{
    uint64_t endTime = getTimeInMicroseconds();
    mLastExecutionTime = (endTime - startTime) / 1000.0f; // Convert to milliseconds

    if (shouldLogThisFrame)
    {
        logInfo(fmt::format("Shader execution time: {0:.2f} ms", mLastExecutionTime));
    }
}
```

### 4. 条件化调试数据读回

修改了所有昂贵的调试操作，包括纹理读回和像素级调试，确保它们只在`shouldLogThisFrame`条件下执行：

```cpp
// Read debug information for first 4 pixels (only in debug mode and at specified intervals)
if (mDebugMode && shouldLogThisFrame && pDebugOutput && pDebugInputData && pDebugCalculation)
{
    pRenderContext->submit(true);  // Make sure compute pass is complete

    // Read debug data (expensive operation)
    std::vector<uint8_t> debugData = pRenderContext->readTextureSubresource(pDebugOutput.get(), 0);
    // ... 其他读回操作 ...
}

// Check if first pixel value verification is needed (only in debug mode and at intervals)
if (mDebugMode && shouldLogThisFrame)
{
    // 只在指定间隔执行昂贵的纹理读回
    std::vector<uint8_t> pixelData = pRenderContext->readTextureSubresource(pOutputPower.get(), 0);
    // ...
}
```

### 5. 添加UI控制

在`renderUI`方法中添加了调试选项控制：

```cpp
// Add debugging controls
auto debugGroup = widget.group("Debug Options", true);
if (debugGroup)
{
    widget.checkbox("Debug Mode", mDebugMode);
    if (mDebugMode)
    {
        widget.slider("Log Frequency (frames)", mDebugLogFrequency, 1u, 300u);
        widget.text(fmt::format("Current frame: {0}", mFrameCount));
        widget.checkbox("Performance Profiling", mEnableProfiling);
        if (mEnableProfiling && mLastExecutionTime > 0)
        {
            widget.text(fmt::format("Last execution time: {0:.2f} ms", mLastExecutionTime));
        }
    }
}
```

## 遇到的问题及解决方案

1. **GpuTimer编译错误**：
   - 问题：初始尝试使用`GpuTimer`类时遇到了编译错误，因为缺少头文件和API使用不正确
   - 解决：移除了复杂的GpuTimer，改用简单的CPU时间测量（`getTimeInMicroseconds()`）

2. **日志条件化**：
   - 问题：需要确保所有日志输出都被条件化，但有些嵌套很深
   - 解决：系统地检查每个`logInfo`调用，添加`mDebugMode`和`shouldLogThisFrame`条件

3. **性能问题根本原因**：
   - 问题：即使添加了调试模式控制，启用调试模式时帧率仍然很低
   - 原因：昂贵的GPU到CPU纹理读回操作每帧都在执行，而不仅仅是日志输出
   - 解决：将所有昂贵的操作（纹理读回、像素级调试）都条件化到`shouldLogThisFrame`控制下

## 验证方法

1. 启用调试模式，设置日志频率为1，确认每帧都有日志输出
2. 禁用调试模式，确认没有日志输出
3. 启用调试模式，设置日志频率为60，确认每60帧输出一次日志
4. 启用性能分析，确认可以看到着色器执行时间

## 性能改进

通过这些优化，我们显著减少了日志输出和昂贵的GPU到CPU数据读回操作，这些操作是造成性能下降的主要原因之一。特别是：

1. **默认情况下禁用调试日志**：减少了CPU开销和控制台输出
2. **条件化所有昂贵操作**：纹理读回、像素级调试只在指定间隔执行
3. **修复Timer性能问题**：移除了复杂的GpuTimer，使用轻量级CPU计时
4. **智能统计计算**：只在真正需要时才计算统计信息

### 关键性能修复：

- **Timer问题**：原来的Timer是每60帧输出一次日志，但昂贵的计算每帧都在执行
- **纹理读回问题**：GPU到CPU的数据传输是最昂贵的操作，现在只在调试间隔执行
- **像素级调试问题**：详细的像素调试日志现在受到频率控制

这些改进应该能够显著提高应用程序的帧率，尤其是在调试模式关闭的情况下。当调试模式启用时，性能影响也大大降低，因为昂贵操作现在受到频率控制（默认60帧一次）。
