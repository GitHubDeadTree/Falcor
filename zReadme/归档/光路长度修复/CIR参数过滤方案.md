基于你的需求和Falcor项目的代码结构，我将为你提供一个完整的代码实现方案，让CPU端的CIR过滤参数可以通过UI界面控制。

## 实现方案概述

你需要在 `PixelStats`类中添加可配置的过滤参数成员变量，修改现有的硬编码验证逻辑，并在UI中添加相应的控件。

## 1. 修改PixelStats.h头文件

首先在 `PixelStats`类中添加可配置的过滤参数： **PixelStats.h:274-278**

在这个位置附近添加新的成员变量：

<pre class="px-2 py-1.5 has-[code]:rounded-md has-[code]:!bg-[#e5e5e5] has-[div]:bg-transparent has-[div]:!p-0 has-[code]:text-stone-900 dark:has-[code]:!bg-[#242424] has-[code]:dark:text-white [&_code]:block [&_code]:border-none [&_code]:bg-transparent [&_code]:p-0"><pre><code><span>// CIR filtering parameters (configurable via UI)  </span><span>
</span><span></span><span>float</span><span> mCIRMinPathLength = </span><span>1.0f</span><span>;        </span><span>///< Minimum path length for CIR filtering (meters)  </span><span>
</span><span></span><span>float</span><span> mCIRMaxPathLength = </span><span>200.0f</span><span>;      </span><span>///< Maximum path length for CIR filtering (meters)  </span><span>
</span><span></span><span>float</span><span> mCIRMinEmittedPower = </span><span>0.0f</span><span>;      </span><span>///< Minimum emitted power for CIR filtering (watts)  </span><span>
</span><span></span><span>float</span><span> mCIRMaxEmittedPower = </span><span>10000.0f</span><span>;  </span><span>///< Maximum emitted power for CIR filtering (watts)  </span><span>
</span><span></span><span>float</span><span> mCIRMinAngle = </span><span>0.0f</span><span>;             </span><span>///< Minimum angle for CIR filtering (radians)  </span><span>
</span><span></span><span>float</span><span> mCIRMaxAngle = </span><span>3.14159f</span><span>;         </span><span>///< Maximum angle for CIR filtering (radians)  </span><span>
</span><span></span><span>float</span><span> mCIRMinReflectance = </span><span>0.0f</span><span>;       </span><span>///< Minimum reflectance for CIR filtering  </span><span>
</span><span></span><span>float</span><span> mCIRMaxReflectance = </span><span>1.0f</span><span>;       </span><span>///< Maximum reflectance for CIR filtering</span></code></pre></pre>

## 2. 修改CIRPathData的isValid()函数

需要修改CPU端的验证函数，使其使用可配置的参数而不是硬编码值。在 **PixelStats.h:65-79** 中，将 `isValid()`函数改为接受参数的版本：

<pre class="px-2 py-1.5 has-[code]:rounded-md has-[code]:!bg-[#e5e5e5] has-[div]:bg-transparent has-[div]:!p-0 has-[code]:text-stone-900 dark:has-[code]:!bg-[#242424] has-[code]:dark:text-white [&_code]:block [&_code]:border-none [&_code]:bg-transparent [&_code]:p-0"><pre><code>boolisValid(float minPathLength, float maxPathLength,
float minEmittedPower, float maxEmittedPower,
float minAngle, float maxAngle,
float minReflectance, float maxReflectance)const
<span>{
</span><span></span><span>return</span><span> pathLength >= minPathLength && pathLength <= maxPathLength &&
</span>           emissionAngle >= minAngle && emissionAngle <= maxAngle &&
           receptionAngle >= minAngle && receptionAngle <= maxAngle &&
           reflectanceProduct >= minReflectance && reflectanceProduct <= maxReflectance &&
           emittedPower > minEmittedPower && emittedPower <= maxEmittedPower;
}</code></pre></pre>

## 3. 修改PixelStats.cpp中的过滤逻辑

在 **PixelStats.cpp:555-560** 处，修改过滤逻辑使用新的参数化验证函数：

<pre class="px-2 py-1.5 has-[code]:rounded-md has-[code]:!bg-[#e5e5e5] has-[div]:bg-transparent has-[div]:!p-0 has-[code]:text-stone-900 dark:has-[code]:!bg-[#242424] has-[code]:dark:text-white [&_code]:block [&_code]:border-none [&_code]:bg-transparent [&_code]:p-0"><pre><code><span>// Apply CPU-side filtering with configurable criteria  </span><span>
</span><span></span><span>if</span><span> (rawData[i].</span><span>isValid</span><span>(mCIRMinPathLength, mCIRMaxPathLength,
</span>                       mCIRMinEmittedPower, mCIRMaxEmittedPower,
                       mCIRMinAngle, mCIRMaxAngle,
                       mCIRMinReflectance, mCIRMaxReflectance))
{
<span>    mCIRRawData.</span><span>push_back</span><span>(rawData[i]);
</span>    filteredCount++;
}</code></pre></pre>

## 4. 扩展renderUI函数

在 **PixelStats.cpp:280-338** 的 `renderUI`函数中，添加CIR过滤参数的UI控件。参考 的UI模式：

<pre class="px-2 py-1.5 has-[code]:rounded-md has-[code]:!bg-[#e5e5e5] has-[div]:bg-transparent has-[div]:!p-0 has-[code]:text-stone-900 dark:has-[code]:!bg-[#242424] has-[code]:dark:text-white [&_code]:block [&_code]:border-none [&_code]:bg-transparent [&_code]:p-0"><pre><code>voidPixelStats::renderUI(Gui::Widgets& widget)
<span>{
</span><span></span><span>// 现有的UI代码...  </span><span>
</span>
<span></span><span>// 在现有UI代码后添加CIR过滤参数控制  </span><span>
</span><span></span><span>if</span><span> (mEnabled && (mCollectionMode == CollectionMode::RawData || mCollectionMode == CollectionMode::Both))
</span>    {
<span></span><span>if</span><span> (</span><span>auto</span><span> group = widget.</span><span>group</span><span>(</span><span>"CIR Filtering Parameters"</span><span>))
</span>        {
<span>            group.</span><span>text</span><span>(</span><span>"Path Length Filtering:"</span><span>);
</span><span>            group.</span><span>var</span><span>(</span><span>"Min Path Length (m)"</span><span>, mCIRMinPathLength, </span><span>0.1f</span><span>, </span><span>500.0f</span><span>, </span><span>0.1f</span><span>);
</span><span>            group.</span><span>tooltip</span><span>(</span><span>"Minimum path length for CIR data filtering (meters)"</span><span>);
</span>
<span>            group.</span><span>var</span><span>(</span><span>"Max Path Length (m)"</span><span>, mCIRMaxPathLength, </span><span>1.0f</span><span>, </span><span>1000.0f</span><span>, </span><span>1.0f</span><span>);
</span><span>            group.</span><span>tooltip</span><span>(</span><span>"Maximum path length for CIR data filtering (meters)"</span><span>);
</span>
<span>            group.</span><span>text</span><span>(</span><span>"Emitted Power Filtering:"</span><span>);
</span><span>            group.</span><span>var</span><span>(</span><span>"Min Emitted Power (W)"</span><span>, mCIRMinEmittedPower, </span><span>0.0f</span><span>, </span><span>100.0f</span><span>, </span><span>0.01f</span><span>);
</span><span>            group.</span><span>tooltip</span><span>(</span><span>"Minimum emitted power for CIR data filtering (watts)"</span><span>);
</span>
<span>            group.</span><span>var</span><span>(</span><span>"Max Emitted Power (W)"</span><span>, mCIRMaxEmittedPower, </span><span>1.0f</span><span>, </span><span>50000.0f</span><span>, </span><span>1.0f</span><span>);
</span><span>            group.</span><span>tooltip</span><span>(</span><span>"Maximum emitted power for CIR data filtering (watts)"</span><span>);
</span>
<span>            group.</span><span>text</span><span>(</span><span>"Angle Filtering:"</span><span>);
</span><span>            group.</span><span>var</span><span>(</span><span>"Min Angle (rad)"</span><span>, mCIRMinAngle, </span><span>0.0f</span><span>, </span><span>3.14159f</span><span>, </span><span>0.01f</span><span>);
</span><span>            group.</span><span>tooltip</span><span>(</span><span>"Minimum angle for emission/reception filtering (radians)"</span><span>);
</span>
<span>            group.</span><span>var</span><span>(</span><span>"Max Angle (rad)"</span><span>, mCIRMaxAngle, </span><span>0.0f</span><span>, </span><span>3.14159f</span><span>, </span><span>0.01f</span><span>);
</span><span>            group.</span><span>tooltip</span><span>(</span><span>"Maximum angle for emission/reception filtering (radians)"</span><span>);
</span>
<span>            group.</span><span>text</span><span>(</span><span>"Reflectance Filtering:"</span><span>);
</span><span>            group.</span><span>var</span><span>(</span><span>"Min Reflectance"</span><span>, mCIRMinReflectance, </span><span>0.0f</span><span>, </span><span>1.0f</span><span>, </span><span>0.01f</span><span>);
</span><span>            group.</span><span>tooltip</span><span>(</span><span>"Minimum reflectance product for CIR data filtering"</span><span>);
</span>
<span>            group.</span><span>var</span><span>(</span><span>"Max Reflectance"</span><span>, mCIRMaxReflectance, </span><span>0.0f</span><span>, </span><span>1.0f</span><span>, </span><span>0.01f</span><span>);
</span><span>            group.</span><span>tooltip</span><span>(</span><span>"Maximum reflectance product for CIR data filtering"</span><span>);
</span>
<span></span><span>// 添加重置按钮  </span><span>
</span><span></span><span>if</span><span> (group.</span><span>button</span><span>(</span><span>"Reset to Defaults"</span><span>))
</span>            {
<span>                mCIRMinPathLength = </span><span>1.0f</span><span>;
</span><span>                mCIRMaxPathLength = </span><span>200.0f</span><span>;
</span><span>                mCIRMinEmittedPower = </span><span>0.0f</span><span>;
</span><span>                mCIRMaxEmittedPower = </span><span>10000.0f</span><span>;
</span><span>                mCIRMinAngle = </span><span>0.0f</span><span>;
</span><span>                mCIRMaxAngle = </span><span>3.14159f</span><span>;
</span><span>                mCIRMinReflectance = </span><span>0.0f</span><span>;
</span><span>                mCIRMaxReflectance = </span><span>1.0f</span><span>;
</span>            }
        }
    }

<span></span><span>// 现有的统计显示代码...  </span><span>
</span>}</code></pre></pre>

## 5. 添加参数保存/加载功能（可选）

为了让用户的设置能够持久化，你可以添加参数保存和加载功能：

<pre class="px-2 py-1.5 has-[code]:rounded-md has-[code]:!bg-[#e5e5e5] has-[div]:bg-transparent has-[div]:!p-0 has-[code]:text-stone-900 dark:has-[code]:!bg-[#242424] has-[code]:dark:text-white [&_code]:block [&_code]:border-none [&_code]:bg-transparent [&_code]:p-0"><pre><code><span>// 在PixelStats类中添加方法  </span><span>
</span><span></span>voidsaveCIRFilteringParams(const std::string& filename)<span>;
</span><span></span>voidloadCIRFilteringParams(const std::string& filename)<span>;</span></code></pre></pre>

## 实现特点

1. **可输入滑条** ：基于 的ImGui实现，这些控件既支持滑动调节，也支持直接数值输入。
2. **实时生效** ：参数修改后立即在下一帧的CIR数据过滤中生效。
3. **工具提示** ：每个参数都有详细的说明，帮助用户理解其作用。
4. **合理范围** ：为每个参数设置了合理的最小值和最大值范围。
5. **重置功能** ：提供重置按钮，方便用户恢复默认设置。

这个实现方案完全基于Falcor现有的UI框架模式，确保了代码的一致性和可维护性。用户可以通过直观的UI界面实时调整CIR过滤参数，无需重新编译代码。
