"""
Configuration file for Track Rat GPS Lap Timer
"""

# GPS Configuration
GPS_HOST = "localhost"
GPS_PORT = 2947
GPS_TIMEOUT = 5.0

# Track Configuration
# Define your track start/finish line coordinates
# Format: (latitude, longitude)
FINISH_LINE_LAT = 0.0  # Replace with your track's finish line latitude
FINISH_LINE_LON = 0.0  # Replace with your track's finish line longitude
FINISH_LINE_RADIUS = 15.0  # meters - detection radius for crossing finish line

# Split Points (optional)
# Add intermediate split points around the track
# Format: [(lat1, lon1), (lat2, lon2), ...]
SPLIT_POINTS = [
    # Example: (40.1234, -74.5678),
]
SPLIT_RADIUS = 15.0  # meters

# Accelerometer Configuration (MPU6050)
MPU6050_ADDRESS = 0x68
ACCEL_SCALE = 16384.0  # For ±2g range
GYRO_SCALE = 131.0  # For ±250°/s range

# Display Configuration
DISPLAY_I2C_ADDRESS = 0x3C
DISPLAY_WIDTH = 128
DISPLAY_HEIGHT = 64
DISPLAY_UPDATE_RATE = 0.1  # seconds

# Data Collection
GPS_UPDATE_RATE = 10  # Hz
SENSOR_UPDATE_RATE = 50  # Hz

# Lap Time Validation
MIN_LAP_TIME = 30.0  # seconds - minimum valid lap time
MAX_LAP_TIME = 600.0  # seconds - maximum valid lap time
