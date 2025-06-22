# CIR过滤参数UI控制实现计划

## 任务目标概述

**最简化版本**: 让CPU端的CIR过滤参数可以通过UI界面控制，将现有硬编码的过滤条件改为可配置的UI参数。

**核心功能**:
1. 在PixelStats类中添加可配置的过滤参数成员变量
2. 修改CIRPathData的isValid()函数，使其接受参数而不是硬编码
3. 修改过滤逻辑使用新的参数化验证函数
4. 在UI中添加控件来实时调整这些参数

## 子任务分解

### 子任务1: 添加可配置过滤参数成员变量

#### 1. 任务目标
在PixelStats类头文件中添加8个可配置的CIR过滤参数成员变量，替换原有硬编码值。

#### 2. 实现方案
**修改文件**: `PixelStats.h`
**修改位置**: 类的private成员变量区域（约274-278行附近）

```cpp
// CIR filtering parameters (configurable via UI)
float mCIRMinPathLength = 1.0f;        ///< Minimum path length for CIR filtering (meters)
float mCIRMaxPathLength = 200.0f;      ///< Maximum path length for CIR filtering (meters)
float mCIRMinEmittedPower = 0.0f;      ///< Minimum emitted power for CIR filtering (watts)
float mCIRMaxEmittedPower = 10000.0f;  ///< Maximum emitted power for CIR filtering (watts)
float mCIRMinAngle = 0.0f;             ///< Minimum angle for CIR filtering (radians)
float mCIRMaxAngle = 3.14159f;         ///< Maximum angle for CIR filtering (radians)
float mCIRMinReflectance = 0.0f;       ///< Minimum reflectance for CIR filtering
float mCIRMaxReflectance = 1.0f;       ///< Maximum reflectance for CIR filtering
```

#### 3. 错误处理
此任务不涉及计算，但需要确保：
- 初始值在合理范围内
- 类型正确（float）
- 命名符合项目规范

#### 4. 验证方法
- 编译通过
- 类中新增8个成员变量
- 初始值符合预期默认值

---

### 子任务2: 修改CIRPathData的isValid函数参数化

#### 1. 任务目标
将CIRPathData结构体的isValid()函数从硬编码验证改为参数化验证，支持动态过滤条件。

#### 2. 实现方案
**修改文件**: `PixelStats.h`
**修改位置**: CIRPathData结构体内的isValid函数（约65-79行）

```cpp
bool isValid(float minPathLength, float maxPathLength,
            float minEmittedPower, float maxEmittedPower,
            float minAngle, float maxAngle,
            float minReflectance, float maxReflectance) const
{
    // Input validation
    if (minPathLength < 0.0f || maxPathLength < minPathLength ||
        minEmittedPower < 0.0f || maxEmittedPower < minEmittedPower ||
        minAngle < 0.0f || maxAngle < minAngle ||
        minReflectance < 0.0f || maxReflectance < minReflectance) {
        logError("CIRPathData::isValid: Invalid parameter ranges");
        return false;  // Default: reject invalid data
    }

    // Data validation
    if (pathLength < 0.0f || emittedPower < 0.0f ||
        emissionAngle < 0.0f || receptionAngle < 0.0f ||
        reflectanceProduct < 0.0f) {
        logWarning("CIRPathData::isValid: Invalid data values detected");
        return false;  // Default: reject invalid data
    }

    // Apply filtering criteria
    bool valid = pathLength >= minPathLength && pathLength <= maxPathLength &&
                emissionAngle >= minAngle && emissionAngle <= maxAngle &&
                receptionAngle >= minAngle && receptionAngle <= maxAngle &&
                reflectanceProduct >= minReflectance && reflectanceProduct <= maxReflectance &&
                emittedPower > minEmittedPower && emittedPower <= maxEmittedPower;

    return valid;
}
```

#### 3. 错误处理
- 参数范围验证：确保最小值 <= 最大值
- 数据值验证：确保数据非负
- 出错时输出错误日志并返回false（默认拒绝）
- 警告信息简练明确

#### 4. 验证方法
- 编译通过
- 函数接受8个参数
- 无效参数时返回false并输出错误日志
- 有效数据按新条件正确过滤

---

### 子任务3: 修改过滤逻辑使用参数化验证

#### 1. 任务目标
修改PixelStats.cpp中的CIR数据过滤逻辑，使用新的参数化isValid函数替代硬编码过滤。

#### 2. 实现方案
**修改文件**: `PixelStats.cpp`
**修改位置**: CIR数据过滤逻辑（约555-560行）

```cpp
// Apply CPU-side filtering with configurable criteria
uint32_t filteredCount = 0;
uint32_t totalCount = static_cast<uint32_t>(rawData.size());

for (size_t i = 0; i < rawData.size(); ++i) {
    try {
        if (rawData[i].isValid(mCIRMinPathLength, mCIRMaxPathLength,
                              mCIRMinEmittedPower, mCIRMaxEmittedPower,
                              mCIRMinAngle, mCIRMaxAngle,
                              mCIRMinReflectance, mCIRMaxReflectance)) {
            mCIRRawData.push_back(rawData[i]);
            filteredCount++;
        }
    } catch (const std::exception& e) {
        logError("CIR filtering error at index {}: {}", i, e.what());
        // Continue processing other data points
        continue;
    }
}

// Log filtering statistics
if (totalCount > 0) {
    float filterRatio = static_cast<float>(filteredCount) / totalCount;
    if (filterRatio < 0.1f) {
        logWarning("CIR filtering: Only {:.1%} of data passed filters ({}/{})",
                  filterRatio, filteredCount, totalCount);
    }
}
```

#### 3. 错误处理
- 异常捕获：处理isValid函数可能的异常
- 统计监控：监控过滤比例，低于10%时警告
- 出错时继续处理其他数据点
- 错误信息包含具体索引位置

#### 4. 验证方法
- 编译通过
- 过滤逻辑调用新的参数化函数
- 过滤统计信息正常输出
- 异常情况下程序不崩溃
- 过滤比例异常时输出警告

---

### 子任务4: 添加UI控件支持参数调整

#### 1. 任务目标
在PixelStats类的renderUI函数中添加UI控件组，允许用户实时调整CIR过滤参数。

#### 2. 实现方案
**修改文件**: `PixelStats.cpp`
**修改位置**: renderUI函数内（约280-338行）

```cpp
void PixelStats::renderUI(Gui::Widgets& widget)
{
    // ... 现有的UI代码 ...

    // Add CIR filtering parameters UI
    if (mEnabled && (mCollectionMode == CollectionMode::RawData ||
                    mCollectionMode == CollectionMode::Both)) {
        if (auto group = widget.group("CIR Filtering Parameters")) {
            try {
                // Path Length Filtering
                group.text("Path Length Filtering:");
                if (group.var("Min Path Length (m)", mCIRMinPathLength, 0.1f, 500.0f, 0.1f)) {
                    // Ensure min <= max
                    if (mCIRMinPathLength > mCIRMaxPathLength) {
                        mCIRMaxPathLength = mCIRMinPathLength;
                        logWarning("CIR UI: Adjusted max path length to match min value");
                    }
                }
                group.tooltip("Minimum path length for CIR data filtering (meters)");

                if (group.var("Max Path Length (m)", mCIRMaxPathLength, 1.0f, 1000.0f, 1.0f)) {
                    // Ensure max >= min
                    if (mCIRMaxPathLength < mCIRMinPathLength) {
                        mCIRMinPathLength = mCIRMaxPathLength;
                        logWarning("CIR UI: Adjusted min path length to match max value");
                    }
                }
                group.tooltip("Maximum path length for CIR data filtering (meters)");

                // Emitted Power Filtering
                group.text("Emitted Power Filtering:");
                if (group.var("Min Emitted Power (W)", mCIRMinEmittedPower, 0.0f, 100.0f, 0.01f)) {
                    if (mCIRMinEmittedPower > mCIRMaxEmittedPower) {
                        mCIRMaxEmittedPower = mCIRMinEmittedPower;
                        logWarning("CIR UI: Adjusted max emitted power to match min value");
                    }
                }
                group.tooltip("Minimum emitted power for CIR data filtering (watts)");

                if (group.var("Max Emitted Power (W)", mCIRMaxEmittedPower, 1.0f, 50000.0f, 1.0f)) {
                    if (mCIRMaxEmittedPower < mCIRMinEmittedPower) {
                        mCIRMinEmittedPower = mCIRMaxEmittedPower;
                        logWarning("CIR UI: Adjusted min emitted power to match max value");
                    }
                }
                group.tooltip("Maximum emitted power for CIR data filtering (watts)");

                // Angle Filtering
                group.text("Angle Filtering:");
                if (group.var("Min Angle (rad)", mCIRMinAngle, 0.0f, 3.14159f, 0.01f)) {
                    if (mCIRMinAngle > mCIRMaxAngle) {
                        mCIRMaxAngle = mCIRMinAngle;
                        logWarning("CIR UI: Adjusted max angle to match min value");
                    }
                }
                group.tooltip("Minimum angle for emission/reception filtering (radians)");

                if (group.var("Max Angle (rad)", mCIRMaxAngle, 0.0f, 3.14159f, 0.01f)) {
                    if (mCIRMaxAngle < mCIRMinAngle) {
                        mCIRMinAngle = mCIRMaxAngle;
                        logWarning("CIR UI: Adjusted min angle to match max value");
                    }
                }
                group.tooltip("Maximum angle for emission/reception filtering (radians)");

                // Reflectance Filtering
                group.text("Reflectance Filtering:");
                if (group.var("Min Reflectance", mCIRMinReflectance, 0.0f, 1.0f, 0.01f)) {
                    if (mCIRMinReflectance > mCIRMaxReflectance) {
                        mCIRMaxReflectance = mCIRMinReflectance;
                        logWarning("CIR UI: Adjusted max reflectance to match min value");
                    }
                }
                group.tooltip("Minimum reflectance product for CIR data filtering");

                if (group.var("Max Reflectance", mCIRMaxReflectance, 0.0f, 1.0f, 0.01f)) {
                    if (mCIRMaxReflectance < mCIRMinReflectance) {
                        mCIRMinReflectance = mCIRMaxReflectance;
                        logWarning("CIR UI: Adjusted min reflectance to match max value");
                    }
                }
                group.tooltip("Maximum reflectance product for CIR data filtering");

                // Reset Button
                if (group.button("Reset to Defaults")) {
                    mCIRMinPathLength = 1.0f;
                    mCIRMaxPathLength = 200.0f;
                    mCIRMinEmittedPower = 0.0f;
                    mCIRMaxEmittedPower = 10000.0f;
                    mCIRMinAngle = 0.0f;
                    mCIRMaxAngle = 3.14159f;
                    mCIRMinReflectance = 0.0f;
                    mCIRMaxReflectance = 1.0f;
                }
            } catch (const std::exception& e) {
                logError("CIR UI rendering error: {}", e.what());
                // UI continues to function with current values
            }
        }
    }

    // ... 现有的统计显示代码 ...
}
```

#### 3. 错误处理
- UI渲染异常捕获：确保UI组件错误不影响整体功能
- 参数一致性检查：确保min <= max
- 自动调整冲突参数并记录警告
- 出错时保持当前参数值继续运行

#### 4. 验证方法
- 编译通过
- UI界面显示CIR过滤参数控件组
- 参数调整时实时生效
- min/max参数冲突时自动调整并警告
- 重置按钮正常工作
- UI异常时程序继续运行

## 整体验证标准

1. **功能完整性**: 所有8个过滤参数都可通过UI调整
2. **实时生效**: 参数修改后立即在下一帧CIR过滤中生效
3. **错误健壮性**: 异常情况下程序不崩溃，有明确错误信息
4. **用户体验**: UI操作直观，有工具提示和重置功能
5. **数据正确性**: 过滤逻辑按新参数正确执行

## 实现顺序

建议按子任务1→2→3→4的顺序实现，每完成一个子任务都进行编译验证，确保不引入编译错误。
