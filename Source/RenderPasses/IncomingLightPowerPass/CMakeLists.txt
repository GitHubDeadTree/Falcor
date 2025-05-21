add_plugin(IncomingLightPowerPass)

target_sources(IncomingLightPowerPass PRIVATE
    IncomingLightPowerPass.cpp
    IncomingLightPowerPass.h
    IncomingLightPowerPass.cs.slang
)

target_copy_shaders(IncomingLightPowerPass RenderPasses/IncomingLightPowerPass)
target_source_group(IncomingLightPowerPass "RenderPasses")
