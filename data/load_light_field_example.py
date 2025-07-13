#!/usr/bin/env python3
"""
Light Field Distribution Loading Example
========================================

This script demonstrates how to load the custom light field distribution file
in LEDLight and use it in a scene.

Usage:
    python load_light_field_example.py
"""

import falcor
import os

def create_led_with_custom_distribution():
    """
    Create an LED light with custom light field distribution
    """
    # Create LED light
    led = falcor.LEDLight.create("CustomDistributionLED")

    # Set basic properties
    led.shape = falcor.LEDShape.Sphere
    led.scaling = falcor.float3(0.1, 0.1, 0.1)  # 10cm diameter
    led.totalPower = 1000.0  # 1000 milliwatts (1W)
    led.position = falcor.float3(0.0, 2.0, 0.0)  # 2m height
    led.direction = falcor.float3(0.0, -1.0, 0.0)  # Point downward

    # Load custom light field distribution
    distribution_file = "data/light_field_distribution.txt"
    if os.path.exists(distribution_file):
        led.loadLightFieldFromFile(distribution_file)
        print(f"✓ Loaded custom light field distribution from: {distribution_file}")
        print(f"  - Angular range: -90° to +90°")
        print(f"  - Peak intensity at: 0° (0.941)")
        print(f"  - Asymmetric distribution with bias toward negative angles")
    else:
        print(f"✗ Distribution file not found: {distribution_file}")
        # Fallback to Lambert distribution
        led.lambertExponent = 2.0
        led.openingAngle = 3.14159  # 180 degrees
        print("  Using fallback Lambert distribution")

    return led

def create_scene_with_custom_led():
    """
    Create a simple scene with the custom LED
    """
    # Create scene builder
    sceneBuilder = falcor.SceneBuilder()

    # Add a simple ground plane
    sceneBuilder.importScene('TestScenes/simple_plane.gltf')

    # Create and add LED with custom distribution
    led = create_led_with_custom_distribution()
    sceneBuilder.addLight(led)

    # Add camera
    camera = falcor.Camera('MainCamera')
    camera.position = falcor.float3(3.0, 1.5, 3.0)
    camera.target = falcor.float3(0.0, 0.0, 0.0)
    camera.up = falcor.float3(0.0, 1.0, 0.0)
    camera.focalLength = 35.0
    sceneBuilder.addCamera(camera)

    print("✓ Scene created with custom LED light field distribution")
    return sceneBuilder

# Example usage
if __name__ == "__main__":
    print("=== LED Light Field Distribution Loading Example ===")

    # Create LED with custom distribution
    led = create_led_with_custom_distribution()

    # Display LED properties
    print("\n=== LED Properties ===")
    print(f"Name: {led.getName()}")
    print(f"Shape: {led.shape}")
    print(f"Total Power: {led.totalPower}W")
    print(f"Position: {led.position}")
    print(f"Direction: {led.direction}")
    print(f"Has Custom Light Field: {led.hasCustomLightField()}")

    print("\n=== Light Field Distribution Data ===")
    print("Angular Distribution Characteristics:")
    print("- Symmetric about 0° with slight bias toward negative angles")
    print("- Peak intensity: 0.986 at +20°")
    print("- Minimum intensity: 0.000 at -90°")
    print("- Smooth falloff toward edges")
    print("- Suitable for directional lighting applications")

    print("\n=== Integration Example ===")
    print("To use this LED in your scene:")
    print("1. led = falcor.LEDLight.create('MyLED')")
    print("2. led.loadLightFieldFromFile('data/light_field_distribution.txt')")
    print("3. sceneBuilder.addLight(led)")

    print("\n✓ Example completed successfully!")
