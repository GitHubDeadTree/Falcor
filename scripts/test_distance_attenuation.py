from falcor import *
import time

def test_distance_attenuation():
    """
    Test script to verify distance attenuation fix for direct light hits.
    This test will measure light power at different distances from an emissive surface
    and verify that it follows the inverse square law (1/r^2).
    """

    print("=== Testing Distance Attenuation Fix ===")

    # Create render graph
    g = RenderGraph("Distance Attenuation Test")

    # Create passes with specific settings
    VBufferRT = createPass("VBufferRT", {"samplePattern": "Stratified", "sampleCount": 16, "useAlphaTest": True})
    PathTracer = createPass("PathTracer", {
        "samplesPerPixel": 64,  # Higher sample count for more accurate measurements
        "maxSurfaceBounces": 0,  # Only direct lighting to test the fix
        "maxDiffuseBounces": 0,
        "maxSpecularBounces": 0
    })
    IncomingLightPower = createPass("IncomingLightPowerPass", {
        "minWavelength": 380.0,
        "maxWavelength": 780.0,
        "filterMode": 0,  # Range mode
        "useVisibleSpectrumOnly": True,
        "enableWavelengthFilter": False,  # Disable wavelength filtering for this test
        "pixelAreaScale": 1.0  # Use physical pixel area
    })

    # Add passes to graph
    g.addPass(VBufferRT, "VBufferRT")
    g.addPass(PathTracer, "PathTracer")
    g.addPass(IncomingLightPower, "IncomingLightPower")

    # Connect passes
    g.addEdge("VBufferRT.vbuffer", "PathTracer.vbuffer")
    g.addEdge("VBufferRT.viewW", "PathTracer.viewW")
    g.addEdge("PathTracer.color", "IncomingLightPower.radiance")
    g.addEdge("PathTracer.initialRayInfo", "IncomingLightPower.rayDirection")

    # Mark outputs
    g.markOutput("IncomingLightPower.lightPower")
    g.markOutput("PathTracer.color")

    # Test distances (in meters, assuming scene units are meters)
    test_distances = [1.0, 2.0, 3.0, 4.0, 5.0]
    test_results = []

    for distance in test_distances:
        print(f"\nTesting at distance: {distance}m")

        # Position camera at the specified distance from emissive surface
        # Assuming emissive surface is at origin facing +Z direction
        camera_pos = float3(0.0, 0.0, -distance)  # Move camera back along -Z axis
        camera_target = float3(0.0, 0.0, 0.0)     # Look at origin
        camera_up = float3(0.0, 1.0, 0.0)         # Standard up vector

        # Update camera position
        try:
            scene = m.getScene()
            if scene:
                camera = scene.getCamera()
                camera.setPosition(camera_pos)
                camera.setTarget(camera_target)
                camera.setUpVector(camera_up)

                # Render frame
                m.renderFrame()
                time.sleep(0.1)  # Small delay for frame completion

                # Get power measurement (this would need to be implemented
                # based on how power data is extracted from the render graph)
                # power_value = extract_power_measurement(g)
                # test_results.append((distance, power_value))

                print(f"  Camera positioned at: {camera_pos}")
                print(f"  Looking at: {camera_target}")
                print("  Frame rendered successfully")

        except Exception as e:
            print(f"  Error during test at distance {distance}: {e}")

    # Analyze results
    print("\n=== Test Results Analysis ===")
    print("Expected: Power should decrease as 1/distance²")
    print("If fix is working correctly:")
    print("- Power at 2m should be 1/4 of power at 1m")
    print("- Power at 3m should be 1/9 of power at 1m")
    print("- Power at 4m should be 1/16 of power at 1m")
    print("- Power at 5m should be 1/25 of power at 1m")

    # Note: Actual power extraction and analysis would require additional implementation
    # depending on how the IncomingLightPowerPass exposes its results

    return g

def create_simple_emissive_scene():
    """
    Create a simple scene with just an emissive plane for testing.
    This function should be called to set up the test scene.
    """
    print("Setting up simple emissive scene for distance attenuation test...")
    print("Please ensure you have loaded a scene with:")
    print("1. A single emissive plane at the origin")
    print("2. No other geometry or lights")
    print("3. The emissive plane facing the camera (+Z direction)")

# Create the test graph
test_graph = test_distance_attenuation()

# Instructions for manual testing
print("\n=== Manual Testing Instructions ===")
print("1. Load a simple scene with only an emissive plane")
print("2. Run this script to set up the render graph")
print("3. Manually position the camera at different distances:")
print("   - 1m, 2m, 3m, 4m, 5m from the emissive surface")
print("4. Record the power measurements from IncomingLightPowerPass")
print("5. Verify that power follows 1/r² law:")
print("   - Power(2m) ≈ Power(1m) / 4")
print("   - Power(3m) ≈ Power(1m) / 9")
print("   - Power(4m) ≈ Power(1m) / 16")
print("   - Power(5m) ≈ Power(1m) / 25")

# Try to add to Mogwai if available
try:
    m.addGraph(test_graph)
    print("\nTest graph added to Mogwai successfully!")
except NameError:
    print("\nMogwai not available, but test graph created successfully.")
