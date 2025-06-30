#!/usr/bin/env python3
"""
LED_Emissive Python Interface Example
=====================================

This script demonstrates how to use the LED_Emissive Python interface to create and control LED light sources.

Usage:
    python led_emissive_example.py
"""

import falcor
import os

def create_spherical_led_at_position(scene, name, position, power, light_profile_file=None):
    """
    Create a spherical LED at specified position and load light distribution file

    Args:
        scene: Falcor scene object
        name: LED name
        position: [x, y, z] position coordinates
        power: Power in watts
        light_profile_file: Light distribution file path (optional)

    Returns:
        LED_Emissive object or None if failed
    """
    try:
        # Create LED_Emissive object
        led = falcor.LED_Emissive.create(name)

        # Set shape to sphere
        led.shape = falcor.LED_EmissiveShape.Sphere

        # Set position
        led.position = falcor.float3(position[0], position[1], position[2])

        # Set power
        led.totalPower = power

        # Set scaling (sphere radius)
        led.scaling = falcor.float3(0.01, 0.01, 0.01)  # 1cm radius

        # Set white color
        led.color = falcor.float3(1.0, 1.0, 1.0)

        # Load light distribution file if provided
        if light_profile_file and os.path.exists(light_profile_file):
            led.loadLightFieldFromFile(light_profile_file)
            print(f"Loaded light profile from: {light_profile_file}")
        else:
            # Use default Lambert distribution
            led.lambertExponent = 1.0
            led.openingAngle = 3.14159  # π radians (180 degrees)
            print(f"Using default Lambert distribution")

        # Add to scene
        led.addToScene(scene)

        print(f"Successfully created spherical LED '{name}' at {position}")
        print(f"  Power: {power}W")
        print(f"  Has custom light field: {led.hasCustomLightField}")

        return led

    except Exception as e:
        print(f"Error creating LED: {e}")
        return None

def create_rectangular_led_array(scene, center_position, array_size, spacing, power_per_led):
    """
    Create a rectangular array of LED lights

    Args:
        scene: Falcor scene object
        center_position: [x, y, z] center position of the array
        array_size: [rows, columns] number of LEDs
        spacing: Distance between LEDs
        power_per_led: Power per LED in watts

    Returns:
        List of LED_Emissive objects
    """
    leds = []
    rows, cols = array_size

    # Calculate starting position
    start_x = center_position[0] - (cols - 1) * spacing / 2.0
    start_y = center_position[1] - (rows - 1) * spacing / 2.0
    start_z = center_position[2]

    for row in range(rows):
        for col in range(cols):
            # Calculate position
            x = start_x + col * spacing
            y = start_y + row * spacing
            z = start_z

            # Create LED name
            led_name = f"LED_Array_{row}_{col}"

            # Create LED
            led = create_spherical_led_at_position(
                scene=scene,
                name=led_name,
                position=[x, y, z],
                power=power_per_led
            )

            if led:
                leds.append(led)

    print(f"Created LED array: {len(leds)} LEDs in {rows}x{cols} configuration")
    return leds

def create_custom_led_with_light_field(scene, name, position, power):
    """
    Create LED with custom light field distribution

    Args:
        scene: Falcor scene object
        name: LED name
        position: [x, y, z] position coordinates
        power: Power in watts

    Returns:
        LED_Emissive object or None if failed
    """
    try:
        led = falcor.LED_Emissive.create(name)

        # Set basic properties
        led.shape = falcor.LED_EmissiveShape.Ellipsoid
        led.position = falcor.float3(position[0], position[1], position[2])
        led.totalPower = power
        led.scaling = falcor.float3(0.008, 0.008, 0.012)  # Slightly elongated

        # Create custom light field data (angle, intensity pairs)
        light_field_data = []
        import math

        # Generate asymmetric light distribution
        for i in range(64):
            angle = float(i) / 63.0 * math.pi  # 0 to π

            # Custom intensity distribution (example: forward-biased)
            if angle < math.pi / 3:  # 0 to 60 degrees
                intensity = math.cos(angle) ** 2
            elif angle < 2 * math.pi / 3:  # 60 to 120 degrees
                intensity = 0.5 * math.cos(angle)
            else:  # 120 to 180 degrees
                intensity = 0.1

            light_field_data.append([angle, max(0.0, intensity)])

        # Load custom light field
        led.loadLightFieldData(light_field_data)

        # Add to scene
        led.addToScene(scene)

        print(f"Created custom LED '{name}' with asymmetric light distribution")
        print(f"  Shape: Ellipsoid")
        print(f"  Has custom light field: {led.hasCustomLightField}")

        return led

    except Exception as e:
        print(f"Error creating custom LED: {e}")
        return None

def setup_led_lighting_demo(scene):
    """
    Set up a complete LED lighting demonstration

    Args:
        scene: Falcor scene object

    Returns:
        Dictionary containing all created LEDs
    """
    print("Setting up LED lighting demonstration...")

    led_objects = {}

    # 1. Create single spherical LED
    led_objects['main_led'] = create_spherical_led_at_position(
        scene=scene,
        name="MainLED",
        position=[0, 0, 5],
        power=2.0
    )

    # 2. Create LED array
    led_objects['led_array'] = create_rectangular_led_array(
        scene=scene,
        center_position=[3, 0, 5],
        array_size=[3, 3],  # 3x3 array
        spacing=0.5,
        power_per_led=0.5
    )

    # 3. Create custom LED with asymmetric distribution
    led_objects['custom_led'] = create_custom_led_with_light_field(
        scene=scene,
        name="CustomLED",
        position=[-3, 0, 5],
        power=1.5
    )

    # 4. Create rectangular LED panel
    try:
        panel_led = falcor.LED_Emissive.create("PanelLED")
        panel_led.shape = falcor.LED_EmissiveShape.Rectangle
        panel_led.position = falcor.float3(0, 3, 5)
        panel_led.scaling = falcor.float3(1.0, 0.1, 0.5)  # Wide panel
        panel_led.totalPower = 5.0
        panel_led.lambertExponent = 2.0  # More focused beam
        panel_led.openingAngle = 1.57  # 90 degrees
        panel_led.addToScene(scene)

        led_objects['panel_led'] = panel_led
        print("Created rectangular LED panel")

    except Exception as e:
        print(f"Error creating panel LED: {e}")

    print(f"LED lighting demo setup complete!")
    print(f"Total LED objects created: {len([led for led in led_objects.values() if led is not None])}")

    return led_objects

def generate_light_profile_csv(filename, distribution_type="lambert"):
    """
    Generate a light profile CSV file for testing

    Args:
        filename: Output CSV filename
        distribution_type: "lambert", "focused", or "wide"
    """
    import math

    with open(filename, 'w') as f:
        f.write("# angle(radians), intensity(normalized)\n")

        for i in range(64):
            angle = float(i) / 63.0 * math.pi  # 0 to π

            if distribution_type == "lambert":
                intensity = math.cos(angle)
            elif distribution_type == "focused":
                intensity = math.cos(angle) ** 4 if angle < math.pi/2 else 0.0
            elif distribution_type == "wide":
                intensity = 0.8 if angle < 2*math.pi/3 else 0.1
            else:
                intensity = math.cos(angle)

            intensity = max(0.0, intensity)
            f.write(f"{angle:.6f}, {intensity:.6f}\n")

    print(f"Generated light profile: {filename} ({distribution_type})")

# Example usage when script is run directly
if __name__ == "__main__":
    print("LED_Emissive Python Interface Example")
    print("=====================================")
    print("")
    print("This script demonstrates the LED_Emissive Python interface.")
    print("To use this script with a Falcor scene:")
    print("")
    print("1. Load a scene in Falcor/Mogwai")
    print("2. Import this module: import led_emissive_example")
    print("3. Call setup function: led_emissive_example.setup_led_lighting_demo(scene)")
    print("")
    print("Available functions:")
    print("- create_spherical_led_at_position()")
    print("- create_rectangular_led_array()")
    print("- create_custom_led_with_light_field()")
    print("- setup_led_lighting_demo()")
    print("")

    # Generate example light profile files
    try:
        os.makedirs("data/light_profiles", exist_ok=True)
        generate_light_profile_csv("data/light_profiles/lambert_profile.csv", "lambert")
        generate_light_profile_csv("data/light_profiles/focused_profile.csv", "focused")
        generate_light_profile_csv("data/light_profiles/wide_profile.csv", "wide")
        print("Generated example light profile files in data/light_profiles/")
    except Exception as e:
        print(f"Note: Could not create example files: {e}")
