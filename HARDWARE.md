# Track Rat - Hardware Shopping List

## Complete Bill of Materials

### Core Components (Required)

| Component | Specification | Approx. Price | Notes |
|-----------|--------------|---------------|-------|
| Raspberry Pi | Pi 4 (2GB+) or Pi 3B+ | $35-55 | Pi Zero W works but slower |
| microSD Card | 16GB+ Class 10 | $8-15 | SanDisk or Samsung recommended |
| GPS Module | USB GPS (BU-353S4) | $30-40 | UART GPS (NEO-6M) cheaper (~$10) |
| Accelerometer | MPU6050 (GY-521) | $3-8 | I2C 6-axis sensor |
| OLED Display | SSD1306 128x64 I2C | $5-10 | Blue or white display |
| Power Supply | 5V 2.5A+ USB-C/micro | $8-12 | Or use USB power bank |

**Total Cost (approx): $90-140**

### Wiring & Mounting

| Component | Price | Notes |
|-----------|-------|-------|
| Jumper Wires | $5 | Female-female for connections |
| Breadboard (optional) | $5 | For testing before soldering |
| Case/Enclosure | $10-20 | Protect from elements |
| Velcro/Mounting Tape | $5 | Secure mounting in vehicle |
| Heat Sinks | $3-5 | For Raspberry Pi cooling |

**Accessories Total: $28-40**

### Optional Upgrades

| Component | Price | Purpose |
|-----------|-------|---------|
| GPS External Antenna | $15-25 | Better signal in car |
| Real-Time Clock (DS3231) | $5-8 | Accurate time without GPS |
| Buttons/Switches | $5-10 | Manual lap marking, mode switching |
| Larger Display (3.5" TFT) | $20-35 | More detailed information |
| Battery Pack / UPS | $20-40 | Clean shutdown, portability |
| Waterproof Case | $15-30 | Outdoor track protection |

## Detailed Component Specifications

### 1. Raspberry Pi Options

**Recommended: Raspberry Pi 4 (2GB or 4GB)**
- Best performance
- USB 3.0 ports
- More RAM for future features
- Better heat management

**Budget Option: Raspberry Pi 3B+**
- Adequate performance
- Lower power consumption
- Cheaper
- Proven reliability

**Compact Option: Raspberry Pi Zero W**
- Very small form factor
- Limited USB ports (need adapter)
- Slower but works fine
- Great for permanent installations

### 2. GPS Module Options

**Option A: USB GPS (Recommended for Beginners)**
- **Model**: GlobalSat BU-353S4
- **Pros**: Plug-and-play, reliable, good accuracy
- **Cons**: More expensive, takes USB port
- **Where**: Amazon, eBay
- **Alternative**: VK-162 USB GPS

**Option B: UART GPS Module**
- **Model**: NEO-6M or NEO-7M
- **Pros**: Cheaper, smaller, programmable
- **Cons**: Requires GPIO setup, may need level shifter
- **Where**: AliExpress, Amazon
- **Alternative**: PA6H, PA1010D

**GPS Antenna Considerations:**
- Active antenna better for in-car use
- Mount with clear sky view
- Avoid metal surfaces
- External antenna if mounting inside dashboard

### 3. Accelerometer Options

**Recommended: MPU6050**
- 3-axis accelerometer + 3-axis gyroscope
- 16-bit ADC resolution
- I2C interface
- Built-in temperature sensor
- Available as GY-521 breakout board
- Most common, lots of library support

**Alternatives:**
- **ADXL345**: Digital accelerometer, good sensitivity
- **MPU9250**: 9-axis (includes magnetometer)
- **LSM6DS3**: Low power, high precision

### 4. Display Options

**Option A: SSD1306 OLED (Recommended)**
- 0.96" or 1.3" diagonal
- 128x64 pixels
- I2C or SPI interface (get I2C)
- High contrast, visible in sunlight
- Low power consumption
- Colors: Blue, White, or Blue/Yellow split

**Option B: Larger Displays**
- **2.8" TFT LCD (ILI9341)**: More information, color
- **3.5" TFT**: Even more space
- **E-Paper**: Readable in all conditions
- Requires SPI, more power, more code

**Option C: Console Only**
- Use SSH or HDMI for testing
- No cost, but not practical in car

### 5. Power Supply Options

**Option A: USB Power Bank**
- **Capacity**: 10,000mAh+ recommended
- **Output**: 5V 2.4A minimum
- **Pros**: Portable, battery backup
- **Cons**: Must remember to charge

**Option B: Car USB Adapter**
- **Output**: 5V 2.5A+ minimum
- **Quality**: Use good brand (Anker, Aukey)
- **Pros**: Powered from car
- **Cons**: No protection from sudden power loss

**Option C: UPS HAT**
- Models: PiJuice, Geekworm UPS
- Built-in battery and charging
- Clean shutdown on power loss
- More expensive but safest

## Where to Buy

### Online Retailers

**USA:**
- Adafruit: https://www.adafruit.com
- SparkFun: https://www.sparkfun.com
- Amazon: https://www.amazon.com
- Micro Center: https://www.microcenter.com

**International:**
- AliExpress: Cheapest, 2-4 week shipping
- eBay: Mix of vendors
- Banggood: Good for bulk orders
- Local electronics suppliers

### Budget-Saving Tips

1. **Buy Kits**: Sensor kits often cheaper than individual components
2. **AliExpress**: Much cheaper but longer shipping
3. **Used Pi**: Raspberry Pi 3B works great and often available used
4. **Skip Optional**: Start basic, add features later
5. **Clone Modules**: Generic MPU6050/SSD1306 work as well as branded

## Recommended Starter Kit ($100-120)

For someone starting from scratch:

1. Raspberry Pi 4 (2GB) - $35
2. microSD Card (32GB) - $10
3. USB GPS (BU-353S4) - $35
4. MPU6050 Module - $5
5. SSD1306 OLED Display - $8
6. Jumper Wires Set - $5
7. USB-C Power Supply - $10
8. Basic Case - $12

**Total: ~$120**

## Advanced Setup ($200+)

For the full-featured experience:

1. Raspberry Pi 4 (4GB) - $55
2. High-quality microSD (64GB) - $15
3. USB GPS with external antenna - $45
4. MPU6050 Module - $5
5. 3.5" TFT Display - $30
6. Real-Time Clock - $8
7. UPS HAT with battery - $40
8. Waterproof case - $25
9. Accessories/wiring - $15

**Total: ~$238**

## Tools Needed

Basic tools for assembly:

- Small Phillips screwdriver
- Wire strippers (if soldering)
- Soldering iron (optional, for permanent connections)
- Multimeter (helpful for debugging)
- Computer for initial setup

## Assembly Time

- **Basic Setup**: 1-2 hours
- **With Soldering**: 3-4 hours
- **Professional Install**: 6-8 hours

## Wiring Diagram

```
Raspberry Pi GPIO Pinout (Relevant Pins):

     3.3V  (1) (2)  5V
I2C1 SDA  (3) (4)  5V
I2C1 SCL  (5) (6)  GND
          (7) (8)  UART TX
      GND  (9) (10) UART RX
         (11) (12)
         (13) (14) GND
         (15) (16)
     3.3V (17) (18)
         (19) (20) GND

Connections:
- MPU6050: SDA→3, SCL→5, VCC→1, GND→6
- OLED: SDA→3, SCL→5, VCC→17, GND→20
- GPS (USB): Any USB port
- GPS (UART): TX→10, RX→8, VCC→2, GND→6
```

## Mounting Recommendations

### In Car

1. **Location**:
   - Secure mounting point
   - GPS with sky view
   - Easy to see display
   - Away from airbags

2. **Methods**:
   - Velcro (removable)
   - 3M mounting tape
   - Custom bracket
   - Dashboard mount

3. **Considerations**:
   - Temperature (Pi can get hot)
   - Vibration dampening
   - Wire management
   - Quick removal for security

## Safety Notes

⚠️ **Important Safety Information:**

1. **Installation**: Never install while vehicle is running
2. **Wiring**: Ensure all connections are secure and insulated
3. **Power**: Use appropriate fuses if hardwiring
4. **Distraction**: Set up before driving, don't adjust while moving
5. **Track Use**: This is designed for closed-course track use
6. **Heat**: Monitor Pi temperature in hot cars
7. **Mounting**: Ensure nothing can become a projectile

## Warranty & Support

- Most components have 30-day to 1-year warranties
- Keep receipts and packaging
- Test components individually before assembly
- Check vendor return policies

## Future Expansion Options

Components to consider later:

- Additional sensors (temperature, tire pressure)
- Data logging storage (USB drive, cloud)
- Wireless connectivity (broadcast to pit)
- Camera integration (dashcam sync)
- CAN bus interface (read car data)
- Bluetooth OBD-II adapter

Enjoy building your Track Rat! 🏁
