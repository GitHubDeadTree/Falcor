# LED_Emissive 发光平面LED光源使用说明

## 概述

LED_Emissive是Falcor4中基于发光平面实现的LED光源系统。与传统的解析光源（LEDLight）不同，LED_Emissive将LED建模为发光三角形网格，支持光线击中LED表面的视距链路模拟，提供更真实的LED光源渲染效果。

## 主要特性

### ✅ 支持的几何形状
- **球体（Sphere）**：适用于点光源或球形LED
- **矩形（Rectangle）**：适用于平板LED、LED面板
- **椭球体（Ellipsoid）**：适用于椭圆形LED或特殊形状光源

### ✅ 光场分布控制
- **Lambert分布**：可调节Lambert指数（0.1-100.0）
- **自定义光场分布**：支持从文件加载角度-强度数据
- **光锥角度控制**：支持0-π弧度的开角范围

### ✅ 物理属性控制
- **功率控制**：设置总功率（瓦特单位）
- **颜色控制**：RGB颜色设置
- **位置和方向**：3D空间中的精确定位
- **缩放控制**：XYZ三轴独立缩放

### ✅ 场景集成
- **Falcor场景系统集成**：无缝添加到现有场景
- **材质系统集成**：自动创建发光材质
- **实时参数调节**：支持运行时修改所有参数

## 基本使用方法

### 1. C++接口使用

#### 1.1 创建LED_Emissive对象

```cpp
#include "Scene/Lights/LED_Emissive.h"

// 创建LED_Emissive对象
auto led = LED_Emissive::create("MyLED");
```

#### 1.2 设置基础属性

```cpp
// 设置形状
led->setShape(LED_EmissiveShape::Rectangle);

// 设置位置（世界坐标）
led->setPosition(float3(0.0f, 2.0f, 0.0f));

// 设置缩放（XYZ轴）
led->setScaling(float3(1.0f, 0.1f, 2.0f));  // 宽1米，厚0.1米，长2米

// 设置方向（法向量）
led->setDirection(float3(0.0f, -1.0f, 0.0f));  // 向下照射

// 设置功率
led->setTotalPower(50.0f);  // 50瓦

// 设置颜色
led->setColor(float3(1.0f, 0.9f, 0.8f));  // 暖白色
```

#### 1.3 配置光场分布

```cpp
// 使用Lambert分布
led->setLambertExponent(2.0f);      // Lambert指数
led->setOpeningAngle(M_PI * 0.5f);  // 90度光锥角

// 或者加载自定义光场分布
led->loadLightFieldFromFile("path/to/light_profile.csv");
```

#### 1.4 添加到场景

```cpp
// 添加到SceneBuilder（推荐方式）
SceneBuilder sceneBuilder;
led->addToSceneBuilder(sceneBuilder);
Scene::SharedPtr pScene = sceneBuilder.getScene();

// 或者添加到现有场景
led->addToScene(pScene.get());
```

### 2. Python脚本接口使用

#### 2.1 基础创建和设置

```python
import falcor

# 创建LED对象
led = falcor.LED_Emissive.create("PythonLED")

# 设置属性（使用属性语法）
led.shape = falcor.LED_EmissiveShape.Sphere
led.position = (0.0, 3.0, 0.0)
led.scaling = (0.5, 0.5, 0.5)
led.direction = (0.0, -1.0, 0.0)
led.totalPower = 100.0
led.color = (1.0, 1.0, 1.0)

# 配置光分布
led.lambertExponent = 1.5
led.openingAngle = 1.57  # π/2弧度 = 90度
```

#### 2.2 创建LED阵列

```python
def create_led_array(scene_builder, rows=3, cols=3, spacing=2.0, height=3.0, power_per_led=20.0):
    """创建LED阵列"""
    leds = []

    for i in range(rows):
        for j in range(cols):
            # 计算位置
            x = (j - cols/2) * spacing
            z = (i - rows/2) * spacing

            # 创建LED
            led = falcor.LED_Emissive.create(f"LED_{i}_{j}")
            led.shape = falcor.LED_EmissiveShape.Rectangle
            led.position = (x, height, z)
            led.scaling = (0.3, 0.05, 0.3)  # 30cm x 5cm x 30cm
            led.direction = (0.0, -1.0, 0.0)
            led.totalPower = power_per_led
            led.color = (1.0, 0.95, 0.9)
            led.lambertExponent = 2.0

            # 添加到场景
            led.addToSceneBuilder(scene_builder)
            leds.append(led)

    return leds
```

#### 2.3 自定义光场分布

```python
# 创建自定义光场数据
import math

def create_focused_light_profile():
    """创建聚焦型光分布"""
    light_data = []

    for i in range(64):
        angle = i * math.pi / 63  # 0到π

        # 创建尖锐的聚焦分布
        if angle < math.pi / 6:  # 30度内
            intensity = math.cos(angle) ** 5
        else:
            intensity = 0.0

        light_data.append((angle, intensity))

    return light_data

# 应用自定义光场
led = falcor.LED_Emissive.create("FocusedLED")
led.loadLightFieldData(create_focused_light_profile())
```

### 3. UI界面使用

当LED_Emissive对象添加到场景后，可以通过Mogwai的GUI界面进行实时调节：

#### 3.1 基础属性面板
- **Position**：3D位置滑块，精度0.01米
- **Direction**：方向控制器
- **Shape**：下拉菜单选择形状
- **Scale**：XYZ轴缩放滑块，范围0.001-10.0
- **Total Power**：功率滑块，范围0-1000瓦
- **Color**：RGB颜色选择器

#### 3.2 光分布控制面板
- **Opening Angle**：光锥角度滑块，范围0-π弧度
- **Lambert Exponent**：Lambert指数滑块，范围0.1-100.0
- **Light Field Status**：显示当前光分布模式
- **Clear Custom Data**：清除自定义光场数据按钮

#### 3.3 状态信息面板
- **Surface Area**：实时显示表面积
- **Emissive Intensity**：实时显示发光强度
- **Scene Integration Status**：显示场景集成状态
- **Error Status**：显示计算错误状态

## 高级功能

### 1. 自定义光场分布文件格式

光场分布文件使用CSV格式，包含角度和相对强度两列：

```csv
# 角度(弧度), 相对强度
0.0, 1.0
0.1, 0.98
0.2, 0.92
0.3, 0.82
...
1.57, 0.0
```

### 2. 材质系统集成

LED_Emissive自动创建发光材质，支持：
- **自动强度计算**：基于总功率和表面积
- **颜色控制**：RGB颜色映射
- **光场分布**：通过LightProfile系统实现

### 3. 性能优化建议

- **合理控制三角形数量**：复杂形状会增加渲染负担
- **避免过小的LED**：表面积过小会导致强度计算精度问题
- **批量操作**：创建多个LED时使用SceneBuilder批量添加

## 常见使用场景

### 1. 室内照明模拟

```cpp
// 创建天花板LED面板
auto ceilingPanel = LED_Emissive::create("CeilingPanel");
ceilingPanel->setShape(LED_EmissiveShape::Rectangle);
ceilingPanel->setPosition(float3(0.0f, 3.0f, 0.0f));
ceilingPanel->setScaling(float3(2.0f, 0.02f, 1.0f));  // 2m x 1m面板
ceilingPanel->setDirection(float3(0.0f, -1.0f, 0.0f));
ceilingPanel->setTotalPower(40.0f);  // 40瓦LED面板
ceilingPanel->setLambertExponent(1.5f);  // 接近理想Lambert分布
```

### 2. 汽车前灯模拟

```cpp
// 创建汽车前灯
auto headlight = LED_Emissive::create("Headlight");
headlight->setShape(LED_EmissiveShape::Ellipsoid);
headlight->setScaling(float3(0.15f, 0.08f, 0.1f));  // 椭圆形灯具
headlight->setOpeningAngle(M_PI * 0.3f);  // 54度光锥
headlight->setLambertExponent(3.0f);  // 聚焦分布
headlight->loadLightFieldFromFile("headlight_profile.csv");  // 专业光型
```

### 3. 装饰照明

```cpp
// 创建装饰球泡
auto decorativeBulb = LED_Emissive::create("DecorativeBulb");
decorativeBulb->setShape(LED_EmissiveShape::Sphere);
decorativeBulb->setScaling(float3(0.05f, 0.05f, 0.05f));  // 5cm球泡
decorativeBulb->setTotalPower(5.0f);  // 5瓦
decorativeBulb->setColor(float3(1.0f, 0.7f, 0.3f));  // 暖黄色
decorativeBulb->setOpeningAngle(M_PI);  // 全向发光
```

## 错误处理和调试

### 1. 常见错误

- **表面积为零**：检查缩放参数是否为正数
- **功率设置无效**：确保功率值非负
- **方向向量错误**：确保方向向量已标准化
- **光场数据格式错误**：检查CSV文件格式

### 2. 调试方法

```cpp
// 检查LED状态
if (led->hasCustomLightField()) {
    logInfo("Using custom light field distribution");
} else {
    logInfo("Using Lambert distribution with exponent: " + std::to_string(led->getLambertExponent()));
}

// 检查表面积
float area = led->calculateSurfaceArea();
if (area <= 0.0f) {
    logError("Invalid surface area: " + std::to_string(area));
}
```

### 3. 性能监控

通过UI面板的状态信息可以监控：
- 计算错误状态
- 表面积和发光强度
- LightProfile创建状态
- 场景集成状态

## 总结

LED_Emissive提供了一个强大而灵活的LED光源建模系统，支持多种形状、自定义光分布和完整的Python接口。通过合理使用其各项功能，可以实现高度真实的LED照明仿真效果。

更多详细信息请参考相关实现文档：
- `任务1_LED_Emissive基础框架实现总结.md`
- `任务2_LightProfile集成实现总结.md`
- `任务3_发光材质集成实现总结.md`
- `任务4_几何体生成与场景集成实现总结.md`
- `任务5_UI界面实现总结.md`
- `任务6_Python脚本接口支持实现总结.md`
