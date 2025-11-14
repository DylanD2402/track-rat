"""
GPS Handler Module
Interfaces with GPS device to get location data
"""

import time
import math
from typing import Optional, Tuple
import logging

try:
    from gpsd import gps, WATCH_ENABLE
except ImportError:
    print("Warning: gpsd-py3 not installed. GPS functionality will be limited.")
    gps = None

logging.basicConfig(level=logging.INFO)
logger = logging.getLogger(__name__)


class GPSHandler:
    """Handles GPS data acquisition and processing"""

    def __init__(self, host: str = "localhost", port: int = 2947):
        self.host = host
        self.port = port
        self.session = None
        self.current_lat = 0.0
        self.current_lon = 0.0
        self.current_speed = 0.0  # m/s
        self.current_altitude = 0.0
        self.satellites = 0
        self.fix_quality = 0
        self.connected = False

    def connect(self) -> bool:
        """Connect to GPS daemon"""
        try:
            if gps is None:
                logger.error("GPS library not available")
                return False

            self.session = gps(mode=WATCH_ENABLE, host=self.host, port=self.port)
            self.connected = True
            logger.info(f"Connected to GPS daemon at {self.host}:{self.port}")
            return True
        except Exception as e:
            logger.error(f"Failed to connect to GPS: {e}")
            self.connected = False
            return False

    def update(self) -> bool:
        """Update GPS data from daemon"""
        if not self.connected or self.session is None:
            return False

        try:
            report = self.session.next()

            if report['class'] == 'TPV':
                # Time-Position-Velocity report
                if hasattr(report, 'lat'):
                    self.current_lat = report.lat
                if hasattr(report, 'lon'):
                    self.current_lon = report.lon
                if hasattr(report, 'speed'):
                    self.current_speed = report.speed  # m/s
                if hasattr(report, 'alt'):
                    self.current_altitude = report.alt
                if hasattr(report, 'mode'):
                    self.fix_quality = report.mode

            elif report['class'] == 'SKY':
                # Satellite info
                if hasattr(report, 'satellites'):
                    self.satellites = len(report.satellites)

            return True

        except StopIteration:
            return False
        except Exception as e:
            logger.error(f"Error updating GPS: {e}")
            return False

    def get_position(self) -> Tuple[float, float]:
        """Get current position as (latitude, longitude)"""
        return (self.current_lat, self.current_lon)

    def get_speed(self) -> float:
        """Get current speed in m/s"""
        return self.current_speed

    def get_speed_kmh(self) -> float:
        """Get current speed in km/h"""
        return self.current_speed * 3.6

    def get_speed_mph(self) -> float:
        """Get current speed in mph"""
        return self.current_speed * 2.23694

    def has_fix(self) -> bool:
        """Check if GPS has a valid fix"""
        return self.fix_quality >= 2  # 2D or 3D fix

    @staticmethod
    def calculate_distance(lat1: float, lon1: float, lat2: float, lon2: float) -> float:
        """
        Calculate distance between two GPS coordinates using Haversine formula
        Returns distance in meters
        """
        R = 6371000  # Earth's radius in meters

        lat1_rad = math.radians(lat1)
        lat2_rad = math.radians(lat2)
        delta_lat = math.radians(lat2 - lat1)
        delta_lon = math.radians(lon2 - lon1)

        a = (math.sin(delta_lat / 2) ** 2 +
             math.cos(lat1_rad) * math.cos(lat2_rad) *
             math.sin(delta_lon / 2) ** 2)

        c = 2 * math.asin(math.sqrt(a))

        distance = R * c
        return distance

    def distance_to_point(self, target_lat: float, target_lon: float) -> float:
        """Calculate distance from current position to target point"""
        return self.calculate_distance(
            self.current_lat, self.current_lon,
            target_lat, target_lon
        )

    def disconnect(self):
        """Disconnect from GPS daemon"""
        self.connected = False
        logger.info("Disconnected from GPS")


class GPSSimulator:
    """Simulated GPS for testing without hardware"""

    def __init__(self):
        self.current_lat = 40.0
        self.current_lon = -74.0
        self.current_speed = 25.0  # m/s
        self.current_altitude = 100.0
        self.satellites = 8
        self.fix_quality = 3
        self.connected = False
        self.time_offset = 0.0

    def connect(self) -> bool:
        """Simulate connection"""
        self.connected = True
        logger.info("GPS Simulator connected")
        return True

    def update(self) -> bool:
        """Simulate GPS movement"""
        if not self.connected:
            return False

        # Simulate circular movement
        self.time_offset += 0.1
        radius = 0.001  # degrees
        self.current_lat = 40.0 + radius * math.cos(self.time_offset)
        self.current_lon = -74.0 + radius * math.sin(self.time_offset)
        self.current_speed = 20.0 + 10.0 * math.sin(self.time_offset * 0.5)

        return True

    def get_position(self) -> Tuple[float, float]:
        return (self.current_lat, self.current_lon)

    def get_speed(self) -> float:
        return self.current_speed

    def get_speed_kmh(self) -> float:
        return self.current_speed * 3.6

    def get_speed_mph(self) -> float:
        return self.current_speed * 2.23694

    def has_fix(self) -> bool:
        return True

    def distance_to_point(self, target_lat: float, target_lon: float) -> float:
        return GPSHandler.calculate_distance(
            self.current_lat, self.current_lon,
            target_lat, target_lon
        )

    def disconnect(self):
        self.connected = False
