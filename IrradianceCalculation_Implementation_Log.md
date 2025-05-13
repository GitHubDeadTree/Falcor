# 辐照度计算实现日志

本文档详细记录了在Falcor PathTracer中实现辐照度计算功能的所有代码修改。

## 第1阶段：修改PathState存储初始光线方向

实现时间：2024-08-14

### 1. 修改 PathState.slang 文件

在PathState结构体中添加initialDir字段，用于存储初始光线方向。

**文件路径**：`Source/RenderPasses/PathTracer/PathState.slang`

**修改内容**：
```diff
struct PathState
{
    uint        id;                     ///< Path ID encodes (pixel, sampleIdx) with 12 bits each for pixel x|y and 8 bits for sample index.

    uint        flagsAndVertexIndex;    ///< Higher kPathFlagsBitCount bits: Flags indicating the current status. This can be multiple PathFlags flags OR'ed together.
                                        ///< Lower kVertexIndexBitCount bits: Current vertex index (0 = camera, 1 = primary hit, 2 = secondary hit, etc.).
    uint16_t    rejectedHits;           ///< Number of false intersections rejected along the path. This is used as a safeguard to avoid deadlock in pathological cases.
    float16_t   sceneLength;            ///< Path length in scene units (0.f for the first path vertex). This is used for russian roulette and PDF calculations.

    uint16_t    bounces;                ///< Total number of surface bounces (0 = camera, 1 = primary hit, etc.)
    struct
    {
        uint8_t diffuse;                ///< Number of diffuse bounces.
        uint8_t specular;               ///< Number of specular bounces.
        uint8_t transmission;           ///< Number of transmission bounces.
    } pathLengths;
    uint8_t     interiorList;           ///< Index into a per-dispatch-group material list if the ray is inside a volume, otherwise kInvalidIndex.

    float3      origin;                 ///< Ray origin for next path segment (world space).
    float3      dir;                    ///< Ray direction for next path segment (world space).
    float3      initialDir;             ///< Initial ray direction from camera (world space).
    float       pdf;                    ///< Pdf for sampling the outgoing direction (solid angle measure).
    float3      normal;                 ///< Shading normal at the current vertex (world space).
    HitInfo     hit;                    ///< Hit information.

    float3      thp;                    ///< Path throughput.
    float3      L;                      ///< Accumulated path radiance.

    // Path classification for NRD.
    uint        nrdPathFlags;           ///< Flags to classify the current path for NRD.

    // Guide data for the denoiser.
    GuideData   guideData;              ///< Guide data for the denoiser.

    // Sample generator.
    SampleGenerator sg;                 ///< Per-path sample generator.
}
```

### 2. 修改 PathTracer.slang 文件

修改generatePath函数，在初始化PathState时设置initialDir字段。

**文件路径**：`Source/RenderPasses/PathTracer/PathTracer.slang`

**修改内容**：
```diff
void generatePath(const uint pathID, out PathState path)
{
    path = {};
    path.setActive();
    path.id = pathID;
    path.thp = float3(1.f);

    const uint2 pixel = path.getPixel();

    // Create primary ray.
    Ray cameraRay = gScene.camera.computeRayPinhole(pixel, params.frameDim);
    if (kUseViewDir) cameraRay.dir = -viewDir[pixel];
    path.origin = cameraRay.origin;
    path.dir = cameraRay.dir;

    // Store initial direction for irradiance calculation
    path.initialDir = cameraRay.dir;

    // Create sample generator.
    const uint maxSpp = kSamplesPerPixel > 0 ? kSamplesPerPixel : kMaxSamplesPerPixel;
    path.sg = SampleGenerator(pixel, params.seed * maxSpp + path.getSampleIdx());
}
```

**说明**：
- 在generatePath函数中，在设置`path.dir`后添加了对`path.initialDir`的初始化
- 将初始光线方向`cameraRay.dir`同时存储到`initialDir`字段中
- 添加了注释，明确说明这是为辐照度计算添加的

## 第2阶段：为PathTracer添加光线数据的新输出通道

实现时间：2024-08-15

### 1. 修改 PathTracer.cpp 文件

添加新的输出通道常量和输出通道定义。

**文件路径**：`Source/RenderPasses/PathTracer/PathTracer.cpp`

**修改内容**：
```diff
const std::string kOutputColor = "color";
const std::string kOutputAlbedo = "albedo";
const std::string kOutputSpecularAlbedo = "specularAlbedo";
const std::string kOutputIndirectAlbedo = "indirectAlbedo";
const std::string kOutputGuideNormal = "guideNormal";
const std::string kOutputReflectionPosW = "reflectionPosW";
const std::string kOutputRayCount = "rayCount";
const std::string kOutputPathLength = "pathLength";
const std::string kOutputInitialRayInfo = "initialRayInfo"; // 新增: 初始光线信息输出通道
const std::string kOutputNRDDiffuseRadianceHitDist = "nrdDiffuseRadianceHitDist";
// ... 其他输出通道定义 ...

const Falcor::ChannelList kOutputChannels =
{
    { kOutputColor,                                     "",     "Output color (linear)", true /* optional */, ResourceFormat::RGBA32Float },
    { kOutputAlbedo,                                    "",     "Output albedo (linear)", true /* optional */, ResourceFormat::RGBA8Unorm },
    { kOutputSpecularAlbedo,                            "",     "Output specular albedo (linear)", true /* optional */, ResourceFormat::RGBA8Unorm },
    { kOutputIndirectAlbedo,                            "",     "Output indirect albedo (linear)", true /* optional */, ResourceFormat::RGBA8Unorm },
    { kOutputGuideNormal,                               "",     "Output guide normal (linear)", true /* optional */, ResourceFormat::RGBA16Float },
    { kOutputReflectionPosW,                            "",     "Output reflection pos (world space)", true /* optional */, ResourceFormat::RGBA32Float },
    { kOutputRayCount,                                  "",     "Per-pixel ray count", true /* optional */, ResourceFormat::R32Uint },
    { kOutputPathLength,                                "",     "Per-pixel path length", true /* optional */, ResourceFormat::R32Uint },
    { kOutputInitialRayInfo,                            "",     "Initial ray direction and intensity", true /* optional */, ResourceFormat::RGBA32Float }, // 新增: 初始光线信息输出通道
    // NRD outputs
    // ... 其他NRD输出通道 ...
}
```

### 2. 修改 PathTracer.h 文件

在PathTracer类中添加新的变量来存储初始光线信息和控制其输出。

**文件路径**：`Source/RenderPasses/PathTracer/PathTracer.h`

**修改内容**：
```diff
    bool                            mFixedSampleCount = true;   ///< True if a fixed sample count per pixel is used. Otherwise load it from the pass sample count input.
    bool                            mOutputGuideData = false;   ///< True if guide data should be generated as outputs.
    bool                            mOutputNRDData = false;     ///< True if NRD diffuse/specular data should be generated as outputs.
    bool                            mOutputNRDAdditionalData = false;   ///< True if NRD data from delta and residual paths should be generated as designated outputs rather than being included in specular NRD outputs.
    bool                            mOutputInitialRayInfo = false; ///< True if initial ray direction and intensity data should be generated as outputs.

    ref<ComputePass>                mpGeneratePaths;            ///< Fullscreen compute pass generating paths starting at primary hits.
    ref<ComputePass>                mpResolvePass;              ///< Sample resolve pass.
    ref<ComputePass>                mpReflectTypes;             ///< Helper for reflecting structured buffer types.

    // ... 其他变量 ...

    ref<Buffer>                     mpSampleColor;              ///< Compact per-sample color buffer. This is used only if spp > 1.
    ref<Buffer>                     mpSampleGuideData;          ///< Compact per-sample denoiser guide data.
    ref<Buffer>                     mpSampleNRDRadiance;        ///< Compact per-sample NRD radiance data.
    ref<Buffer>                     mpSampleNRDHitDist;         ///< Compact per-sample NRD hit distance data.
    ref<Buffer>                     mpSampleNRDPrimaryHitNeeOnDelta;///< Compact per-sample NEE on delta primary vertices data.
    ref<Buffer>                     mpSampleNRDEmission;        ///< Compact per-sample NRD emission data.
    ref<Buffer>                     mpSampleNRDReflectance;     ///< Compact per-sample NRD reflectance data.
    ref<Buffer>                     mpSampleInitialRayInfo;     ///< Per-sample initial ray direction and intensity buffer.
```

### 3. 修改 StaticParams.slang 文件

在StaticParams中添加kOutputInitialRayInfo常量。

**文件路径**：`Source/RenderPasses/PathTracer/StaticParams.slang`

**修改内容**：
```diff
static const bool kUseNRDDemodulation = USE_NRD_DEMODULATION;
static const uint kColorFormat = COLOR_FORMAT;
static const uint kMISHeuristic = MIS_HEURISTIC;
static const float kMISPowerExponent = MIS_POWER_EXPONENT;
static const bool kOutputInitialRayInfo = OUTPUT_INITIAL_RAY_INFO;
```

### 4. 修改 PathTracer.cpp 中的 beginFrame 函数

添加对初始光线信息输出通道的检查。

**文件路径**：`Source/RenderPasses/PathTracer/PathTracer.cpp`

**修改内容**：
```diff
    // Check if additional NRD data should be generated.
    bool prevOutputNRDAdditionalData = mOutputNRDAdditionalData;
    mOutputNRDAdditionalData =
        renderData[kOutputNRDDeltaReflectionRadianceHitDist] != nullptr
        || renderData[kOutputNRDDeltaTransmissionRadianceHitDist] != nullptr
        || renderData[kOutputNRDDeltaReflectionReflectance] != nullptr
        || renderData[kOutputNRDDeltaReflectionEmission] != nullptr
        || renderData[kOutputNRDDeltaReflectionNormWRoughMaterialID] != nullptr
        || renderData[kOutputNRDDeltaReflectionPathLength] != nullptr
        || renderData[kOutputNRDDeltaReflectionHitDist] != nullptr
        || renderData[kOutputNRDDeltaTransmissionReflectance] != nullptr
        || renderData[kOutputNRDDeltaTransmissionEmission] != nullptr
        || renderData[kOutputNRDDeltaTransmissionNormWRoughMaterialID] != nullptr
        || renderData[kOutputNRDDeltaTransmissionPathLength] != nullptr
        || renderData[kOutputNRDDeltaTransmissionPosW] != nullptr;
    if (mOutputNRDAdditionalData != prevOutputNRDAdditionalData) mRecompile = true;

    // Check if initial ray info should be generated.
    bool prevOutputInitialRayInfo = mOutputInitialRayInfo;
    mOutputInitialRayInfo = renderData[kOutputInitialRayInfo] != nullptr;
    if (mOutputInitialRayInfo != prevOutputInitialRayInfo) mRecompile = true;
```

### 5. 修改 PathTracer.cpp 中的 prepareResources 函数

添加对初始光线信息缓冲区的创建。

**文件路径**：`Source/RenderPasses/PathTracer/PathTracer.cpp`

**修改内容**：
```diff
    if (mOutputNRDData && (!mpSampleNRDRadiance || mpSampleNRDRadiance->getElementCount() < sampleCount || mVarsChanged))
    {
        mpSampleNRDRadiance = mpDevice->createStructuredBuffer(var["sampleNRDRadiance"], sampleCount, ResourceBindFlags::ShaderResource | ResourceBindFlags::UnorderedAccess, MemoryType::DeviceLocal, nullptr, false);
        mpSampleNRDHitDist = mpDevice->createStructuredBuffer(var["sampleNRDHitDist"], sampleCount, ResourceBindFlags::ShaderResource | ResourceBindFlags::UnorderedAccess, MemoryType::DeviceLocal, nullptr, false);
        mpSampleNRDPrimaryHitNeeOnDelta = mpDevice->createStructuredBuffer(var["sampleNRDPrimaryHitNeeOnDelta"], sampleCount, ResourceBindFlags::ShaderResource | ResourceBindFlags::UnorderedAccess, MemoryType::DeviceLocal, nullptr, false);
        mpSampleNRDEmission = mpDevice->createStructuredBuffer(var["sampleNRDEmission"], sampleCount, ResourceBindFlags::ShaderResource | ResourceBindFlags::UnorderedAccess, MemoryType::DeviceLocal, nullptr, false);
        mpSampleNRDReflectance = mpDevice->createStructuredBuffer(var["sampleNRDReflectance"], sampleCount, ResourceBindFlags::ShaderResource | ResourceBindFlags::UnorderedAccess, MemoryType::DeviceLocal, nullptr, false);
        mVarsChanged = true;
    }

    // 为初始光线信息创建缓冲区
    if (mOutputInitialRayInfo && (!mpSampleInitialRayInfo || mpSampleInitialRayInfo->getElementCount() < sampleCount || mVarsChanged))
    {
        mpSampleInitialRayInfo = mpDevice->createStructuredBuffer(var["sampleInitialRayInfo"], sampleCount, ResourceBindFlags::ShaderResource | ResourceBindFlags::UnorderedAccess, MemoryType::DeviceLocal, nullptr, false);
        mVarsChanged = true;
    }
```

### 6. 修改 PathTracer::StaticParams::getDefines 函数

在DefineList中添加OUTPUT_INITIAL_RAY_INFO宏定义。

**文件路径**：`Source/RenderPasses/PathTracer/PathTracer.cpp`

**修改内容**：
```diff
DefineList PathTracer::StaticParams::getDefines(const PathTracer& owner) const
{
    DefineList defines;

    // ... 其他宏定义 ...

    // 输出通道相关宏
    defines.add("OUTPUT_GUIDE_DATA", owner.mOutputGuideData ? "1" : "0");
    defines.add("OUTPUT_NRD_DATA", owner.mOutputNRDData ? "1" : "0");
    defines.add("OUTPUT_NRD_ADDITIONAL_DATA", owner.mOutputNRDAdditionalData ? "1" : "0");
    defines.add("OUTPUT_INITIAL_RAY_INFO", owner.mOutputInitialRayInfo ? "1" : "0"); // 新增宏定义

    // ... 其他定义 ...

    return defines;
}
```

### 7. 修改 GeneratePaths.cs.slang 文件

在PathGenerator结构体中添加sampleInitialRayInfo字段，并在writeBackground函数中添加对初始光线信息的写入。

**文件路径**：`Source/RenderPasses/PathTracer/GeneratePaths.cs.slang`

**修改内容**：
```diff
    RWStructuredBuffer<ColorType> sampleColor;      ///< Output per-sample color if kSamplesPerPixel != 1.
    RWStructuredBuffer<GuideData> sampleGuideData;  ///< Output per-sample guide data.
    RWStructuredBuffer<float4> sampleInitialRayInfo; ///< Output per-sample initial ray direction and intensity. Only valid if kOutputInitialRayInfo == true.
    NRDBuffers outputNRD;                           ///< Output NRD data.

    RWTexture2D<float4> outputColor;                ///< Output color buffer if kSamplesPerPixel == 1.

    // ... 其他字段 ...

    // Additional specialization.
    static const bool kOutputGuideData = OUTPUT_GUIDE_DATA;
    static const bool kOutputInitialRayInfo = OUTPUT_INITIAL_RAY_INFO;

// ... 其他代码 ...

// 在writeBackground函数中添加初始光线信息的存储
            if (kOutputNRDData)
            {
                outputNRD.sampleRadiance[outIdx + i] = {};
                outputNRD.sampleHitDist[outIdx + i] = kNRDInvalidPathLength;
                outputNRD.sampleEmission[outIdx + i] = 0.f;
                outputNRD.sampleReflectance[outIdx + i] = 1.f;
            }

            if (kOutputInitialRayInfo)
            {
                sampleInitialRayInfo[outIdx + i] = float4(dir, luminance(color));
            }
```

### 8. 修改 ResolvePass.cs.slang 文件

在ResolvePass结构体中添加sampleInitialRayInfo字段和输出通道，并在execute函数中添加对初始光线信息的处理。

**文件路径**：`Source/RenderPasses/PathTracer/ResolvePass.cs.slang`

**修改内容**：
```diff
struct ResolvePass
{
    // Resources
    PathTracerParams params;                                ///< Runtime parameters.

    StructuredBuffer<ColorType>   sampleColor;              ///< Input per-sample color if kSamplesPerPixel != 1.
    StructuredBuffer<GuideData>   sampleGuideData;          ///< Input per-sample guide data. Only valid if kOutputGuideData == true.
    StructuredBuffer<NRDRadiance> sampleNRDRadiance;        ///< Input per-sample NRD radiance data. Only valid if kOutputNRDData == true.
    StructuredBuffer<float>       sampleNRDHitDist;         ///< Input per-sample NRD hit distance data. Only valid if kOutputNRDData == true.
    StructuredBuffer<float4>      sampleNRDEmission;        ///< Input per-sample NRD emission data. Only valid if kOutputNRDData == true.
    StructuredBuffer<float4>      sampleNRDReflectance;     ///< Input per-sample NRD reflectance data. Only valid if kOutputNRDData == true.
    StructuredBuffer<float4>      sampleInitialRayInfo;     ///< Input per-sample initial ray direction and intensity data. Only valid if kOutputInitialRayInfo == true.

    // ... 其他输入字段 ...

    RWTexture2D<float4> outputColor;                        ///< Output resolved color.
    RWTexture2D<float4> outputAlbedo;                       ///< Output resolved albedo. Only valid if kOutputGuideData == true.
    RWTexture2D<float4> outputSpecularAlbedo;               ///< Output resolved specular albedo. Only valid if kOutputGuideData == true.
    RWTexture2D<float4> outputIndirectAlbedo;               ///< Output resolved indirect albedo. Only valid if kOutputGuideData == true.
    RWTexture2D<float4> outputGuideNormal;                  ///< Output resolved guide normal. Only valid if kOutputGuideData == true.
    RWTexture2D<float4> outputReflectionPosW;               ///< Output resolved reflection pos. Only valid if kOutputGuideData == true.
    RWTexture2D<float4> outputInitialRayInfo;               ///< Output resolved initial ray direction and intensity. Only valid if kOutputInitialRayInfo == true.

    // ... 其他输出字段 ...

    static const bool kOutputGuideData = OUTPUT_GUIDE_DATA;
    static const bool kOutputNRDData = OUTPUT_NRD_DATA;
    static const bool kOutputInitialRayInfo = OUTPUT_INITIAL_RAY_INFO;

// ... 在execute函数中添加对初始光线信息的处理 ...

        // 处理初始光线信息通道
        if (kOutputInitialRayInfo)
        {
            float4 initialRayInfo = float4(0.f);

            for (uint sampleIdx = 0; sampleIdx < spp; sampleIdx++)
            {
                uint idx = offset + sampleIdx;
                initialRayInfo += sampleInitialRayInfo[idx];
            }

            outputInitialRayInfo[pixel] = float4(invSpp * initialRayInfo.xyz, initialRayInfo.w * invSpp);
        }
```

### 9. 修改 bindShaderData 函数

将初始光线信息缓冲区添加到shader绑定中。

**文件路径**：`Source/RenderPasses/PathTracer/PathTracer.cpp`

**修改内容**：
```diff
void PathTracer::bindShaderData(const ShaderVar& var, const RenderData& renderData, bool useLightSampling) const
{
    // Bind static resources that don't change per frame.
    if (mVarsChanged)
    {
        if (useLightSampling && mpEnvMapSampler) mpEnvMapSampler->bindShaderData(var["envMapSampler"]);

        var["sampleOffset"] = mpSampleOffset; // Can be nullptr
        var["sampleColor"] = mpSampleColor;
        var["sampleGuideData"] = mpSampleGuideData;
        var["sampleInitialRayInfo"] = mpSampleInitialRayInfo; // 新增: 绑定初始光线信息缓冲区
    }

    // ... 其他代码 ...
}
```

### 10. 修改 resolvePass 函数

将初始光线信息的输出通道和样本缓冲区绑定到ResolvePass。

**文件路径**：`Source/RenderPasses/PathTracer/PathTracer.cpp`

**修改内容**：
```diff
void PathTracer::resolvePass(RenderContext* pRenderContext, const RenderData& renderData)
{
    // ... 其他代码 ...

    // Bind resources.
    auto var = mpResolvePass->getRootVar()["CB"]["gResolvePass"];
    // ... 其他绑定 ...
    var["outputNRDDeltaReflectionRadianceHitDist"] = renderData.getTexture(kOutputNRDDeltaReflectionRadianceHitDist);
    var["outputNRDDeltaTransmissionRadianceHitDist"] = renderData.getTexture(kOutputNRDDeltaTransmissionRadianceHitDist);
    var["outputNRDResidualRadianceHitDist"] = renderData.getTexture(kOutputNRDResidualRadianceHitDist);
    var["outputInitialRayInfo"] = renderData.getTexture(kOutputInitialRayInfo); // 新增：绑定初始光线信息输出纹理

    if (mVarsChanged)
    {
        var["sampleOffset"] = mpSampleOffset; // Can be nullptr
        var["sampleColor"] = mpSampleColor;
        var["sampleGuideData"] = mpSampleGuideData;
        var["sampleNRDRadiance"] = mpSampleNRDRadiance;
        var["sampleNRDHitDist"] = mpSampleNRDHitDist;
        var["sampleNRDEmission"] = mpSampleNRDEmission;
        var["sampleNRDReflectance"] = mpSampleNRDReflectance;
        var["sampleInitialRayInfo"] = mpSampleInitialRayInfo; // 新增：绑定初始光线信息样本缓冲区

        var["sampleNRDPrimaryHitNeeOnDelta"] = mpSampleNRDPrimaryHitNeeOnDelta;
        var["primaryHitDiffuseReflectance"] = renderData.getTexture(kOutputNRDDiffuseReflectance);
    }

    // ... 其他代码 ...
}
```

### 11. 修改 PathTracer.slang 文件

添加输出初始光线信息的功能。

**文件路径**：`Source/RenderPasses/PathTracer/PathTracer.slang`

**修改内容**：
```diff
    // Outputs
    RWStructuredBuffer<ColorType> sampleColor;      ///< Output per-sample color if kSamplesPerPixel != 1.
    RWStructuredBuffer<GuideData> sampleGuideData;  ///< Output per-sample guide data.
    RWStructuredBuffer<float4> sampleInitialRayInfo; ///< Output per-sample initial ray direction and intensity data.
    NRDBuffers outputNRD;                           ///< Output NRD data.

    RWTexture2D<float4> outputColor;                ///< Output color buffer if kSamplesPerPixel == 1.

    /*******************************************************************
                                Static members
    *******************************************************************/

    // Render settings that depend on the scene.
    // TODO: Move into scene defines.
    static const bool kUseEnvLight = USE_ENV_LIGHT;
    static const bool kUseEmissiveLights = USE_EMISSIVE_LIGHTS;
    static const bool kUseAnalyticLights = USE_ANALYTIC_LIGHTS;
    static const bool kUseCurves = USE_CURVES;
    static const bool kUseHairMaterial = USE_HAIR_MATERIAL;

    // Additional specialization.
    static const bool kOutputGuideData = OUTPUT_GUIDE_DATA;
    static const bool kOutputInitialRayInfo = OUTPUT_INITIAL_RAY_INFO;

// 在writeOutput函数中添加保存初始光线信息
    void writeOutput(const PathState path)
    {
        // ... 其他代码 ...

        if (kOutputGuideData)
        {
            sampleGuideData[outIdx] = path.guideData;
        }

        if (kOutputInitialRayInfo)
        {
            sampleInitialRayInfo[outIdx] = float4(path.initialDir, luminance(path.L));
        }

        // ... 其他代码 ...
    }
```

## 第2阶段实现总结

第2阶段的实现使得PathTracer能够输出每个像素的初始光线方向和强度。关键点如下：

1. 在PathTracer.cpp中添加了新的输出通道常量`kOutputInitialRayInfo`和相应的通道定义。
2. 在PathTracer.h中添加了标志位`mOutputInitialRayInfo`和缓冲区成员`mpSampleInitialRayInfo`。
3. 在StaticParams.slang中添加了`kOutputInitialRayInfo`编译时常量。
4. 在PathTracer.cpp的beginFrame函数中检查是否需要输出初始光线信息。
5. 在prepareResources函数中创建初始光线信息缓冲区。
6. 在StaticParams::getDefines函数中添加了OUTPUT_INITIAL_RAY_INFO的宏定义。
7. 在GeneratePaths.cs.slang中添加了sampleInitialRayInfo字段，并在writeBackground函数中保存初始光线信息。
8. 在ResolvePass.cs.slang中添加了对初始光线信息的处理，将样本数据解析为像素数据。
9. 在bindShaderData函数中添加了初始光线信息缓冲区的绑定。
10. 在resolvePass函数中为ResolvePass添加了初始光线信息的绑定。
11. 在PathTracer.slang中添加了对初始光线信息的输出处理。

这些修改确保了PathTracer可以将初始光线方向（相机到像素的方向向量）和相应的光线强度（辐射亮度）输出到专用的缓冲区中，为后续的辐照度计算提供了必要的数据基础。
