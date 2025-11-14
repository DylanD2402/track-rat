# Track Rat - GPS Lap Timer for Raspberry Pi

A comprehensive GPS-based lap timer system for Raspberry Pi that tracks lap times, split times, speed, and G-forces for track racing applications.

## Features

- **GPS Lap Timing**: Automatic lap detection using GPS coordinates
- **Split Times**: Track intermediate split points around the track
- **G-Force Monitoring**: Real-time lateral and longitudinal G-force measurement
- **Speed Tracking**: Current, average, and maximum speed per lap
- **OLED Display**: Live display of lap times and telemetry data
- **Best Lap Tracking**: Automatic detection and display of best lap time
- **Delta Display**: Real-time comparison to best lap
- **Session Summary**: Complete statistics after each session

## Hardware Requirements

### Required Components

1. **Raspberry Pi** (3/4/Zero W recommended)
2. **GPS Module** (USB or UART)
   - Examples: GlobalSat BU-353S4, u-blox NEO-6M/7M/8M
   - Must support NMEA protocol
3. **MPU6050 Accelerometer/Gyroscope** (I2C)
   - 3-axis accelerometer for G-force measurement
   - Available as GY-521 module
4. **SSD1306 OLED Display** (128x64, I2C)
   - For real-time data display
5. **Power Supply**
   - 5V USB power bank or car USB adapter
   - Minimum 2A recommended

### Optional Components

- Case/enclosure for weather protection
- SD card (16GB+ recommended)
- Heat sinks for Raspberry Pi
- External antenna for GPS (for better signal in car)

## Wiring Diagram

### MPU6050 to Raspberry Pi (I2C)
```
MPU6050    ->  Raspberry Pi
VCC        ->  3.3V (Pin 1)
GND        ->  GND (Pin 6)
SCL        ->  GPIO 3 / SCL (Pin 5)
SDA        ->  GPIO 2 / SDA (Pin 3)
```

### SSD1306 OLED to Raspberry Pi (I2C)
```
SSD1306    ->  Raspberry Pi
VCC        ->  3.3V (Pin 17)
GND        ->  GND (Pin 20)
SCL        ->  GPIO 3 / SCL (Pin 5)
SDA        ->  GPIO 2 / SDA (Pin 3)
```

### GPS Module (USB)
```
Simply plug into any USB port on the Raspberry Pi
```

### GPS Module (UART - if not using USB)
```
GPS        ->  Raspberry Pi
VCC        ->  5V (Pin 2)
GND        ->  GND (Pin 6)
TX         ->  RX / GPIO 15 (Pin 10)
RX         ->  TX / GPIO 14 (Pin 8)
```

## Software Installation

### 1. Prepare Raspberry Pi

```bash
# Update system
sudo apt update && sudo apt upgrade -y

# Enable I2C
sudo raspi-config
# Navigate to: Interface Options -> I2C -> Enable

# Install system dependencies
sudo apt install -y python3-pip python3-dev i2c-tools gpsd gpsd-clients python3-gps
```

### 2. Install Track Rat

```bash
# Clone or download Track Rat
cd ~
git clone https://github.com/yourusername/track-rat.git
cd track-rat

# Install Python dependencies
pip3 install -r requirements.txt
```

### 3. Configure GPS Daemon

```bash
# Edit gpsd configuration
sudo nano /etc/default/gpsd

# Set the following (adjust device path if needed):
DEVICES="/dev/ttyUSB0"  # or /dev/ttyAMA0 for UART GPS
GPSD_OPTIONS="-n"
START_DAEMON="true"

# Restart gpsd
sudo systemctl restart gpsd
sudo systemctl enable gpsd

# Test GPS
cgps
# You should see GPS data. Press Ctrl+C to exit.
```

### 4. Test I2C Devices

```bash
# Scan for I2C devices
sudo i2cdetect -y 1

# You should see:
# 0x68 - MPU6050 accelerometer
# 0x3C - SSD1306 OLED display
```

## Configuration

### Configure Your Track

1. Open `config.py`:
```bash
nano config.py
```

2. Set your track's finish line coordinates:
```python
# Get these coordinates using a GPS app on your phone
FINISH_LINE_LAT = 40.123456  # Your track's finish line latitude
FINISH_LINE_LON = -74.123456  # Your track's finish line longitude
FINISH_LINE_RADIUS = 15.0  # Detection radius in meters
```

3. (Optional) Add split points:
```python
SPLIT_POINTS = [
    (40.123500, -74.123500),  # Split 1
    (40.123600, -74.123600),  # Split 2
]
```

### Getting Track Coordinates

1. **Using Phone GPS App**:
   - Install a GPS app (e.g., "GPS Status & Toolbox" for Android)
   - Stand at your track's finish line
   - Note the latitude and longitude

2. **Using Google Maps**:
   - Right-click on the finish line location
   - Select "What's here?"
   - Copy the coordinates

## Usage

### Basic Usage

```bash
# Run Track Rat
python3 main.py

# The system will:
# 1. Initialize GPS and sensors
# 2. Wait for GPS fix
# 3. Start tracking when you cross the finish line
# 4. Display lap times and telemetry
```

### Testing Mode (No Hardware)

```bash
# Test with simulated GPS and sensors
python3 main.py --simulate

# Use console display instead of OLED
python3 main.py --console

# Both options
python3 main.py --simulate --console
```

### Configuration Wizard

```bash
# Get help configuring your track
python3 main.py --configure
```

## Display Layout

The OLED display shows:

```
CUR: 01:23.456      (Current lap time)
LST: 01:22.123      (Last lap time)
BST: 01:21.987      (Best lap time)
DLT: +1.5           (Delta to best lap)
SPD: 125.3 km/h     (Current speed)
G: +0.85L +0.12A    (Lateral and Longitudinal G-forces)
```

## Tips for Best Results

### GPS Accuracy
- Wait for at least 8 satellites before starting
- Use external antenna for better signal in the car
- Mount GPS module with clear view of sky
- Avoid metal surfaces that can block signal

### Mounting
- Mount Raspberry Pi securely to avoid vibration
- Keep GPS module away from electronic interference
- Ensure accelerometer is level and aligned with car axis
- Use vibration dampening for sensors

### Track Setup
- Set finish line radius based on track width (10-20m typical)
- Place split points at key corners or straights
- Test detection radius before actual use
- Record multiple reference laps to verify accuracy

### Power Management
- Use quality power supply (min 2A)
- Consider UPS/battery backup for data safety
- Monitor temperature in hot cars
- Shutdown properly to avoid SD card corruption

## Auto-Start on Boot (Optional)

To automatically start Track Rat when Raspberry Pi boots:

```bash
# Create systemd service
sudo nano /etc/systemd/system/trackrat.service
```

Add:
```ini
[Unit]
Description=Track Rat GPS Lap Timer
After=gpsd.service

[Service]
Type=simple
User=pi
WorkingDirectory=/home/pi/track-rat
ExecStart=/usr/bin/python3 /home/pi/track-rat/main.py
Restart=on-failure

[Install]
WantedBy=multi-user.target
```

Enable:
```bash
sudo systemctl enable trackrat.service
sudo systemctl start trackrat.service
```

## Troubleshooting

### GPS Not Working
```bash
# Check GPS device
ls /dev/ttyUSB* /dev/ttyAMA*

# Check gpsd status
sudo systemctl status gpsd

# View GPS data
cgps
# or
gpsmon

# Restart gpsd
sudo systemctl restart gpsd
```

### I2C Devices Not Detected
```bash
# Check I2C is enabled
sudo raspi-config
# Interface Options -> I2C -> Enable

# Check device detection
sudo i2cdetect -y 1

# Check permissions
sudo usermod -a -G i2c pi
```

### Display Not Working
```bash
# Test I2C connection
sudo i2cdetect -y 1  # Should show 0x3C

# Check display libraries
pip3 list | grep -i adafruit

# Reinstall if needed
pip3 install --upgrade adafruit-circuitpython-ssd1306
```

### Accelerometer Issues
```bash
# Check I2C address
sudo i2cdetect -y 1  # Should show 0x68

# If 0x69, update config.py:
MPU6050_ADDRESS = 0x69
```

## File Structure

```
track-rat/
├── main.py              # Main application
├── config.py            # Configuration file
├── gps_handler.py       # GPS interface module
├── sensor_handler.py    # Accelerometer interface
├── lap_timer.py         # Lap timing logic
├── display_handler.py   # Display interface
├── requirements.txt     # Python dependencies
└── README.md           # This file
```

## Data Logging (Future Enhancement)

Track Rat can be extended to log data to CSV or database for later analysis. Session data includes:
- Lap times
- Speed data (current, average, max)
- G-force data (lateral, longitudinal)
- GPS coordinates
- Split times

## Safety Warning

⚠️ **IMPORTANT**: This device is for track use only. Never interact with or adjust the device while driving on public roads. Always obey traffic laws and track safety regulations.

## License

MIT License - See LICENSE file for details

## Contributing

Contributions welcome! Please open an issue or submit a pull request.

## Support

For issues, questions, or suggestions:
- Open an issue on GitHub
- Check existing issues for solutions

## Credits

Created for motorsport enthusiasts who want professional lap timing on a budget.

## Version History

- v1.0.0 - Initial release
  - GPS lap timing
  - Split time tracking
  - G-force monitoring
  - OLED display support
  - Console mode for testing
