基于您的理论方案和Falcor项目的现有代码架构，我将为您提供LED光源类的详细实现方案：

## 1. 扩展LightType枚举

首先需要在现有的光源类型中添加LED类型。 **LightData.slang:36-44**

需要在枚举中添加：

<pre class="px-2 py-1.5 has-[code]:rounded-md has-[code]:!bg-[#e5e5e5] has-[div]:bg-transparent has-[div]:!p-0 has-[code]:text-stone-900 dark:has-[code]:!bg-[#242424] has-[code]:dark:text-white [&_code]:block [&_code]:border-none [&_code]:bg-transparent [&_code]:p-0"><pre><code>enumclassLightType :<span></span><span>uint32_t</span><span>
</span>{
<span>    Point,          </span><span>///< Point light source, can be a spot light if its opening angle is < 2pi  </span><span>
</span><span>    Directional,    </span><span>///< Directional light source  </span><span>
</span><span>    Distant,        </span><span>///< Distant light that subtends a non-zero solid angle  </span><span>
</span><span>    Rect,           </span><span>///< Quad shaped area light source  </span><span>
</span><span>    Disc,           </span><span>///< Disc shaped area light source  </span><span>
</span><span>    Sphere,         </span><span>///< Spherical area light source  </span><span>
</span><span>    LED,            </span><span>///< LED light source with customizable shape and spectrum  </span><span>
</span>};</code></pre></pre>

## 2. 扩展LightData结构

基于现有的LightData结构， **LightData.slang:48-67**

需要添加LED特有的数据字段：

<pre class="px-2 py-1.5 has-[code]:rounded-md has-[code]:!bg-[#e5e5e5] has-[div]:bg-transparent has-[div]:!p-0 has-[code]:text-stone-900 dark:has-[code]:!bg-[#242424] has-[code]:dark:text-white [&_code]:block [&_code]:border-none [&_code]:bg-transparent [&_code]:p-0"><pre><code>structLightData
{<span>
</span><span></span><span>// 现有字段保持不变...  </span><span>
</span>
<span></span><span>// LED特有字段  </span><span>
</span><span></span><span>float</span><span>    lambertN           = </span><span>1.0f</span><span>;             </span><span>///< Lambert exponent for angular attenuation  </span><span>
</span><span></span><span>float</span><span>    totalPower         = </span><span>0.0f</span><span>;             </span><span>///< Total power of LED in watts  </span><span>
</span><span></span><span>uint32_t</span><span> shapeType          = </span><span>0</span><span>;                </span><span>///< LED geometry shape (0=sphere, 1=ellipsoid, 2=rectangle)  </span><span>
</span><span></span><span>uint32_t</span><span> hasCustomLightField = </span><span>0</span><span>;               </span><span>///< Flag indicating if custom light field data is loaded  </span><span>
</span>
<span></span><span>// Spectrum and light field data pointers (for GPU access)  </span><span>
</span><span></span><span>uint32_t</span><span> spectrumDataOffset = </span><span>0</span><span>;                </span><span>///< Offset to spectrum data in buffer  </span><span>
</span><span></span><span>uint32_t</span><span> lightFieldDataOffset = </span><span>0</span><span>;              </span><span>///< Offset to light field data in buffer  </span><span>
</span><span></span><span>uint32_t</span><span> spectrumDataSize   = </span><span>0</span><span>;                </span><span>///< Size of spectrum data  </span><span>
</span><span></span><span>uint32_t</span><span> lightFieldDataSize = </span><span>0</span><span>;                </span><span>///< Size of light field data  </span><span>
</span>};</code></pre></pre>

## 3. LEDLight类实现

LEDLight类需要继承Light基类并混合PointLight和AnalyticAreaLight的功能。参考现有的光源类架构： **Light.h:147-223** 和 **Light.h:308-346**

<pre class="px-2 py-1.5 has-[code]:rounded-md has-[code]:!bg-[#e5e5e5] has-[div]:bg-transparent has-[div]:!p-0 has-[code]:text-stone-900 dark:has-[code]:!bg-[#242424] has-[code]:dark:text-white [&_code]:block [&_code]:border-none [&_code]:bg-transparent [&_code]:p-0"><pre><code><span>// LEDLight.h  </span><span>
</span><span></span>classFALCOR_APILEDLight :<span></span><span>public</span><span> Light
</span>{
<span></span><span>public</span><span>:
</span><span></span>enumclassLEDShape
    {<span>
</span><span>        Sphere = </span><span>0</span><span>,
</span><span>        Ellipsoid = </span><span>1</span><span>,
</span><span>        Rectangle = </span><span>2</span><span>
</span>    };

<span></span>static ref<LEDLight> create(const std::string& name = "")<span>{ </span><span>return</span><span> make_ref<LEDLight>(name); }
</span>
<span></span><span>LEDLight</span><span>(</span><span>const</span><span> std::string& name);
</span><span>    ~</span><span>LEDLight</span><span>() = </span><span>default</span><span>;
</span>
<span></span><span>// 基础光源接口  </span><span>
</span><span></span>voidrenderUI(Gui::Widgets& widget)override<span>;
</span><span></span>floatgetPower()constoverride<span>;
</span><span></span>voidsetIntensity(const float3& intensity)override<span>;
</span><span></span>voidupdateFromAnimation(const float4x4& transform)override<span>;
</span>
<span></span><span>// LED特有接口  </span><span>
</span><span></span>voidsetLEDShape(LEDShape shape)<span>;
</span><span></span>LEDShape getLEDShape()const<span>{ </span><span>return</span><span> mLEDShape; }
</span>
<span></span>voidsetLambertExponent(float n)<span>;
</span><span></span>floatgetLambertExponent()const<span>{ </span><span>return</span><span> mData.lambertN; }
</span>
<span></span>voidsetTotalPower(float power)<span>;
</span><span></span>floatgetTotalPower()const<span>{ </span><span>return</span><span> mData.totalPower; }
</span>
<span></span><span>// 几何形状和变换（借用AnalyticAreaLight的功能）  </span><span>
</span><span></span>voidsetScaling(float3 scale)<span>;
</span><span></span>float3 getScaling()const<span>{ </span><span>return</span><span> mScaling; }
</span><span></span>voidsetTransformMatrix(const float4x4& mtx)<span>;
</span><span></span>float4x4 getTransformMatrix()const<span>{ </span><span>return</span><span> mTransformMatrix; }
</span>
<span></span><span>// PointLight的角度控制功能  </span><span>
</span><span></span>voidsetOpeningAngle(float openingAngle)<span>;
</span><span></span>floatgetOpeningAngle()const<span>{ </span><span>return</span><span> mData.openingAngle; }
</span><span></span>voidsetWorldDirection(const float3& dir)<span>;
</span><span></span>const float3& getWorldDirection()const<span>{ </span><span>return</span><span> mData.dirW; }
</span>
<span></span><span>// 光谱和光场分布  </span><span>
</span><span></span>voidloadSpectrumData(const std::vector<float2>& spectrumData)<span>;
</span><span></span>voidloadLightFieldData(const std::vector<float2>& lightFieldData)<span>;
</span><span></span>voidclearCustomLightField()<span>;
</span>
<span></span><span>// Python接口支持  </span><span>
</span><span></span>voidloadSpectrumFromFile(const std::string& filePath)<span>;
</span><span></span>voidloadLightFieldFromFile(const std::string& filePath)<span>;
</span>
<span></span><span>private</span><span>:
</span><span></span>voidupdateGeometry()<span>;
</span><span></span>voidupdateIntensityFromPower()<span>;
</span><span></span>floatcalculateSurfaceArea()const<span>;
</span><span></span>float3 calculateIntensityAtAngle(float theta, float lambda = 550.0f)const<span>;
</span>
    LEDShape mLEDShape = LEDShape::Sphere;
<span>    float3 mScaling = </span><span>float3</span><span>(</span><span>1.0f</span><span>);
</span><span>    float4x4 mTransformMatrix = float4x4::</span><span>identity</span><span>();
</span>
<span></span><span>// 光谱和光场数据  </span><span>
</span><span>    std::vector<float2> mSpectrumData;      </span><span>// wavelength, intensity pairs  </span><span>
</span><span>    std::vector<float2> mLightFieldData;    </span><span>// angle, intensity pairs    </span><span>
</span><span></span><span>bool</span><span> mHasCustomSpectrum = </span><span>false</span><span>;
</span><span></span><span>bool</span><span> mHasCustomLightField = </span><span>false</span><span>;
</span>
<span></span><span>// GPU数据缓冲区  </span><span>
</span>    ref<Buffer> mSpectrumBuffer;
    ref<Buffer> mLightFieldBuffer;
};</code></pre></pre>

## 4. 功率计算实现

参考PointLight的功率计算方式： **Light.cpp:450-474**

LEDLight需要实现类似的功率到强度的转换，但要考虑表面积和角度衰减：

<pre class="px-2 py-1.5 has-[code]:rounded-md has-[code]:!bg-[#e5e5e5] has-[div]:bg-transparent has-[div]:!p-0 has-[code]:text-stone-900 dark:has-[code]:!bg-[#242424] has-[code]:dark:text-white [&_code]:block [&_code]:border-none [&_code]:bg-transparent [&_code]:p-0"><pre><code>floatLEDLight::getPower()const
<span>{
</span><span></span><span>if</span><span> (mData.totalPower > </span><span>0.0f</span><span>)
</span>    {
<span></span><span>return</span><span> mData.totalPower;
</span>    }

<span></span><span>// 基于强度计算功率，考虑表面积和角度分布  </span><span>
</span><span></span><span>float</span><span> surfaceArea = </span><span>calculateSurfaceArea</span><span>();
</span><span></span><span>float</span><span> solidAngle = </span><span>2.0f</span><span> * (</span><span>float</span><span>)M_PI * (</span><span>1.0f</span><span> - mData.cosOpeningAngle);
</span><span></span><span>return</span><span></span><span>luminance</span><span>(mData.intensity) * surfaceArea * solidAngle / (mData.lambertN + </span><span>1.0f</span><span>);
</span>}

<span></span>voidLEDLight::setTotalPower(float power)
<span>{
</span>    mData.totalPower = power;
<span></span><span>updateIntensityFromPower</span><span>();
</span>}</code></pre></pre>

## 5. 几何形状支持

参考AnalyticAreaLight的变换处理： **Light.h:313-335**

<pre class="px-2 py-1.5 has-[code]:rounded-md has-[code]:!bg-[#e5e5e5] has-[div]:bg-transparent has-[div]:!p-0 has-[code]:text-stone-900 dark:has-[code]:!bg-[#242424] has-[code]:dark:text-white [&_code]:block [&_code]:border-none [&_code]:bg-transparent [&_code]:p-0"><pre><code>voidLEDLight::updateGeometry()
<span>{
</span><span></span><span>// 更新变换矩阵  </span><span>
</span><span>    float4x4 scaleMat = float4x4::</span><span>scale</span><span>(mScaling);
</span>    mData.transMat = mTransformMatrix * scaleMat;
<span>    mData.transMatIT = </span><span>inverse</span><span>(</span><span>transpose</span><span>(mData.transMat));
</span>
<span></span><span>// 计算表面积  </span><span>
</span><span>    mData.surfaceArea = </span><span>calculateSurfaceArea</span><span>();
</span>
<span></span><span>// 更新切线和副切线向量  </span><span>
</span><span>    mData.tangent = </span><span>normalize</span><span>(</span><span>transformVector</span><span>(mData.transMat, </span><span>float3</span><span>(</span><span>1</span><span>, </span><span>0</span><span>, </span><span>0</span><span>)));
</span><span>    mData.bitangent = </span><span>normalize</span><span>(</span><span>transformVector</span><span>(mData.transMat, </span><span>float3</span><span>(</span><span>0</span><span>, </span><span>1</span><span>, </span><span>0</span><span>)));
</span>}

<span></span>floatLEDLight::calculateSurfaceArea()const
<span>{
</span><span></span><span>switch</span><span> (mLEDShape)
</span>    {
<span></span><span>case</span><span> LEDShape::Sphere:
</span><span></span><span>return</span><span></span><span>4.0f</span><span> * (</span><span>float</span><span>)M_PI * mScaling.x * mScaling.x;
</span><span></span><span>case</span><span> LEDShape::Rectangle:
</span><span></span><span>return</span><span></span><span>2.0f</span><span> * (mScaling.x * mScaling.y + mScaling.y * mScaling.z + mScaling.x * mScaling.z);
</span><span></span><span>case</span><span> LEDShape::Ellipsoid:
</span><span></span><span>// 椭球表面积近似计算  </span><span>
</span><span></span><span>float</span><span> a = mScaling.x, b = mScaling.y, c = mScaling.z;
</span><span></span><span>float</span><span> p = </span><span>1.6075f</span><span>;
</span><span></span><span>return</span><span></span><span>4.0f</span><span> * (</span><span>float</span><span>)M_PI * std::</span><span>pow</span><span>((std::</span><span>pow</span><span>(a*b, p) + std::</span><span>pow</span><span>(b*c, p) + std::</span><span>pow</span><span>(a*c, p)) / </span><span>3.0f</span><span>, </span><span>1.0f</span><span>/p);
</span>    }
<span></span><span>return</span><span></span><span>1.0f</span><span>;
</span>}</code></pre></pre>

## 6. Python绑定扩展

参考现有的Python绑定实现： **Light.cpp:678-746**

<pre class="px-2 py-1.5 has-[code]:rounded-md has-[code]:!bg-[#e5e5e5] has-[div]:bg-transparent has-[div]:!p-0 has-[code]:text-stone-900 dark:has-[code]:!bg-[#242424] has-[code]:dark:text-white [&_code]:block [&_code]:border-none [&_code]:bg-transparent [&_code]:p-0"><pre><code><span>// 在FALCOR_SCRIPT_BINDING(Light)中添加：  </span><span>
</span><span>pybind11::class_<LEDLight, Light, ref<LEDLight>> </span><span>ledLight</span><span>(m, </span><span>"LEDLight"</span><span>);
</span><span>ledLight.</span><span>def</span><span>(pybind11::</span><span>init</span><span>(&LEDLight::create), </span><span>"name"</span><span>_a = </span><span>""</span><span>);
</span><span>ledLight.</span><span>def_property</span><span>(</span><span>"shape"</span><span>, &LEDLight::getLEDShape, &LEDLight::setLEDShape);
</span><span>ledLight.</span><span>def_property</span><span>(</span><span>"lambertExponent"</span><span>, &LEDLight::getLambertExponent, &LEDLight::setLambertExponent);
</span><span>ledLight.</span><span>def_property</span><span>(</span><span>"totalPower"</span><span>, &LEDLight::getTotalPower, &LEDLight::setTotalPower);
</span><span>ledLight.</span><span>def_property</span><span>(</span><span>"scaling"</span><span>, &LEDLight::getScaling, &LEDLight::setScaling);
</span><span>ledLight.</span><span>def_property</span><span>(</span><span>"transform"</span><span>, &LEDLight::getTransformMatrix, &LEDLight::setTransformMatrix);
</span><span>ledLight.</span><span>def_property</span><span>(</span><span>"openingAngle"</span><span>, &LEDLight::getOpeningAngle, &LEDLight::setOpeningAngle);
</span><span>ledLight.</span><span>def_property</span><span>(</span><span>"direction"</span><span>, &LEDLight::getWorldDirection, &LEDLight::setWorldDirection);
</span>
<span></span><span>// 数据加载接口  </span><span>
</span><span>ledLight.</span><span>def</span><span>(</span><span>"loadSpectrumFromFile"</span><span>, &LEDLight::loadSpectrumFromFile);
</span><span>ledLight.</span><span>def</span><span>(</span><span>"loadLightFieldFromFile"</span><span>, &LEDLight::loadLightFieldFromFile);
</span><span>ledLight.</span><span>def</span><span>(</span><span>"clearCustomLightField"</span><span>, &LEDLight::clearCustomLightField);</span></code></pre></pre>

## 7. 着色器端支持

需要在LightHelpers.slang中添加LED光源采样函数：

<pre class="px-2 py-1.5 has-[code]:rounded-md has-[code]:!bg-[#e5e5e5] has-[div]:bg-transparent has-[div]:!p-0 has-[code]:text-stone-900 dark:has-[code]:!bg-[#242424] has-[code]:dark:text-white [&_code]:block [&_code]:border-none [&_code]:bg-transparent [&_code]:p-0"><pre><code><span>// LightHelpers.slang中添加  </span><span>
</span><span></span>boolsampleLEDLight(float3 posW, LightData light, float2 u, out AnalyticLightSample ls)
<span>{
</span><span></span><span>// 基于几何形状采样光源表面  </span><span>
</span><span>    float3 localPos = </span><span>sampleLEDSurface</span><span>(light.geometryType, u);
</span>
<span></span><span>// 变换到世界空间  </span><span>
</span><span>    float3 lightPos = </span><span>transformPoint</span><span>(light.transMat, localPos);
</span>
<span></span><span>// 计算方向和距离  </span><span>
</span>    float3 toLight = lightPos - posW;
<span></span><span>float</span><span> dist = </span><span>length</span><span>(toLight);
</span>    float3 L = toLight / dist;

<span></span><span>// 检查是否在光锥内  </span><span>
</span><span></span><span>float</span><span> cosTheta = </span><span>dot</span><span>(-L, light.dirW);
</span><span></span><span>if</span><span> (cosTheta < light.cosOpeningAngle) </span><span>return</span><span></span><span>false</span><span>;
</span>
<span></span><span>// 计算角度衰减  </span><span>
</span><span></span><span>float</span><span> angleFalloff = </span><span>calculateAngleFalloff</span><span>(light, cosTheta);
</span>
<span></span><span>// 设置采样结果  </span><span>
</span>    ls.posW = lightPos;
<span>    ls.normalW = </span><span>getNormalAtSurface</span><span>(light, localPos);
</span>    ls.dir = L;
    ls.distance = dist;
    ls.Li = light.intensity * angleFalloff;
<span>    ls.pdf = </span><span>1.0f</span><span> / light.surfaceArea;
</span>
<span></span><span>return</span><span></span><span>true</span><span>;
</span>}

<span></span>floatcalculateAngleFalloff(LightData light, float cosTheta)
<span>{
</span><span></span><span>if</span><span> (light.hasLightField > </span><span>0</span><span>)
</span>    {
<span></span><span>// 使用光场分布数据  </span><span>
</span><span></span><span>return</span><span></span><span>sampleLightFieldDistribution</span><span>(light.lightFieldIndex, cosTheta);
</span>    }
<span></span><span>else</span><span>
</span>    {
<span></span><span>// 使用朗伯衰减公式  </span><span>
</span><span></span><span>return</span><span></span><span>pow</span><span>(cosTheta, light.lambertianOrder) / (light.lambertianOrder + </span><span>1.0f</span><span>);
</span>    }
}</code></pre></pre>

## 8. 实时功率调节和UI界面

参考 **Light.cpp:244-293** 的PointLight UI实现，为LED光源添加完整的UI控制：

<pre class="px-2 py-1.5 has-[code]:rounded-md has-[code]:!bg-[#e5e5e5] has-[div]:bg-transparent has-[div]:!p-0 has-[code]:text-stone-900 dark:has-[code]:!bg-[#242424] has-[code]:dark:text-white [&_code]:block [&_code]:border-none [&_code]:bg-transparent [&_code]:p-0"><pre><code>voidLEDLight::renderUI(Gui::Widgets& widget)
<span>{
</span><span>    Light::</span><span>renderUI</span><span>(widget);
</span>
<span></span><span>// 基本属性  </span><span>
</span><span>    widget.</span><span>var</span><span>(</span><span>"World Position"</span><span>, mData.posW, -FLT_MAX, FLT_MAX);
</span><span>    widget.</span><span>direction</span><span>(</span><span>"Direction"</span><span>, mData.dirW);
</span>
<span></span><span>// 几何形状设置  </span><span>
</span><span></span><span>int</span><span> geometryType = (</span><span>int</span><span>)mGeometryType;
</span><span></span><span>if</span><span> (widget.</span><span>dropdown</span><span>(</span><span>"Geometry Type"</span><span>, kGeometryTypeNames, geometryType))
</span>    {
<span></span><span>setGeometryType</span><span>((GeometryType)geometryType);
</span>    }

<span>    widget.</span><span>var</span><span>(</span><span>"Shape Parameters"</span><span>, mShapeParams, </span><span>0.1f</span><span>, </span><span>10.0f</span><span>);
</span>
<span></span><span>// 朗伯次数控制  </span><span>
</span><span></span><span>float</span><span> lambertianOrder = </span><span>getLambertianOrder</span><span>();
</span><span></span><span>if</span><span> (widget.</span><span>var</span><span>(</span><span>"Lambertian Order"</span><span>, lambertianOrder, </span><span>0.1f</span><span>, </span><span>10.0f</span><span>))
</span>    {
<span></span><span>setLambertianOrder</span><span>(lambertianOrder);
</span>    }

<span></span><span>// 功率控制  </span><span>
</span><span>    widget.</span><span>separator</span><span>();
</span><span></span><span>float</span><span> power = </span><span>getPower</span><span>();
</span><span></span><span>if</span><span> (widget.</span><span>var</span><span>(</span><span>"Power (Watts)"</span><span>, power, </span><span>0.0f</span><span>, </span><span>10000.0f</span><span>))
</span>    {
<span></span><span>setPower</span><span>(power);
</span>    }

<span></span><span>// 分布数据控制  </span><span>
</span><span>    widget.</span><span>separator</span><span>();
</span><span></span><span>if</span><span> (widget.</span><span>button</span><span>(</span><span>"Load Light Distribution"</span><span>))
</span>    {
<span></span><span>// 触发文件加载对话框  </span><span>
</span>    }

<span></span><span>if</span><span> (</span><span>hasCustomDistribution</span><span>() && widget.</span><span>button</span><span>(</span><span>"Clear Distribution"</span><span>))
</span>    {
<span></span><span>clearLightDistribution</span><span>();
</span>    }

<span>    widget.</span><span>text</span><span>(</span><span>"Distribution Points: "</span><span> + std::</span><span>to_string</span><span>(mLightDistribution.</span><span>size</span><span>()));
</span>}</code></pre></pre>

# 变量管理说明

对于LED光源，建议采用混合方式：

1. **GPU需要的数据**放在 `LightData`中：
   * `lambertN`（朗伯次数）
   * `shapeType`（几何形状类型）
   * `hasCustomLightField`（是否有自定义光场）
2. **CPU管理的数据**放在 `LEDLight`类中：
   * 光谱数据缓冲区
   * 光场分布数据
   * 文件路径等管理信息

这样既保证了GPU着色器的高效访问，又保持了代码的清晰结构，避免了 **Light.cpp:37** 中提到的 `LightData`大小限制问题。

基于您提供的LED光源变量列表，我来分析哪些变量适合放在 `LightData`结构体中，哪些适合放在 `LEDLight`类的私有成员中。

## 适合放在LightData中的变量

以下变量应该放在 **LightData.slang:48-67** 中，因为GPU着色器需要直接访问：

1. **`lambertN`** - 朗伯次数，GPU计算角度衰减时需要
2. **`totalPower`** - 总功率，GPU计算光强度时需要
3. **`shapeType`** - 几何形状类型，GPU采样和碰撞检测需要
4. **`hasCustomLightField`** - 自定义光场标志，GPU选择计算路径需要
5. **`spectrumDataOffset`** - 光谱数据偏移，GPU访问缓冲区需要
6. **`lightFieldDataOffset`** - 光场数据偏移，GPU访问缓冲区需要
7. **`spectrumDataSize`** - 光谱数据大小，GPU边界检查需要
8. **`lightFieldDataSize`** - 光场数据大小，GPU边界检查需要

## 适合放在LEDLight类中的变量

以下变量应该作为LEDLight类的私有成员，参考 **Light.h:220-223** 和 **Light.h:340-346** 的模式：

1. **`mLEDShape`** - LED形状枚举，CPU管理用
2. **`mScaling`** - 缩放参数，CPU计算后更新到LightData
3. **`mTransformMatrix`** - 变换矩阵，CPU计算后更新到LightData
4. **`mSpectrumData`** - 光谱数据向量，CPU数据管理
5. **`mLightFieldData`** - 光场数据向量，CPU数据管理
6. **`mHasCustomSpectrum`** - 自定义光谱标志，CPU逻辑控制
7. **`mHasCustomLightField`** - 自定义光场标志，CPU逻辑控制
8. **`mSpectrumBuffer`** - GPU缓冲区引用，CPU资源管理
9. **`mLightFieldBuffer`** - GPU缓冲区引用，CPU资源管理

## 设计原理

这种分离遵循Falcor的设计模式：

* `LightData`保持紧凑，只包含GPU直接需要的数据，符合 **Light.cpp:37** 的16字节对齐要求
* 复杂的CPU管理数据（如STL容器、缓冲区引用）放在类的私有成员中
* CPU负责数据处理和缓冲区管理，然后将结果同步到 `LightData`供GPU使用

这样既保证了GPU访问效率，又保持了代码的清晰结构和内存布局的优化。
