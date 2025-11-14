# Track Rat - Quick Installation Guide

## Quick Start (15 minutes)

### Step 1: Hardware Setup (5 minutes)

1. **Connect MPU6050 Accelerometer:**
   - VCC → Pin 1 (3.3V)
   - GND → Pin 6 (GND)
   - SCL → Pin 5 (GPIO 3)
   - SDA → Pin 3 (GPIO 2)

2. **Connect SSD1306 OLED Display:**
   - VCC → Pin 17 (3.3V)
   - GND → Pin 20 (GND)
   - SCL → Pin 5 (GPIO 3) - shared with MPU6050
   - SDA → Pin 3 (GPIO 2) - shared with MPU6050

3. **Connect GPS Module:**
   - Plug USB GPS into any USB port
   - OR connect UART GPS: TX→Pin 10, RX→Pin 8, VCC→Pin 2, GND→Pin 6

### Step 2: System Setup (5 minutes)

```bash
# Update system
sudo apt update && sudo apt upgrade -y

# Enable I2C
sudo raspi-config
# Select: Interface Options → I2C → Yes → OK → Finish

# Reboot
sudo reboot
```

### Step 3: Install Dependencies (3 minutes)

```bash
# Install system packages
sudo apt install -y python3-pip python3-dev i2c-tools gpsd gpsd-clients python3-gps git

# Install Track Rat
cd ~
git clone https://github.com/yourusername/track-rat.git
cd track-rat

# Install Python packages
pip3 install -r requirements.txt
```

### Step 4: Configure GPS (2 minutes)

```bash
# Edit GPS daemon config
sudo nano /etc/default/gpsd
```

**Add these lines:**
```
DEVICES="/dev/ttyUSB0"
GPSD_OPTIONS="-n"
START_DAEMON="true"
```

Save (Ctrl+X, Y, Enter)

```bash
# Restart GPS daemon
sudo systemctl restart gpsd
sudo systemctl enable gpsd

# Wait 30 seconds, then test
cgps
```

You should see GPS data. Press Ctrl+C when done.

### Step 5: Verify Hardware (1 minute)

```bash
# Check I2C devices
sudo i2cdetect -y 1
```

**Expected output:**
```
     0  1  2  3  4  5  6  7  8  9  a  b  c  d  e  f
00:          -- -- -- -- -- -- -- -- -- -- -- -- --
10: -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --
20: -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --
30: -- -- -- -- -- -- -- -- -- -- -- -- 3c -- -- --
40: -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --
50: -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --
60: -- -- -- -- -- -- -- -- 68 -- -- -- -- -- -- --
```

- `3c` = SSD1306 Display
- `68` = MPU6050 Accelerometer

## First Run - Testing

### Test in Simulation Mode

```bash
cd ~/track-rat
python3 main.py --simulate --console
```

You should see simulated lap timing in the console. Press Ctrl+C to stop.

### Test with Real Hardware

```bash
python3 main.py --console
```

This uses real GPS and sensors but shows output in console instead of OLED.

## Configure Your Track

```bash
nano ~/track-rat/config.py
```

**Update these values:**
```python
FINISH_LINE_LAT = 40.123456  # Your finish line latitude
FINISH_LINE_LON = -74.123456  # Your finish line longitude
FINISH_LINE_RADIUS = 15.0     # Detection radius in meters
```

### How to Get Coordinates:

1. **On Phone:** Use GPS app (e.g., "GPS Status") at finish line
2. **Google Maps:** Right-click location → "What's here?"
3. **On Pi:** Run `cgps` and walk to finish line, note coordinates

## Run Track Rat

```bash
cd ~/track-rat
python3 main.py
```

The system will:
1. Initialize (5-10 seconds)
2. Wait for GPS fix (may take 30-60 seconds first time)
3. Display "Ready" when GPS is locked
4. Start timing when you cross finish line

## Optional: Auto-Start on Boot

```bash
# Create service file
sudo nano /etc/systemd/system/trackrat.service
```

**Paste this:**
```ini
[Unit]
Description=Track Rat GPS Lap Timer
After=gpsd.service network.target

[Service]
Type=simple
User=pi
WorkingDirectory=/home/pi/track-rat
ExecStart=/usr/bin/python3 /home/pi/track-rat/main.py
Restart=on-failure
RestartSec=10

[Install]
WantedBy=multi-user.target
```

**Enable and start:**
```bash
sudo systemctl enable trackrat.service
sudo systemctl start trackrat.service

# Check status
sudo systemctl status trackrat.service
```

## Troubleshooting

### No GPS Fix

```bash
# Check GPS is detected
ls /dev/ttyUSB* /dev/ttyAMA*

# Check gpsd
sudo systemctl status gpsd

# Try restarting
sudo systemctl restart gpsd
sudo killall gpsd
sudo gpsd /dev/ttyUSB0 -n -F /var/run/gpsd.sock

# View GPS data
gpsmon
```

### I2C Devices Not Found

```bash
# Make sure I2C is enabled
sudo raspi-config
# Interface Options → I2C → Enable

# Add user to i2c group
sudo usermod -a -G i2c $USER

# Reboot
sudo reboot

# Test again
sudo i2cdetect -y 1
```

### Display Not Working

```bash
# Check I2C connection
sudo i2cdetect -y 1  # Should show 0x3C

# Try console mode instead
python3 main.py --console
```

### Permission Denied Errors

```bash
# Add user to required groups
sudo usermod -a -G dialout,i2c,gpio $USER

# Logout and login again, or reboot
sudo reboot
```

## Tips for Track Day

1. **Before Track Day:**
   - Test everything at home
   - Fully charge power bank
   - Update track coordinates
   - Test GPS accuracy at track

2. **At Track:**
   - Let GPS acquire satellites before first session
   - Mount securely to avoid vibration
   - Keep GPS module with clear sky view
   - Start system before entering car

3. **After Session:**
   - Let system save data before powering off
   - Review session summary
   - Note any issues for next time

## Next Steps

- Read full README.md for detailed information
- Experiment with split points
- Adjust detection radius for your track
- Consider data logging enhancements

## Need Help?

- Check README.md for detailed documentation
- Review existing GitHub issues
- Open new issue with error details
- Include output of: `sudo i2cdetect -y 1` and `cgps`

Enjoy your track day! 🏁
