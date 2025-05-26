# IncomingLightPowerPass统计功能修复计划

## 问题概述

IncomingLightPowerPass中存在一个显著问题：屏幕上显示有非黑色像素，表明有像素通过了波长过滤，但统计面板却显示"Filtered pixels: 0 / X (0%)"，所有功率统计值（总功率、平均功率、峰值功率）也都为零。这表明GPU渲染结果和CPU统计计算之间存在不一致。

## 根本原因分析

经过代码分析，最可能的根本原因是从GPU读回纹理数据到CPU进行统计计算时出现了错误。具体包括：

1. 内存复制问题：从GPU读回的原始字节数据（`std::vector<uint8_t>`）没有被正确解析为适当的数据类型（`float4`）
2. 纹理数据格式不匹配：读回操作可能没有考虑正确的纹理格式
3. 内存大小不一致：复制的字节数可能不正确

## 子任务分解

### 子任务1：增强调试日志，确认问题范围

#### 任务目标
添加详细的调试日志，确认问题的具体位置和范围，验证是否在GPU渲染和CPU读回之间存在数据不一致。

#### 实现方案
1. 在`calculateStatistics`函数中添加统计非零像素的调试日志
2. 在`readbackData`函数中添加数据大小和格式的日志
3. 对读回的纹理数据添加简单的验证和采样日志

```cpp
// 在calculateStatistics函数中添加
uint32_t nonZeroPixels = 0;
for (uint32_t i = 0; i < mPowerReadbackBuffer.size(); i++)
{
    const float4& power = mPowerReadbackBuffer[i];
    if (power.x > 0 || power.y > 0 || power.z > 0)
    {
        nonZeroPixels++;
        if (nonZeroPixels < 10) // 记录前10个非零像素的值
        {
            logInfo(fmt::format("NonZero pixel [{0}]: R={1}, G={2}, B={3}, W={4}",
                                i, power.x, power.y, power.z, power.w));
        }
    }
}
logInfo("IncomingLightPowerPass: Found " + std::to_string(nonZeroPixels) +
        " non-zero power pixels out of " + std::to_string(mPowerReadbackBuffer.size()));

// 在readbackData函数中添加
logInfo(fmt::format("Power texture: format={0}, size={1}x{2}, raw data size={3} bytes",
                    to_string(pOutputPower->getFormat()), width, height, powerRawData.size()));
```

#### 验证方法
1. 通过日志确认GPU上是否有非零像素
2. 确认读回的数据大小是否与预期一致
3. 检查原始数据前几个像素的值，确认是否正确读取

### 子任务2：修复内存复制和数据解析问题

#### 任务目标
修复`readbackData`函数中的内存复制和数据解析问题，确保从GPU读回的数据被正确解析为适当的数据类型。

#### 实现方案
修改`readbackData`函数中的数据复制逻辑：

```cpp
// 修改前
std::memcpy(mPowerReadbackBuffer.data(), powerRawData.data(), powerRawData.size());
std::memcpy(mWavelengthReadbackBuffer.data(), wavelengthRawData.data(), wavelengthRawData.size());

// 修改后
// 正确解析RGBA32Float格式的数据
if (powerRawData.size() >= numPixels * sizeof(float4))
{
    // 使用适当的方式重新解释数据
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

// 同样正确解析R32Float格式的波长数据
if (wavelengthRawData.size() >= numPixels * sizeof(float))
{
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

#### 验证方法
1. 确认没有错误日志输出
2. 验证非零像素的数量与屏幕上的实际显示一致
3. 统计面板显示的"Filtered pixels"数值大于0，且与日志中报告的非零像素数量一致

### 子任务3：优化统计计算逻辑

#### 任务目标
改进`calculateStatistics`函数，确保所有统计数据正确计算。

#### 实现方案
修改`calculateStatistics`函数中的统计逻辑：

```cpp
void IncomingLightPowerPass::calculateStatistics(RenderContext* pRenderContext, const RenderData& renderData)
{
    // 读回数据
    if (!readbackData(pRenderContext, renderData))
    {
        logError("Failed to read back data for statistics calculation");
        return;
    }

    // 初始化统计数据
    if (!mAccumulatePower || mAccumulatedFrames == 0)
    {
        resetStatistics();
    }

    // 计数变量，用于调试和验证
    uint32_t nonZeroCount = 0;

    // 统计像素，累积功率值
    for (uint32_t i = 0; i < mPowerReadbackBuffer.size(); i++)
    {
        const float4& power = mPowerReadbackBuffer[i];

        // 只处理非零功率的像素（通过过滤的像素）
        if (power.x > 0.0f || power.y > 0.0f || power.z > 0.0f)
        {
            nonZeroCount++;
            mPowerStats.pixelCount++;

            // 累积总功率
            mPowerStats.totalPower[0] += power.x;
            mPowerStats.totalPower[1] += power.y;
            mPowerStats.totalPower[2] += power.z;

            // 跟踪峰值功率
            mPowerStats.peakPower[0] = std::max(mPowerStats.peakPower[0], power.x);
            mPowerStats.peakPower[1] = std::max(mPowerStats.peakPower[1], power.y);
            mPowerStats.peakPower[2] = std::max(mPowerStats.peakPower[2], power.z);

            // 跟踪波长分布（按10nm区间分组）
            float wavelength = power.w;
            if (wavelength > 0.0f) // 确保波长有效
            {
                int wavelengthBin = static_cast<int>(wavelength / 10.0f);
                mPowerStats.wavelengthDistribution[wavelengthBin]++;
            }
        }
    }

    // 更新总像素数
    mPowerStats.totalPixels = mFrameDim.x * mFrameDim.y;

    // 计算平均值
    if (mPowerStats.pixelCount > 0)
    {
        mPowerStats.averagePower[0] = mPowerStats.totalPower[0] / mPowerStats.pixelCount;
        mPowerStats.averagePower[1] = mPowerStats.totalPower[1] / mPowerStats.pixelCount;
        mPowerStats.averagePower[2] = mPowerStats.totalPower[2] / mPowerStats.pixelCount;
    }

    // 更新累积帧计数
    if (mAccumulatePower)
    {
        mAccumulatedFrames++;
    }

    // 调试信息
    logInfo(fmt::format("Statistics calculation: Found {0} non-zero pixels in current frame, total counted: {1}",
                        nonZeroCount, mPowerStats.pixelCount));

    // 标记统计数据已更新
    mNeedStatsUpdate = false;
}
```

#### 验证方法
1. 验证日志中新增的统计信息是否合理
2. 确认"Filtered pixels"数值与日志中报告的非零像素数量一致
3. 确认功率统计值（总功率、平均功率、峰值功率）不再全为零

### 子任务4：处理边缘情况和健壮性增强

#### 任务目标
增强代码的健壮性，处理各种边缘情况，确保即使在特殊情况下也能正常工作。

#### 实现方案
1. 添加对读回失败的更好处理
2. 处理波长值无效的情况
3. 增加安全检查以避免除零和边界溢出

```cpp
// 改进readbackData函数的错误处理
bool IncomingLightPowerPass::readbackData(RenderContext* pRenderContext, const RenderData& renderData)
{
    // ... 现有代码 ...

    try
    {
        // 获取原始纹理数据
        std::vector<uint8_t> powerRawData;
        std::vector<uint8_t> wavelengthRawData;

        try
        {
            powerRawData = pRenderContext->readTextureSubresource(pOutputPower.get(), 0);
        }
        catch (const std::exception& e)
        {
            logError(fmt::format("Failed to read power texture: {0}", e.what()));
            return false;
        }

        try
        {
            wavelengthRawData = pRenderContext->readTextureSubresource(pOutputWavelength.get(), 0);
        }
        catch (const std::exception& e)
        {
            logError(fmt::format("Failed to read wavelength texture: {0}", e.what()));
            return false;
        }

        // ... 剩余处理代码 ...
    }
    catch (const std::exception& e)
    {
        logError(fmt::format("Unexpected error in readbackData: {0}", e.what()));
        return false;
    }

    return true;
}

// 增强calculateStatistics中的健壮性
void IncomingLightPowerPass::calculateStatistics(RenderContext* pRenderContext, const RenderData& renderData)
{
    // ... 现有代码 ...

    // 确保buffer大小合理
    if (mPowerReadbackBuffer.empty())
    {
        logError("Power readback buffer is empty, cannot calculate statistics");
        return;
    }

    // 添加安全限制，防止像素计数异常增长
    const uint32_t maxPixelCount = mFrameDim.x * mFrameDim.y * 10; // 允许累积的最大像素数
    if (mAccumulatePower && mPowerStats.pixelCount >= maxPixelCount)
    {
        logWarning(fmt::format("Pixel count reached limit ({0}), resetting statistics", maxPixelCount));
        resetStatistics();
    }

    // ... 剩余代码 ...
}
```

#### 验证方法
1. 测试特殊情况，如空纹理或尺寸变化
2. 检查是否有异常日志输出
3. 确认长时间运行时累积功能仍然正常工作

### 子任务5：UI改进和用户体验增强

#### 任务目标
改进统计UI界面，提供更好的用户体验和更有用的信息。

#### 实现方案
修改`renderStatisticsUI`函数，增加额外信息和调试选项：

```cpp
void IncomingLightPowerPass::renderStatisticsUI(Gui::Widgets& widget)
{
    auto statsGroup = widget.group("Power Statistics", true);
    if (statsGroup)
    {
        // 启用/禁用统计
        bool statsChanged = widget.checkbox("Enable Statistics", mEnableStatistics);

        if (mEnableStatistics)
        {
            // 显示基本统计信息
            if (mPowerStats.totalPixels > 0)
            {
                const float passRate = 100.0f * float(mPowerStats.pixelCount) / float(mPowerStats.totalPixels);

                // 添加更多格式化和详细信息
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

                // 添加波长统计简要信息
                if (!mPowerStats.wavelengthDistribution.empty())
                {
                    widget.text(fmt::format("Wavelength distribution: {0} distinct bands",
                                          mPowerStats.wavelengthDistribution.size()));

                    // 可折叠的波长分布详情（可选）
                    auto wlGroup = widget.group("Wavelength Details", false);
                    if (wlGroup)
                    {
                        int maxBandsToShow = 10; // 限制UI中显示的波段数量
                        int bandsShown = 0;

                        for (const auto& [wavelength, count] : mPowerStats.wavelengthDistribution)
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

            // 功率累积选项
            statsChanged |= widget.checkbox("Accumulate Power", mAccumulatePower);
            if (mAccumulatePower)
            {
                widget.text(fmt::format("Accumulated frames: {0}", mAccumulatedFrames));
            }

            // 手动重置按钮
            if (widget.button("Reset Statistics"))
            {
                resetStatistics();
            }

            // 添加刷新按钮，强制重新计算统计数据
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

#### 验证方法
1. 确认UI显示的数据格式更清晰，数字格式适当
2. 测试新增按钮功能（Force Refresh Statistics）
3. 验证波长分布摘要信息是否显示正确

### 子任务6：最终集成和全面测试

#### 任务目标
将所有修复整合到一起，并进行全面测试，确保统计功能在各种情况下都能正常工作。

#### 实现方案
1. 合并所有修复到代码中
2. 添加全面的日志和异常处理
3. 在不同的场景和条件下测试功能

#### 验证方法
1. 测试不同波长范围和过滤设置下的统计功能
2. 验证累积功能在多帧渲染中的正确性
3. 确认UI显示与实际渲染结果一致
4. 测试边缘情况，如全黑场景或所有像素都通过过滤

## 时间估计和优先级

| 子任务 | 优先级 | 估计时间 |
|-------|--------|---------|
| 1. 增强调试日志 | 高 | 2小时 |
| 2. 修复内存复制和数据解析问题 | 高 | 3小时 |
| 3. 优化统计计算逻辑 | 中 | 2小时 |
| 4. 处理边缘情况和健壮性增强 | 中 | 2小时 |
| 5. UI改进和用户体验增强 | 低 | 2小时 |
| 6. 最终集成和全面测试 | 高 | 4小时 |

总计：约15小时

## 风险评估

1. **API兼容性问题**：Falcor框架的不同版本可能有API差异，需要根据实际版本调整代码。
2. **数据格式不一致**：GPU和CPU之间的数据格式转换可能存在未预见的问题。
3. **性能影响**：读回大量纹理数据可能影响性能，特别是在高分辨率下。
4. **并发问题**：在某些情况下，渲染和统计计算的并发可能导致数据不一致。

## 后续计划

完成基本修复后，可以考虑以下增强：

1. 添加更多的统计指标，如中位数功率、标准差等
2. 实现更高级的波长分布可视化
3. 添加导出统计数据的功能
4. 优化读回性能，考虑使用异步读回或仅读回部分数据进行统计
