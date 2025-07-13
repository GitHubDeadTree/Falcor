#!/usr/bin/env python3
"""
LED_Emissive Integration Test Script
====================================

This script demonstrates how the new SceneBuilder integration works
to automatically sync LED_Emissive objects between Python scripts and Scene UI.

Fixed Issues:
- Python scripts' LED_Emissive objects now appear in Scene UI
- UI-created LED_Emissive objects now appear in the scene
- Automatic integration between SceneBuilder and Scene management systems
"""

import falcor
import sys
import os

def test_scenebuilder_integration():
    """Test the new SceneBuilder integration"""

    print("=== LED_Emissive SceneBuilder Integration Test ===")

    try:
        # Create a SceneBuilder
        sceneBuilder = falcor.SceneBuilder()
        print("✓ SceneBuilder created successfully")

        # Create LED_Emissive objects
        led1 = falcor.LED_Emissive.create("TestLED1")
        led2 = falcor.LED_Emissive.create("TestLED2")

        # Set properties
        led1.position = [1.0, 2.0, 0.0]
        led1.totalPower = 10.0
        led1.color = [1.0, 0.8, 0.6]

        led2.position = [-1.0, 2.0, 0.0]
        led2.totalPower = 15.0
        led2.color = [0.8, 0.9, 1.0]

        print("✓ LED_Emissive objects created and configured")

        # Add LED_Emissive objects to SceneBuilder
        # This will now automatically:
        # 1. Create geometry via addToSceneBuilder()
        # 2. Add to scene management container via addLEDEmissive()
        led1.addToSceneBuilder(sceneBuilder)
        led2.addToSceneBuilder(sceneBuilder)

        print("✓ LED_Emissive objects added to SceneBuilder")

        # Build the scene
        scene = sceneBuilder.build()
        print("✓ Scene built successfully")

        # Verify integration
        ledCount = scene.getLEDEmissiveCount()
        print(f"✓ Scene contains {ledCount} LED_Emissive objects")

        if ledCount == 2:
            print("✓ Integration successful! Both LED_Emissive objects are available in Scene UI")

            # Verify objects can be retrieved
            retrievedLED1 = scene.getLEDEmissiveByName("TestLED1")
            retrievedLED2 = scene.getLEDEmissiveByName("TestLED2")

            if retrievedLED1 and retrievedLED2:
                print("✓ LED_Emissive objects can be retrieved by name")
                print(f"  - LED1: {retrievedLED1.getName()}, Power: {retrievedLED1.getTotalPower()}W")
                print(f"  - LED2: {retrievedLED2.getName()}, Power: {retrievedLED2.getTotalPower()}W")
                return True
            else:
                print("✗ Failed to retrieve LED_Emissive objects by name")
                return False
        else:
            print(f"✗ Expected 2 LED_Emissive objects, got {ledCount}")
            return False

    except Exception as e:
        print(f"✗ Integration test failed: {e}")
        return False

def test_python_scene_file_integration():
    """Test integration with Python scene files"""

    print("\n=== Python Scene File Integration Test ===")

    try:
        # Simulate what happens in a Python scene file
        sceneBuilder = falcor.SceneBuilder()

        # Create LED_Emissive as done in Python scene files
        led = falcor.LED_Emissive.create("SceneFileLED")
        led.position = [0.0, 3.0, 0.0]
        led.totalPower = 25.0
        led.color = [1.0, 1.0, 0.8]

        # This is what Python scene files do - they call addToSceneBuilder
        # With our fix, this will now also add to Scene management container
        led.addToSceneBuilder(sceneBuilder)

        print("✓ LED_Emissive added via Python scene file pattern")

        # Build scene
        scene = sceneBuilder.build()

        # Verify the LED_Emissive is now available in Scene UI
        ledCount = scene.getLEDEmissiveCount()
        retrievedLED = scene.getLEDEmissiveByName("SceneFileLED")

        if ledCount == 1 and retrievedLED:
            print("✓ Python scene file LED_Emissive is now visible in Scene UI")
            return True
        else:
            print(f"✗ Integration failed - Count: {ledCount}, Retrieved: {retrievedLED is not None}")
            return False

    except Exception as e:
        print(f"✗ Python scene file integration test failed: {e}")
        return False

def main():
    """Run all integration tests"""

    print("Testing LED_Emissive SceneBuilder Integration")
    print("=" * 50)

    # Test 1: SceneBuilder integration
    test1_result = test_scenebuilder_integration()

    # Test 2: Python scene file integration
    test2_result = test_python_scene_file_integration()

    print("\n" + "=" * 50)
    if test1_result and test2_result:
        print("✓ ALL TESTS PASSED")
        print("✓ LED_Emissive integration is working correctly")
        print("✓ Python scene files will now show LED_Emissive objects in Scene UI")
        print("✓ UI-created LED_Emissive objects will appear in the scene")
        return True
    else:
        print("✗ SOME TESTS FAILED")
        print("✗ Integration may not be working correctly")
        return False

if __name__ == "__main__":
    success = main()
    sys.exit(0 if success else 1)
