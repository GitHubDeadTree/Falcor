# CIR集成PathTracer最简化实施计划

## 任务总览

**核心目标**: 将CIR（信道冲激响应）数据收集功能集成到PathTracer中，实现最小化修改、最大化数据复用的方案。

**简化版本**:
- 仅在PathState中添加3个必要的CIR字段（12字节）
- 复用现有PathState字段获取66.7%的CIR参数
- 实现CIR数据的结构化缓冲区输出
- 提供基本的数据验证和错误处理

## 子任务清单

### 子任务1: PathState结构扩展

#### 1. 任务目标
在PathState.slang中添加3个CIR特定字段，并实现数据复用方法，确保对现有代码的最小影响。

#### 2. 实现方案

**修改文件**: `Source/RenderPasses/PathTracer/PathState.slang`

```hlsl
// 在PathState结构体的最后添加CIR字段
struct PathState
{
    // ... 所有现有字段保持不变 ...
    
    // === 新增：CIR特定字段（12字节） ===
    float cirEmissionAngle;        ///< 发射角 (radians)
    float cirReceptionAngle;       ///< 接收角 (radians)  
    float cirReflectanceProduct;   ///< 反射率乘积 [0,1]

    // === 新增：CIR数据管理方法 ===
    
    /** 初始化CIR数据 */
    [mutating] void initCIRData()
    {
        cirEmissionAngle = 0.0f;
        cirReceptionAngle = 0.0f; 
        cirReflectanceProduct = 1.0f;
    }

    /** 更新发射角 */
    [mutating] void updateCIREmissionAngle(float3 surfaceNormal)
    {
        float3 rayDir = normalize(dir);
        float3 normal = normalize(surfaceNormal);
        float cosAngle = abs(dot(rayDir, normal));
        
        // 错误检查：确保cosAngle在有效范围内
        if (cosAngle < 0.0f || cosAngle > 1.0f || isnan(cosAngle) || isinf(cosAngle))
        {
            cirEmissionAngle = 0.0f; // 默认值：垂直发射
            return;
        }
        
        cirEmissionAngle = acos(clamp(cosAngle, 0.0f, 1.0f));
    }

    /** 更新接收角 */
    [mutating] void updateCIRReceptionAngle(float3 incomingDir, float3 surfaceNormal)
    {
        float3 incDir = normalize(-incomingDir);
        float3 normal = normalize(surfaceNormal);
        float cosAngle = abs(dot(incDir, normal));
        
        // 错误检查
        if (cosAngle < 0.0f || cosAngle > 1.0f || isnan(cosAngle) || isinf(cosAngle))
        {
            cirReceptionAngle = 0.0f; // 默认值：垂直接收
            return;
        }
        
        cirReceptionAngle = acos(clamp(cosAngle, 0.0f, 1.0f));
    }

    /** 累积反射率 */
    [mutating] void updateCIRReflectance(float reflectance)
    {
        // 错误检查：确保反射率在合理范围内
        if (reflectance < 0.0f || reflectance > 1.0f || isnan(reflectance) || isinf(reflectance))
        {
            // 警告：反射率值异常，使用默认值
            return; // 保持现有值不变
        }
        
        cirReflectanceProduct *= reflectance;
        
        // 防止下溢
        if (cirReflectanceProduct < 1e-6f)
        {
            cirReflectanceProduct = 1e-6f;
        }
    }

    /** 获取完整CIR数据（复用现有字段） */
    CIRPathData getCIRData()
    {
        CIRPathData cir;
        
        // 复用现有字段
        cir.pathLength = float(sceneLength);
        cir.reflectionCount = getBounces(BounceType::Diffuse) + getBounces(BounceType::Specular);
        cir.emittedPower = getLightIntensity();
        uint2 pixel = getPixel();
        cir.pixelX = pixel.x;
        cir.pixelY = pixel.y;
        cir.pathIndex = id;
        
        // 使用新增字段
        cir.emissionAngle = cirEmissionAngle;
        cir.receptionAngle = cirReceptionAngle;
        cir.reflectanceProduct = cirReflectanceProduct;
        
        return cir;
    }
};
```

#### 3. 错误处理和调试
- **角度计算错误**: 当dot产品超出[-1,1]范围时，输出默认值（垂直角度0.0）
- **反射率异常**: 当反射率超出[0,1]范围时，保持原值不变
- **数值溢出检查**: 使用isnan()和isinf()检查计算结果
- **下溢保护**: 防止反射率乘积过小导致数值问题

#### 4. 验证方法
- **编译验证**: 确保PathState结构编译无错误
- **字段访问验证**: 确保新增字段可以正常读写
- **方法调用验证**: 确保CIR方法可以正常调用
- **数据合理性**: 角度值在[0,π]范围内，反射率乘积在[0,1]范围内

---

### 子任务2: PathTracer输出通道扩展

#### 1. 任务目标
在PathTracer中添加CIR数据的结构化缓冲区输出通道，使CIR数据能够通过渲染图传递给下游通道。

#### 2. 实现方案

**修改文件1**: `Source/RenderPasses/PathTracer/PathTracer.h`

```cpp
class FALCOR_API PathTracer : public RenderPass
{
    // ... 现有声明保持不变 ...
    
private:
    // ... 现有成员保持不变 ...
    
    // === 新增：CIR数据输出缓冲区 ===
    ref<Buffer> mpSampleCIRData;                ///< CIR路径数据输出缓冲区
};
```

**修改文件2**: `Source/RenderPasses/PathTracer/PathTracer.cpp`

```cpp
namespace
{
    // 在kOutputChannels中添加CIR输出通道
    const ChannelList kOutputChannels = {
        // ... 现有输出通道保持不变 ...
        {"color",               "gOutputColor",             "Output color (sum of direct and indirect)", true /* optional */, ResourceFormat::Unknown },
        {"albedo",              "gOutputAlbedo",            "Surface albedo (base color)", true /* optional */, ResourceFormat::Unknown },
        
        // === 新增：CIR数据输出通道 ===
        {"cirData",             "gOutputCIRData",           "CIR path data for VLC analysis", true /* optional */, ResourceFormat::Unknown },
    };
}

// 在prepareResources函数中添加CIR缓冲区创建
void PathTracer::prepareResources(RenderContext* pRenderContext, const RenderData& renderData)
{
    // ... 现有资源准备逻辑保持不变 ...
    
    // === 新增：CIR数据缓冲区准备 ===
    const uint32_t sampleCount = mParams.frameDim.x * mParams.frameDim.y * mStaticParams.samplesPerPixel;
    
    // 创建或调整CIR数据输出缓冲区
    if (!mpSampleCIRData || mpSampleCIRData->getElementCount() < sampleCount || mVarsChanged)
    {
        try 
        {
            auto var = mpPathTracerBlock->getRootVar();
            mpSampleCIRData = mpDevice->createStructuredBuffer(
                var["sampleCIRData"], 
                sampleCount, 
                ResourceBindFlags::ShaderResource | ResourceBindFlags::UnorderedAccess, 
                MemoryType::DeviceLocal, 
                nullptr, 
                false
            );
            
            if (!mpSampleCIRData)
            {
                logError("PathTracer: Failed to create CIR data buffer");
                return;
            }
            
            mVarsChanged = true;
            logInfo("PathTracer: Created CIR data buffer with {} elements", sampleCount);
        }
        catch (const std::exception& e)
        {
            logError("PathTracer: Exception creating CIR buffer: {}", e.what());
            return;
        }
    }
}

// 在reflect函数中添加CIR数据缓冲区输出反射
RenderPassReflection PathTracer::reflect(const CompileData& compileData)
{
    RenderPassReflection reflector;
    const uint2 sz = RenderPassHelpers::calculateIOSize(mOutputSizeSelection, mFixedOutputSize, compileData.defaultTexDims);

    addRenderPassInputs(reflector, kInputChannels);
    addRenderPassOutputs(reflector, kOutputChannels, ResourceBindFlags::UnorderedAccess, sz);
    
    // === 新增：CIR数据缓冲区输出反射 ===
    try 
    {
        const uint32_t maxSamples = sz.x * sz.y * mStaticParams.samplesPerPixel;
        reflector.addOutput("cirData", "CIR path data buffer for VLC analysis")
            .bindFlags(ResourceBindFlags::UnorderedAccess | ResourceBindFlags::ShaderResource)
            .structuredBuffer(sizeof(CIRPathData), maxSamples)
            .flags(RenderPassReflection::Field::Flags::Optional);
    }
    catch (const std::exception& e)
    {
        logError("PathTracer: Exception adding CIR output reflection: {}", e.what());
    }
    
    return reflector;
}

// 在bindShaderData函数中添加CIR数据绑定
void PathTracer::bindShaderData(const ShaderVar& var, const RenderData& renderData, bool useLightSampling) const
{
    // ... 现有绑定逻辑保持不变 ...
    
    // === 新增：绑定CIR数据输出 ===
    try 
    {
        // 优先使用渲染图连接的输出缓冲区
        if (auto pCIROutput = renderData.getBuffer("cirData"))
        {
            var["sampleCIRData"] = pCIROutput;
        }
        else if (mpSampleCIRData)
        {
            var["sampleCIRData"] = mpSampleCIRData;
        }
        else 
        {
            logWarning("PathTracer: CIR data buffer not available");
        }
    }
    catch (const std::exception& e)
    {
        logError("PathTracer: Exception binding CIR data: {}", e.what());
    }
}
```

#### 3. 错误处理和调试
- **缓冲区创建失败**: 记录错误日志并优雅降级
- **内存不足**: 捕获异常并输出具体错误信息
- **绑定失败**: 警告用户但不影响正常渲染
- **大小验证**: 确保缓冲区大小匹配采样数量

#### 4. 验证方法
- **缓冲区创建成功**: 检查mpSampleCIRData是否非空
- **Shader绑定成功**: 确保sampleCIRData在着色器中可访问
- **渲染图连接**: 验证cirData输出通道在渲染图中可连接
- **内存大小正确**: 验证缓冲区元素数量与采样数匹配

---

### 子任务3: CIR数据收集逻辑集成

#### 1. 任务目标
在PathTracer的光线追踪过程中集成CIR数据收集逻辑，确保在正确的时机更新CIR参数。

#### 2. 实现方案

**修改文件**: `Source/RenderPasses/PathTracer/PathTracer.slang`

```hlsl
// 在PathTracer结构体中添加CIR输出声明
struct PathTracer
{
    // ... 现有字段保持不变 ...
    
    // === 新增：CIR数据输出 ===
    RWStructuredBuffer<CIRPathData> sampleCIRData;   ///< CIR路径数据输出缓冲区
};

/** CIR数据更新函数（精简版） */
void updateCIRDataDuringTracing(inout PathState path, float3 surfaceNormal, float reflectance)
{
    // 仅更新需要特殊计算的CIR参数，其他参数通过getCIRData()动态获取
    
    try 
    {
        // 更新发射角：仅在第一次表面交互时计算
        if (path.getVertexIndex() == 1) // 在发射表面
        {
            path.updateCIREmissionAngle(surfaceNormal);
        }
        
        // 累积反射率：每次有效反射时更新
        if (reflectance > 0.0f && path.getVertexIndex() > 1)
        {
            path.updateCIRReflectance(reflectance);
        }
    }
    catch 
    {
        // 计算失败时继续执行，不影响渲染
    }
}

/** 路径终止时输出CIR数据 */
void outputCIRDataOnPathCompletion(PathState path, uint sampleIndex)
{
    try 
    {
        // 在路径即将终止时更新接收角
        if (path.isHit() && length(path.normal) > 0.1f)
        {
            path.updateCIRReceptionAngle(path.dir, path.normal);
        }
        
        // 动态生成完整CIR数据
        CIRPathData cirData = path.getCIRData();
        
        // 数据合理性检查
        if (cirData.pathLength < 0.0f || cirData.pathLength > 10000.0f)
        {
            // 路径长度异常，使用默认值
            cirData.pathLength = 1.0f;
        }
        
        if (cirData.reflectanceProduct < 0.0f || cirData.reflectanceProduct > 1.0f)
        {
            // 反射率异常，使用默认值
            cirData.reflectanceProduct = 0.5f;
        }
        
        // 计算输出索引
        uint2 pixel = path.getPixel();
        uint pixelIndex = pixel.y * params.frameDim.x + pixel.x;
        uint outputIndex = pixelIndex * params.samplesPerPixel + sampleIndex;
        
        // 边界检查
        uint bufferSize = sampleCIRData.GetDimensions();
        if (outputIndex < bufferSize)
        {
            sampleCIRData[outputIndex] = cirData;
        }
    }
    catch 
    {
        // 输出失败时静默处理，不影响渲染
    }
}
```

**修改光线追踪着色器**: `Source/RenderPasses/PathTracer/TracePass.rt.slang`

```hlsl
// 在路径生成函数中初始化CIR数据
void generatePath(uint2 pixel, uint sampleIdx, inout PathState path)
{
    // ... 现有路径生成逻辑保持不变 ...
    
    // === 新增：初始化CIR数据 ===
    path.initCIRData();
}

// 在材质交互处理中更新CIR数据
[shader("closesthit")]
void closestHit(inout PathPayload payload, BuiltInTriangleIntersectionAttributes attribs)
{
    // ... 现有命中处理逻辑 ...
    
    // 解包路径状态
    PathState path = payload.unpack();
    
    // 加载着色数据
    ShadingData sd = loadShadingData(path.hit, path.origin, path.dir);
    
    // === 新增：更新CIR数据 ===
    if (gCollectCIRData) // 条件编译标志
    {
        // 简化的反射率计算
        float reflectance = luminance(sd.diffuse) * 0.8f + luminance(sd.specular) * 0.2f;
        updateCIRDataDuringTracing(path, sd.faceN, reflectance);
    }
    
    // ... 现有材质和光照处理 ...
    
    // 打包路径状态
    payload.pack(path);
}

// 在路径终止时输出CIR数据
[shader("miss")]
void miss(inout PathPayload payload)
{
    // ... 现有miss处理逻辑 ...
    
    // 解包路径状态
    PathState path = payload.unpack();
    
    // === 新增：输出CIR数据 ===
    if (gCollectCIRData && path.getVertexIndex() > 0)
    {
        outputCIRDataOnPathCompletion(path, payload.getSampleIndex());
    }
    
    // 终止路径
    path.terminate();
    payload.pack(path);
}
```

#### 3. 错误处理和调试
- **数值异常**: 使用try-catch捕获计算异常，使用默认值
- **缓冲区越界**: 检查输出索引是否在有效范围内
- **数据合理性**: 验证CIR参数是否在物理合理范围内
- **条件编译**: 使用标志控制CIR功能的启用/禁用

#### 4. 验证方法
- **数据收集验证**: 检查CIR缓冲区是否包含有效数据
- **角度合理性**: 发射角和接收角应在[0,π]范围内
- **路径长度验证**: 路径长度应为正值且合理
- **反射率验证**: 反射率乘积应在[0,1]范围内

---

### 子任务4: 基础验证和测试

#### 1. 任务目标
实现基础的CIR数据验证和测试功能，确保收集的数据正确且系统稳定运行。

#### 2. 实现方案

**新建测试文件**: `Source/RenderPasses/PathTracer/CIRValidator.slang`

```hlsl
/** CIR数据验证和统计 */
struct CIRValidator
{
    /** 验证单个CIR数据的合理性 */
    static bool validateCIRData(const CIRPathData data, out string errorMsg)
    {
        // 路径长度检查
        if (data.pathLength < 0.01f || data.pathLength > 1000.0f)
        {
            errorMsg = "Path length out of range: " + string(data.pathLength);
            return false;
        }
        
        // 角度检查
        if (data.emissionAngle < 0.0f || data.emissionAngle > 3.14159f)
        {
            errorMsg = "Emission angle out of range: " + string(data.emissionAngle);
            return false;
        }
        
        if (data.receptionAngle < 0.0f || data.receptionAngle > 3.14159f)
        {
            errorMsg = "Reception angle out of range: " + string(data.receptionAngle);
            return false;
        }
        
        // 反射率检查
        if (data.reflectanceProduct < 0.0f || data.reflectanceProduct > 1.0f)
        {
            errorMsg = "Reflectance product out of range: " + string(data.reflectanceProduct);
            return false;
        }
        
        // 发射功率检查
        if (data.emittedPower <= 0.0f || data.emittedPower > 10000.0f)
        {
            errorMsg = "Emitted power out of range: " + string(data.emittedPower);
            return false;
        }
        
        errorMsg = "";
        return true;
    }
    
    /** 计算CIR数据统计信息 */
    static CIRStatistics computeStatistics(StructuredBuffer<CIRPathData> cirBuffer, uint count)
    {
        CIRStatistics stats;
        stats.validCount = 0;
        stats.invalidCount = 0;
        stats.avgPathLength = 0.0f;
        stats.avgReflectanceProduct = 0.0f;
        
        for (uint i = 0; i < count; i++)
        {
            CIRPathData data = cirBuffer[i];
            string errorMsg;
            
            if (validateCIRData(data, errorMsg))
            {
                stats.validCount++;
                stats.avgPathLength += data.pathLength;
                stats.avgReflectanceProduct += data.reflectanceProduct;
            }
            else 
            {
                stats.invalidCount++;
            }
        }
        
        if (stats.validCount > 0)
        {
            stats.avgPathLength /= float(stats.validCount);
            stats.avgReflectanceProduct /= float(stats.validCount);
        }
        
        return stats;
    }
};

/** CIR统计信息结构 */
struct CIRStatistics
{
    uint validCount;
    uint invalidCount;
    float avgPathLength;
    float avgReflectanceProduct;
};
```

**在PathTracer.cpp中添加验证逻辑**:

```cpp
// 在PathTracer.cpp中添加验证方法
void PathTracer::validateCIRData()
{
    if (!mpSampleCIRData)
    {
        logWarning("PathTracer: CIR data buffer not available for validation");
        return;
    }
    
    try 
    {
        // 获取CIR数据
        const CIRPathData* pData = static_cast<const CIRPathData*>(mpSampleCIRData->map(Buffer::MapType::Read));
        uint32_t elementCount = mpSampleCIRData->getElementCount();
        
        uint32_t validCount = 0;
        uint32_t invalidCount = 0;
        float avgPathLength = 0.0f;
        float avgReflectance = 0.0f;
        
        for (uint32_t i = 0; i < elementCount && i < 1000; i++) // 限制检查数量
        {
            const CIRPathData& data = pData[i];
            
            // 基础合理性检查
            bool isValid = true;
            
            if (data.pathLength < 0.01f || data.pathLength > 1000.0f)
                isValid = false;
            if (data.emissionAngle < 0.0f || data.emissionAngle > 3.14159f)
                isValid = false;
            if (data.receptionAngle < 0.0f || data.receptionAngle > 3.14159f)
                isValid = false;
            if (data.reflectanceProduct < 0.0f || data.reflectanceProduct > 1.0f)
                isValid = false;
            if (data.emittedPower <= 0.0f)
                isValid = false;
            
            if (isValid)
            {
                validCount++;
                avgPathLength += data.pathLength;
                avgReflectance += data.reflectanceProduct;
            }
            else 
            {
                invalidCount++;
            }
        }
        
        if (validCount > 0)
        {
            avgPathLength /= validCount;
            avgReflectance /= validCount;
        }
        
        mpSampleCIRData->unmap();
        
        // 输出验证结果
        logInfo("CIR Validation: Valid={}, Invalid={}, AvgLength={:.3f}m, AvgReflectance={:.3f}", 
                validCount, invalidCount, avgPathLength, avgReflectance);
        
        if (invalidCount > validCount * 0.1f) // 超过10%无效数据时警告
        {
            logWarning("CIR: High invalid data rate: {:.1f}%", 
                      100.0f * invalidCount / (validCount + invalidCount));
        }
    }
    catch (const std::exception& e)
    {
        logError("CIR validation failed: {}", e.what());
    }
}

// 在endFrame中调用验证（可选，用于调试）
void PathTracer::endFrame(RenderContext* pRenderContext, const RenderData& renderData)
{
    // ... 现有逻辑 ...
    
    #ifdef _DEBUG
    // 定期验证CIR数据（仅在调试模式下）
    static uint32_t frameCount = 0;
    if (++frameCount % 60 == 0) // 每60帧验证一次
    {
        validateCIRData();
    }
    #endif
}
```

#### 3. 错误处理和调试
- **数据读取失败**: 捕获异常并记录错误信息
- **高无效率警告**: 当无效数据超过10%时发出警告
- **内存映射失败**: 优雅处理缓冲区访问失败
- **调试信息**: 仅在调试模式下输出详细验证信息

#### 4. 验证方法
- **有效数据比例**: 有效CIR数据应占总数的90%以上
- **数据范围检查**: 所有CIR参数应在物理合理范围内
- **平均值合理性**: 平均路径长度和反射率应符合场景预期
- **无错误日志**: 验证过程不应产生错误或异常

## 实施顺序和依赖关系

1. **子任务1** → **子任务2** → **子任务3** → **子任务4**
2. 每个子任务完成后进行独立验证
3. 全部完成后进行集成测试
4. 性能影响评估和优化

## 成功标准

- ✅ PathTracer能够正常渲染，无性能显著下降
- ✅ CIR数据能够成功收集和输出
- ✅ 90%以上的CIR数据通过合理性验证
- ✅ 系统稳定运行，无崩溃或错误

## 回滚策略

如果任何子任务出现问题，可以：
1. 使用条件编译宏禁用CIR功能
2. 恢复到上一个子任务的状态
3. 保持PathTracer原有功能不受影响 