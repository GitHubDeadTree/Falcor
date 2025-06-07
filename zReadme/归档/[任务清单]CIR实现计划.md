# 基于Falcor路径追踪的CIR计算实现计划

## 任务复述（最简化版本）

**核心目标**：基于Falcor的PathTracer收集光传播路径数据，通过新建的CIR计算渲染通道来计算信道冲激响应（Channel Impulse Response），最终输出可用于可见光通信分析的CIR数据。

**核心流程**：

1. PathTracer在路径追踪过程中自动收集每条路径的6个关键参数
2. PathTracer将收集的数据输出到共享缓冲区
3. 新的CIRComputePass读取路径数据，计算每条路径的功率增益和传播时延
4. CIRComputePass将所有路径叠加形成离散时间的CIR向量并输出

**简化原则**：

- PathTracer始终收集CIR数据，无需开关控制
- 专注于核心功能实现，减少不必要的复杂性
- 数据收集和计算过程透明化，便于调试和验证

## 子任务分解

### 第一部分：PathTracer修改（4个子任务）

#### 子任务1.1：PathTracer数据结构扩展

**任务目标**：在PathTracer中添加CIR相关的数据结构，用于存储每条路径的关键参数。

**实现方案**：

```hlsl
// 在PathTracer.slang中添加CIR路径数据结构
struct CIRPathData
{
    float pathLength;           // d_i: 路径总长度(米)
    float emissionAngle;        // φ_i: 发射角(弧度)
    float receptionAngle;       // θ_i: 接收角(弧度)
    float reflectanceProduct;   // r_i乘积: 所有反射面反射率乘积
    uint reflectionCount;       // K_i: 反射次数
    float emittedPower;         // P_t: 发射功率(瓦)
    uint2 pixelCoord;          // 像素坐标
    uint pathIndex;            // 路径索引
};

// 在PathTracer类中添加缓冲区
RWStructuredBuffer<CIRPathData> gCIRPathBuffer;
```

**计算验证**：

- 检查pathLength > 0且< 1000m（合理传播距离）
- 检查角度值在[0, π]范围内
- 检查reflectanceProduct在[0, 1]范围内
- 如果数据异常，记录logWarning并设置默认值

**验证方法**：

- 调试输出显示收集到的路径数量和参数范围
- 检查缓冲区是否正确分配和填充

#### 子任务1.2：路径参数收集逻辑

**任务目标**：在PathTracer的路径追踪过程中，实时收集每条路径的CIR计算所需参数。

**实现方案**：

```hlsl
// 在generatePath函数中添加CIR数据收集
void collectCIRData(const PathState pathState, const Ray ray, const HitInfo hit, inout CIRPathData cirData)
{
    // 调试：记录数据收集开始
    bool isDebugPath = (cirData.pathIndex % 10000 == 0); // 每10000条路径输出一次调试信息

    if (isDebugPath)
    {
        logInfo("CIR: Collecting data for path " + std::to_string(cirData.pathIndex));
    }

    // 1. 路径长度：累积sceneLength
    float prevLength = cirData.pathLength;
    cirData.pathLength += pathState.sceneLength;

    if (isDebugPath)
    {
        logInfo("CIR: Path length - Previous: " + std::to_string(prevLength) +
                "m, Added: " + std::to_string(pathState.sceneLength) +
                "m, Total: " + std::to_string(cirData.pathLength) + "m");
    }

    // 验证路径长度合理性
    if (cirData.pathLength < 0.0f || cirData.pathLength > 1000.0f)
    {
        logWarning("CIR: Invalid path length " + std::to_string(cirData.pathLength) +
                  "m for path " + std::to_string(cirData.pathIndex));
        cirData.pathLength = max(0.3f, min(cirData.pathLength, 1000.0f)); // 限制在合理范围
    }

    // 2. 发射角：第一次反射时计算
    if (pathState.bounceCounters.diffuseReflectionBounces == 0)
    {
        float3 surfaceNormal = hit.getNormalW();
        float3 rayDir = normalize(ray.dir);
        float dotProduct = abs(dot(-rayDir, surfaceNormal));
        cirData.emissionAngle = acos(clamp(dotProduct, 0.0f, 1.0f));

        if (isDebugPath)
        {
            logInfo("CIR: Emission angle calculated - Dot product: " + std::to_string(dotProduct) +
                    ", Angle: " + std::to_string(cirData.emissionAngle) + " rad (" +
                    std::to_string(cirData.emissionAngle * 180.0f / M_PI) + " deg)");
        }

        // 验证发射角
        if (cirData.emissionAngle < 0.0f || cirData.emissionAngle > M_PI)
        {
            logWarning("CIR: Invalid emission angle " + std::to_string(cirData.emissionAngle) +
                      " for path " + std::to_string(cirData.pathIndex));
            cirData.emissionAngle = M_PI / 4.0f; // 默认45度
        }
    }

    // 3. 接收角：最后一次接收时计算
    if (isReceiver(hit))
    {
        float3 surfaceNormal = hit.getNormalW();
        float3 rayDir = normalize(ray.dir);
        float dotProduct = abs(dot(-rayDir, surfaceNormal));
        cirData.receptionAngle = acos(clamp(dotProduct, 0.0f, 1.0f));

        if (isDebugPath)
        {
            logInfo("CIR: Reception angle calculated - Dot product: " + std::to_string(dotProduct) +
                    ", Angle: " + std::to_string(cirData.receptionAngle) + " rad (" +
                    std::to_string(cirData.receptionAngle * 180.0f / M_PI) + " deg)");
        }

        // 验证接收角
        if (cirData.receptionAngle < 0.0f || cirData.receptionAngle > M_PI)
        {
            logWarning("CIR: Invalid reception angle " + std::to_string(cirData.receptionAngle) +
                      " for path " + std::to_string(cirData.pathIndex));
            cirData.receptionAngle = M_PI / 4.0f; // 默认45度
        }
    }

    // 4. 反射率：每次反射时累乘
    if (hit.getType() != HitType::None)
    {
        float reflectance = computeReflectance(hit, ray);
        float prevProduct = cirData.reflectanceProduct;
        cirData.reflectanceProduct *= reflectance;

        if (isDebugPath)
        {
            logInfo("CIR: Reflectance - Current: " + std::to_string(reflectance) +
                    ", Previous product: " + std::to_string(prevProduct) +
                    ", New product: " + std::to_string(cirData.reflectanceProduct));
        }

        // 验证反射率合理性
        if (reflectance < 0.0f || reflectance > 1.0f)
        {
            logWarning("CIR: Invalid reflectance value " + std::to_string(reflectance) +
                      " for path " + std::to_string(cirData.pathIndex));
            cirData.reflectanceProduct = prevProduct * 0.5f; // 使用默认值
        }

        // 验证反射率乘积
        if (cirData.reflectanceProduct < 0.0f || cirData.reflectanceProduct > 1.0f)
        {
            logWarning("CIR: Invalid reflectance product " + std::to_string(cirData.reflectanceProduct) +
                      " for path " + std::to_string(cirData.pathIndex));
            cirData.reflectanceProduct = clamp(cirData.reflectanceProduct, 0.0f, 1.0f);
        }
    }

    // 5. 反射次数：使用bounceCounters
    uint prevReflectionCount = cirData.reflectionCount;
    cirData.reflectionCount = pathState.bounceCounters.diffuseReflectionBounces +
                             pathState.bounceCounters.specularReflectionBounces;

    if (isDebugPath)
    {
        logInfo("CIR: Reflection count - Diffuse: " + std::to_string(pathState.bounceCounters.diffuseReflectionBounces) +
                ", Specular: " + std::to_string(pathState.bounceCounters.specularReflectionBounces) +
                ", Total: " + std::to_string(cirData.reflectionCount));
    }

    // 6. 发射功率：从radiance计算
    float prevPower = cirData.emittedPower;
    cirData.emittedPower = luminance(pathState.radiance);

    if (isDebugPath)
    {
        logInfo("CIR: Emitted power - Radiance: (" +
                std::to_string(pathState.radiance.x) + ", " +
                std::to_string(pathState.radiance.y) + ", " +
                std::to_string(pathState.radiance.z) + "), Luminance: " +
                std::to_string(cirData.emittedPower) + "W");
    }

    // 验证发射功率
    if (cirData.emittedPower < 0.0f || isnan(cirData.emittedPower) || isinf(cirData.emittedPower))
    {
        logWarning("CIR: Invalid emitted power " + std::to_string(cirData.emittedPower) +
                  " for path " + std::to_string(cirData.pathIndex));
        cirData.emittedPower = 1.0f; // 默认1W
    }
}

// 添加路径数据写入缓冲区的函数
void writeCIRPathToBuffer(const CIRPathData cirData, uint pathIndex)
{
    // 检查缓冲区边界
    if (pathIndex >= gMaxCIRPaths)
    {
        logError("CIR: Path index " + std::to_string(pathIndex) +
                " exceeds buffer capacity " + std::to_string(gMaxCIRPaths));
        return;
    }

    // 写入数据到缓冲区
    gCIRPathBuffer[pathIndex] = cirData;

    // 每1000条路径输出一次状态
    if (pathIndex % 1000 == 0)
    {
        logInfo("CIR: Written " + std::to_string(pathIndex + 1) + " paths to buffer");

        // 输出数据样本用于验证
        if (pathIndex % 10000 == 0)
        {
            logInfo("CIR: Sample data for path " + std::to_string(pathIndex) + ":");
            logInfo("  Length: " + std::to_string(cirData.pathLength) + "m");
            logInfo("  Emission angle: " + std::to_string(cirData.emissionAngle * 180.0f / M_PI) + " deg");
            logInfo("  Reception angle: " + std::to_string(cirData.receptionAngle * 180.0f / M_PI) + " deg");
            logInfo("  Reflectance product: " + std::to_string(cirData.reflectanceProduct));
            logInfo("  Reflection count: " + std::to_string(cirData.reflectionCount));
            logInfo("  Emitted power: " + std::to_string(cirData.emittedPower) + "W");
        }
    }
}
```

**计算验证**：

- 验证每个参数的合理范围
- 路径长度异常时输出logError，设置为光速传播1纳秒的距离（0.3m）
- 角度异常时设置为π/4（45度）
- 功率为负或过大时设置为1.0W

**验证方法**：

- 统计收集的有效路径数量
- 检查参数分布是否合理（路径长度、角度分布等）

#### 子任务1.3：CIR数据缓冲区管理

**任务目标**：在PathTracer中实现CIR数据的缓冲区分配、写入和管理机制。

**实现方案**：

```cpp
// 在PathTracer.cpp中添加
class PathTracer : public RenderPass
{
private:
    Buffer::SharedPtr mpCIRPathBuffer;
    uint32_t mMaxCIRPaths = 1000000; // 最大路径数
    uint32_t mCurrentCIRPathCount = 0;
    bool mCIRBufferBound = false;        // 缓冲区绑定状态

    void allocateCIRBuffers()
    {
        // 调试信息：开始分配缓冲区
        logInfo("CIR: Starting buffer allocation...");
        logInfo("CIR: Requested buffer size - Elements: " + std::to_string(mMaxCIRPaths) +
                ", Element size: " + std::to_string(sizeof(CIRPathData)) + " bytes");
        logInfo("CIR: Total buffer size: " + std::to_string(mMaxCIRPaths * sizeof(CIRPathData) / (1024*1024)) + " MB");

        mpCIRPathBuffer = Buffer::createStructured(
            sizeof(CIRPathData),
            mMaxCIRPaths,
            Resource::BindFlags::ShaderResource | Resource::BindFlags::UnorderedAccess
        );

        if (!mpCIRPathBuffer)
        {
            logError("CIR: Failed to allocate CIR path buffer");
            mCIRBufferBound = false;
            return;
        }

        // 成功分配后的调试信息
        logInfo("CIR: Buffer allocation successful");
        logInfo("CIR: Buffer element count: " + std::to_string(mpCIRPathBuffer->getElementCount()));
        logInfo("CIR: Buffer element size: " + std::to_string(mpCIRPathBuffer->getElementSize()) + " bytes");
        logInfo("CIR: Buffer total size: " + std::to_string(mpCIRPathBuffer->getSize() / (1024*1024)) + " MB");
        logInfo("CIR: Buffer GPU memory address: 0x" + std::to_string(reinterpret_cast<uintptr_t>(mpCIRPathBuffer->getGpuAddress())));
    }

    bool bindCIRBufferToShader()
    {
        if (!mpCIRPathBuffer)
        {
            logError("CIR: Cannot bind buffer - buffer not allocated");
            mCIRBufferBound = false;
            return false;
        }

        try
        {
            // 绑定到计算着色器
            auto shader = mpComputePass->getProgram();
            auto vars = mpComputePass->getVars();

            // 绑定结构化缓冲区
            vars["gCIRPathBuffer"] = mpCIRPathBuffer;

            // 验证绑定是否成功
            auto boundBuffer = vars["gCIRPathBuffer"].asBuffer();
            if (boundBuffer != mpCIRPathBuffer)
            {
                logError("CIR: Buffer binding verification failed");
                mCIRBufferBound = false;
                return false;
            }

            mCIRBufferBound = true;
            logInfo("CIR: Buffer successfully bound to shader variable 'gCIRPathBuffer'");
            logInfo("CIR: Bound buffer element count: " + std::to_string(boundBuffer->getElementCount()));
            return true;
        }
        catch (const std::exception& e)
        {
            logError("CIR: Exception during buffer binding: " + std::string(e.what()));
            mCIRBufferBound = false;
            return false;
        }
    }

    void resetCIRData()
    {
        logInfo("CIR: Resetting buffer data...");

        mCurrentCIRPathCount = 0;

        if (!mpCIRPathBuffer)
        {
            logWarning("CIR: Cannot reset - buffer not allocated");
            return;
        }

        // 清零缓冲区
        mpRenderContext->clearUAV(mpCIRPathBuffer->getUAV().get(), uint4(0));

        logInfo("CIR: Buffer reset complete - path count: " + std::to_string(mCurrentCIRPathCount));
        logInfo("CIR: Buffer state - Allocated: " + std::string(mpCIRPathBuffer ? "Yes" : "No") +
                ", Bound: " + std::string(mCIRBufferBound ? "Yes" : "No"));
    }

    void logCIRBufferStatus()
    {
        logInfo("=== CIR Buffer Status ===");
        logInfo("CIR: Buffer allocated: " + std::string(mpCIRPathBuffer ? "Yes" : "No"));
        logInfo("CIR: Buffer bound to shader: " + std::string(mCIRBufferBound ? "Yes" : "No"));
        logInfo("CIR: Current path count: " + std::to_string(mCurrentCIRPathCount));
        logInfo("CIR: Max path capacity: " + std::to_string(mMaxCIRPaths));

        if (mpCIRPathBuffer)
        {
            float usagePercent = (float)mCurrentCIRPathCount / mMaxCIRPaths * 100.0f;
            logInfo("CIR: Buffer usage: " + std::to_string(usagePercent) + "%");

            if (usagePercent > 90.0f)
            {
                logWarning("CIR: Buffer usage exceeds 90% - consider increasing buffer size");
            }
        }
        logInfo("========================");
    }
};
```

**计算验证**：

- 检查缓冲区分配是否成功，输出详细的内存信息
- 验证缓冲区绑定状态，确保shader可以访问
- 监控缓冲区使用率，超过90%时输出logWarning
- 缓冲区溢出时停止写入并记录logError
- 实时验证每个CIR参数的合理性范围

**验证方法**：

- **缓冲区状态验证**：
  - 检查分配成功：`logInfo("CIR: Buffer allocation successful")`
  - 输出缓冲区大小：`logInfo("CIR: Total buffer size: X MB")`
  - 验证绑定状态：`logInfo("CIR: Buffer successfully bound to shader variable")`
  - 监控使用率：`logInfo("CIR: Buffer usage: X%")`

- **数据收集验证**：
  - 每10000条路径输出详细参数信息
  - 每1000条路径报告写入进度
  - 参数异常时立即输出警告和修正信息
  - 输出数据样本供人工检查

- **调试信息示例**：
```
CIR: Starting buffer allocation...
CIR: Total buffer size: 152 MB
CIR: Buffer allocation successful
CIR: Buffer successfully bound to shader variable 'gCIRPathBuffer'
CIR: Collecting data for path 10000
CIR: Path length - Total: 2.35m
CIR: Emission angle calculated - Angle: 0.524 rad (30.0 deg)
CIR: Written 11000 paths to buffer
CIR: Buffer usage: 1.1%
```

#### 子任务1.4：PathTracer输出接口

**任务目标**：实现PathTracer向CIR计算通道传递数据的接口机制。

**实现方案**：

```cpp
// 在PathTracer.cpp中添加输出接口
struct CIRDataOutput
{
    Buffer::SharedPtr pathDataBuffer;
    uint32_t validPathCount;
    bool isDataReady;
    uint32_t frameIndex;
    size_t bufferSizeBytes;      // 缓冲区字节大小
    float bufferUsagePercent;    // 缓冲区使用率
    bool bufferBound;           // 缓冲区绑定状态
};

class PathTracer : public RenderPass
{
public:
    CIRDataOutput getCIRData() const
    {
        logInfo("CIR: Preparing data output...");

        CIRDataOutput output;
        output.pathDataBuffer = mpCIRPathBuffer;
        output.validPathCount = mCurrentCIRPathCount;
        output.isDataReady = (mCurrentCIRPathCount > 0 && mpCIRPathBuffer != nullptr);
        output.frameIndex = mFrameCount;
        output.bufferBound = mCIRBufferBound;

        // 计算缓冲区使用信息
        if (mpCIRPathBuffer)
        {
            output.bufferSizeBytes = mpCIRPathBuffer->getSize();
            output.bufferUsagePercent = (float)mCurrentCIRPathCount / mMaxCIRPaths * 100.0f;
        }
        else
        {
            output.bufferSizeBytes = 0;
            output.bufferUsagePercent = 0.0f;
        }

        // 详细的输出状态调试信息
        logInfo("=== CIR Data Output Status ===");
        logInfo("CIR: Frame index: " + std::to_string(output.frameIndex));
        logInfo("CIR: Valid path count: " + std::to_string(output.validPathCount));
        logInfo("CIR: Max path capacity: " + std::to_string(mMaxCIRPaths));
        logInfo("CIR: Buffer usage: " + std::to_string(output.bufferUsagePercent) + "%");
        logInfo("CIR: Buffer size: " + std::to_string(output.bufferSizeBytes / (1024*1024)) + " MB");
        logInfo("CIR: Buffer allocated: " + std::string(mpCIRPathBuffer ? "Yes" : "No"));
        logInfo("CIR: Buffer bound: " + std::string(output.bufferBound ? "Yes" : "No"));
        logInfo("CIR: Data ready: " + std::string(output.isDataReady ? "Yes" : "No"));

        if (!output.isDataReady)
        {
            if (!mpCIRPathBuffer)
            {
                logWarning("CIR: Data not ready - buffer not allocated");
            }
            else if (mCurrentCIRPathCount == 0)
            {
                logWarning("CIR: Data not ready - no paths collected (count: " +
                          std::to_string(mCurrentCIRPathCount) + ")");
            }
            else if (!mCIRBufferBound)
            {
                logWarning("CIR: Data not ready - buffer not bound to shader");
            }
        }
        else
        {
            logInfo("CIR: Data successfully prepared for output");

            // 输出数据统计信息
            if (mCurrentCIRPathCount > 0)
            {
                logInfo("CIR: Data statistics:");
                logInfo("  - Total paths: " + std::to_string(mCurrentCIRPathCount));
                logInfo("  - Buffer utilization: " + std::to_string(output.bufferUsagePercent) + "%");
                logInfo("  - Memory used: " + std::to_string(mCurrentCIRPathCount * sizeof(CIRPathData) / 1024) + " KB");

                // 警告缓冲区使用率过高
                if (output.bufferUsagePercent > 90.0f)
                {
                    logWarning("CIR: Buffer usage exceeds 90% - consider increasing buffer size");
                }
                else if (output.bufferUsagePercent > 80.0f)
                {
                    logWarning("CIR: Buffer usage exceeds 80% - monitor closely");
                }
            }
        }

        logInfo("==============================");
        return output;
    }

    // 添加获取详细统计信息的方法
    void logDetailedCIRStatistics() const
    {
        if (!mpCIRPathBuffer || mCurrentCIRPathCount == 0)
        {
            logInfo("CIR: No statistics available - no data collected");
            return;
        }

        logInfo("=== Detailed CIR Statistics ===");
        logInfo("CIR: Collection status:");
        logInfo("  - Buffer allocated: " + std::string(mpCIRPathBuffer ? "Yes" : "No"));
        logInfo("  - Buffer bound: " + std::string(mCIRBufferBound ? "Yes" : "No"));
        logInfo("  - Current frame: " + std::to_string(mFrameCount));
        logInfo("  - Paths collected: " + std::to_string(mCurrentCIRPathCount));
        logInfo("  - Buffer capacity: " + std::to_string(mMaxCIRPaths));

        if (mpCIRPathBuffer)
        {
            logInfo("CIR: Buffer details:");
            logInfo("  - Element size: " + std::to_string(mpCIRPathBuffer->getElementSize()) + " bytes");
            logInfo("  - Total size: " + std::to_string(mpCIRPathBuffer->getSize() / (1024*1024)) + " MB");
            logInfo("  - GPU address: 0x" + std::to_string(reinterpret_cast<uintptr_t>(mpCIRPathBuffer->getGpuAddress())));
        }

        logInfo("================================");
    }
};
```

**计算验证**：

- 验证输出数据的完整性和有效性
- 检查帧索引的连续性
- 数据无效时返回安全的默认状态

**验证方法**：

- **输出状态验证**：
  - 每次调用`getCIRData()`时输出完整状态信息
  - 验证缓冲区分配、绑定、数据就绪状态
  - 监控缓冲区使用率和内存占用
  - 检查帧索引连续性和数据完整性

- **详细调试输出示例**：
```
CIR: Preparing data output...
=== CIR Data Output Status ===
CIR: Frame index: 1523
CIR: Valid path count: 45678
CIR: Max path capacity: 1000000
CIR: Buffer usage: 4.57%
CIR: Buffer size: 152 MB
CIR: Buffer allocated: Yes
CIR: Buffer bound: Yes
CIR: Data ready: Yes
CIR: Data successfully prepared for output
CIR: Data statistics:
  - Total paths: 45678
  - Buffer utilization: 4.57%
  - Memory used: 2928 KB
==============================
```

- **异常情况处理**：
  - 缓冲区未分配时的明确错误提示
  - 缓冲区未绑定时的警告信息
  - 无数据收集时的状态说明
  - 使用率过高时的容量警告

### 第二部分：新建CIR计算渲染通道（4个子任务）

#### 子任务2.1：CIRComputePass基础框架

**任务目标**：创建新的CIR计算渲染通道的基础框架和类结构。

**实现方案**：

```cpp
// CIRComputePass.h
class CIRComputePass : public RenderPass
{
public:
    using SharedPtr = std::shared_ptr<CIRComputePass>;
    static SharedPtr create(RenderContext* pRenderContext, const Dictionary& dict);

    virtual Dictionary getScriptingDictionary() override;
    virtual RenderPassReflection reflect(const CompileData& compileData) override;
    virtual void execute(RenderContext* pRenderContext, const RenderData& renderData) override;
    virtual void renderUI(Gui::Widgets& widget) override;

private:
    // CIR计算参数
    float mTimeResolution = 1e-9f;    // 时间分辨率(秒)
    float mMaxDelay = 1e-6f;          // 最大延迟(秒)
    uint32_t mCIRBins = 1000;         // CIR向量长度

    // LED参数
    float mLEDPower = 1.0f;           // LED功率(瓦)
    float mHalfPowerAngle = 0.5236f;  // 半功率角(弧度，30度)

    // 接收器参数
    float mReceiverArea = 1e-4f;      // 接收器面积(平方米)
    float mFieldOfView = 1.047f;      // 视场角(弧度，60度)

    Buffer::SharedPtr mpCIRBuffer;    // CIR结果缓冲区
    ComputePass::SharedPtr mpComputePass;
};
```

**计算验证**：

- 检查所有参数的合理性范围
- 参数异常时输出logError并设置为默认值
- 验证缓冲区创建成功

**验证方法**：

- 检查类初始化是否成功
- 验证参数设置是否生效

#### 子任务2.2：CIR功率增益计算

**任务目标**：实现每条路径功率增益的精确计算逻辑。

**实现方案**：

```hlsl
// CIRCompute.cs.slang
float computePathGain(CIRPathData pathData, float receiverArea, float lambertianOrder)
{
    // 计算朗伯阶数 m = -ln(2)/ln(cos(半功率角))
    float m = lambertianOrder;

    // 基础增益计算
    float distanceSquared = pathData.pathLength * pathData.pathLength;
    float areaGain = receiverArea / (4.0f * M_PI * distanceSquared);

    // 朗伯发射模式
    float emissionGain = (m + 1.0f) / (2.0f * M_PI) * pow(cos(pathData.emissionAngle), m);

    // 接收角度增益
    float receptionGain = cos(pathData.receptionAngle);

    // 反射损耗
    float reflectionLoss = pathData.reflectanceProduct;

    // 总增益
    float totalGain = areaGain * emissionGain * receptionGain * reflectionLoss;

    // 验证计算结果
    if (totalGain < 0.0f || isnan(totalGain) || isinf(totalGain))
    {
        // 返回极小的默认值而不是0，避免完全丢失路径
        return 1e-12f;
    }

    return totalGain;
}

float computePathPower(CIRPathData pathData, float pathGain)
{
    float power = pathData.emittedPower * pathGain;

    // 功率验证
    if (power < 0.0f || isnan(power) || isinf(power))
    {
        return 1e-12f; // 默认最小功率
    }

    return power;
}
```

**计算验证**：

- 检查增益值的合理范围（0-1之间）
- 验证角度计算的正确性
- 异常值处理：NaN、无穷大设为极小默认值

**验证方法**：

- 输出功率增益的统计分布
- 验证总功率守恒（输入功率≥输出功率）

#### 子任务2.3：CIR时延计算和离散化

**任务目标**：计算每条路径的传播时延，并将连续的CIR转换为离散时间序列。

**实现方案**：

```hlsl
// 时延计算
float computePathDelay(float pathLength)
{
    const float LIGHT_SPEED = 2.998e8f; // 光速 m/s
    float delay = pathLength / LIGHT_SPEED;

    // 验证时延合理性
    if (delay < 0.0f || delay > 1e-3f) // 超过1ms视为异常
    {
        return 1e-9f; // 默认1ns延迟
    }

    return delay;
}

// CIR离散化
[numthreads(256, 1, 1)]
void computeCIR(uint3 id : SV_DispatchThreadID)
{
    uint pathIndex = id.x;
    if (pathIndex >= gPathCount) return;

    CIRPathData pathData = gPathDataBuffer[pathIndex];

    // 计算功率和时延
    float pathGain = computePathGain(pathData, gReceiverArea, gLambertianOrder);
    float pathPower = computePathPower(pathData, pathGain);
    float pathDelay = computePathDelay(pathData.pathLength);

    // 计算时间bin索引
    uint binIndex = uint(pathDelay / gTimeResolution);

    if (binIndex < gCIRBins)
    {
        // 原子操作累加到CIR向量
        InterlockedAdd(gCIRBuffer[binIndex], asuint(pathPower));
    }
    else
    {
        // 超出范围的路径记录到溢出计数器
        InterlockedAdd(gOverflowCounter[0], 1);
    }
}
```

**计算验证**：

- 验证时延计算的物理合理性
- 检查bin索引不越界
- 监控溢出计数，超过总路径10%时输出logWarning

**验证方法**：

- 检查CIR向量的峰值位置和形状
- 验证时延分布的合理性
- 统计溢出路径数量

#### 子任务2.4：CIR结果输出和可视化

**任务目标**：实现CIR计算结果的输出、存储和基本可视化功能。

**实现方案**：

```cpp
// CIR结果处理
void CIRComputePass::outputCIRResults(RenderContext* pRenderContext)
{
    // 读取CIR数据
    std::vector<float> cirData(mCIRBins);
    float* pData = static_cast<float*>(mpCIRBuffer->map(Buffer::MapType::Read));
    std::memcpy(cirData.data(), pData, mCIRBins * sizeof(float));
    mpCIRBuffer->unmap();

    // 验证数据有效性
    float totalPower = 0.0f;
    uint32_t nonZeroBins = 0;

    for (uint32_t i = 0; i < mCIRBins; ++i)
    {
        if (cirData[i] > 0.0f)
        {
            totalPower += cirData[i];
            nonZeroBins++;
        }

        // 检查异常值
        if (std::isnan(cirData[i]) || std::isinf(cirData[i]))
        {
            logWarning("Invalid CIR value at bin " + std::to_string(i));
            cirData[i] = 0.0f;
        }
    }

    // 输出统计信息
    logInfo("CIR computed: " + std::to_string(nonZeroBins) + " non-zero bins, " +
            "total power: " + std::to_string(totalPower) + "W");

    // 保存到文件
    saveCIRToFile(cirData, "cir_output.csv");

    // 更新可视化数据
    updateVisualization(cirData);
}

void CIRComputePass::saveCIRToFile(const std::vector<float>& cirData, const std::string& filename)
{
    std::ofstream file(filename);
    if (!file.is_open())
    {
        logError("Failed to open file for CIR output: " + filename);
        return;
    }

    file << "Time(ns),Power(W)\n";
    for (uint32_t i = 0; i < cirData.size(); ++i)
    {
        float timeNs = i * mTimeResolution * 1e9f;
        file << timeNs << "," << cirData[i] << "\n";
    }

    file.close();
    logInfo("CIR data saved to: " + filename);
}
```

**计算验证**：

- 验证CIR数据的总功率不超过输入功率
- 检查数据中的NaN和无穷大值
- 验证非零bin的数量合理性

**验证方法**：

- 检查输出文件是否正确生成
- 验证CIR数据的统计特性（峰值、延迟扩散等）
- 可视化CIR波形检查合理性

## 任务完成后的增强方向

### 1. 精度和性能优化

- **多精度计算**：支持float16/float32/float64精度选择
- **GPU加速优化**：使用更高效的并行算法
- **内存优化**：实现流式处理减少内存占用
- **自适应采样**：根据场景复杂度调整路径数量

### 2. 物理模型增强

- **波长相关性**：支持多波长CIR计算
- **极化效应**：考虑光的极化对传播的影响
- **大气散射**：加入瑞利散射和米氏散射模型
- **材质属性扩展**：支持更复杂的光学材质模型

### 3. 应用功能扩展

- **频域分析**：提供频率响应计算
- **信道容量评估**：计算信道容量和误码率
- **多用户场景**：支持多发射器多接收器系统
- **实时分析**：提供实时CIR监控和分析

### 4. 交互和可视化增强

- **3D可视化**：显示光传播路径的3D轨迹
- **交互式参数调节**：实时调整LED和接收器参数
- **统计分析面板**：提供详细的CIR统计信息
- **批量仿真**：支持参数扫描和批量计算

### 5. 验证和测试增强

- **理论验证**：与解析解对比验证
- **实验数据对比**：与实测数据进行对比
- **基准测试**：建立标准测试用例
- **自动化测试**：集成单元测试和回归测试

这个实现计划确保了CIR计算功能的核心实现，同时为后续的功能扩展留出了充足的接口和架构空间。

## 补充说明：子任务依赖关系和数据收集时机

### 数据收集开始的前提条件

**PathTracer开始收集CIR数据需要完成**：

1. 子任务1.1：数据结构定义（`CIRPathData`结构体）
2. 子任务1.3：缓冲区分配和管理
3. 子任务1.2：收集逻辑实现

**建议的实现顺序**：

```
子任务1.1 → 子任务1.3 → 子任务1.2 → 自动开始收集数据
```

### 数据输出的时机

**PathTracer的数据输出接口**：

- 完成子任务1.4后，`getCIRData()`接口立即可用
- 即使没有外部通道连接，接口也会返回当前数据状态
- 通过 `isDataReady`字段判断数据是否有效

### 简化设计的优势

1. **减少控制复杂性**：无需 `mEnableCIRCollection`开关变量
2. **降低出错概率**：减少条件判断和状态管理
3. **便于调试**：数据收集过程始终活跃，便于观察和验证
4. **代码简洁**：专注于核心功能，避免过度设计
