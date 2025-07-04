# LEDLight类功能说明文档

## 概述

LEDLight类是Falcor渲染框架中实现的一个新型光源类，专门为LED光源建模而设计。该类继承自Light基类，提供了丰富的几何形状支持、光谱控制、角度衰减修正和外部数据加载功能，可以精确模拟真实LED光源的光学特性。

## 核心功能特性

### 1. 几何形状支持

LEDLight支持三种基本几何形状：

- **球形 (Sphere)**: 最基础的形状，适用于大多数标准LED
- **椭球 (Ellipsoid)**: 支持非对称的椭球形状，可以模拟特殊形状的LED
- **矩形 (Rectangle)**: 用于模拟条形或面板型LED光源

每种形状都支持独立的三轴缩放控制，可以创建各种尺寸比例的光源。

### 2. 朗伯衰减模型

实现了基于朗伯次数的角度衰减控制：

- **朗伯指数 (Lambert Exponent)**: 控制光强随角度的衰减程度
- **开口角度 (Opening Angle)**: 定义光源的有效辐射角度范围
- **角度衰减公式**: `I(θ) = I_unit × cos(θ)^N / (N + 1)`

其中N为朗伯次数，θ为观察角度。

### 3. 功率管理系统

提供了完整的功率和光强度管理：

- **总功率设置**: 以瓦特(W)为单位设置LED的总功率
- **自动光强计算**: 根据功率、表面积和角度分布自动计算光强度
- **功率-光强转换**: `Radiance = Power / (π × SurfaceArea)`

### 4. 光谱数据支持

#### 4.1 光谱数据结构
- 支持自定义光谱分布数据
- 数据格式：波长-强度对 `(wavelength, intensity)`
- 波长单位：纳米(nm)
- 强度单位：相对强度

#### 4.2 光谱采样功能
- 基于累积分布函数(CDF)的重要性采样
- 支持光谱范围查询和统计
- 自动构建采样查找表

### 5. 光场分布支持

#### 5.1 自定义光场数据
- 支持从外部加载实测光场分布数据
- 数据格式：角度-强度对 `(angle, intensity)`
- 角度单位：弧度或度数
- 自动归一化处理

#### 5.2 光场数据应用
- 替代默认朗伯衰减模型
- 支持复杂的非对称光分布
- 实时光强度插值计算

## UI界面控制功能

### 基础光源参数
- **强度 (Intensity)**: 光源基础强度值，支持RGB三通道设置
- **位置 (Position)**: 光源在世界坐标系中的位置
- **方向 (Direction)**: 光源主要辐射方向

### LED特有参数

#### 几何参数
- **形状类型 (Shape Type)**: 下拉菜单选择球形/椭球/矩形
- **缩放 (Scaling)**: 三轴缩放控制，范围0.01-10.0
- **开口角度 (Opening Angle)**: 有效辐射角度，范围1°-180°

#### 光学参数
- **朗伯指数 (Lambert Exponent)**: 角度衰减控制，范围0.1-10.0
- **总功率 (Total Power)**: LED总功率设置，范围0-1000W

#### 数据状态显示
- **光谱状态**: 显示是否加载了自定义光谱数据
- **光场状态**: 显示是否加载了自定义光场分布
- **采样数量**: 显示光谱数据的采样点数量
- **波长范围**: 显示光谱数据的波长覆盖范围

## 外部数据加载功能

### 1. 光谱数据文件加载

#### 支持的文件格式
- **文本文件**: `.txt`, `.dat`, `.csv`等
- **数据格式**: 每行包含两个数值，以空格或制表符分隔
- **注释支持**: 以`#`开头的行将被忽略

#### 文件内容示例
```
# LED光谱数据文件
# 波长(nm) 相对强度
380.0 0.05
400.0 0.12
450.0 0.45
500.0 0.78
550.0 0.92
600.0 0.65
650.0 0.32
700.0 0.15
```

#### 加载方法
```cpp
ledLight->loadSpectrumFromFile("path/to/spectrum_data.txt");
```

### 2. 光场分布文件加载

#### 支持的文件格式
- **文本文件**: `.txt`, `.dat`, `.csv`等
- **数据格式**: 每行包含角度和对应的光强度值
- **注释支持**: 以`#`开头的行将被忽略

#### 文件内容示例
```
# LED光场分布数据
# 角度(度) 相对强度
0.0   1.00
15.0  0.95
30.0  0.85
45.0  0.70
60.0  0.50
75.0  0.25
90.0  0.05
```

#### 加载方法
```cpp
ledLight->loadLightFieldFromFile("path/to/lightfield_data.txt");
```

### 3. 程序化数据加载

#### 光谱数据
```cpp
std::vector<float2> spectrumData = {
    {380.0f, 0.1f},  // 380nm, 10%强度
    {450.0f, 0.8f},  // 450nm, 80%强度
    {650.0f, 0.3f}   // 650nm, 30%强度
};
ledLight->loadSpectrumData(spectrumData);
```

#### 光场数据
```cpp
std::vector<float2> lightFieldData = {
    {0.0f, 1.0f},     // 0度, 100%强度
    {M_PI/4, 0.7f},   // 45度, 70%强度
    {M_PI/2, 0.1f}    // 90度, 10%强度
};
ledLight->loadLightFieldData(lightFieldData);
```

## 数据处理和优化

### 光谱数据处理
1. **自动排序**: 按波长升序排列数据点
2. **CDF构建**: 构建累积分布函数用于重要性采样
3. **范围计算**: 自动计算波长覆盖范围
4. **归一化**: 可选的数据归一化处理

### 光场数据处理
1. **强度归一化**: 自动将强度值归一化到[0,1]范围
2. **角度验证**: 确保角度值在有效范围内
3. **插值准备**: 为实时插值计算优化数据结构

## 使用示例

### 基础LED光源创建
```cpp
// 创建LED光源
auto ledLight = LEDLight::create("MyLED");

// 设置基础参数
ledLight->setWorldPosition(float3(0, 5, 0));
ledLight->setWorldDirection(float3(0, -1, 0));
ledLight->setLEDShape(LEDLight::LEDShape::Sphere);
ledLight->setScaling(float3(0.5f));

// 设置光学参数
ledLight->setTotalPower(50.0f);  // 50W
ledLight->setLambertExponent(2.0f);
ledLight->setOpeningAngle(math::radians(60.0f));  // 60度开口角
```

### 加载自定义数据
```cpp
// 加载光谱数据
ledLight->loadSpectrumFromFile("data/led_spectrum.txt");

// 加载光场分布
ledLight->loadLightFieldFromFile("data/led_lightfield.txt");

// 验证数据加载状态
if (ledLight->hasCustomSpectrum()) {
    auto range = ledLight->getSpectrumRange();
    std::cout << "光谱范围: " << range.x << "-" << range.y << "nm" << std::endl;
    std::cout << "采样点数: " << ledLight->getSpectrumSampleCount() << std::endl;
}
```

## 技术实现细节

### 几何计算
- **表面积计算**: 根据不同形状自动计算准确的表面积
- **变换矩阵**: 支持复杂的几何变换和缩放
- **法线计算**: 正确计算各种形状的表面法线

### 渲染集成
- **GPU数据传输**: 高效的GPU数据上传和管理
- **着色器支持**: 与Falcor着色器系统深度集成
- **采样优化**: 优化的光源采样算法

### 错误处理
- **数据验证**: 全面的输入数据验证
- **异常处理**: 完善的错误处理和恢复机制
- **日志记录**: 详细的操作日志和调试信息

## 限制和注意事项

1. **文件格式**: 目前仅支持文本格式的数据文件
2. **数据大小**: 建议单个数据文件不超过10000个采样点
3. **波长范围**: 光谱数据建议覆盖380-780nm可见光范围
4. **角度范围**: 光场数据角度应在0-π/2范围内
5. **内存使用**: 大量数据会占用较多GPU内存

## 未来扩展计划

1. **二进制文件支持**: 支持更高效的二进制数据格式
2. **实时数据更新**: 支持运行时动态更新光谱和光场数据
3. **多光谱渲染**: 支持光谱渲染管线集成
4. **IES文件支持**: 支持标准IES光度文件格式
5. **物理单位**: 更完善的物理单位支持和转换

## 版本信息

- **当前版本**: 1.0
- **兼容性**: Falcor 4.x
- **最后更新**: 2024年
- **维护状态**: 活跃开发中
