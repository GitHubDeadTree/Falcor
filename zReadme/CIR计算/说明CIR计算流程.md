## CIR计算流程详解

我来详细解释我们当前CIR计算的完整流程，这确实是一个集成到原生PathTracer中的系统。

## 整体架构概述

我们的CIR计算**完全集成到了原生的PathTracer计算流程中**，而不是作为独立的后处理步骤。这种设计的优势是：

1. **零额外开销**: 利用现有的光线追踪计算，不需要重复计算
2. **实时数据收集**: 在光线追踪的同时收集CIR参数
3. **完整路径信息**: 能够获取到每条光线路径的完整信息

## 详细流程图

```
PathTracer渲染循环
├── beginFrame() - 初始化和配置检查
├── generatePaths() - 生成初始光线路径  
├── tracePass() - 执行光线追踪
│   ├── 主光线命中处理
│   ├── 后续反射/折射处理  
│   └── 路径终止处理
└── resolvePass() - 结果合成和输出

CIR数据流
├── 路径初始化: initCIRData()
├── 路径追踪中: updateCIRDataDuringTracing()  
└── 路径完成时: outputCIRDataOnPathCompletion()
```

## 具体计算流程

### 1. 系统初始化阶段

**beginFrame()函数的作用**:

```cpp
// 检查和强制启用CIR数据收集
bool prevOutputCIRData = mOutputCIRData;
mOutputCIRData = true;  // 强制启用
if (mOutputCIRData != prevOutputCIRData) mRecompile = true;
```

**作用**:

- 强制启用CIR数据收集，不依赖渲染图配置
- 检查是否需要重新编译shader（当CIR状态改变时）
- 初始化像素统计、调试系统等

### 2. 路径生成阶段

**generatePaths()函数的作用**:

```cpp
// 添加CIR条件编译宏
mpGeneratePaths->addDefine("OUTPUT_CIR_DATA", mOutputCIRData ? "1" : "0");
```

**作用**:

- 为每个像素生成初始光线路径
- 设置条件编译宏，告诉shader是否启用CIR功能
- 计算初始路径参数（起点、方向、强度等）

### 3. 光线追踪执行阶段

**tracePass()函数的作用**:

```cpp
// 添加CIR条件编译宏
tracePass.pProgram->addDefine("OUTPUT_CIR_DATA", mOutputCIRData ? "1" : "0");
```

这是**核心计算阶段**，包含多个子通道：

#### 3.1 路径初始化子通道

```hlsl
// 在ReorderingScheduler和Scheduler中
#if OUTPUT_CIR_DATA
path.initCIRData();  // 初始化CIR参数
#endif
```

#### 3.2 表面交互处理子通道

```hlsl
// 在handleHit函数中
#if OUTPUT_CIR_DATA
updateCIRDataDuringTracing(path, sd.faceN, bsdfProperties.diffuseReflectionAlbedo.r);
#endif
```

**具体处理**:

- 计算发射角（仅在第一次表面交互时）
- 累积反射率（每次有效反射时）
- 更新路径长度、反射次数等参数

#### 3.3 路径终止处理子通道

```hlsl
// 路径正常完成时
#if OUTPUT_CIR_DATA
gPathTracer.outputCIRDataOnPathCompletion(path);
#endif

// 路径miss时（在miss shader中）
#if OUTPUT_CIR_DATA
if (path.getVertexIndex() > 0)
{
    gPathTracer.outputCIRDataOnPathCompletion(path);
}
#endif
```

## 涉及的关键函数

### CIR专用函数

1. **initCIRData()** (PathState中)

   - 初始化CIR参数为默认值
   - 在路径开始时调用
2. **updateCIRDataDuringTracing()** (PathTracer中)

   - 在光线与表面交互时更新CIR参数
   - 计算发射角、累积反射率
3. **outputCIRDataOnPathCompletion()** (PathTracer中)

   - 路径完成时输出最终CIR数据
   - 计算接收角、数据验证、写入缓冲区
4. **getCIRData()** (PathState中)

   - 动态生成完整的CIR数据结构
   - 复用现有PathState字段

### 系统配置函数

**getDefines()函数的作用**:

```cpp
defines.add("OUTPUT_CIR_DATA", owner.mOutputCIRData ? "1" : "0");
```

**作用**:

- 为shader编译提供静态配置参数
- 控制CIR相关代码的编译包含

## 数据流向

```
GPU Shader ───────────► CPU Host
    ↓                      ↓
PathState.cirData ──► sampleCIRData Buffer ──► 后续分析
    ↓                      ↓
实时更新参数           批量数据传输        CIR计算Pass
```

## 条件编译宏定义的作用

我添加的条件编译宏定义体系有以下重要作用：

### 1. 功能开关控制

```hlsl
#if OUTPUT_CIR_DATA
// CIR相关代码只在启用时编译
#endif
```

### 2. 性能优化

- **编译时优化**: 未启用时完全移除CIR代码，零性能影响
- **运行时效率**: 避免运行时的条件判断开销

### 3. 代码维护性

- **统一控制**: 通过单一宏控制所有CIR功能
- **模块化**: CIR功能可以独立开启/关闭
- **兼容性**: 不影响现有的PathTracer功能

### 4. 编译器优化对策

- **防止死代码优化**: 确保CIR缓冲区在启用时不被优化掉
- **跨模块一致性**: 所有编译单元对CIR状态有一致理解

## 执行顺序总结

```
1. beginFrame() - 检查并强制启用CIR功能
2. generatePaths() - 生成路径，设置CIR编译宏
3. tracePass() - 执行核心追踪
   3.1 initCIRData() - 路径初始化
   3.2 updateCIRDataDuringTracing() - 表面交互时更新
   3.3 outputCIRDataOnPathCompletion() - 路径完成时输出
4. resolvePass() - 合成最终结果（CIR数据已在步骤3中输出）
```

## 与原生PathTracer的关系

CIR计算**完全嵌入**到原生PathTracer流程中：

- **不增加额外的渲染pass**: 利用现有的光线追踪计算
- **最小化性能影响**: 只在必要时进行额外的CIR参数计算
- **保持兼容性**: 通过条件编译确保不影响原有功能
- **无缝集成**: CIR数据收集与光线追踪同步进行

这种设计使得CIR计算成为PathTracer的一个**原生功能**，而不是外挂的后处理系统，从而实现了高效的实时CIR数据收集。
