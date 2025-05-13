add_plugin(IrradiancePass)

target_sources(IrradiancePass PRIVATE
    IrradiancePass.cpp
    IrradiancePass.h
    IrradiancePass.cs.slang
)

target_copy_shaders(IrradiancePass RenderPasses/IrradiancePass)

target_source_group(IrradiancePass "RenderPasses")
