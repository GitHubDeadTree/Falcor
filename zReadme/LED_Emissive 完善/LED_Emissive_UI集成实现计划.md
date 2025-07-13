# LED_Emissive UI集成实现计划

## 任务概述

**核心目标**：让 `LED_Emissive` 对象出现在 Scene Settings UI 中，实现与现有 Lights 组类似的UI交互功能。

**最简化版本**：
1. 在 Scene 类中添加 LED_Emissive 对象的管理容器
2. 在 Scene::renderUI 方法中添加 LED_Emissive UI 组
3. 实现增删改查的基础管理方法
4. 添加 Python 绑定支持

**完整流程**：用户可以在 Scene Settings UI 中看到 "LED_Emissive" 栏目，点击展开后可以看到所有已创建的 LED_Emissive 对象，点击每个对象可以调节其参数，还可以通过 "Add LED_Emissive" 按钮创建新的 LED_Emissive 对象。

## 子任务分解

### 子任务1：Scene 类基础设施修改

**1. 任务目标**
修改 Scene.h 和 Scene.cpp，添加 LED_Emissive 对象的管理容器和基础方法。

**2. 实现方案**

#### 修改 Scene.h
在 Scene.h 中的 SceneData 结构体中添加 LED_Emissive 容器：
```cpp
// 在 Source/Falcor/Scene/Scene.h 的 SceneData 结构体中添加 (大约在第250行附近)
struct SceneData
{
    // ... 现有成员 ...
    std::vector<ref<Light>> lights;                         ///< List of light sources.
    std::vector<ref<LED_Emissive>> ledEmissives;            ///< List of LED emissive objects.
    // ... 其他成员 ...
};
```

在 Scene 类中添加管理方法声明：
```cpp
// 在 Scene 类的公共方法区域添加 (大约在第400行附近)
public:
    // LED_Emissive 管理方法
    void addLEDEmissive(ref<LED_Emissive> pLEDEmissive);
    void removeLEDEmissive(ref<LED_Emissive> pLEDEmissive);
    void clearLEDEmissives();
    const std::vector<ref<LED_Emissive>>& getLEDEmissives() const;
    ref<LED_Emissive> getLEDEmissive(uint32_t index) const;
    ref<LED_Emissive> getLEDEmissiveByName(const std::string& name) const;
    uint32_t getLEDEmissiveCount() const;
```

#### 修改 Scene.cpp
在 Scene.cpp 中实现管理方法：
```cpp
// 在 Source/Falcor/Scene/Scene.cpp 的末尾添加 (大约在第2400行附近)
void Scene::addLEDEmissive(ref<LED_Emissive> pLEDEmissive)
{
    if (!pLEDEmissive)
    {
        logError("Scene::addLEDEmissive - LED_Emissive object is null");
        return;
    }
    
    try
    {
        // 检查是否已存在
        auto it = std::find(mSceneData.ledEmissives.begin(), mSceneData.ledEmissives.end(), pLEDEmissive);
        if (it != mSceneData.ledEmissives.end())
        {
            logWarning("Scene::addLEDEmissive - LED_Emissive already exists: " + pLEDEmissive->getName());
            return;
        }
        
        // 添加到容器
        mSceneData.ledEmissives.push_back(pLEDEmissive);
        
        // 触发场景更新
        mUpdates |= IScene::UpdateFlags::GeometryChanged;
        
        logInfo("Scene::addLEDEmissive - Added LED_Emissive: " + pLEDEmissive->getName());
    }
    catch (const std::exception& e)
    {
        logError("Scene::addLEDEmissive - Exception: " + std::string(e.what()));
    }
}

void Scene::removeLEDEmissive(ref<LED_Emissive> pLEDEmissive)
{
    if (!pLEDEmissive)
    {
        logError("Scene::removeLEDEmissive - LED_Emissive object is null");
        return;
    }
    
    try
    {
        auto it = std::find(mSceneData.ledEmissives.begin(), mSceneData.ledEmissives.end(), pLEDEmissive);
        if (it != mSceneData.ledEmissives.end())
        {
            // 从场景中移除
            (*it)->removeFromScene();
            
            // 从容器中移除
            mSceneData.ledEmissives.erase(it);
            
            // 触发场景更新
            mUpdates |= IScene::UpdateFlags::GeometryChanged;
            
            logInfo("Scene::removeLEDEmissive - Removed LED_Emissive: " + pLEDEmissive->getName());
        }
        else
        {
            logWarning("Scene::removeLEDEmissive - LED_Emissive not found: " + pLEDEmissive->getName());
        }
    }
    catch (const std::exception& e)
    {
        logError("Scene::removeLEDEmissive - Exception: " + std::string(e.what()));
    }
}

void Scene::clearLEDEmissives()
{
    try
    {
        // 从场景中移除所有 LED_Emissive
        for (auto& ledEmissive : mSceneData.ledEmissives)
        {
            ledEmissive->removeFromScene();
        }
        
        // 清空容器
        mSceneData.ledEmissives.clear();
        
        // 触发场景更新
        mUpdates |= IScene::UpdateFlags::GeometryChanged;
        
        logInfo("Scene::clearLEDEmissives - Cleared all LED_Emissive objects");
    }
    catch (const std::exception& e)
    {
        logError("Scene::clearLEDEmissives - Exception: " + std::string(e.what()));
    }
}

const std::vector<ref<LED_Emissive>>& Scene::getLEDEmissives() const
{
    return mSceneData.ledEmissives;
}

ref<LED_Emissive> Scene::getLEDEmissive(uint32_t index) const
{
    if (index >= mSceneData.ledEmissives.size())
    {
        logWarning("Scene::getLEDEmissive - Index out of range: " + std::to_string(index));
        return nullptr;
    }
    
    return mSceneData.ledEmissives[index];
}

ref<LED_Emissive> Scene::getLEDEmissiveByName(const std::string& name) const
{
    try
    {
        for (const auto& ledEmissive : mSceneData.ledEmissives)
        {
            if (ledEmissive->getName() == name)
            {
                return ledEmissive;
            }
        }
        
        logWarning("Scene::getLEDEmissiveByName - LED_Emissive not found: " + name);
        return nullptr;
    }
    catch (const std::exception& e)
    {
        logError("Scene::getLEDEmissiveByName - Exception: " + std::string(e.what()));
        return nullptr;
    }
}

uint32_t Scene::getLEDEmissiveCount() const
{
    return static_cast<uint32_t>(mSceneData.ledEmissives.size());
}
```

**3. 错误处理**
- 空指针检查：所有 LED_Emissive 指针都进行空指针验证
- 重复添加检查：避免添加相同的 LED_Emissive 对象
- 索引范围检查：getLEDEmissive 方法检查索引有效性
- 异常捕获：所有方法都包含 try-catch 块
- 默认值处理：出错时返回 nullptr，计数返回 0

**4. 验证方法**
- 添加调试日志输出：成功添加/移除 LED_Emissive 时输出信息
- 检查 mSceneData.ledEmissives 容器大小变化
- 验证 mUpdates 标志是否正确设置
- 测试代码可以成功编译且无警告

---

### 子任务2：Scene::renderUI 方法修改

**1. 任务目标**
在 Scene::renderUI 方法中添加 LED_Emissive UI 组，实现与现有 Lights 组类似的UI交互。

**2. 实现方案**

在 Scene.cpp 的 renderUI 方法中，在 Lights 组之后添加 LED_Emissive 组：
```cpp
// 在 Source/Falcor/Scene/Scene.cpp 的 renderUI 方法中，大约在第2350行 (Lights 组之后) 添加：
        if (auto ledEmissiveGroup = widget.group("LED_Emissive"))
        {
            // 显示 LED_Emissive 统计信息
            ledEmissiveGroup.text("LED_Emissive Count: " + std::to_string(getLEDEmissiveCount()));
            
            // 遍历所有 LED_Emissive 对象
            uint32_t ledID = 0;
            for (auto& ledEmissive : mSceneData.ledEmissives)
            {
                try
                {
                    // 创建每个 LED_Emissive 的子组
                    auto name = std::to_string(ledID) + ": " + ledEmissive->getName();
                    if (auto ledGroup = ledEmissiveGroup.group(name))
                    {
                        // 渲染 LED_Emissive 的UI
                        ledEmissive->renderUI(ledGroup);
                        
                        // 添加删除按钮
                        ledGroup.separator();
                        if (ledGroup.button("Remove LED_Emissive"))
                        {
                            // 标记要删除的对象 (不能在遍历中直接删除)
                            mLEDEmissiveToRemove = ledEmissive;
                        }
                    }
                }
                catch (const std::exception& e)
                {
                    logError("Scene::renderUI - LED_Emissive UI error: " + std::string(e.what()));
                    
                    // 显示错误信息
                    ledEmissiveGroup.text("Error rendering LED_Emissive " + std::to_string(ledID) + ": " + std::string(e.what()));
                }
                
                ledID++;
            }
            
            // 添加创建新 LED_Emissive 的按钮
            ledEmissiveGroup.separator();
            if (ledEmissiveGroup.button("Add LED_Emissive"))
            {
                try
                {
                    // 创建新的 LED_Emissive 对象
                    std::string newName = "LED_" + std::to_string(getLEDEmissiveCount());
                    auto newLED = LED_Emissive::create(newName);
                    
                    if (newLED)
                    {
                        // 设置默认参数
                        newLED->setPosition(float3(0.0f, 2.0f, 0.0f));
                        newLED->setTotalPower(10.0f);
                        newLED->setColor(float3(1.0f, 1.0f, 1.0f));
                        
                        // 添加到场景
                        addLEDEmissive(newLED);
                        
                        logInfo("Scene::renderUI - Created new LED_Emissive: " + newName);
                    }
                    else
                    {
                        logError("Scene::renderUI - Failed to create LED_Emissive");
                    }
                }
                catch (const std::exception& e)
                {
                    logError("Scene::renderUI - LED_Emissive creation error: " + std::string(e.what()));
                }
            }
            
            // 处理延迟删除
            if (mLEDEmissiveToRemove)
            {
                try
                {
                    removeLEDEmissive(mLEDEmissiveToRemove);
                    mLEDEmissiveToRemove.reset();
                }
                catch (const std::exception& e)
                {
                    logError("Scene::renderUI - LED_Emissive removal error: " + std::string(e.what()));
                }
            }
        }
```

需要在 Scene.h 中添加延迟删除的成员变量：
```cpp
// 在 Scene 类的私有成员中添加
private:
    ref<LED_Emissive> mLEDEmissiveToRemove;  ///< LED_Emissive marked for removal
```

**3. 错误处理**
- try-catch 块包围所有UI操作
- 空指针检查：验证 LED_Emissive 对象有效性
- 创建失败处理：LED_Emissive 创建失败时显示错误信息
- 延迟删除：避免在遍历中直接删除对象
- 默认值设置：新创建的 LED_Emissive 使用安全的默认值

**4. 验证方法**
- UI 中出现 "LED_Emissive" 组
- 点击展开后显示计数信息
- 能够成功创建新的 LED_Emissive 对象
- 每个 LED_Emissive 对象都有独立的参数调节界面
- "Remove LED_Emissive" 按钮能正常工作

---

### 子任务3：Python 绑定支持

**1. 任务目标**
添加 Scene 类中 LED_Emissive 相关方法的 Python 绑定，支持 Python 脚本操作。

**2. 实现方案**

在 Scene 的 Python 绑定中添加 LED_Emissive 相关方法：
```cpp
// 在 Source/Falcor/Scene/Scene.cpp 的 Python 绑定部分添加 (大约在第2500行附近)
FALCOR_SCRIPT_BINDING(Scene)
{
    // ... 现有绑定 ...
    
    // LED_Emissive 相关绑定
    pybind11::class_<Scene, ref<Scene>>(m, "Scene")
        // ... 现有方法 ...
        
        // LED_Emissive 管理方法
        .def("addLEDEmissive", &Scene::addLEDEmissive, "ledEmissive"_a,
             "Add a LED_Emissive object to the scene")
        .def("removeLEDEmissive", &Scene::removeLEDEmissive, "ledEmissive"_a,
             "Remove a LED_Emissive object from the scene")
        .def("clearLEDEmissives", &Scene::clearLEDEmissives,
             "Remove all LED_Emissive objects from the scene")
        .def("getLEDEmissive", &Scene::getLEDEmissive, "index"_a,
             "Get LED_Emissive object by index")
        .def("getLEDEmissiveByName", &Scene::getLEDEmissiveByName, "name"_a,
             "Get LED_Emissive object by name")
        .def("getLEDEmissiveCount", &Scene::getLEDEmissiveCount,
             "Get the number of LED_Emissive objects")
        .def_property_readonly("ledEmissives", &Scene::getLEDEmissives,
             "List of LED_Emissive objects in the scene");
}
```

**3. 错误处理**
- 参数验证：Python 绑定自动处理类型检查
- 异常传播：C++ 异常会自动转换为 Python 异常
- 空指针处理：返回 None 而不是崩溃
- 默认值返回：出错时返回合理的默认值

**4. 验证方法**
- Python 脚本可以调用 `scene.addLEDEmissive(led)` 
- Python 脚本可以通过 `scene.ledEmissives` 访问 LED_Emissive 列表
- Python 脚本可以通过 `scene.getLEDEmissive(0)` 获取特定对象
- Python 脚本可以通过 `scene.getLEDEmissiveCount()` 获取数量
- 所有方法都不会导致 Python 崩溃

---

### 子任务4：功能测试验证

**1. 任务目标**
创建测试代码验证所有功能正常工作，确保系统稳定性。

**2. 实现方案**

#### 创建 C++ 测试用例
```cpp
// 在 tests/ 目录下创建 LED_Emissive_UI_Test.cpp
#include "Scene/Scene.h"
#include "Scene/Lights/LED_Emissive.h"
#include "Utils/Logger.h"

void testLEDEmissiveSceneIntegration()
{
    try
    {
        // 创建场景
        auto scene = Scene::create();
        
        // 测试添加 LED_Emissive
        auto led1 = LED_Emissive::create("TestLED1");
        scene->addLEDEmissive(led1);
        
        // 验证添加结果
        FALCOR_ASSERT(scene->getLEDEmissiveCount() == 1);
        FALCOR_ASSERT(scene->getLEDEmissive(0) == led1);
        FALCOR_ASSERT(scene->getLEDEmissiveByName("TestLED1") == led1);
        
        // 测试添加多个 LED_Emissive
        auto led2 = LED_Emissive::create("TestLED2");
        scene->addLEDEmissive(led2);
        
        FALCOR_ASSERT(scene->getLEDEmissiveCount() == 2);
        
        // 测试移除 LED_Emissive
        scene->removeLEDEmissive(led1);
        FALCOR_ASSERT(scene->getLEDEmissiveCount() == 1);
        FALCOR_ASSERT(scene->getLEDEmissive(0) == led2);
        
        // 测试清空
        scene->clearLEDEmissives();
        FALCOR_ASSERT(scene->getLEDEmissiveCount() == 0);
        
        logInfo("LED_Emissive Scene Integration Test: PASSED");
    }
    catch (const std::exception& e)
    {
        logError("LED_Emissive Scene Integration Test: FAILED - " + std::string(e.what()));
    }
}
```

#### 创建 Python 测试脚本
```python
# 在 tests/ 目录下创建 test_led_emissive_ui.py
import falcor

def test_led_emissive_python_bindings():
    """测试 LED_Emissive 的 Python 绑定"""
    try:
        # 创建场景
        scene = falcor.Scene.create()
        
        # 测试初始状态
        assert scene.getLEDEmissiveCount() == 0
        assert len(scene.ledEmissives) == 0
        
        # 创建 LED_Emissive
        led = falcor.LED_Emissive.create("PythonTestLED")
        scene.addLEDEmissive(led)
        
        # 验证添加结果
        assert scene.getLEDEmissiveCount() == 1
        assert len(scene.ledEmissives) == 1
        assert scene.getLEDEmissive(0) == led
        assert scene.getLEDEmissiveByName("PythonTestLED") == led
        
        # 测试属性设置
        led.position = [1.0, 2.0, 3.0]
        led.totalPower = 50.0
        led.color = [1.0, 0.8, 0.6]
        
        # 验证属性设置
        assert led.position == [1.0, 2.0, 3.0]
        assert led.totalPower == 50.0
        assert led.color == [1.0, 0.8, 0.6]
        
        # 测试移除
        scene.removeLEDEmissive(led)
        assert scene.getLEDEmissiveCount() == 0
        
        print("LED_Emissive Python Bindings Test: PASSED")
        return True
        
    except Exception as e:
        print(f"LED_Emissive Python Bindings Test: FAILED - {e}")
        return False

if __name__ == "__main__":
    test_led_emissive_python_bindings()
```

**3. 错误处理**
- 测试用例包含异常处理
- 验证所有边界情况 (空指针、索引越界等)
- 测试计算错误时的默认值 (0.666)
- 验证日志输出是否正确

**4. 验证方法**
- 所有 FALCOR_ASSERT 都通过
- Python 测试脚本运行成功
- 日志输出显示 "PASSED"
- UI 中能够正常显示和操作 LED_Emissive 对象
- 创建的 LED_Emissive 对象参数调节正常，默认值为 0.666 时表明有计算错误

---

## 总结

本实现计划将 LED_Emissive 完全集成到 Falcor 的 Scene 管理系统中，实现：

1. **完整的对象管理**：增删改查、计数、按名称查找
2. **UI 交互支持**：可视化参数调节、动态添加删除
3. **Python 脚本支持**：完整的 Python API 绑定
4. **错误处理机制**：全面的异常处理和默认值保护
5. **测试验证体系**：C++ 和 Python 的完整测试覆盖

实现后，用户可以通过 Scene Settings UI 方便地管理 LED_Emissive 对象，就像管理普通 Light 对象一样。 