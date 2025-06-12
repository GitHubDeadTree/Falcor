你的分析**非常正确**！这是一个很好的洞察。让我详细分析你的想法：

## 🎯 **你的理解完全正确**

### **PixelStats vs CIR数据需求对比**

| 方面               | PixelStats               | CIR数据需求              | 解决方案           |
| ------------------ | ------------------------ | ------------------------ | ------------------ |
| **数据收集** | ✅ 每像素记录到GPU缓冲区 | ✅ 每路径记录到GPU缓冲区 | **相同模式** |
| **数据传输** | ✅ GPU→CPU成功传输      | ✅ 需要GPU→CPU传输      | **相同模式** |
| **数据处理** | ❌ 归约求和/平均         | ✅ 保留原始路径数据      | **去掉归约** |
| **最终输出** | 统计信息                 | 结构化缓冲区             | **直接读取** |

## 📋 **实现方案：创建CIRPathDataCollector**

基于PixelStats的成功模式，我们可以创建一个专门的CIR数据收集器：
Read file: Source/Falcor/Rendering/Utils/PixelStats.h
现在让我创建一个专门的CIR数据收集器的设计方案：
让我创建一个详细的CIR数据收集器设计方案：

## 🏗️ **CIRPathDataCollector设计方案**

### **核心思路：PixelStats模式 - 归约处理**

```cpp
class FALCOR_API CIRPathDataCollector 
{
public:
    // 直接输出原始路径数据，不做统计处理
    struct CIRPathData 
    {
        float pathLength;           // d_i
        float emissionAngle;        // φ_i  
        float receptionAngle;       // θ_i
        float reflectanceProduct;   // Π r_k
        uint reflectionCount;       // K_i
        float emittedPower;         // P_t
        uint pixelX, pixelY;        // 像素坐标
        uint pathIndex;             // 路径索引
    };

private:
    // 【关键差异】：使用StructuredBuffer而不是Texture2D
    ref<Buffer> mpCIRPathDataBuffer;    ///< 原始路径数据缓冲区
    ref<Buffer> mpCIRReadbackBuffer;    ///< CPU可读的回读缓冲区
  
    // 【省略部分】：不需要ParallelReduction！
    // std::unique_ptr<ParallelReduction> mpParallelReduction; // ❌不需要
  
    // 【保留部分】：GPS同步机制
    ref<Fence> mpFence;                 ///< GPU fence for synchronization
};
```

### **关键实现差异对比**

| 步骤               | PixelStats                   | CIRPathDataCollector                       |
| ------------------ | ---------------------------- | ------------------------------------------ |
| **数据收集** | `RWTexture2D<uint>` 每像素 | `RWStructuredBuffer<CIRPathData>` 每路径 |
| **数据处理** | ❌ ParallelReduction求和     | ✅ 跳过处理，保留原始数据                  |
| **数据传输** | 归约结果→CPU                | 直接复制整个缓冲区→CPU                    |
| **最终输出** | 统计数据                     | 完整的路径数据数组                         |

## 🔧 **具体实现步骤**

### **1. GPU端收集（修改PathTracer.slang）**

```slang
// 替换PixelStats调用
void outputCIRDataOnPathCompletion(PathState path)
{
    // 生成完整CIR数据
    CIRPathData cirData = path.getCIRData();
  
    // 计算全局路径索引
    const uint2 pixel = path.getPixel();
    const uint pathIdx = params.getSampleOffset(pixel, sampleOffset) + path.getSampleIdx();
  
    // 【关键】：直接写入结构化缓冲区，不经过统计处理
    if (pathIdx < gCIRPathDataBuffer.GetDimensions())
    {
        gCIRPathDataBuffer[pathIdx] = cirData;
    }
}
```

### **2. CPU端读取（类似PixelStats的copyStatsToCPU）**

```cpp
void CIRPathDataCollector::copyDataToCPU()
{
    // 等待GPU完成
    mpFence->wait();
  
    // 【关键差异】：复制整个缓冲区，不做归约
    pRenderContext->copyBufferRegion(mpCIRReadbackBuffer.get(), 0, 
                                     mpCIRPathDataBuffer.get(), 0, 
                                     mTotalPathCount * sizeof(CIRPathData));
  
    // 直接映射并读取原始数据
    const CIRPathData* pathData = static_cast<const CIRPathData*>(mpCIRReadbackBuffer->map());
  
    // 输出到文件或进一步处理
    saveCIRPathData(pathData, mTotalPathCount);
  
    mpCIRReadbackBuffer->unmap();
}
```

### **3. 数据输出格式**

```
// 输出示例：每条路径一行
Path_0: d=2.35m, φ=0.52rad, θ=0.73rad, r=0.82, K=2, P=100W
Path_1: d=3.14m, φ=0.34rad, θ=0.91rad, r=0.75, K=3, P=100W  
Path_2: d=1.87m, φ=0.61rad, θ=0.45rad, r=0.91, K=1, P=100W
...
```

## ✅ **你的分析完全正确的原因**

1. **✅ 数据收集成功**：PixelStats有数据证明PathState→GPU传输成功
2. **✅ 传输机制可靠**：GPU→CPU的fence同步机制已验证可行
3. **✅ 缓冲区管理完善**：Falcor的Buffer管理系统成熟稳定
4. **✅ 唯一差异明确**：就是去掉ParallelReduction归约步骤

## 🎯 **实施建议**

1. **保留PixelStats**：继续用于实时监控和调试
2. **新增CIRPathDataCollector**：专门输出原始路径数据
3. **双重验证**：两个系统的数据应该是一致的（统计 vs 原始）

这个方案的**最大优势**：

- 🔄 **复用成功模式**：基于已验证的PixelStats架构
- 🚀 **实现简单**：主要是去掉归约，而非重新设计
- 🔒 **稳定可靠**：利用Falcor现有的缓冲区和同步机制
- 📊 **数据完整**：输出CIR计算所需的每条路径数据

你的想法非常准确，这确实是一个优雅且实用的解决方案！

很好的问题！让我分析CIR数据收集器在PathTracer中的执行架构。首先查看PathTracer的主要函数结构：
Ran tool
Read file: Source/RenderPasses/PathTracer/PathTracer.cpp
Read file: Source/RenderPasses/PathTracer/PathTracer.cpp
根据分析，我现在可以清楚地回答你的问题：

## 🏗️ **CIR数据收集器的执行架构**

### **CIR数据收集器将在PathTracer的多个函数中执行，每个函数负责不同的阶段：**

## 📋 **详细执行流程**

### **1. 初始化阶段 - `beginFrame()`**

```cpp
// 在PathTracer.cpp第1388行 - 仿照PixelStats模式
bool PathTracer::beginFrame(RenderContext* pRenderContext, const RenderData& renderData)
{
    // 现有代码...
    mpPixelStats->beginFrame(pRenderContext, mParams.frameDim);
  
    // 【新增】CIR数据收集器初始化
    if (mOutputCIRData) 
    {
        mpCIRDataCollector->beginFrame(pRenderContext, mParams.frameDim);
    }
  
    return true;
}
```

**负责内容**：

- 创建/调整CIR数据缓冲区大小
- 清空缓冲区
- 准备GPU内存

### **2. 数据绑定阶段 - `tracePass()` → `prepareProgram()`**

```cpp
// 在PathTracer.cpp第1488行 - 仿照PixelStats模式  
void PathTracer::tracePass(RenderContext* pRenderContext, const RenderData& renderData, TracePass& tracePass)
{
    // 现有代码...
    mpPixelStats->prepareProgram(tracePass.pProgram, var);
  
    // 【新增】CIR数据收集器绑定
    if (mOutputCIRData)
    {
        mpCIRDataCollector->prepareProgram(tracePass.pProgram, var);
    }
  
    // GPU光线追踪执行
    mpScene->raytrace(pRenderContext, tracePass.pProgram.get(), tracePass.pVars, uint3(mParams.frameDim, 1));
}
```

**负责内容**：

- 绑定CIR数据缓冲区到shader
- 设置编译器define
- 为GPU端数据收集做准备

### **3. 数据收集阶段 - GPU Shader中**

```slang
// 在PathTracer.slang的outputCIRDataOnPathCompletion()中
void outputCIRDataOnPathCompletion(PathState path)
{
    CIRPathData cirData = path.getCIRData();
  
    // 【修改】写入结构化缓冲区而不是PixelStats
    const uint2 pixel = path.getPixel();
    const uint pathIdx = params.getSampleOffset(pixel, sampleOffset) + path.getSampleIdx();
  
    if (pathIdx < gCIRPathBuffer.Length)
    {
        gCIRPathBuffer[pathIdx] = cirData;  // 直接写入完整结构体
    }
}
```

**负责内容**：

- 在每条路径完成时收集CIR数据
- 直接写入结构化缓冲区

### **4. 数据传输阶段 - `endFrame()`**

```cpp
// 在PathTracer.cpp第1401行 - 仿照PixelStats模式
void PathTracer::endFrame(RenderContext* pRenderContext, const RenderData& renderData)
{
    mpPixelStats->endFrame(pRenderContext);
  
    // 【新增】CIR数据收集器传输
    if (mOutputCIRData)
    {
        mpCIRDataCollector->endFrame(pRenderContext);
    }
  
    // 【新增】定期输出CIR数据
    static uint32_t debugFrameCounter = 0;
    if (mOutputCIRData && ++debugFrameCounter % 60 == 0)
    {
        mpCIRDataCollector->outputToFile("cir_data.txt");
    }
}
```

**负责内容**：

- GPU→CPU数据传输
- 数据同步等待
- 触发数据输出

### **5. 数据输出阶段 - 新增专门函数**

```cpp
// 新增函数，可从endFrame()或UI中调用
void PathTracer::exportCIRData(const std::string& filename)
{
    if (mpCIRDataCollector)
    {
        mpCIRDataCollector->exportToFile(filename);
    }
}
```

## 📊 **执行时机对比表**

| 阶段             | 函数                              | PixelStats         | CIR数据收集器      | 频率   |
| ---------------- | --------------------------------- | ------------------ | ------------------ | ------ |
| **初始化** | `beginFrame()`                  | ✅ 缓冲区创建/清空 | ✅ 缓冲区创建/清空 | 每帧   |
| **绑定**   | `tracePass()→prepareProgram()` | ✅ Shader绑定      | ✅ Shader绑定      | 每pass |
| **收集**   | GPU Shader                        | ✅ 统计累积        | ✅ 路径数据写入    | 每路径 |
| **传输**   | `endFrame()`                    | ✅ 归约+传输       | ✅ 直接传输        | 每帧   |
| **输出**   | UI/专门函数                       | ✅ 实时显示        | ✅ 文件输出        | 按需   |

## 🎯 **关键差异总结**

### **与PixelStats的相同点**：

- ✅ **相同的生命周期管理**：beginFrame → prepareProgram → GPU执行 → endFrame
- ✅ **相同的同步机制**：使用GPU fence确保数据传输完成
- ✅ **相同的集成方式**：作为PathTracer的内部工具类

### **与PixelStats的不同点**：

- 🔄 **数据收集**：结构化缓冲区 vs 多个纹理
- 🔄 **数据处理**：直接传输 vs 并行归约
- 🔄 **数据输出**：文件导出 vs 实时UI显示

**答案：CIR数据收集器将在PathTracer的4个主要函数中执行，每个函数负责数据收集管道的不同阶段。这种多函数分工的架构确保了数据收集的完整性和系统的稳定性。**

非常重要的要求！你说得对，我们需要**同时保留两个系统**。让我设计一个完整的双重架构：

## 🏗️ **双重CIR系统架构**

### **系统1：PixelStats的CIR统计（保留）**

- **用途**：实时监控、调试分析
- **输出**：统计信息（平均值、总数）
- **显示**：PathTracer UI中实时显示

### **系统2：CIRDataCollector（新增）**

- **用途**：科学计算、完整CIR分析
- **输出**：每条路径的原始数据
- **导出**：文件输出，供后续CIR计算

## 📊 **双重数据流设计**

### **GPU端：同时写入两个系统**

```slang
// 修改PathTracer.slang中的outputCIRDataOnPathCompletion
void outputCIRDataOnPathCompletion(PathState path)
{
    // 更新reception angle
    if (path.isHit() && length(path.normal) > 0.1f)
    {
        path.updateCIRReceptionAngle(path.dir, path.normal);
    }
  
    // 生成完整CIR数据
    CIRPathData cirData = path.getCIRData();
  
    // === 系统1：PixelStats统计（保留现有逻辑） ===
    if (cirData.pathLength > 0.0f && cirData.pathLength < 10000.0f)
    {
        logCIRPathLength(cirData.pathLength);
    }
  
    if (cirData.emissionAngle >= 0.0f && cirData.emissionAngle <= 3.14159f)
    {
        logCIREmissionAngle(cirData.emissionAngle);
    }
  
    if (cirData.receptionAngle >= 0.0f && cirData.receptionAngle <= 3.14159f)
    {
        logCIRReceptionAngle(cirData.receptionAngle);
    }
  
    if (cirData.reflectanceProduct >= 0.0f && cirData.reflectanceProduct <= 1.0f)
    {
        logCIRReflectanceProduct(cirData.reflectanceProduct);
    }
  
    if (cirData.emittedPower >= 0.0f)
    {
        logCIREmittedPower(cirData.emittedPower);
    }
  
    logCIRReflectionCount(cirData.reflectionCount);
  
    // === 系统2：CIRDataCollector原始数据（新增） ===
    const uint2 pixel = path.getPixel();
    const uint pathIdx = params.getSampleOffset(pixel, sampleOffset) + path.getSampleIdx();
  
    if (pathIdx < gCIRPathBuffer.Length)
    {
        // 写入完整的路径数据到结构化缓冲区
        gCIRPathBuffer[pathIdx] = cirData;
    }
}
```

## 🔧 **CIRDataCollector类设计**

```cpp
// CIRDataCollector.h
class FALCOR_API CIRDataCollector
{
public:
    CIRDataCollector(ref<Device> pDevice);

    void setEnabled(bool enabled) { mEnabled = enabled; }
    bool isEnabled() const { return mEnabled; }

    void beginFrame(RenderContext* pRenderContext, const uint2& frameDim);
    void endFrame(RenderContext* pRenderContext);
    void prepareProgram(const ref<Program>& pProgram, const ShaderVar& var);

    void renderUI(Gui::Widgets& widget);
    void exportToFile(const std::string& filename);
  
    // 获取原始数据用于验证
    bool getCIRData(std::vector<CIRPathData>& outData);

private:
    ref<Device> mpDevice;
  
    // 配置
    bool mEnabled = false;
    bool mAutoExport = false;
    std::string mExportFilename = "cir_data.txt";
  
    // 运行时状态
    bool mRunning = false;
    bool mWaitingForData = false;
    uint2 mFrameDim = { 0, 0 };
    uint32_t mMaxPathCount = 0;
  
    // GPU缓冲区
    ref<Buffer> mpCIRPathBuffer;        ///< GPU端CIR路径数据缓冲区
    ref<Buffer> mpCIRReadbackBuffer;    ///< CPU端读回缓冲区
    ref<Fence> mpFence;                 ///< GPU同步fence
  
    // 统计信息
    uint32_t mTotalPathsCollected = 0;
    uint32_t mValidPathsCollected = 0;
    bool mDataValid = false;
};
```

## 📋 **PathTracer中的集成**

### **PathTracer.h修改**

```cpp
class PathTracer : public RenderPass
{
    // 现有成员...
    std::unique_ptr<PixelStats> mpPixelStats;        ///< 保留：用于统计监控
    std::unique_ptr<CIRDataCollector> mpCIRCollector; ///< 新增：用于原始数据

public:
    // 现有接口...
    PixelStats& getPixelStats() { return *mpPixelStats; }
  
    // 新增接口
    CIRDataCollector& getCIRDataCollector() { return *mpCIRCollector; }
    void exportCIRData(const std::string& filename);
};
```

### **PathTracer.cpp修改**

```cpp
// 构造函数
PathTracer::PathTracer(ref<Device> pDevice, const Properties& props)
{
    // 现有代码...
    mpPixelStats = std::make_unique<PixelStats>(mpDevice);
  
    // 新增：创建CIR数据收集器
    mpCIRCollector = std::make_unique<CIRDataCollector>(mpDevice);
}

// beginFrame函数
bool PathTracer::beginFrame(RenderContext* pRenderContext, const RenderData& renderData)
{
    // 现有代码...
    mpPixelStats->beginFrame(pRenderContext, mParams.frameDim);
  
    // 新增：CIR数据收集器初始化
    if (mOutputCIRData)
    {
        mpCIRCollector->setEnabled(true);
        mpCIRCollector->beginFrame(pRenderContext, mParams.frameDim);
    }
  
    return true;
}

// tracePass函数 
void PathTracer::tracePass(RenderContext* pRenderContext, const RenderData& renderData, TracePass& tracePass)
{
    // 现有代码...
    mpPixelStats->prepareProgram(tracePass.pProgram, var);
  
    // 新增：CIR数据收集器绑定
    if (mOutputCIRData)
    {
        mpCIRCollector->prepareProgram(tracePass.pProgram, var);
    }
  
    // GPU执行...
    mpScene->raytrace(pRenderContext, tracePass.pProgram.get(), tracePass.pVars, uint3(mParams.frameDim, 1));
}

// endFrame函数
void PathTracer::endFrame(RenderContext* pRenderContext, const RenderData& renderData)
{
    mpPixelStats->endFrame(pRenderContext);
  
    // 新增：CIR数据收集器传输
    if (mOutputCIRData)
    {
        mpCIRCollector->endFrame(pRenderContext);
    }
  
    // 定期自动导出
    static uint32_t frameCounter = 0;
    if (mOutputCIRData && ++frameCounter % 300 == 0) // 每5秒导出一次
    {
        mpCIRCollector->exportToFile("cir_frame_" + std::to_string(mParams.frameCount) + ".txt");
    }
}

// renderUI函数
void PathTracer::renderUI(Gui::Widgets& widget)
{
    // 现有UI...
  
    // PixelStats UI（保留）
    if (widget.collapsingHeader("Statistics"))
    {
        mpPixelStats->renderUI(widget);
    }
  
    // 新增：CIR数据收集器UI
    if (widget.collapsingHeader("CIR Data Export"))
    {
        mpCIRCollector->renderUI(widget);
    }
}
```

## 🎯 **UI界面设计**

### **统计监控区域（PixelStats - 保留）**

```
=== Statistics ===
✅ Ray stats: [✓]

--- Ray Statistics ---
Path length (avg): 2.85
...

--- CIR Statistics ---
Valid CIR samples: 12450
CIR Path length (avg): 3.24m
CIR Emission angle (avg): 0.52 rad
CIR Reception angle (avg): 0.73 rad
...
```

### **数据导出区域（CIRDataCollector - 新增）**

```
=== CIR Data Export ===
✅ Enable CIR data collection: [✓]
📁 Export filename: [cir_data.txt]
⏰ Auto export every: [300] frames
📊 Collected paths: 15680
✅ Valid paths: 12450 (79.4%)

[Export Now] [Clear Data] [Browse...]
```

## 💡 **数据验证机制**

```cpp
// 验证两个系统的数据一致性
void PathTracer::validateCIRData()
{
    // 从PixelStats获取统计信息
    PixelStats::Stats pixelStats;
    if (mpPixelStats->getStats(pixelStats))
    {
        // 从CIRDataCollector获取原始数据
        std::vector<CIRPathData> rawData;
        if (mpCIRCollector->getCIRData(rawData))
        {
            // 计算原始数据的统计值
            float avgPathLength = 0.0f;
            uint32_t validSamples = 0;
      
            for (const auto& data : rawData)
            {
                if (data.isValid())
                {
                    avgPathLength += data.pathLength;
                    validSamples++;
                }
            }
      
            if (validSamples > 0)
            {
                avgPathLength /= validSamples;
          
                // 比较两个系统的结果
                float diff = abs(avgPathLength - pixelStats.avgCIRPathLength);
                if (diff > 0.01f) // 容忍1cm误差
                {
                    logWarning(fmt::format("CIR data mismatch: PixelStats={:.3f}m, Collector={:.3f}m", 
                                         pixelStats.avgCIRPathLength, avgPathLength));
                }
            }
        }
    }
}
```

## 📊 **双重系统优势**

| 方面                 | PixelStats        | CIRDataCollector      | 组合优势      |
| -------------------- | ----------------- | --------------------- | ------------- |
| **实时监控**   | ✅ 实时统计显示   | ❌ 批量导出           | 实时+离线分析 |
| **数据完整性** | ❌ 只有统计信息   | ✅ 完整路径数据       | 完整+快速     |
| **性能影响**   | ✅ 低（归约优化） | ⚠️ 中（缓冲区传输） | 可选择性使用  |
| **数据验证**   | ❌ 无原始数据     | ✅ 可验证计算         | 相互验证      |
| **用户体验**   | ✅ 即时反馈       | ✅ 科学分析           | 最佳体验      |

这种双重架构确保了：

1. **开发调试**：PixelStats提供实时反馈
2. **科学研究**：CIRDataCollector提供完整数据
3. **数据验证**：两个系统可以相互验证
4. **性能灵活**：用户可以按需开启不同系统

**【可选】**

**您的想法非常明智！在PixelStats系统基础上扩展确实比并行实现一个独立收集器更优雅。让我先查看当前的PixelStats系统实现，然后设计一个使用结构化缓冲区的扩展方案。**
**Read file: Source/RenderPasses/PathTracer/PixelStats.h**
**Read file: Source/RenderPasses/PathTracer/PixelStats.cpp**
**Ran tool**
**Read file: Source/Falcor/Rendering/Utils/PixelStats.h**
**Read file: Source/Falcor/Rendering/Utils/PixelStats.cpp**
**Read file: Source/Falcor/Rendering/Utils/PixelStats.cpp**
**现在我来查看一下当前的结构化缓冲区是如何实现的。让我搜索现有的结构化缓冲区用法：**
**Ran tool**
**很好！我看到已经有CIR相关的结构化缓冲区实现了。让我查看一下CIRPathData的定义：**
**Ran tool**
**Read file: Source/RenderPasses/PathTracer/CIRPathData.slang**
**完美！现在我理解了您的想法。您想在PixelStats系统基础上扩展支持结构化缓冲区，这样可以收集完整的CIR路径数据，而不仅仅是统计值。让我设计一个扩展方案：**

## 分析现状

**从代码分析可以看到：**

1. **现有PixelStats系统**：使用 `RWTexture2D`收集统计数据，通过ParallelReduction进行GPU端求和
2. **现有CIR实现**：已经有 `CIRPathData`结构和 `RWStructuredBuffer<CIRPathData>`
3. **您的需求**：在PixelStats基础上扩展支持结构化缓冲区收集

## 建议的设计方案

**可以为PixelStats添加一个****混合模式**：既支持原有的统计聚合模式，也支持新的结构化缓冲区原始数据收集模式。

### 方案优势

1. **最小修改**：只需扩展现有系统，不重新设计
2. **向后兼容**：保持现有统计功能不变
3. **统一管理**：使用同一套生命周期管理（beginFrame/endFrame）
4. **灵活配置**：可选择统计模式还是原始数据模式

### 实现设计

```
// 在PixelStats.h中添加新的配置选项
enum class CollectionMode 
{
    Statistics,     // 原有的统计聚合模式
    RawData,        // 新的原始数据收集模式  
    Both            // 两种模式都启用
};

class PixelStats 
{
    // 新增配置
    CollectionMode mCollectionMode = CollectionMode::Statistics;
  
    // 新增结构化缓冲区支持
    ref<Buffer> mpCIRRawDataBuffer;          // RWStructuredBuffer<CIRPathData>
    ref<Buffer> mpCIRCounterBuffer;          // 计数器，跟踪写入的数据量
    uint32_t mMaxCIRPathsPerFrame = 1000000; // 每帧最大CIR路径数
  
    // 新增API
    void setCollectionMode(CollectionMode mode);
    void setMaxCIRPathsPerFrame(uint32_t maxPaths);
    const ref<Buffer> getCIRRawDataBuffer() const;
    uint32_t getCIRPathCount(); // 获取实际收集的路径数量
};
```

### SLANG端扩展

```
// 在PixelStats.slang中添加
#ifdef _PIXEL_STATS_RAW_DATA_ENABLED
RWStructuredBuffer<CIRPathData> gCIRRawDataBuffer;
RWByteAddressBuffer gCIRCounterBuffer;

void logCIRRawPath(CIRPathData pathData)
{
    if (!pathData.isValid()) return;
  
    // 原子递增计数器获取写入索引
    uint index = gCIRCounterBuffer.InterlockedAdd(0, 1);
  
    // 检查是否超出缓冲区大小
    if (index < gMaxCIRPaths)
    {
        gCIRRawDataBuffer[index] = pathData;
    }
}
#endif
```

**这种设计的好处是：**

1. **只修改最后环节**：在GPU数据收集完成后的CPU读取阶段增加新功能
2. **复用现有基础设施**：使用PixelStats的缓冲区管理、生命周期控制
3. **配置灵活性**：可以选择只要统计、只要原始数据，或者两者都要
4. **性能可控**：通过 `mMaxCIRPathsPerFrame`限制内存使用

**您觉得这个方向如何？如果您同意这个思路，我可以开始实现具体的代码修改。**
