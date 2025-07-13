# Light Field Distribution Files

This directory contains light field distribution files that can be loaded into LEDLight for custom angular light distributions.

## Files

### 1. `light_field_distribution.txt`
- **Format**: Simple text format (angle in radians, intensity)
- **Usage**: Direct loading into LEDLight
- **Range**: -90° to +90° (-1.5708 to +1.5708 radians)
- **Data Points**: 37 samples

### 2. `light_field_distribution.ies`
- **Format**: IES LM-63-2002 standard format
- **Usage**: Industry standard photometric data
- **Range**: -90° to +90°
- **Data Points**: 37 samples

### 3. `load_light_field_example.py`
- **Format**: Python example script
- **Usage**: Demonstrates how to load and use the distribution files

## Distribution Characteristics

| Property | Value |
|----------|-------|
| Angular Range | -90° to +90° |
| Peak Intensity | 0.986 at +20° |
| Center Intensity | 0.941 at 0° |
| Minimum Intensity | 0.000 at -90° |
| Distribution Type | Asymmetric with slight negative bias |

## Usage Examples

### Python Script Usage
```python
import falcor

# Create LED light
led = falcor.LEDLight.create("CustomLED")

# Load from simple text format
led.loadLightFieldFromFile("data/light_field_distribution.txt")

# Or load from IES format
led.loadLightFieldFromFile("data/light_field_distribution.ies")

# Set other properties
led.shape = falcor.LEDShape.Sphere
led.totalPower = 1000.0  # 1W
led.position = falcor.float3(0.0, 2.0, 0.0)
led.direction = falcor.float3(0.0, -1.0, 0.0)

# Add to scene
sceneBuilder.addLight(led)
```

### PyScene Usage
```python
# In a .pyscene file
led = LEDLight.create('CustomDistributionLED')
led.loadLightFieldFromFile('data/light_field_distribution.txt')
led.shape = LEDShape.Sphere
led.totalPower = 1000.0
led.position = float3(0.0, 2.0, 0.0)
led.direction = float3(0.0, -1.0, 0.0)
sceneBuilder.addLight(led)
```

## Data Format Details

### Simple Text Format (.txt)
```
# Comments start with #
# Format: angle_in_radians intensity
-1.5708 0.000
-1.4835 0.029
...
0.0000 0.941
...
1.5708 0.029
```

### IES Format (.ies)
Standard IES LM-63-2002 format with:
- Header information
- Lamp parameters (1 lamp, 1000 lumens)
- 37 vertical angles (-90° to +90°)
- 1 horizontal angle (0°)
- Corresponding candela values

## Integration with LEDLight

The LEDLight class automatically:
- ✅ Reads both formats
- ✅ Normalizes intensity values
- ✅ Uploads data to GPU buffers
- ✅ Applies distribution during rendering
- ✅ Supports real-time parameter changes

## Applications

This distribution is suitable for:
- Directional lighting with controlled beam spread
- Asymmetric lighting patterns
- Architectural lighting simulation
- Automotive lighting systems
- Street lighting applications

## Notes

- The distribution shows peak intensity at +20°, making it suitable for slightly forward-biased lighting
- The smooth falloff toward edges provides realistic light distribution
- Both file formats contain identical data, choose based on your workflow preferences
- The IES format is recommended for professional lighting applications
