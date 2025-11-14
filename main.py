#!/usr/bin/env python3
"""
Track Rat - GPS Lap Timer for Raspberry Pi
Main application entry point
"""

import time
import sys
import signal
import logging
import argparse
from typing import Optional

# Import configuration
import config

# Import modules
from gps_handler import GPSHandler, GPSSimulator
from sensor_handler import MPU6050, SensorSimulator
from lap_timer import LapTimer
from display_handler import DisplayHandler, ConsoleDisplay

logging.basicConfig(
    level=logging.INFO,
    format='%(asctime)s - %(name)s - %(levelname)s - %(message)s'
)
logger = logging.getLogger(__name__)


class TrackRat:
    """Main application class"""

    def __init__(self, simulate: bool = False, console_display: bool = False):
        """
        Initialize Track Rat application

        Args:
            simulate: Use simulated GPS and sensors instead of hardware
            console_display: Use console output instead of OLED display
        """
        self.simulate = simulate
        self.running = False

        # Initialize GPS
        if simulate:
            self.gps = GPSSimulator()
            logger.info("Using GPS simulator")
        else:
            self.gps = GPSHandler(config.GPS_HOST, config.GPS_PORT)
            logger.info("Using hardware GPS")

        # Initialize sensors
        if simulate:
            self.sensor = SensorSimulator()
            logger.info("Using sensor simulator")
        else:
            self.sensor = MPU6050(config.MPU6050_ADDRESS)
            logger.info("Using hardware sensors")

        # Initialize display
        if console_display or simulate:
            self.display = ConsoleDisplay()
            logger.info("Using console display")
        else:
            self.display = DisplayHandler(
                config.DISPLAY_WIDTH,
                config.DISPLAY_HEIGHT,
                config.DISPLAY_I2C_ADDRESS
            )
            logger.info("Using OLED display")

        # Initialize lap timer
        self.lap_timer = LapTimer(
            finish_line=(config.FINISH_LINE_LAT, config.FINISH_LINE_LON),
            finish_radius=config.FINISH_LINE_RADIUS,
            min_lap_time=config.MIN_LAP_TIME,
            max_lap_time=config.MAX_LAP_TIME
        )

        # Add split points if configured
        for i, (lat, lon) in enumerate(config.SPLIT_POINTS):
            self.lap_timer.add_split_point(lat, lon, config.SPLIT_RADIUS, f"Split {i+1}")

        # Setup signal handlers
        signal.signal(signal.SIGINT, self._signal_handler)
        signal.signal(signal.SIGTERM, self._signal_handler)

    def _signal_handler(self, signum, frame):
        """Handle shutdown signals"""
        logger.info("Shutdown signal received")
        self.stop()
        sys.exit(0)

    def initialize(self) -> bool:
        """Initialize all hardware components"""
        logger.info("Initializing Track Rat...")

        # Connect GPS
        if not self.gps.connect():
            logger.error("Failed to connect to GPS")
            return False

        # Connect sensors
        if not self.sensor.connect():
            logger.error("Failed to connect to sensors")
            return False

        # Calibrate sensors if using hardware
        if not self.simulate:
            logger.info("Calibrating sensors (keep device still)...")
            time.sleep(1)
            self.sensor.calibrate()

        # Connect display
        if not self.display.connect():
            logger.error("Failed to connect to display")
            return False

        logger.info("Track Rat initialized successfully!")
        return True

    def run(self):
        """Main application loop"""
        self.running = True

        # Wait for GPS fix
        logger.info("Waiting for GPS fix...")
        self.display.draw_status_screen(
            gps_fix=False,
            satellites=0,
            sensor_connected=self.sensor.connected,
            lap_count=0
        )

        while self.running and not self.gps.has_fix():
            self.gps.update()
            time.sleep(0.5)

        if not self.running:
            return

        logger.info("GPS fix acquired! Starting lap timer...")
        time.sleep(2)

        # Main loop
        last_update_time = time.time()
        update_interval = 1.0 / 10.0  # 10 Hz update rate

        try:
            while self.running:
                current_time = time.time()

                # Update GPS
                self.gps.update()

                # Update sensors
                self.sensor.update()

                # Get current data
                lat, lon = self.gps.get_position()
                speed = self.gps.get_speed_kmh()
                lateral_g = self.sensor.get_lateral_g()
                long_g = self.sensor.get_longitudinal_g()

                # Update lap timer
                completed_lap = self.lap_timer.update(
                    lat, lon, self.gps.get_speed(),
                    lateral_g, long_g,
                    self.gps.distance_to_point
                )

                # Log completed lap
                if completed_lap:
                    logger.info(f"Lap {completed_lap.lap_number} completed!")
                    logger.info(f"  Time: {self.lap_timer._format_time(completed_lap.lap_time)}")
                    logger.info(f"  Avg Speed: {completed_lap.avg_speed * 3.6:.1f} km/h")
                    logger.info(f"  Max Speed: {completed_lap.max_speed * 3.6:.1f} km/h")
                    logger.info(f"  Max Lateral G: {completed_lap.max_lateral_g:.2f}g")
                    logger.info(f"  Max Longitudinal G: {completed_lap.max_longitudinal_g:.2f}g")

                # Update display at regular intervals
                if current_time - last_update_time >= update_interval:
                    self.display.draw_lap_screen(
                        current_lap=self.lap_timer.format_current_lap_time(),
                        last_lap=self.lap_timer.format_last_lap_time(),
                        best_lap=self.lap_timer.format_best_lap_time(),
                        delta=self.lap_timer.format_delta(),
                        speed=speed,
                        lateral_g=lateral_g,
                        long_g=long_g
                    )
                    last_update_time = current_time

                # Small sleep to prevent CPU spinning
                time.sleep(0.01)

        except KeyboardInterrupt:
            logger.info("Interrupted by user")
        except Exception as e:
            logger.error(f"Error in main loop: {e}", exc_info=True)
        finally:
            self.stop()

    def stop(self):
        """Stop application and cleanup"""
        logger.info("Stopping Track Rat...")
        self.running = False

        # Print summary
        if self.lap_timer.get_lap_count() > 0:
            print("\n" + "="*50)
            print("SESSION SUMMARY")
            print("="*50)
            print(f"Total laps: {self.lap_timer.get_lap_count()}")
            print(f"Best lap: {self.lap_timer.format_best_lap_time()}")

            avg_time = self.lap_timer.get_average_lap_time()
            if avg_time:
                print(f"Average lap: {self.lap_timer._format_time(avg_time)}")

            best_lap = self.lap_timer.get_best_lap()
            if best_lap:
                print(f"Best lap avg speed: {best_lap.avg_speed * 3.6:.1f} km/h")
                print(f"Best lap max speed: {best_lap.max_speed * 3.6:.1f} km/h")

            print("="*50 + "\n")

        # Disconnect components
        self.gps.disconnect()
        self.sensor.disconnect()
        self.display.disconnect()

        logger.info("Track Rat stopped")


def main():
    """Main entry point"""
    parser = argparse.ArgumentParser(description='Track Rat - GPS Lap Timer')
    parser.add_argument('--simulate', action='store_true',
                       help='Use simulated GPS and sensors for testing')
    parser.add_argument('--console', action='store_true',
                       help='Use console display instead of OLED')
    parser.add_argument('--configure', action='store_true',
                       help='Run configuration wizard')

    args = parser.parse_args()

    if args.configure:
        print("\nTrack Rat Configuration Wizard")
        print("="*50)
        print("\nTo configure your track:")
        print("1. Edit config.py")
        print("2. Set FINISH_LINE_LAT and FINISH_LINE_LON to your track's finish line coordinates")
        print("3. Adjust FINISH_LINE_RADIUS (default: 15 meters)")
        print("4. Optionally add SPLIT_POINTS for intermediate timing")
        print("\nYou can use a GPS app on your phone to get coordinates.")
        print("="*50 + "\n")
        return

    # Check if finish line is configured
    if config.FINISH_LINE_LAT == 0.0 and config.FINISH_LINE_LON == 0.0 and not args.simulate:
        print("\nWARNING: Finish line not configured!")
        print("Run: python3 main.py --configure")
        print("Or use: python3 main.py --simulate for testing\n")
        response = input("Continue anyway? (y/N): ")
        if response.lower() != 'y':
            return

    # Create and run application
    app = TrackRat(simulate=args.simulate, console_display=args.console)

    if not app.initialize():
        logger.error("Failed to initialize Track Rat")
        sys.exit(1)

    logger.info("Starting Track Rat...")
    app.run()


if __name__ == "__main__":
    main()
