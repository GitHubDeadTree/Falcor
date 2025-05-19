# IrradiancePass纹理显示区域问题修复记录

## 问题描述

在IrradiancePass中发现了一个显示区域受限的问题：当启用Passthrough Mode（直通模式）时，输出的纹理仅显示在屏幕左上角的一小块区域，而不是整个屏幕。这与预期的行为不符，因为PathTracer的initialRayInfo输出能够正常显示在整个屏幕上。

## 问题分析

通过对代码的深入分析，我们发现问题出在IrradiancePass.cpp文件中的线程派发计算上。具体问题在于：

1. **错误的线程派发参数**：
   ```cpp
   // 错误代码
   mpComputePass->execute(pRenderContext, { (width + 15) / 16, (height + 15) / 16, 1 });
   ```

2. **错误原因**：
   - ComputePass::execute方法预期接收的是线程总数（即像素总数）
   - 代码却传入了线程组数量 (每组16x16个线程)
   - 结果是只派发了 1/256 (1/16 × 1/16) 的线程数量，只处理了屏幕左上角部分

3. **参考正确的实现**：
   在PathTracer.cpp中可以看到正确的实现方式：
   ```cpp
   // 正确代码
   mpResolvePass->execute(pRenderContext, { mParams.frameDim, 1u });
   ```

4. **底层机制**：
   当调用ComputePass::execute时，会自动计算需要的线程组数量：
   ```cpp
   void ComputePass::execute(ComputeContext* pContext, uint32_t nThreadX, uint32_t nThreadY, uint32_t nThreadZ)
   {
       FALCOR_ASSERT(mpVars);
       uint3 threadGroupSize = mpState->getProgram()->getReflector()->getThreadGroupSize();
       uint3 groups = div_round_up(uint3(nThreadX, nThreadY, nThreadZ), threadGroupSize);
       pContext->dispatch(mpState.get(), mpVars.get(), groups);
   }
   ```

## 解决方案

我们通过修改IrradiancePass.cpp文件的execute方法，将传入的参数从线程组数量改为线程总数：

```cpp
// 修改前
mpComputePass->execute(pRenderContext, { (width + 15) / 16, (height + 15) / 16, 1 });

// 修改后
mpComputePass->execute(pRenderContext, width, height, 1);
```

此外，还添加了调试日志以便于问题排查：

```cpp
logInfo("IrradiancePass::execute() - Dispatching compute with dimensions {}x{}", width, height);
```

## 验证和测试

修复后，IrradiancePass在Passthrough Mode下能够正确地显示整个屏幕的内容。对比测试确认：

1. 当使用PathTracer的initialRayInfo输出作为输入时，IrradiancePass在Passthrough Mode下输出的纹理与输入完全一致
2. 整个屏幕区域都能够正确显示，不再限制在左上角的小块区域
3. 日志正确显示了派发的分辨率与输入/输出纹理的分辨率一致

## 总结

此修复解决了IrradiancePass纹理显示区域受限的问题，使其能够正确处理整个屏幕的每个像素。问题的根本原因是在计算着色器的派发参数计算中混淆了线程总数和线程组数量的概念。

通过这次修复，我们也学到了HLSL计算着色器的线程调度机制，以及在Falcor框架中ComputePass正确的使用方式。特别是要注意execute方法的参数应该是要处理的线程总数（像素总数），而不是线程组数量。
