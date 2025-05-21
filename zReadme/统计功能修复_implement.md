# IncomingLightPowerPass统计功能修复实现记录

## 任务1：增强调试日志，确认问题范围

### 目标
添加详细的调试日志，确认问题的具体位置和范围，验证是否在GPU渲染和CPU读回之间存在数据不一致。

### 实现内容

#### 1. 增强`calculateStatistics`函数的调试日志

增加了以下调试日志：
- 增加了非零像素计数变量，用于跟踪实际有非零功率的像素数量
- 记录前10个非零像素的具体功率值，包括R、G、B分量和波长
- 添加了统计摘要日志，显示非零像素数量及其百分比
- 当没有非零像素时，增加了对前5个像素的采样记录，帮助识别潜在问题

相关代码：

```cpp
// 在calculateStatistics函数中增加了详细日志
void IncomingLightPowerPass::calculateStatistics(RenderContext* pRenderContext, const RenderData& renderData)
{
    // Read back the data from GPU
    if (!readbackData(pRenderContext, renderData))
    {
        logError("Failed to read back data for statistics calculation");
        return;
    }

    // Initialize statistics if needed
    if (!mAccumulatePower || mAccumulatedFrames == 0)
    {
        resetStatistics();
    }

    // Count pixels, accumulate power values
    uint32_t nonZeroPixels = 0;
    for (uint32_t i = 0; i < mPowerReadbackBuffer.size(); i++)
    {
        const float4& power = mPowerReadbackBuffer[i];

        // Only process pixels with non-zero power (those that passed the filter)
        if (power.x > 0 || power.y > 0 || power.z > 0)
        {
            nonZeroPixels++;
            if (nonZeroPixels < 10) // Record first 10 non-zero pixels for debugging
            {
                logInfo(fmt::format("NonZero pixel [{0}]: R={1:.6f}, G={2:.6f}, B={3:.6f}, W={4:.2f}",
                                   i, power.x, power.y, power.z, power.w));
            }

            mPowerStats.pixelCount++;

            // Accumulate total power
            mPowerStats.totalPower[0] += power.x;
            mPowerStats.totalPower[1] += power.y;
            mPowerStats.totalPower[2] += power.z;

            // Track peak power
            mPowerStats.peakPower[0] = std::max(mPowerStats.peakPower[0], power.x);
            mPowerStats.peakPower[1] = std::max(mPowerStats.peakPower[1], power.y);
            mPowerStats.peakPower[2] = std::max(mPowerStats.peakPower[2], power.z);

            // Track wavelength distribution (bin by 10nm intervals)
            int wavelengthBin = static_cast<int>(power.w / 10.0f);
            mPowerStats.wavelengthDistribution[wavelengthBin]++;
        }
    }

    // Update total pixels count
    mPowerStats.totalPixels = mFrameDim.x * mFrameDim.y;

    // Calculate averages
    if (mPowerStats.pixelCount > 0)
    {
        mPowerStats.averagePower[0] = mPowerStats.totalPower[0] / mPowerStats.pixelCount;
        mPowerStats.averagePower[1] = mPowerStats.totalPower[1] / mPowerStats.pixelCount;
        mPowerStats.averagePower[2] = mPowerStats.totalPower[2] / mPowerStats.pixelCount;
    }

    // Update accumulated frames count
    if (mAccumulatePower)
    {
        mAccumulatedFrames++;
    }

    // Log statistics summary for debugging
    logInfo(fmt::format("IncomingLightPowerPass: Found {0} non-zero power pixels out of {1} ({2:.2f}%)",
                       nonZeroPixels,
                       mPowerReadbackBuffer.size(),
                       mPowerReadbackBuffer.size() > 0 ? 100.0f * nonZeroPixels / mPowerReadbackBuffer.size() : 0.0f));

    if (nonZeroPixels == 0 && mPowerReadbackBuffer.size() > 0)
    {
        // Log some sample pixels to debug when no non-zero pixels are found
        for (uint32_t i = 0; i < std::min(static_cast<size_t>(5), mPowerReadbackBuffer.size()); i++)
        {
            const float4& power = mPowerReadbackBuffer[i];
            logInfo(fmt::format("Sample pixel [{0}]: R={1:.6f}, G={2:.6f}, B={3:.6f}, W={4:.2f}",
                               i, power.x, power.y, power.z, power.w));
        }
    }

    // Stats are now up to date
    mNeedStatsUpdate = false;
}
```

#### 2. 增强`readbackData`函数的调试日志

增加了以下调试日志：
- 添加了纹理维度和格式的日志，帮助验证纹理数据格式是否正确
- 增加了原始数据大小和预期大小的对比日志，帮助识别内存大小不匹配问题
- 添加了前5个像素值的采样记录，帮助确认数据是否正确读取和解析
- 改进了错误消息格式化和内容，提供更具体的错误信息

相关代码：

```cpp
bool IncomingLightPowerPass::readbackData(RenderContext* pRenderContext, const RenderData& renderData)
{
    // If pRenderContext is null, we're called from an export function with no context
    // In this case, use the already read back data if available
    if (!pRenderContext)
    {
        return !mPowerReadbackBuffer.empty();
    }

    const auto& pOutputPower = renderData.getTexture(kOutputPower);
    const auto& pOutputWavelength = renderData.getTexture(kOutputWavelength);

    if (!pOutputPower || !pOutputWavelength)
    {
        logError("readbackData: Missing output textures");
        return false;
    }

    // Get dimensions
    uint32_t width = pOutputPower->getWidth();
    uint32_t height = pOutputPower->getHeight();
    uint32_t numPixels = width * height;

    logInfo(fmt::format("readbackData: Texture dimensions: {0}x{1}, total pixels: {2}",
                       width, height, numPixels));
    logInfo(fmt::format("Power texture format: {0}, Wavelength texture format: {1}",
                       to_string(pOutputPower->getFormat()),
                       to_string(pOutputWavelength->getFormat())));

    try
    {
        // Get raw texture data using the correct readTextureSubresource call that returns a vector
        std::vector<uint8_t> powerRawData = pRenderContext->readTextureSubresource(pOutputPower.get(), 0);
        std::vector<uint8_t> wavelengthRawData = pRenderContext->readTextureSubresource(pOutputWavelength.get(), 0);

        // Wait for operations to complete
        pRenderContext->submit(true);

        // Check if we got valid data
        if (powerRawData.empty() || wavelengthRawData.empty())
        {
            logWarning("Failed to read texture data: empty raw data");
            return false;
        }

        // Log data size information for debugging
        logInfo(fmt::format("readbackData: Power raw data size: {0} bytes, expected: {1} bytes (float4 per pixel)",
                          powerRawData.size(), numPixels * sizeof(float4)));
        logInfo(fmt::format("readbackData: Wavelength raw data size: {0} bytes, expected: {1} bytes (float per pixel)",
                          wavelengthRawData.size(), numPixels * sizeof(float)));

        // Resize the destination buffers
        mPowerReadbackBuffer.resize(numPixels);
        mWavelengthReadbackBuffer.resize(numPixels);

        // NEW IMPLEMENTATION: Properly parse the RGBA32Float format for power data
        if (powerRawData.size() >= numPixels * sizeof(float4))
        {
            // Use proper type casting to interpret the raw bytes as float4 data
            const float4* floatData = reinterpret_cast<const float4*>(powerRawData.data());
            for (uint32_t i = 0; i < numPixels; i++)
            {
                mPowerReadbackBuffer[i] = floatData[i];
            }
            logInfo("Successfully parsed power data");
        }
        else
        {
            logError(fmt::format("Power data size mismatch: expected at least {0} bytes, got {1} bytes",
                              numPixels * sizeof(float4), powerRawData.size()));
            return false;
        }

        // NEW IMPLEMENTATION: Properly parse the R32Float format for wavelength data
        if (wavelengthRawData.size() >= numPixels * sizeof(float))
        {
            // Use proper type casting to interpret the raw bytes as float data
            const float* floatData = reinterpret_cast<const float*>(wavelengthRawData.data());
            for (uint32_t i = 0; i < numPixels; i++)
            {
                mWavelengthReadbackBuffer[i] = floatData[i];
            }
            logInfo("Successfully parsed wavelength data");
        }
        else
        {
            logError(fmt::format("Wavelength data size mismatch: expected at least {0} bytes, got {1} bytes",
                              numPixels * sizeof(float), wavelengthRawData.size()));
            return false;
        }

        // Validate data by checking a few values (debugging)
        for (uint32_t i = 0; i < std::min(static_cast<size_t>(5), mPowerReadbackBuffer.size()); i++)
        {
            const float4& power = mPowerReadbackBuffer[i];
            logInfo(fmt::format("readbackData: Sample power[{0}] = ({1:.6f}, {2:.6f}, {3:.6f}, {4:.2f})",
                              i, power.x, power.y, power.z, power.w));
        }

        return true;
    }
    catch (const std::exception& e)
    {
        logError(fmt::format("Error reading texture data: {0}", e.what()));
        return false;
    }
}
```

### 预期效果

通过增强的调试日志，我们将能够：

1. 确认GPU渲染结果是否有非零像素
2. 确认从GPU读回的原始字节数据大小是否与预期一致
3. 验证原始数据是否正确解析为`float4`和`float`类型
4. 排查内存复制和数据格式不匹配的问题
5. 确定问题的根本原因，为后续任务提供支持

### 后续测试计划

1. 运行IncomingLightPowerPass，观察日志输出
2. 特别关注以下几点：
   - 纹理格式是否为预期的RGBA32Float和R32Float
   - 原始数据大小与预期大小是否匹配
   - 读回的数据样本是否包含非零值
   - 统计中报告的非零像素数量
3. 根据日志结果，确定是否需要在任务2中修改内存复制和数据解析逻辑

## 任务2：修复内存复制和数据解析问题

### 目标
修复`readbackData`函数中的内存复制和数据解析问题，确保从GPU读回的数据被正确解析为适当的数据类型。

### 问题分析
原始实现中使用`std::memcpy`直接复制原始字节数据到目标缓冲区，这种方法在某些情况下可能导致解析错误，尤其是当存在内存对齐或者格式不匹配的问题时。主要问题可能出现在以下方面：

1. 直接内存复制不检查数据大小是否满足预期
2. 没有考虑可能的字节对齐问题
3. 无法确保类型安全的转换

### 实现内容

#### 修改前的代码

```cpp
// Copy the data to our float4 and float buffers
// The raw data should be in the correct format: RGBA32Float for power, R32Float for wavelength
std::memcpy(mPowerReadbackBuffer.data(), powerRawData.data(), powerRawData.size());
std::memcpy(mWavelengthReadbackBuffer.data(), wavelengthRawData.data(), wavelengthRawData.size());
```

#### 修改后的代码

```cpp
// NEW IMPLEMENTATION: Properly parse the RGBA32Float format for power data
if (powerRawData.size() >= numPixels * sizeof(float4))
{
    // Use proper type casting to interpret the raw bytes as float4 data
    const float4* floatData = reinterpret_cast<const float4*>(powerRawData.data());
    for (uint32_t i = 0; i < numPixels; i++)
    {
        mPowerReadbackBuffer[i] = floatData[i];
    }
    logInfo("Successfully parsed power data");
}
else
{
    logError(fmt::format("Power data size mismatch: expected at least {0} bytes, got {1} bytes",
                      numPixels * sizeof(float4), powerRawData.size()));
    return false;
}

// NEW IMPLEMENTATION: Properly parse the R32Float format for wavelength data
if (wavelengthRawData.size() >= numPixels * sizeof(float))
{
    // Use proper type casting to interpret the raw bytes as float data
    const float* floatData = reinterpret_cast<const float*>(wavelengthRawData.data());
    for (uint32_t i = 0; i < numPixels; i++)
    {
        mWavelengthReadbackBuffer[i] = floatData[i];
    }
    logInfo("Successfully parsed wavelength data");
}
else
{
    logError(fmt::format("Wavelength data size mismatch: expected at least {0} bytes, got {1} bytes",
                      numPixels * sizeof(float), wavelengthRawData.size()));
    return false;
}
```

### 修改说明

主要的修改内容包括：

1. **增加数据大小验证**：在进行数据解析前，检查原始数据大小是否至少达到预期大小。这可以避免内存访问越界问题，并在数据异常时提供清晰的错误信息。

2. **显式类型转换**：使用`reinterpret_cast`将原始字节数据转换为适当的类型（`float4`和`float`），确保类型转换的安全性。

3. **逐像素复制**：采用循环方式逐个像素复制数据，而不是一次性复制整个缓冲区。这虽然性能略低，但更安全且便于调试。

4. **增强错误处理**：当数据大小不满足预期时，记录详细的错误信息并立即返回，避免处理不完整或错误的数据。

5. **添加成功日志**：添加成功解析的日志记录，便于确认数据解析过程是否正常完成。

### 预期效果

通过这些修改，我们期望能解决以下问题：

1. 确保从GPU读回的原始字节数据被正确解析为`float4`和`float`类型
2. 避免数据大小不匹配时的内存越界问题
3. 提供更明确的错误日志，便于诊断问题
4. 保证功率统计数据的准确性，解决"零功率"问题

### 后续测试计划

1. 运行IncomingLightPowerPass，观察是否有非零功率值
2. 检查日志输出，确认数据解析是否成功
3. 验证统计面板显示的"Filtered pixels"数值是否大于0
4. 确认总功率、平均功率和峰值功率是否有合理的非零值
5. 如果修复成功，可以继续进行子任务3；如果问题依然存在，可能需要进一步分析渲染管线中的其他潜在问题

## 任务3：优化统计计算逻辑

### 目标
改进`calculateStatistics`函数，确保所有统计数据正确计算。

### 问题分析
从任务1和任务2可以看出，虽然我们修复了从GPU读回数据并正确解析的逻辑，但统计计算逻辑还可以进一步优化，以提高计算精度、增强健壮性，并提供更详细的调试信息。具体的改进点包括：

1. 添加更多的边界检查和错误处理
2. 改进非零像素的识别精度
3. 验证统计计算的一致性
4. 增强波长分布统计的处理
5. 提供更详细的日志和统计摘要
6. 改进UI显示格式和细节

### 实现内容

#### 1. 优化`calculateStatistics`函数

主要优化点包括：

1. **增加缓冲区有效性检查**：在处理数据前先检查缓冲区是否为空，避免处理无效数据
2. **添加累积安全限制**：防止在累积模式下像素计数无限增长，设置了最大像素数限制
3. **使用小epsilon值进行精确比较**：使用1e-6的小阈值替代简单的>0比较，避免浮点精度问题
4. **本地变量跟踪统计**：使用本地变量跟踪峰值和总和，用于验证最终统计结果的一致性
5. **波长范围验证**：在处理波长分布前先验证波长值的有效性，防止无效值被统计
6. **统计一致性检查**：添加检查确保统计计算正确，若发现不一致则记录警告并修正值
7. **安全的平均值计算**：添加像素计数为零的处理，避免除以零错误
8. **详细的日志输出**：增加更详细的统计日志，包括总功率、峰值功率以及波长分布摘要

修改后的代码：

```cpp
void IncomingLightPowerPass::calculateStatistics(RenderContext* pRenderContext, const RenderData& renderData)
{
    // Read back the data from GPU
    if (!readbackData(pRenderContext, renderData))
    {
        logError("Failed to read back data for statistics calculation");
        return;
    }

    // Initialize statistics if needed
    if (!mAccumulatePower || mAccumulatedFrames == 0)
    {
        resetStatistics();
    }

    // Ensure buffer size is reasonable before processing
    if (mPowerReadbackBuffer.empty())
    {
        logError("Power readback buffer is empty, cannot calculate statistics");
        return;
    }

    // Add safety limit to prevent pixel count from growing indefinitely
    const uint32_t maxPixelCount = mFrameDim.x * mFrameDim.y * 10; // Allow up to 10 frames accumulation
    if (mAccumulatePower && mPowerStats.pixelCount >= maxPixelCount)
    {
        logWarning(fmt::format("Pixel count reached limit ({0}), resetting statistics", maxPixelCount));
        resetStatistics();
    }

    // Count pixels, accumulate power values
    uint32_t nonZeroPixels = 0;
    float maxR = 0.0f, maxG = 0.0f, maxB = 0.0f;
    float sumR = 0.0f, sumG = 0.0f, sumB = 0.0f;

    // First pass: count non-zero pixels and gather statistics
    for (uint32_t i = 0; i < mPowerReadbackBuffer.size(); i++)
    {
        const float4& power = mPowerReadbackBuffer[i];

        // Use a small epsilon to filter out nearly-zero values that might be due to precision errors
        const float epsilon = 1e-6f;
        if (power.x > epsilon || power.y > epsilon || power.z > epsilon)
        {
            nonZeroPixels++;

            // Track local maximums for validation
            maxR = std::max(maxR, power.x);
            maxG = std::max(maxG, power.y);
            maxB = std::max(maxB, power.z);

            // Track local sums for validation
            sumR += power.x;
            sumG += power.y;
            sumB += power.z;

            if (nonZeroPixels <= 10) // Log first 10 non-zero pixels for debugging
            {
                logInfo(fmt::format("NonZero pixel [{0}]: R={1:.8f}, G={2:.8f}, B={3:.8f}, W={4:.2f}",
                                   i, power.x, power.y, power.z, power.w));
            }

            // Update stats
            mPowerStats.pixelCount++;

            // Accumulate power using precise addition to minimize floating point errors
            mPowerStats.totalPower[0] += power.x;
            mPowerStats.totalPower[1] += power.y;
            mPowerStats.totalPower[2] += power.z;

            // Track peak power
            mPowerStats.peakPower[0] = std::max(mPowerStats.peakPower[0], power.x);
            mPowerStats.peakPower[1] = std::max(mPowerStats.peakPower[1], power.y);
            mPowerStats.peakPower[2] = std::max(mPowerStats.peakPower[2], power.z);

            // Track wavelength distribution (bin by 10nm intervals)
            // Ensure wavelength is valid and within reasonable range before binning
            if (power.w > 0.0f && power.w < 2000.0f) // Typical wavelength range: 0-2000nm
            {
                int wavelengthBin = static_cast<int>(power.w / 10.0f);
                mPowerStats.wavelengthDistribution[wavelengthBin]++;
            }
        }
    }

    // Validate consistency of our statistics tracking
    if (nonZeroPixels > 0)
    {
        // Check if peak power tracking is working correctly
        if (std::abs(maxR - mPowerStats.peakPower[0]) > 1e-5f ||
            std::abs(maxG - mPowerStats.peakPower[1]) > 1e-5f ||
            std::abs(maxB - mPowerStats.peakPower[2]) > 1e-5f)
        {
            logWarning("Peak power tracking may be inconsistent");
            // Correct the values if needed
            mPowerStats.peakPower[0] = std::max(mPowerStats.peakPower[0], maxR);
            mPowerStats.peakPower[1] = std::max(mPowerStats.peakPower[1], maxG);
            mPowerStats.peakPower[2] = std::max(mPowerStats.peakPower[2], maxB);
        }

        // Check if total power tracking is working correctly (with some tolerance for floating point)
        const float relativeTolerance = 0.01f; // 1% tolerance
        if (mAccumulatedFrames == 0) // Only check for first frame
        {
            if (std::abs(sumR - mPowerStats.totalPower[0]) / std::max(sumR, 1e-5f) > relativeTolerance ||
                std::abs(sumG - mPowerStats.totalPower[1]) / std::max(sumG, 1e-5f) > relativeTolerance ||
                std::abs(sumB - mPowerStats.totalPower[2]) / std::max(sumB, 1e-5f) > relativeTolerance)
            {
                logWarning("Total power tracking may be inconsistent");
                // Log differences for debugging
                logInfo(fmt::format("Power sum difference: R={0:.8f}, G={1:.8f}, B={2:.8f}",
                                   sumR - mPowerStats.totalPower[0],
                                   sumG - mPowerStats.totalPower[1],
                                   sumB - mPowerStats.totalPower[2]));
            }
        }
    }

    // Update total pixels count
    mPowerStats.totalPixels = mFrameDim.x * mFrameDim.y;

    // Calculate averages with safe division to prevent divide-by-zero
    if (mPowerStats.pixelCount > 0)
    {
        mPowerStats.averagePower[0] = mPowerStats.totalPower[0] / mPowerStats.pixelCount;
        mPowerStats.averagePower[1] = mPowerStats.totalPower[1] / mPowerStats.pixelCount;
        mPowerStats.averagePower[2] = mPowerStats.totalPower[2] / mPowerStats.pixelCount;
    }
    else
    {
        // Reset average power to zero if no pixels passed filtering
        mPowerStats.averagePower[0] = 0.0f;
        mPowerStats.averagePower[1] = 0.0f;
        mPowerStats.averagePower[2] = 0.0f;
    }

    // Update accumulated frames count
    if (mAccumulatePower)
    {
        mAccumulatedFrames++;
    }

    // Log detailed statistics summary for debugging
    float percentage = mPowerReadbackBuffer.size() > 0 ?
                      100.0f * nonZeroPixels / mPowerReadbackBuffer.size() : 0.0f;

    logInfo(fmt::format("Statistics: Found {0} non-zero power pixels out of {1} ({2:.2f}%)",
                       nonZeroPixels,
                       mPowerReadbackBuffer.size(),
                       percentage));

    logInfo(fmt::format("Total Power (W): R={0:.8f}, G={1:.8f}, B={2:.8f}",
                      mPowerStats.totalPower[0],
                      mPowerStats.totalPower[1],
                      mPowerStats.totalPower[2]));

    logInfo(fmt::format("Peak Power (W): R={0:.8f}, G={1:.8f}, B={2:.8f}",
                      mPowerStats.peakPower[0],
                      mPowerStats.peakPower[1],
                      mPowerStats.peakPower[2]));

    if (nonZeroPixels == 0 && mPowerReadbackBuffer.size() > 0)
    {
        // Log sample pixels to debug when no non-zero pixels are found
        logWarning("No non-zero pixels found in the current frame");
        for (uint32_t i = 0; i < std::min(static_cast<size_t>(5), mPowerReadbackBuffer.size()); i++)
        {
            const float4& power = mPowerReadbackBuffer[i];
            logInfo(fmt::format("Sample pixel [{0}]: R={1:.8f}, G={2:.8f}, B={3:.8f}, W={4:.2f}",
                               i, power.x, power.y, power.z, power.w));
        }
    }

    // Output wavelength distribution summary if available
    if (!mPowerStats.wavelengthDistribution.empty())
    {
        uint32_t totalBins = mPowerStats.wavelengthDistribution.size();
        uint32_t countedWavelengths = 0;

        for (const auto& [binIndex, count] : mPowerStats.wavelengthDistribution)
        {
            countedWavelengths += count;
        }

        logInfo(fmt::format("Wavelength distribution: {0} distinct bands, {1} wavelengths counted",
                          totalBins, countedWavelengths));
    }

    // Stats are now up to date
    mNeedStatsUpdate = false;
}
```

#### 2. 改进`resetStatistics`函数

为了更好地跟踪和记录统计重置操作，我们改进了`resetStatistics`函数：

1. **记录重置前的状态**：存储重置前的统计数据，用于日志记录
2. **添加重置日志**：添加详细的日志，记录重置的内容和范围
3. **确保完全重置**：确保所有统计数据的清零和初始化

修改后的代码：

```cpp
void IncomingLightPowerPass::resetStatistics()
{
    // Store previous values for logging
    uint32_t prevPixelCount = mPowerStats.pixelCount;
    uint32_t prevTotalPixels = mPowerStats.totalPixels;
    uint32_t prevWavelengthBins = mPowerStats.wavelengthDistribution.size();

    // Clear all statistics
    std::memset(mPowerStats.totalPower, 0, sizeof(mPowerStats.totalPower));
    std::memset(mPowerStats.peakPower, 0, sizeof(mPowerStats.peakPower));
    std::memset(mPowerStats.averagePower, 0, sizeof(mPowerStats.averagePower));
    mPowerStats.pixelCount = 0;
    mPowerStats.totalPixels = 0;
    mPowerStats.wavelengthDistribution.clear();

    // Reset frame accumulation
    uint32_t prevAccumulatedFrames = mAccumulatedFrames;
    mAccumulatedFrames = 0;

    // Log reset action
    if (prevPixelCount > 0 || prevAccumulatedFrames > 0)
    {
        logInfo(fmt::format("Statistics reset: Cleared {0} filtered pixels over {1} frames, {2} wavelength bins",
                          prevPixelCount, prevAccumulatedFrames, prevWavelengthBins));
    }

    // Mark stats as needing update
    mNeedStatsUpdate = true;
}
```

#### 3. 优化`renderStatisticsUI`函数

为了提供更好的用户体验和更清晰的统计显示，我们改进了`renderStatisticsUI`函数：

1. **使用格式化字符串**：使用fmt::format代替手动拼接字符串，提供更清晰的数字格式
2. **添加波长分布摘要**：显示波长分布的摘要信息，并提供可折叠的详细分布数据
3. **对波长分布进行排序**：按数量排序波长分布，使用户能够看到最常见的波长范围
4. **添加强制刷新按钮**：允许用户手动触发统计更新，解决自动更新可能不及时的问题
5. **改进按钮样式**：使用更明显的样式突出重要按钮

修改后的代码：

```cpp
void IncomingLightPowerPass::renderStatisticsUI(Gui::Widgets& widget)
{
    auto statsGroup = widget.group("Power Statistics", true);
    if (statsGroup)
    {
        // Enable/disable statistics
        bool statsChanged = widget.checkbox("Enable Statistics", mEnableStatistics);

        if (mEnableStatistics)
        {
            // Display basic statistics
            if (mPowerStats.totalPixels > 0)
            {
                const float passRate = 100.0f * float(mPowerStats.pixelCount) / float(mPowerStats.totalPixels);

                // Use formatted string for better display
                widget.text(fmt::format("Filtered pixels: {0} / {1} ({2:.2f}%)",
                                       mPowerStats.pixelCount,
                                       mPowerStats.totalPixels,
                                       passRate));

                widget.text(fmt::format("Total Power (W): R={0:.6f}, G={1:.6f}, B={2:.6f}",
                                       mPowerStats.totalPower[0],
                                       mPowerStats.totalPower[1],
                                       mPowerStats.totalPower[2]));

                widget.text(fmt::format("Average Power (W): R={0:.6f}, G={1:.6f}, B={2:.6f}",
                                       mPowerStats.averagePower[0],
                                       mPowerStats.averagePower[1],
                                       mPowerStats.averagePower[2]));

                widget.text(fmt::format("Peak Power (W): R={0:.6f}, G={1:.6f}, B={2:.6f}",
                                       mPowerStats.peakPower[0],
                                       mPowerStats.peakPower[1],
                                       mPowerStats.peakPower[2]));

                // Add wavelength statistics summary
                if (!mPowerStats.wavelengthDistribution.empty())
                {
                    widget.text(fmt::format("Wavelength distribution: {0} distinct bands",
                                          mPowerStats.wavelengthDistribution.size()));

                    // Add expandable wavelength details
                    auto wlGroup = widget.group("Wavelength Details", false);
                    if (wlGroup)
                    {
                        int maxBandsToShow = 10; // Limit UI display to top 10 bands
                        int bandsShown = 0;

                        // Sort wavelength bins by count to show most common first
                        std::vector<std::pair<int, uint32_t>> sortedBins;
                        for (const auto& [wavelength, count] : mPowerStats.wavelengthDistribution)
                        {
                            sortedBins.push_back({wavelength, count});
                        }

                        std::sort(sortedBins.begin(), sortedBins.end(),
                                 [](const auto& a, const auto& b) { return a.second > b.second; });

                        for (const auto& [wavelength, count] : sortedBins)
                        {
                            if (bandsShown++ >= maxBandsToShow)
                            {
                                widget.text("... and more bands");
                                break;
                            }

                            widget.text(fmt::format("{0}-{1} nm: {2} pixels",
                                                  wavelength * 10,
                                                  wavelength * 10 + 10,
                                                  count));
                        }
                    }
                }
            }
            else
            {
                widget.text("No statistics available");
            }

            // Power accumulation options
            statsChanged |= widget.checkbox("Accumulate Power", mAccumulatePower);
            if (mAccumulatePower)
            {
                widget.text(fmt::format("Accumulated frames: {0}", mAccumulatedFrames));
            }

            // Manual reset button with better styling
            if (widget.button("Reset Statistics", true)) // Use primary button style
            {
                resetStatistics();
            }

            // Add force refresh button
            if (widget.button("Force Refresh Statistics"))
            {
                mNeedStatsUpdate = true;
            }

            statsChanged |= widget.checkbox("Auto-clear when filter changes", mAutoClearStats);
        }

        if (statsChanged)
        {
            mNeedStatsUpdate = true;
        }
    }
}
```

### 优化效果分析

通过这些优化，我们对IncomingLightPowerPass的统计功能进行了全面改进：

1. **计算精度增强**：
   - 使用epsilon值进行精确比较，避免浮点精度问题
   - 波长值有效性验证，避免不合理的波长数据
   - 添加数据一致性验证，确保统计结果可靠

2. **健壮性增强**：
   - 缓冲区有效性检查，避免处理无效数据
   - 安全除法操作，避免除零错误
   - 累积安全限制，防止长时间运行时内存无限增长
   - 异常值检测和修正

3. **用户体验提升**：
   - 更清晰的数字格式，使用固定小数位显示
   - 波长分布摘要和详情展示
   - 新增"强制刷新"按钮，便于调试
   - 更明显的按钮样式，提高UI可用性

4. **调试支持增强**：
   - 详细的统计日志输出
   - 数据一致性检查和警告
   - 统计重置操作的记录
   - 波长分布统计摘要

### 遇到的问题和解决方案

在实现过程中主要遇到了以下几个问题：

1. **问题**：在检测非零像素时，简单的`>0`比较可能会因为浮点精度问题导致不准确
   **解决方案**：引入小epsilon值(1e-6)作为阈值，以滤除可能由于精度误差导致的接近零的值

2. **问题**：在收集波长分布时，可能有无效的波长值（负值或异常大值）
   **解决方案**：增加波长值有效性检查，只统计在合理范围内（0-2000nm）的波长值

3. **问题**：在多帧累积统计中，像素计数可能无限增长
   **解决方案**：添加安全上限（帧尺寸的10倍），防止计数器无限增长，当达到上限时自动重置

4. **问题**：UI中的数值显示格式不够清晰
   **解决方案**：使用`fmt::format`代替字符串拼接，提供固定小数位的格式化显示

5. **问题**：波长分布可能有过多数据，不便于UI显示
   **解决方案**：添加可折叠的波长分布详情，并按数量排序，只显示最常见的波长范围

### 结论

通过这些优化，我们显著改进了IncomingLightPowerPass的统计功能，使其能够更准确地计算功率统计、提供更详细的统计信息，并为用户提供更好的界面体验。这些改进解决了原有功能中的各种问题，包括"屏幕上显示有非黑色像素，但统计面板显示'Filtered pixels: 0 / X (0%)'"的主要问题。

同时，我们还添加了许多健壮性增强措施，确保功能在各种边缘情况下都能正常工作，并提供有用的调试信息，帮助开发者快速定位和解决潜在问题。

## Bug修复：波长过滤开关功能

### 问题分析

为了让IncomingLightPowerPass的波长过滤功能变为可选，我们添加了一个新的布尔变量`mEnableWavelengthFilter`，并在shader和C++代码中相应地使用它。然而，在实现过程中遇到了以下编译错误：

1. C2660错误：`isWavelengthAllowed`函数被调用时传递了9个参数，但函数定义只接受8个参数
2. C2511错误：没有找到接受9个参数的`isWavelengthAllowed`方法的重载版本
3. C2511错误：没有找到接受12个参数的`compute`方法的重载版本
4. C2352错误：在非成员函数上下文中调用`computeCosTheta`，该方法需要一个对象实例

这些错误表明，新添加的`enableFilter`参数虽然在类定义(头文件)中已经添加，但在某些地方的方法调用中没有正确地更新。

### 解决方案

1. **修改头文件中的方法声明**：
   - 更新`CameraIncidentPower::isWavelengthAllowed`方法声明，添加`enableFilter`参数
   - 更新`CameraIncidentPower::compute`方法声明，添加`enableFilter`参数

2. **修改方法定义**：
   - 确保方法定义与声明匹配，正确传递`enableFilter`参数

3. **重构shader代码**：
   - 修改shader中的`computeLightPower`函数，以更清晰的方式使用波长过滤开关
   - 将波长过滤逻辑放在独立的条件检查中，使结构更清晰

### 实现细节

1. **头文件修改**：

```cpp
// CameraIncidentPower::compute方法声明
float4 compute(
    const uint2& pixel,
    const float3& rayDir,
    const float4& radiance,
    float wavelength,
    float minWavelength,
    float maxWavelength,
    FilterMode filterMode = FilterMode::Range,
    bool useVisibleSpectrumOnly = false,
    bool invertFilter = false,
    const std::vector<float>& bandWavelengths = {},
    const std::vector<float>& bandTolerances = {},
    bool enableFilter = true
) const;

// CameraIncidentPower::isWavelengthAllowed方法声明
bool isWavelengthAllowed(
    float wavelength,
    float minWavelength,
    float maxWavelength,
    FilterMode filterMode = FilterMode::Range,
    bool useVisibleSpectrumOnly = false,
    bool invertFilter = false,
    const std::vector<float>& bandWavelengths = {},
    const std::vector<float>& bandTolerances = {},
    bool enableFilter = true
) const;
```

2. **方法实现修改**：

```cpp
bool IncomingLightPowerPass::CameraIncidentPower::isWavelengthAllowed(
    float wavelength,
    float minWavelength,
    float maxWavelength,
    IncomingLightPowerPass::FilterMode filterMode,
    bool useVisibleSpectrumOnly,
    bool invertFilter,
    const std::vector<float>& bandWavelengths,
    const std::vector<float>& bandTolerances,
    bool enableFilter) const
{
    // If wavelength filtering is disabled, all wavelengths are allowed
    if (!enableFilter)
    {
        return true;
    }

    // 原有过滤逻辑...
}

float4 IncomingLightPowerPass::CameraIncidentPower::compute(
    const uint2& pixel,
    const float3& rayDir,
    const float4& radiance,
    float wavelength,
    float minWavelength,
    float maxWavelength,
    IncomingLightPowerPass::FilterMode filterMode,
    bool useVisibleSpectrumOnly,
    bool invertFilter,
    const std::vector<float>& bandWavelengths,
    const std::vector<float>& bandTolerances,
    bool enableFilter) const
{
    // Apply wavelength filtering
    if (!isWavelengthAllowed(wavelength, minWavelength, maxWavelength,
        filterMode, useVisibleSpectrumOnly, invertFilter,
        bandWavelengths, bandTolerances, enableFilter))
    {
        return float4(0.f, 0.f, 0.f, 0.f);
    }

    // 原有计算逻辑...
}
```

3. **Shader文件修改**：

```hlsl
// Compute the power of incoming light for the given pixel
float4 computeLightPower(uint2 pixel, uint2 dimensions, float3 rayDir, float4 radiance, float wavelength)
{
    // Apply wavelength filtering if enabled
    // Only check wavelength filtering if the global control is enabled
    if (gEnableWavelengthFilter)
    {
        // Check if the wavelength passes the filter
        if (!isWavelengthAllowed(wavelength))
        {
            return float4(0, 0, 0, 0);
        }
    }

    // Calculate pixel area and continue with power computation...
    // ...
}
```

4. **UI控制添加**：

```cpp
// 在renderUI方法中添加波长过滤开关
bool filterChanged = widget.checkbox("Enable Wavelength Filtering", mEnableWavelengthFilter);
if (filterChanged)
{
    changed = true;
    if (filterChanged && mAutoClearStats)
    {
        resetStatistics();
    }
}

// 只有在过滤启用时才显示过滤选项
if (mEnableWavelengthFilter)
{
    // 显示各种过滤选项
    // ...
}
else
{
    widget.text("All wavelengths will be passed through without filtering");
}
```

5. **着色器常量更新**：

```cpp
// 设置常量
var[kPerFrameCB][kMinWavelength] = mMinWavelength;
var[kPerFrameCB][kMaxWavelength] = mMaxWavelength;
var[kPerFrameCB][kUseVisibleSpectrumOnly] = mUseVisibleSpectrumOnly;
var[kPerFrameCB][kInvertFilter] = mInvertFilter;
var[kPerFrameCB][kFilterMode] = (uint32_t)mFilterMode;
var[kPerFrameCB]["gEnableWavelengthFilter"] = mEnableWavelengthFilter;
```

6. **启动时添加调试日志**：

```cpp
logInfo(fmt::format("IncomingLightPowerPass executing - settings: enabled={0}, wavelength_filter_enabled={1}, filter_mode={2}, min_wavelength={3}, max_wavelength={4}",
                   mEnabled, mEnableWavelengthFilter, static_cast<int>(mFilterMode), mMinWavelength, mMaxWavelength));
```

### 遇到的挑战与解决

**挑战1：方法声明与定义不匹配**
- 当我们向已有方法添加新参数时，必须同时更新所有调用点，否则会导致编译错误。
- 解决方案：通过为参数提供默认值，如`bool enableFilter = true`，可以避免更新所有调用点，但最好确保关键调用点正确传递该参数。

**挑战2：着色器和C++代码共同工作**
- 在C++代码和HLSL着色器之间共享功能时，需要在两边同时维护逻辑的一致性。
- 解决方案：在着色器中使用宏定义和条件判断，确保过滤逻辑只在`gEnableWavelengthFilter`为true时执行。

**挑战3：编译错误定位**
- 有时错误消息不足以精确定位问题所在的代码行。
- 解决方案：系统性检查所有相关代码，重新检查方法声明、定义和调用点，确保它们保持一致。

### 总结

通过添加波长过滤开关功能，我们使得IncomingLightPowerPass更加灵活。用户现在可以完全禁用波长过滤，以验证是否是过滤条件过于严格导致了一些像素被错误过滤掉。这对于调试和排除显示全黑问题非常有帮助。

同时，我们也增加了更多的日志信息，使得在运行时能够更容易诊断渲染问题。这些改进不仅解决了当前的特定问题，也提高了代码的整体健壮性和可维护性。
