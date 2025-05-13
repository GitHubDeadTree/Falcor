# Falcor PathTracer 辐照度计算实现方案

本文档概述了在 Falcor PathTracer 中实现辐照度计算功能的计划。实现将包含三个主要组件：

1. 修改 `PathState` 结构，保存光线的初始方向
2. 为 PathTracer 添加新的输出通道，输出光线数据
3. 创建新的渲染通道，计算辐照度并输出结果

## 1. PathState 修改

### PathState 需要做哪些修改？

`PathState` 结构（定义在 `PathState.slang` 中）需要扩展一个新字段来存储初始光线方向。当前的 `PathState` 结构在 `dir` 字段中跟踪当前光线方向，但这个值会在每次反弹后被覆盖。我们需要添加：

```cpp
struct PathState
{
    // 现有字段
    float3      origin;     ///< 散射光线的起点
    float3      dir;        ///< 散射光线的单位方向向量
    float       pdf;        ///< 生成散射光线的概率密度函数值
    float3      normal;     ///< 散射光线起点处的着色法线
    HitInfo     hit;        ///< 散射光线的命中信息，在命中三角形时填写

    float3      thp;        ///< 路径通量
    float3      L;          ///< 累积的路径贡献（辐射亮度）

    // 新增字段
    float3      initialDir; ///< 相机发出的初始光线方向

    // 其他现有字段和方法
    // ...
};
```

这个新的 `initialDir` 字段将存储相机发出的原始光线方向，并在整个路径追踪过程中保持不变，即使 `dir` 字段在每次反弹时被更新。

## 2. 获取初始光线方向

初始光线方向可以在路径首次生成时捕获。在 `GeneratePaths.cs.slang` 中，当计算相机光线时：

```cpp
void generatePath(const uint pathID, out PathState path)
{
    // ... 现有代码 ...

    // 计算主光线
    Ray cameraRay = gScene.camera.computeRayPinhole(pixel, params.frameDim);
    if (kUseViewDir) cameraRay.dir = -viewDir[pixel];

    // 初始化路径状态
    path.origin = cameraRay.origin;
    path.dir = cameraRay.dir;

    // 存储初始方向 - 新增
    path.initialDir = cameraRay.dir;

    // ... 现有代码 ...
}
```

这确保了对于每个路径，我们同时存储当前光线方向（每次反弹后会改变）和初始光线方向（保持不变）。

## 3. PathTracer 中的新输出通道

### 输出格式

PathTracer 需要一个新的输出通道来导出最终辐射亮度（L）和初始光线方向。这将在 `PathTracer.cpp` 中与现有输出通道一起定义：

```cpp
const std::string kOutputRayData = "rayData";

const Falcor::ChannelList kOutputChannels =
{
    // 现有输出通道...
    { kOutputRayData, "", "光线数据（辐射亮度和初始方向）", true /* 可选 */, ResourceFormat::RGBA32Float },
    // 其他现有通道...
};
```

对于光线数据输出，我们建议使用：
- `ResourceFormat::RGBA32Float` 用于辐射亮度分量（xyz）和一个额外值（w）
- `ResourceFormat::RGBA16Float` 用于方向分量（xyz）和一个额外值（w）

这遵循 Falcor 对类似数据类型的现有模式。

### 如何确保光线信息的持续输出

为确保 PathTracer 持续输出所有路径的光线信息：

1. **持久性缓冲区创建**：光线数据的输出纹理将在渲染通道初始化时与其他输出通道一起创建。

2. **每帧更新**：在 PathTracer 的 `execute()` 方法中，光线数据将在每帧写入输出通道。

3. **着色器实现**：修改 PathTracer 着色器中的 `writeOutput()` 函数，将初始光线方向和辐射亮度写入新的输出通道：

```cpp
void writeOutput(const PathState path)
{
    // ... 现有输出写入 ...

    // 将光线数据写入新输出通道
    if (kOutputRayData)
    {
        uint2 pixel = DispatchRaysIndex().xy;
        gOutputRayRadiance[pixel] = float4(path.L, 0.0);
        gOutputRayDirection[pixel] = float4(path.initialDir, 0.0);
    }

    // ... 其他现有输出写入 ...
}
```

4. **缓冲区管理**：由于 PathTracer 已经处理了 GPU 上多条光线的并行处理，新的输出通道将自然包含所有处理光线的数据，每个像素对应一条不同的光线。

## 4. 确保持续输出

Falcor 中的 PathTracer 在 GPU 上以大规模并行方式运行，每个像素/光线由不同的 GPU 线程处理。为确保光线信息的持续输出：

1. **GPU 端存储**：光线数据（辐射亮度和初始方向）将写入 GPU 纹理，可同时存储所有光线的数据。

2. **渲染图集成**：通过将新输出通道正确集成到 Falcor 的渲染图中，数据将可用于后续渲染通道。

3. **帧间持久性**：如果需要跨帧累积，我们可以使用 Falcor 的内置累积机制来组合多帧数据。

4. **资源管理**：Falcor 的资源管理系统将处理这些输出通道所需的 GPU 内存的分配和管理。

## 5. 辐照度计算中的光线方向

对于接收器上的辐照度计算，**我们确实需要考虑光线的方向**。辐照度公式需要入射光线与接收器法线之间的夹角余弦：

$$E = \int_{\Omega} L_i(\omega) \cos\theta \, d\omega$$

在代码实现中：

1. **方向很重要**：存储在 `initialDir` 中的方向向量表示从相机到场景的光线方向。

2. **无需反转**：对于辐照度计算，我们按原样使用这个方向。不需要反转它，因为：
   - 辐照度积分中的余弦项已经考虑了入射光方向与接收器法线之间的角度。
   - 辐射亮度值 (L) 与这个特定方向相关联。

3. **余弦加权**：我们将入射辐射亮度乘以光线方向与接收器法线之间夹角的余弦：
   ```cpp
   float cosTheta = max(0.0f, dot(rayDirection, receiverNormal));
   float3 irradianceContribution = rayRadiance * cosTheta;
   ```

## 6. 辐照度计算的新渲染通道

### 计算方法

新的渲染通道将使用文档中的蒙特卡洛估计公式实现辐照度计算：

$$E \approx \frac{2\pi}{n} \sum_{i=1}^{n} L_i \cdot \cos\theta_i$$

实现将：

1. 以 PathTracer 的光线数据（辐射亮度和初始方向）作为输入。
2. 定义一个具有位置和法线的接收器表面。
3. 对于每个像素/光线：
   - 提取光线的辐射亮度 (L) 和初始方向。
   - 检查光线是否与接收器相交。
   - 如果相交，计算余弦项并将加权贡献添加到总辐照度。
4. 应用适当的归一化，考虑样本数量和立体角。

着色器中的核心计算将如下所示：

```cpp
float3 computeIrradiance(Texture2D rayRadiance, Texture2D rayDirection, float3 receiverPos, float3 receiverNormal)
{
    float3 totalIrradiance = float3(0.0f);
    uint sampleCount = 0;

    // 处理所有光线/像素
    for (uint y = 0; y < gFrameDim.y; y++)
    {
        for (uint x = 0; x < gFrameDim.x; x++)
        {
            uint2 pixel = uint2(x, y);
            float3 L = rayRadiance[pixel].xyz;
            float3 dir = rayDirection[pixel].xyz;

            // 检查光线是否与接收器相交
            // 这是一个简化测试；实际实现会更复杂
            if (isRayHittingReceiver(dir, receiverPos))
            {
                // 计算余弦项
                float cosTheta = max(0.0f, dot(dir, receiverNormal));

                // 添加加权贡献
                totalIrradiance += L * cosTheta;
                sampleCount++;
            }
        }
    }

    // 应用蒙特卡洛归一化
    // 因子 2π 来自半球积分归一化
    if (sampleCount > 0)
    {
        totalIrradiance *= (2.0f * PI) / float(sampleCount);
    }

    return totalIrradiance;
}
```

### 与 Falcor 集成

新的渲染通道将：

1. 声明 PathTracer 光线数据的输入。
2. 定义接收器位置和法线的参数。
3. 设置计算辐照度的输出。
4. 实现 Execute 方法来执行计算。

## 7. 辐照度输出

### 输出格式

辐照度计算渲染通道将输出：

1. **总辐照度**：表示接收器上总辐照度的单个 float3 值。
2. **辐照度图**：可选地，显示接收器表面上辐照度空间分布的纹理。

### 存储位置

辐照度输出将存储在：

1. **GPU 纹理**：作为标准 Falcor 输出纹理，可以显示或被其他渲染通道使用。
2. **CPU 可访问缓冲区**：用于分析或导出到文件，辐照度可以复制到 CPU 可访问的缓冲区。
3. **文件输出**：可选地，辐照度值可以导出到 CSV 或图像文件进行离线分析。

具体的输出位置可以通过渲染通道的 UI 配置，提供数据使用方式的灵活性。

## 附加考虑事项

### 性能优化

1. **接收器剔除**：实现提前剔除不会击中接收器的光线，避免不必要的计算。
2. **空间加速**：使用空间数据结构快速识别可能击中接收器的光线。
3. **多帧累积**：允许跨多帧累积辐照度以减少噪声。

### 验证和可视化

1. **调试可视化**：添加可视化模式以调试辐照度计算。
2. **基准比较**：将计算的辐照度与简单场景的解析解进行比较。
3. **交互式参数**：允许实时调整接收器参数以探索不同配置。

### 与 VLC 系统集成

对于可见光通信 (VLC) 应用：

1. **时间分析**：跟踪辐照度随时间的变化，用于时间信号分析。
2. **光谱响应**：考虑接收器的光谱响应，实现更准确的 VLC 建模。
3. **信号处理**：添加信号处理功能，从辐照度模式中提取通信数据。

## 实现阶段

1. **第 1 阶段**：修改 PathState 以存储初始光线方向。
2. **第 2 阶段**：为 PathTracer 添加光线数据的新输出通道。
3. **第 3 阶段**：创建辐照度计算渲染通道。
4. **第 4 阶段**：添加可视化和分析工具。
5. **第 5 阶段**：优化大场景性能。

通过遵循此实现计划，您将能够基于路径追踪场景准确计算接收器表面上的辐照度，这对 VLC 系统仿真、照明设计和其他需要精确光传输建模的应用非常有价值。
