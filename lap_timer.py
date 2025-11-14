"""
Lap Timer Module
Handles lap detection, timing, and split time tracking
"""

import time
import logging
from typing import List, Tuple, Optional
from dataclasses import dataclass
from datetime import datetime

logging.basicConfig(level=logging.INFO)
logger = logging.getLogger(__name__)


@dataclass
class LapData:
    """Data structure for storing lap information"""
    lap_number: int
    lap_time: float  # seconds
    timestamp: datetime
    avg_speed: float  # m/s
    max_speed: float  # m/s
    max_lateral_g: float
    max_longitudinal_g: float
    split_times: List[float]  # seconds for each split


@dataclass
class SplitPoint:
    """Represents a split timing point on the track"""
    lat: float
    lon: float
    radius: float
    name: str = ""


class LapTimer:
    """Manages lap timing and detection"""

    def __init__(self, finish_line: Tuple[float, float], finish_radius: float,
                 min_lap_time: float = 30.0, max_lap_time: float = 600.0):
        """
        Initialize lap timer

        Args:
            finish_line: (latitude, longitude) of finish line
            finish_radius: Detection radius in meters
            min_lap_time: Minimum valid lap time in seconds
            max_lap_time: Maximum valid lap time in seconds
        """
        self.finish_lat, self.finish_lon = finish_line
        self.finish_radius = finish_radius
        self.min_lap_time = min_lap_time
        self.max_lap_time = max_lap_time

        # Split points
        self.split_points: List[SplitPoint] = []

        # Lap tracking
        self.laps: List[LapData] = []
        self.current_lap_number = 0
        self.lap_start_time: Optional[float] = None
        self.in_finish_zone = False

        # Current lap statistics
        self.current_lap_max_speed = 0.0
        self.current_lap_speed_sum = 0.0
        self.current_lap_speed_count = 0
        self.current_lap_max_lateral_g = 0.0
        self.current_lap_max_longitudinal_g = 0.0
        self.current_lap_split_times: List[float] = []
        self.split_crossed: List[bool] = []

        # Best lap tracking
        self.best_lap_time: Optional[float] = None
        self.best_lap: Optional[LapData] = None

        logger.info(f"Lap timer initialized at ({self.finish_lat:.6f}, {self.finish_lon:.6f})")

    def add_split_point(self, lat: float, lon: float, radius: float, name: str = ""):
        """Add a split timing point"""
        split = SplitPoint(lat, lon, radius, name)
        self.split_points.append(split)
        self.split_crossed.append(False)
        logger.info(f"Added split point: {name or f'Split {len(self.split_points)}'}")

    def update(self, current_lat: float, current_lon: float,
               current_speed: float, lateral_g: float, longitudinal_g: float,
               gps_distance_func) -> Optional[LapData]:
        """
        Update lap timer with current position and sensor data

        Args:
            current_lat: Current latitude
            current_lon: Current longitude
            current_speed: Current speed in m/s
            lateral_g: Lateral G-force
            longitudinal_g: Longitudinal G-force
            gps_distance_func: Function to calculate distance between two points

        Returns:
            LapData if a lap was just completed, None otherwise
        """
        # Calculate distance to finish line
        distance_to_finish = gps_distance_func(
            current_lat, current_lon,
            self.finish_lat, self.finish_lon
        )

        # Update current lap statistics
        if self.lap_start_time is not None:
            self.current_lap_max_speed = max(self.current_lap_max_speed, current_speed)
            self.current_lap_speed_sum += current_speed
            self.current_lap_speed_count += 1
            self.current_lap_max_lateral_g = max(self.current_lap_max_lateral_g, abs(lateral_g))
            self.current_lap_max_longitudinal_g = max(self.current_lap_max_longitudinal_g, abs(longitudinal_g))

            # Check split points
            for i, split in enumerate(self.split_points):
                distance_to_split = gps_distance_func(
                    current_lat, current_lon,
                    split.lat, split.lon
                )

                if distance_to_split <= split.radius:
                    if not self.split_crossed[i]:
                        # Crossed a new split
                        split_time = time.time() - self.lap_start_time
                        self.current_lap_split_times.append(split_time)
                        self.split_crossed[i] = True
                        logger.info(f"Split {i+1} crossed at {split_time:.3f}s")
                else:
                    # Reset split when we leave its zone
                    if self.split_crossed[i] and distance_to_split > split.radius * 1.5:
                        self.split_crossed[i] = False

        # Check finish line crossing
        currently_in_zone = distance_to_finish <= self.finish_radius

        completed_lap = None

        if currently_in_zone and not self.in_finish_zone:
            # Just entered finish zone - potential lap completion
            current_time = time.time()

            if self.lap_start_time is not None:
                # Complete the current lap
                lap_time = current_time - self.lap_start_time

                # Validate lap time
                if self.min_lap_time <= lap_time <= self.max_lap_time:
                    # Calculate average speed
                    avg_speed = (self.current_lap_speed_sum / self.current_lap_speed_count
                                if self.current_lap_speed_count > 0 else 0.0)

                    # Create lap data
                    lap_data = LapData(
                        lap_number=self.current_lap_number,
                        lap_time=lap_time,
                        timestamp=datetime.now(),
                        avg_speed=avg_speed,
                        max_speed=self.current_lap_max_speed,
                        max_lateral_g=self.current_lap_max_lateral_g,
                        max_longitudinal_g=self.current_lap_max_longitudinal_g,
                        split_times=self.current_lap_split_times.copy()
                    )

                    self.laps.append(lap_data)
                    completed_lap = lap_data

                    # Update best lap
                    if self.best_lap_time is None or lap_time < self.best_lap_time:
                        self.best_lap_time = lap_time
                        self.best_lap = lap_data
                        logger.info(f"New best lap! {self._format_time(lap_time)}")

                    logger.info(f"Lap {self.current_lap_number} completed: {self._format_time(lap_time)}")
                else:
                    logger.warning(f"Invalid lap time: {lap_time:.1f}s (must be between "
                                 f"{self.min_lap_time}s and {self.max_lap_time}s)")

            # Start new lap
            self.lap_start_time = current_time
            self.current_lap_number += 1
            self._reset_lap_stats()

            self.in_finish_zone = True

        elif not currently_in_zone and self.in_finish_zone:
            # Just left finish zone
            self.in_finish_zone = False

        return completed_lap

    def _reset_lap_stats(self):
        """Reset statistics for new lap"""
        self.current_lap_max_speed = 0.0
        self.current_lap_speed_sum = 0.0
        self.current_lap_speed_count = 0
        self.current_lap_max_lateral_g = 0.0
        self.current_lap_max_longitudinal_g = 0.0
        self.current_lap_split_times = []
        self.split_crossed = [False] * len(self.split_points)

    def get_current_lap_time(self) -> Optional[float]:
        """Get current lap time in seconds"""
        if self.lap_start_time is None:
            return None
        return time.time() - self.lap_start_time

    def get_last_lap(self) -> Optional[LapData]:
        """Get the most recent completed lap"""
        if len(self.laps) == 0:
            return None
        return self.laps[-1]

    def get_best_lap(self) -> Optional[LapData]:
        """Get the best (fastest) lap"""
        return self.best_lap

    def get_delta_to_best(self) -> Optional[float]:
        """Get time delta to best lap (positive = slower, negative = faster)"""
        current_time = self.get_current_lap_time()
        if current_time is None or self.best_lap_time is None:
            return None
        return current_time - self.best_lap_time

    def get_average_lap_time(self) -> Optional[float]:
        """Get average lap time across all laps"""
        if len(self.laps) == 0:
            return None
        total_time = sum(lap.lap_time for lap in self.laps)
        return total_time / len(self.laps)

    def get_lap_count(self) -> int:
        """Get number of completed laps"""
        return len(self.laps)

    @staticmethod
    def _format_time(seconds: float) -> str:
        """Format time as MM:SS.mmm"""
        minutes = int(seconds // 60)
        secs = seconds % 60
        return f"{minutes:02d}:{secs:06.3f}"

    def format_current_lap_time(self) -> str:
        """Format current lap time as string"""
        lap_time = self.get_current_lap_time()
        if lap_time is None:
            return "--:--.---"
        return self._format_time(lap_time)

    def format_last_lap_time(self) -> str:
        """Format last lap time as string"""
        last_lap = self.get_last_lap()
        if last_lap is None:
            return "--:--.---"
        return self._format_time(last_lap.lap_time)

    def format_best_lap_time(self) -> str:
        """Format best lap time as string"""
        if self.best_lap_time is None:
            return "--:--.---"
        return self._format_time(self.best_lap_time)

    def format_delta(self) -> str:
        """Format delta to best lap"""
        delta = self.get_delta_to_best()
        if delta is None:
            return "+--.-"

        sign = "+" if delta >= 0 else "-"
        return f"{sign}{abs(delta):.1f}"
