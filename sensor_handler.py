"""
Sensor Handler Module
Interfaces with accelerometer and other sensors for G-force measurement
"""

import time
import math
import logging
from typing import Tuple, Optional

try:
    import smbus2
    SMBUS_AVAILABLE = True
except ImportError:
    SMBUS_AVAILABLE = False
    print("Warning: smbus2 not installed. Sensor functionality will be limited.")

logging.basicConfig(level=logging.INFO)
logger = logging.getLogger(__name__)


class MPU6050:
    """MPU6050 Accelerometer/Gyroscope interface"""

    # MPU6050 Registers
    PWR_MGMT_1 = 0x6B
    ACCEL_XOUT_H = 0x3B
    ACCEL_YOUT_H = 0x3D
    ACCEL_ZOUT_H = 0x3F
    GYRO_XOUT_H = 0x43
    GYRO_YOUT_H = 0x45
    GYRO_ZOUT_H = 0x47
    TEMP_OUT_H = 0x41

    def __init__(self, address: int = 0x68, bus: int = 1):
        self.address = address
        self.bus_num = bus
        self.bus = None
        self.connected = False

        # Calibration offsets
        self.accel_offset_x = 0.0
        self.accel_offset_y = 0.0
        self.accel_offset_z = 0.0

        # Current readings
        self.accel_x = 0.0
        self.accel_y = 0.0
        self.accel_z = 0.0
        self.gyro_x = 0.0
        self.gyro_y = 0.0
        self.gyro_z = 0.0
        self.temperature = 0.0

    def connect(self) -> bool:
        """Initialize MPU6050"""
        if not SMBUS_AVAILABLE:
            logger.error("SMBus not available")
            return False

        try:
            self.bus = smbus2.SMBus(self.bus_num)
            # Wake up MPU6050
            self.bus.write_byte_data(self.address, self.PWR_MGMT_1, 0)
            time.sleep(0.1)
            self.connected = True
            logger.info(f"MPU6050 connected at address 0x{self.address:02x}")
            return True
        except Exception as e:
            logger.error(f"Failed to connect to MPU6050: {e}")
            self.connected = False
            return False

    def read_word_2c(self, reg: int) -> int:
        """Read two bytes and convert to signed integer"""
        high = self.bus.read_byte_data(self.address, reg)
        low = self.bus.read_byte_data(self.address, reg + 1)
        val = (high << 8) + low
        if val >= 0x8000:
            return -((65535 - val) + 1)
        return val

    def update(self) -> bool:
        """Read all sensor data"""
        if not self.connected or self.bus is None:
            return False

        try:
            # Read accelerometer (in g's)
            accel_x_raw = self.read_word_2c(self.ACCEL_XOUT_H)
            accel_y_raw = self.read_word_2c(self.ACCEL_YOUT_H)
            accel_z_raw = self.read_word_2c(self.ACCEL_ZOUT_H)

            self.accel_x = accel_x_raw / 16384.0 - self.accel_offset_x
            self.accel_y = accel_y_raw / 16384.0 - self.accel_offset_y
            self.accel_z = accel_z_raw / 16384.0 - self.accel_offset_z

            # Read gyroscope (in degrees/sec)
            gyro_x_raw = self.read_word_2c(self.GYRO_XOUT_H)
            gyro_y_raw = self.read_word_2c(self.GYRO_YOUT_H)
            gyro_z_raw = self.read_word_2c(self.GYRO_ZOUT_H)

            self.gyro_x = gyro_x_raw / 131.0
            self.gyro_y = gyro_y_raw / 131.0
            self.gyro_z = gyro_z_raw / 131.0

            # Read temperature
            temp_raw = self.read_word_2c(self.TEMP_OUT_H)
            self.temperature = temp_raw / 340.0 + 36.53

            return True

        except Exception as e:
            logger.error(f"Error reading MPU6050: {e}")
            return False

    def calibrate(self, samples: int = 100):
        """Calibrate accelerometer by averaging samples at rest"""
        logger.info("Calibrating accelerometer... Keep sensor still!")

        sum_x, sum_y, sum_z = 0.0, 0.0, 0.0

        for _ in range(samples):
            if self.update():
                sum_x += self.accel_x + self.accel_offset_x
                sum_y += self.accel_y + self.accel_offset_y
                sum_z += self.accel_z + self.accel_offset_z
            time.sleep(0.01)

        self.accel_offset_x = sum_x / samples
        self.accel_offset_y = sum_y / samples
        self.accel_offset_z = (sum_z / samples) - 1.0  # Subtract gravity

        logger.info(f"Calibration complete: X={self.accel_offset_x:.3f}, "
                   f"Y={self.accel_offset_y:.3f}, Z={self.accel_offset_z:.3f}")

    def get_acceleration(self) -> Tuple[float, float, float]:
        """Get acceleration in g's (x, y, z)"""
        return (self.accel_x, self.accel_y, self.accel_z)

    def get_total_g_force(self) -> float:
        """Get total G-force magnitude"""
        return math.sqrt(self.accel_x**2 + self.accel_y**2 + self.accel_z**2)

    def get_lateral_g(self) -> float:
        """Get lateral (cornering) G-force"""
        return self.accel_y

    def get_longitudinal_g(self) -> float:
        """Get longitudinal (acceleration/braking) G-force"""
        return self.accel_x

    def disconnect(self):
        """Close connection"""
        if self.bus is not None:
            self.bus.close()
        self.connected = False


class SensorSimulator:
    """Simulated sensor for testing without hardware"""

    def __init__(self):
        self.connected = False
        self.time_offset = 0.0
        self.accel_x = 0.0
        self.accel_y = 0.0
        self.accel_z = 1.0  # Gravity
        self.gyro_x = 0.0
        self.gyro_y = 0.0
        self.gyro_z = 0.0
        self.temperature = 25.0

    def connect(self) -> bool:
        self.connected = True
        logger.info("Sensor simulator connected")
        return True

    def update(self) -> bool:
        """Simulate sensor readings"""
        if not self.connected:
            return False

        self.time_offset += 0.02

        # Simulate cornering and acceleration
        self.accel_x = 0.5 * math.sin(self.time_offset * 0.5)  # Longitudinal
        self.accel_y = 0.8 * math.cos(self.time_offset * 0.7)  # Lateral
        self.accel_z = 1.0  # Gravity

        self.gyro_z = 30.0 * math.sin(self.time_offset * 0.3)  # Yaw rate

        return True

    def calibrate(self, samples: int = 100):
        logger.info("Simulated calibration complete")

    def get_acceleration(self) -> Tuple[float, float, float]:
        return (self.accel_x, self.accel_y, self.accel_z)

    def get_total_g_force(self) -> float:
        return math.sqrt(self.accel_x**2 + self.accel_y**2 + self.accel_z**2)

    def get_lateral_g(self) -> float:
        return self.accel_y

    def get_longitudinal_g(self) -> float:
        return self.accel_x

    def disconnect(self):
        self.connected = False
