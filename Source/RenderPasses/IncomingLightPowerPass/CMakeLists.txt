add_plugin(IncomingLightPowerPass)

target_sources(IncomingLightPowerPass PRIVATE
    IncomingLightPowerPass.cpp
    IncomingLightPowerPass.h
    IncomingLightPowerPass.cs.slang
)

target_source_group(IncomingLightPowerPass "RenderPasses")
