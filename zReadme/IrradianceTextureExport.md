# IrradiancePass纹理导出功能实现方案

## 功能概述

本文档详细描述如何在IrradiancePass渲染通道中添加纹理导出功能，以便将计算出的辐照度纹理保存为PNG或EXR文件，方便查看和分析完整图像，而不受Debug窗口显示限制。

## 实现目标

1. 在IrradiancePass的Debug UI中添加纹理保存按钮和路径输入框
2. 实现在指定路径保存当前渲染帧的纹理功能
3. 支持PNG和EXR等常见图像格式
4. 提供清晰的状态反馈和错误处理

## 具体实现步骤

### 1. 修改IrradiancePass.h

在IrradiancePass类中添加必要的成员变量和方法：

```cpp
// 在IrradiancePass.h文件的private部分添加
private:
    // 其他现有成员...

    // 纹理保存功能
    bool mSaveNextFrame = false;            ///< 标记是否需要在下一帧保存纹理
    std::string mSaveFilePath = "Screenshots/irradiance_output.png";  ///< 保存路径

    // 纹理保存辅助方法
    void saveTextureToFile(RenderContext* pRenderContext, const Texture::SharedPtr& pTexture, const std::string& filename);
    bool hasValidImageExtension(const std::string& filename);
```

### 2. 修改IrradiancePass.cpp

#### 2.1 添加必要的头文件

```cpp
#include "Utils/Image/ImageIO.h"
#include <filesystem>
```

#### 2.2 实现hasValidImageExtension辅助函数

```cpp
bool IrradiancePass::hasValidImageExtension(const std::string& filename)
{
    std::string lower = filename;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    return (lower.ends_with(".png") || lower.ends_with(".exr") ||
            lower.ends_with(".jpg") || lower.ends_with(".jpeg") ||
            lower.ends_with(".bmp"));
}
```

#### 2.3 在renderUI方法中添加UI控件

```cpp
void IrradiancePass::renderUI(Gui::Widgets& widget)
{
    // 现有UI代码...

    // 添加纹理保存功能
    widget.separator();
    widget.text("--- Texture Export ---");

    // 添加文件路径输入框
    if (widget.textInput("Save Path", mSaveFilePath))
    {
        // 确保路径有.png或.exr扩展名
        if (!hasValidImageExtension(mSaveFilePath))
        {
            mSaveFilePath += ".png"; // 默认添加PNG扩展名
        }
    }

    if (widget.button("Save Current Texture"))
    {
        mSaveNextFrame = true;
        logInfo("IrradiancePass::renderUI() - Will save texture on next frame to: {}", mSaveFilePath);
    }

    if (mSaveNextFrame)
    {
        widget.text("Status: Will save on next frame", true);
    }
}
```

#### 2.4 修改execute方法，添加纹理保存逻辑

```cpp
void IrradiancePass::execute(RenderContext* pRenderContext, const RenderData& renderData)
{
    // 现有的execute代码...

    // Execute compute pass
    uint32_t width = mOutputResolution.x;
    uint32_t height = mOutputResolution.y;
    mpComputePass->execute(pRenderContext, { (width + 15) / 16, (height + 15) / 16, 1 });

    // 如果需要保存纹理，在渲染完成后执行
    if (mSaveNextFrame)
    {
        // 获取输出纹理
        const auto& pOutputIrradiance = renderData.getTexture(kOutputIrradiance);
        if (pOutputIrradiance)
        {
            // 创建目录（如果不存在）
            std::filesystem::path filePath(mSaveFilePath);
            std::filesystem::create_directories(filePath.parent_path());

            // 保存纹理
            saveTextureToFile(pRenderContext, pOutputIrradiance, mSaveFilePath);
            logInfo("IrradiancePass::execute() - Saved texture to: {}", mSaveFilePath);
        }
        else
        {
            logError("IrradiancePass::execute() - Cannot save texture: output texture is null");
        }

        // 重置保存标志
        mSaveNextFrame = false;
    }
}
```

#### 2.5 实现saveTextureToFile方法

```cpp
void IrradiancePass::saveTextureToFile(RenderContext* pRenderContext, const Texture::SharedPtr& pTexture, const std::string& filename)
{
    // 确保纹理有效
    if (!pTexture)
    {
        logError("IrradiancePass::saveTextureToFile() - Null texture provided");
        return;
    }

    try
    {
        // 创建一个临时纹理，避免修改原始纹理
        Texture::SharedPtr pStagingTexture = Texture::create2D(
            mpDevice,
            pTexture->getWidth(),
            pTexture->getHeight(),
            pTexture->getFormat(),
            1, 1, nullptr,
            ResourceBindFlags::None
        );

        // 复制数据
        pRenderContext->copyResource(pStagingTexture.get(), pTexture.get());

        // 等待复制完成
        pRenderContext->flush(true);

        // 根据文件扩展名决定保存格式
        std::string ext = std::filesystem::path(filename).extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

        // 保存到文件
        if (ext == ".exr")
        {
            // 保存为EXR（保留HDR信息）
            Bitmap::SharedPtr pBitmap = pStagingTexture->captureToSurface(0, 0);
            pBitmap->saveToFile(filename, Bitmap::FileFormat::ExrFile);
        }
        else
        {
            // 保存为PNG或其他格式
            Bitmap::SharedPtr pBitmap = pStagingTexture->captureToSurface(0, 0);
            pBitmap->saveToFile(filename,
                ext == ".png" ? Bitmap::FileFormat::PngFile :
                ext == ".jpg" || ext == ".jpeg" ? Bitmap::FileFormat::JpgFile :
                Bitmap::FileFormat::PngFile); // 默认使用PNG
        }

        logInfo("IrradiancePass::saveTextureToFile() - Texture saved successfully to {}", filename);
    }
    catch (const std::exception& e)
    {
        logError("IrradiancePass::saveTextureToFile() - Failed to save texture: {}", e.what());
    }
}
```

## 使用方法

实现完成后的使用步骤：

1. 启动Falcor并加载包含IrradiancePass的渲染图
2. 打开IrradiancePass的Debug UI
3. 在"Texture Export"部分找到"Save Path"输入框，设置保存路径
4. 点击"Save Current Texture"按钮
5. 在下一帧渲染完成后，纹理将被保存到指定路径
6. 检查控制台日志，确认保存是否成功

## 注意事项

1. **文件格式**：根据文件扩展名自动选择保存格式
   - .exr: 保存为EXR格式，保留HDR数据
   - .png: 保存为PNG格式（默认）
   - .jpg/.jpeg: 保存为JPEG格式
   - 其他: 默认使用PNG格式

2. **路径创建**：如果保存路径中的目录不存在，会自动创建

3. **性能考虑**：纹理保存操作会在渲染线程上执行，可能导致短暂的性能下降

4. **版本兼容性**：
   - 在较新的Falcor版本中，可能需要使用ImageIO替代Bitmap
   - 某些API可能有所变化，请根据你的Falcor版本调整实现细节

## 扩展建议

1. **自动文件名**：添加自动生成带时间戳的文件名功能
2. **批量保存**：添加连续多帧保存功能，用于创建动画或时间序列
3. **预览功能**：在UI中添加小型预览窗口显示即将保存的内容
4. **格式选择**：添加下拉菜单选择保存格式，而不仅依赖文件扩展名

## 调试提示

如果纹理保存功能不正常工作，请检查：

1. 日志输出中的错误消息
2. 文件路径是否有效且有写入权限
3. 纹理数据是否有效（非空，分辨率正确）
4. Falcor版本对应的API是否与实现匹配

## 可能的版本差异处理

不同版本的Falcor可能有API差异，以下是常见的适配点：

### 较老版本Falcor

```cpp
// Bitmap保存方法
pBitmap->saveToFile(filename, Bitmap::FileFormat::PngFile);
```

### 较新版本Falcor

```cpp
// 使用ImageIO
ImageIO::saveToDisk(pBitmap, filename);
```

请根据你的具体Falcor版本选择适当的实现方式。
