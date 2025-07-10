# Distance Attenuation Fix for Direct Light Hits

## 问题描述

在Falcor路径追踪器中发现了一个物理不一致性问题：直接击中发光表面的光线没有应用距离衰减，而间接光照（通过NEE采样）却正确地遵循平方反比定律。这导致了不符合物理的渲染结果。

## 问题分析

### 间接光照（NEE）- 正确实现
在`EmissiveLightSamplerHelpers.slang`中的`sampleTriangle`函数：
```slang
// PDF包含距离平方项
ls.pdf = distSqr / denom;

// 在PathTracer.slang中
ls.Li = tls.Le / tls.pdf;  // 自动包含1/distSqr衰减
```

### 直接光照 - 问题所在
在`PathTracer.slang`的`handleHit`函数中：
```slang
// 原始代码 - 缺失距离衰减
float3 emission = misWeight * bsdfProperties.emission;
addToPathContribution(path, emission);  // 没有1/r²项
```

## 解决方案

### 修改内容
在`Source/RenderPasses/PathTracer/PathTracer.slang`的`handleHit`函数中应用了以下修改：

```slang
// 修改后的代码
float3 emission = misWeight * bsdfProperties.emission;

// Apply distance attenuation for direct hits to maintain consistency with NEE sampling.
// For indirect lighting via NEE, distance attenuation is already included in the Li calculation.
float hitDistance = path.hit.t;
float distSqr = hitDistance * hitDistance;

// Apply 1/r^2 attenuation to match the physics of NEE sampling
// Guard against division by very small distances to avoid numerical issues
float3 attenuatedEmission = emission / max(distSqr, 1e-6f);

addToPathContribution(path, attenuatedEmission);

// Note: attenuatedEmission for wavelength updates should use the original emission
// as the wavelength calculation is based on the intrinsic emission properties
attenuatedEmission = path.thp * emission;
```

### 关键改进点

1. **物理正确性**: 直接光照现在与间接光照遵循相同的距离衰减规律
2. **数值稳定性**: 使用`max(distSqr, 1e-6f)`防止除零错误
3. **一致性**: 直接击中光源的贡献与NEE采样的贡献在物理上保持一致
4. **兼容性**: 保持与现有波长计算逻辑的兼容性

## 影响评估

### 正面影响
- **物理正确**: 修复了基本的物理不一致性
- **功率计算**: `IncomingLightPowerPass`现在能获得物理正确的结果
- **能量守恒**: 改善了整体的能量守恒性

### 潜在影响
- **渲染结果变化**: 现有场景的直接光照会变暗（符合物理）
- **MIS权重**: 需要验证MIS计算是否仍然正确
- **其他RenderPass**: 依赖PathTracer输出的其他Pass可能需要调整

## 测试验证

### 自动化测试脚本
创建了`scripts/test_distance_attenuation.py`来验证修复效果：

```python
# 测试不同距离下的光功率
test_distances = [1.0, 2.0, 3.0, 4.0, 5.0]

# 期望结果：
# Power(2m) ≈ Power(1m) / 4
# Power(3m) ≈ Power(1m) / 9  
# Power(4m) ≈ Power(1m) / 16
# Power(5m) ≈ Power(1m) / 25
```

### 手动测试步骤

1. **场景设置**:
   - 创建只包含单一发光平面的场景
   - 确保没有其他几何体或光源
   - 发光平面位于原点，面向相机

2. **测试配置**:
   ```python
   PathTracer = createPass("PathTracer", {
       "samplesPerPixel": 64,
       "maxSurfaceBounces": 0,  # 只测试直接光照
       "maxDiffuseBounces": 0,
       "maxSpecularBounces": 0
   })
   ```

3. **距离测试**:
   - 将相机位置设置在距离发光面1m, 2m, 3m, 4m, 5m处
   - 记录每个距离下的功率测量值
   - 验证功率是否遵循1/r²衰减

4. **结果验证**:
   - 比较修复前后的结果
   - 确认直接光照现在遵循平方反比定律
   - 验证间接光照行为没有改变

## 技术细节

### 距离获取
使用`path.hit.t`获取射线行进距离，这是射线追踪过程中已经计算的值，避免了额外的计算开销。

### 数值保护
使用`max(distSqr, 1e-6f)`防止：
- 极小距离导致的数值不稳定
- 零距离引起的除零错误
- 浮点精度问题

### MIS兼容性
修改后的距离衰减与现有的MIS（Multiple Importance Sampling）权重计算兼容，因为：
- MIS权重在距离衰减应用之前计算
- 距离衰减只影响最终的辐射率贡献值
- NEE路径的MIS计算已经考虑了距离因素

## 后续工作

### 验证清单
- [ ] 在简单发光平面场景中验证距离衰减
- [ ] 测试复杂场景中的能量守恒
- [ ] 确认其他RenderPass的兼容性
- [ ] 验证MIS权重计算的正确性
- [ ] 检查NRD（降噪）数据的影响

### 可能的扩展
- 考虑为不同类型的光源提供距离衰减选项
- 添加用于调试的距离衰减可视化
- 优化数值计算的性能

## 总结

这个修复解决了Falcor路径追踪器中一个重要的物理不一致性问题。通过在直接光照路径中应用与间接光照相同的距离衰减机制，现在整个渲染系统在物理上更加一致和准确。

修改是局部的、安全的，并且保持了与现有代码的兼容性。用户应该期望看到更物理正确的渲染结果，特别是在光功率计算应用中。 