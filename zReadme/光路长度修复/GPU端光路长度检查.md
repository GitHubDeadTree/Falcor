根据你的方案和Falcor项目的代码实际情况，我来给出具体的代码实现方案。你的需求是GPU端只检查异常的路径长度（NaN/无穷大），异常的路径直接抛弃，而不是钳制。

## 实现方案概述

基于当前的FalcorCIR架构，需要修改以下关键文件：

1. GPU端的CIR数据验证逻辑
2. GPU端的数据记录函数
3. CPU端的验证标准

## 1. 修改GPU端路径长度检查

首先修改 **PixelStats.slang:200-211** ：

<pre class="px-2 py-1.5 has-[code]:rounded-md has-[code]:!bg-[#e5e5e5] has-[div]:bg-transparent has-[div]:!p-0 has-[code]:text-stone-900 dark:has-[code]:!bg-[#242424] has-[code]:dark:text-white [&_code]:block [&_code]:border-none [&_code]:bg-transparent [&_code]:p-0"><pre><code><span>void log</span>CIRPathComplete(CIRPathData pathData)<span>
</span>{
<span></span><span>// 只检查路径长度的NaN/无穷大值，如果异常则直接丢弃整条光路  </span><span>
</span><span></span><span>if</span><span> (isnan(pathData.pathLength)</span> || <span>isinf(pathData.pathLength))
</span>    {
<span></span><span>// 直接返回，不记录任何数据到缓冲区  </span><span>
</span>        return;
    }

<span></span><span>// 对于路径长度有效的数据，直接记录而不进行任何修改  </span><span>
</span><span></span><span>// 其他字段的验证交给CPU端处理  </span><span>
</span><span>    log</span>CIRStatisticsInternal(pathData)<span>;
</span><span>    log</span>CIRRawPathInternal(pathData)<span>;
</span>}</code></pre></pre>

## 2. 简化GPU端的sanitize函数

修改 **CIRPathData.slang:85-113** ：

<pre class="px-2 py-1.5 has-[code]:rounded-md has-[code]:!bg-[#e5e5e5] has-[div]:bg-transparent has-[div]:!p-0 has-[code]:text-stone-900 dark:has-[code]:!bg-[#242424] has-[code]:dark:text-white [&_code]:block [&_code]:border-none [&_code]:bg-transparent [&_code]:p-0"><pre><code><span>[mutating] void sanitize()
</span>{
<span></span><span>//</span><span> GPU端只处理路径长度的NaN/无穷大值
</span><span></span><span>//</span><span> 其他所有验证和处理都交给CPU端
</span><span></span><span>if</span><span> (isnan(pathLength) || isinf(pathLength))
</span>    {
<span>        pathLength = </span><span>1.0</span><span>f; </span><span>//</span><span> 设置默认值，但实际上这种情况下整条路径会被丢弃
</span>    }

<span></span><span>//</span><span> 移除所有其他字段的clamp和处理逻辑
</span><span></span><span>//</span><span> 保留原始数值，让CPU端进行完整验证
</span>}</code></pre></pre>

## 3. 更新GPU端的isValid函数

修改 **CIRPathData.slang:54-78** ：

<pre class="px-2 py-1.5 has-[code]:rounded-md has-[code]:!bg-[#e5e5e5] has-[div]:bg-transparent has-[div]:!p-0 has-[code]:text-stone-900 dark:has-[code]:!bg-[#242424] has-[code]:dark:text-white [&_code]:block [&_code]:border-none [&_code]:bg-transparent [&_code]:p-0"><pre><code><span>bool</span><span> isValid()
</span>{
<span></span><span>// GPU端只检查路径长度的NaN/无穷大值  </span><span>
</span><span></span><span>if</span><span> (</span><span>isnan</span><span>(pathLength) || </span><span>isinf</span><span>(pathLength)) </span><span>return</span><span></span><span>false</span><span>;
</span>
<span></span><span>// 其他所有验证都移到CPU端  </span><span>
</span><span></span><span>return</span><span></span><span>true</span><span>;
</span>}</code></pre></pre>

## 数据流程

修改后的数据流程：

1. **GPU端** ： **TracePass.rt.slang:407** 调用CIR数据输出
2. **路径长度检查** ：只检查 `pathLength`的NaN/无穷大值，异常则直接丢弃整条光路
3. **数据传输** ：有效数据写入 **PixelStats.slang:52**
4. **CPU端验证** ：对所有字段进行完整的物理范围验证和NaN检查
5. **最终输出** ：只有通过CPU端严格验证的数据才进入最终分析

## 优势

这种实现方案的优势：

1. **保持数据真实性** ：GPU端不修改任何有效数值，保留原始分布
2. **高效过滤** ：只在GPU端过滤最严重的异常（路径长度NaN/无穷大）
3. **灵活验证** ：CPU端可以根据具体需求调整验证标准
4. **缓冲区效率** ：异常路径不占用宝贵的GPU缓冲区空间（ **PixelStats.h:274** ）

这样的设计既保证了数据质量，又最大程度保持了数据的真实性和完整性。
