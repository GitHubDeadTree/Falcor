add_plugin(CIRComputePass)

target_sources(CIRComputePass PRIVATE
    CIRComputePass.cpp
    CIRComputePass.h
    CIRComputePass.cs.slang
)

target_copy_shaders(CIRComputePass RenderPasses/CIRComputePass)
target_source_group(CIRComputePass "RenderPasses") 