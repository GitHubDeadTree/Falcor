# CosTheta计算问题分析

## CosTheta的理论用途

在光功率计算中，CosTheta（余弦θ）是一个关键的物理量，代表入射光线方向与相机表面法线之间的夹角余弦值。这个值在光功率计算公式中起着至关重要的作用：

```
功率(Power) = 辐射度(Radiance) × 像素面积(PixelArea) × cos(θ)
```

这个余弦项有着明确的物理意义：

1. **投影效应**：它表示光线投射到相机传感器表面的投影效应。当光线垂直入射时，cos(θ) = 1，效率最高；当光线与表面平行时，cos(θ) = 0，无法被捕获。

2. **Lambert余弦定律**：遵循Lambert余弦定律，表面接收的辐射强度与入射角的余弦成正比。

3. **有效接收面积**：cos(θ)实际上代表了传感器在光线方向上的有效接收面积比例。

## 正常情况下的计算

在正常情况下，`computeCosTheta`函数应该正确计算入射光线与相机法线的夹角余弦值。C++版本的实现如下：

```cpp
float IncomingLightPowerPass::CameraIncidentPower::computeCosTheta(const float3& rayDir) const
{
    // Calculate the cosine of the angle between the ray direction and camera normal
    // For rays entering the camera, we need the angle with the inverted camera normal
    float3 invNormal = -mCameraNormal;

    // Calculate cosine using dot product
    float cosTheta = std::max(0.f, dot(rayDir, invNormal));

    // Ensure cosine is not too small or zero
    const float minCosTheta = 0.00001f;
    cosTheta = std::max(cosTheta, minCosTheta);

    return cosTheta;
}
```

HLSL着色器版本的实现类似：

```hlsl
float computeCosTheta(float3 rayDir)
{
    // Calculate camera normal (forward direction)
    float3 cameraNormal = normalize(gCameraTarget - gCameraPosition);

    // For rays entering the camera, use inverse normal
    float3 invNormal = -cameraNormal;

    // Compute cosine using dot product (clamp to non-negative)
    return max(0.0f, dot(rayDir, invNormal));
}
```

这两个实现的逻辑是：
1. 计算相机的正向法线（从相机位置指向目标位置）
2. 反转法线方向（因为我们关心的是进入相机的光线，它们应与反向法线有正的点积）
3. 计算光线方向与反向法线的点积，得到余弦值
4. 确保余弦值非负（因为负值意味着光线从相机背面进入，通常不考虑）
5. C++版本还确保余弦值不会太小（至少为0.00001），以避免数值问题

## 当前问题分析

根据调试输出，所有前4个像素的CosTheta值都是0：

```
(Info) DEBUG - Pixel [0,0] - CALCULATION: PixelArea=0.00000102, CosTheta=0.00000000, RayLength=1.00000000
(Info) DEBUG - Pixel [1,0] - CALCULATION: PixelArea=0.00000102, CosTheta=0.00000000, RayLength=0.99999994
(Info) DEBUG - Pixel [0,1] - CALCULATION: PixelArea=0.00000102, CosTheta=0.00000000, RayLength=0.99999994
(Info) DEBUG - Pixel [1,1] - CALCULATION: PixelArea=0.00000102, CosTheta=0.00000000, RayLength=1.00000000
```

可能导致此问题的原因分析如下：

### 1. HLSL版本缺少最小值保证

注意到C++版本的实现有一个保障措施：它确保cosTheta不会小于0.00001，而HLSL版本没有这个保障。但这不能解释为什么所有像素的CosTheta都是精确的0，而不是一个非常小的值。

### 2. 光线方向与相机法线垂直

更可能的原因是计算出的光线方向与相机法线严格垂直，导致它们的点积为0。这表明可能有以下问题：

- **相机设置问题**：相机位置和目标设置可能导致计算出的法线不正确。
- **光线方向计算错误**：光线方向的计算可能有问题，特别是当使用`computeRayDirection`函数时。
- **坐标系统不一致**：相机法线和光线方向可能在不同的坐标系统中，导致点积计算错误。

### 3. 渲染管线中的坐标变换问题

在渲染管线中，经常需要在不同坐标系统间转换（如世界坐标、相机坐标、NDC坐标等）。如果这些转换有问题，特别是涉及到相机视图和投影矩阵的转换，可能导致法线或光线方向的计算错误。

从调试信息可以看到，`RayLength`值都非常接近1.0，这表明光线方向已经被正确地归一化。问题很可能出在相机法线的计算或者光线方向与法线的关系上。

### 4. 相机模型假设不匹配

不同的相机模型可能有不同的假设。例如，有些渲染系统假设相机看向-Z方向，而有些假设看向+Z方向。如果代码中的假设与实际使用的相机模型不匹配，可能导致法线方向错误。

## 可能的解决方案

1. **调试相机法线**：在计算CosTheta之前输出相机法线和光线方向的值，验证它们是否合理。

2. **修改HLSL版本**：为HLSL版本的`computeCosTheta`函数添加最小值保障，类似于C++版本：
   ```hlsl
   // 添加最小值保障
   const float minCosTheta = 0.00001f;
   return max(minCosTheta, max(0.0f, dot(rayDir, invNormal)));
   ```

3. **检查光线生成**：仔细检查`computeRayDirection`函数，确保它正确地生成从相机出发的光线方向。

4. **强制相机法线**：可以尝试暂时强制设置相机法线为一个已知的方向（如(0,0,-1)），看是否能解决问题。

5. **检查坐标系一致性**：确保所有相关计算都在同一坐标系中进行。

6. **添加更多调试信息**：输出相机位置、目标、法线和光线方向，以便更全面地诊断问题。

总的来说，CosTheta计算为0的问题很可能是由于相机设置或坐标系统转换导致的。通过添加更详细的调试信息，应该能够定位并解决这个问题。
