"""
Display Handler Module
Manages the display output for lap times and sensor data
"""

import time
import logging
from typing import Optional

try:
    from PIL import Image, ImageDraw, ImageFont
    import adafruit_ssd1306
    import board
    import busio
    DISPLAY_AVAILABLE = True
except ImportError:
    DISPLAY_AVAILABLE = False
    print("Warning: Display libraries not installed. Display functionality will be limited.")

logging.basicConfig(level=logging.INFO)
logger = logging.getLogger(__name__)


class DisplayHandler:
    """Handles OLED display output (SSD1306)"""

    def __init__(self, width: int = 128, height: int = 64, address: int = 0x3C):
        self.width = width
        self.height = height
        self.address = address
        self.display = None
        self.image = None
        self.draw = None
        self.font = None
        self.font_large = None
        self.connected = False

    def connect(self) -> bool:
        """Initialize display"""
        if not DISPLAY_AVAILABLE:
            logger.error("Display libraries not available")
            return False

        try:
            # Create I2C interface
            i2c = busio.I2C(board.SCL, board.SDA)

            # Create SSD1306 display
            self.display = adafruit_ssd1306.SSD1306_I2C(
                self.width, self.height, i2c, addr=self.address
            )

            # Clear display
            self.display.fill(0)
            self.display.show()

            # Create image for drawing
            self.image = Image.new("1", (self.width, self.height))
            self.draw = ImageDraw.Draw(self.image)

            # Load default font
            try:
                self.font = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", 10)
                self.font_large = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf", 14)
            except:
                self.font = ImageFont.load_default()
                self.font_large = ImageFont.load_default()

            self.connected = True
            logger.info("Display connected successfully")
            return True

        except Exception as e:
            logger.error(f"Failed to connect to display: {e}")
            self.connected = False
            return False

    def clear(self):
        """Clear the display"""
        if self.draw:
            self.draw.rectangle((0, 0, self.width, self.height), outline=0, fill=0)

    def draw_lap_screen(self, current_lap: str, last_lap: str, best_lap: str,
                       delta: str, speed: float, lateral_g: float, long_g: float):
        """
        Draw main lap timing screen

        Args:
            current_lap: Current lap time string
            last_lap: Last lap time string
            best_lap: Best lap time string
            delta: Delta to best lap string
            speed: Current speed in km/h
            lateral_g: Lateral G-force
            long_g: Longitudinal G-force
        """
        if not self.connected or self.draw is None:
            return

        self.clear()

        # Current lap time (large, top)
        self.draw.text((2, 0), f"CUR: {current_lap}", font=self.font_large, fill=255)

        # Last lap time
        self.draw.text((2, 16), f"LST: {last_lap}", font=self.font, fill=255)

        # Best lap time
        self.draw.text((2, 26), f"BST: {best_lap}", font=self.font, fill=255)

        # Delta
        self.draw.text((2, 36), f"DLT: {delta}", font=self.font, fill=255)

        # Speed
        self.draw.text((2, 46), f"SPD:{speed:5.1f}km/h", font=self.font, fill=255)

        # G-forces
        self.draw.text((2, 56), f"G:{lateral_g:+.2f}L {long_g:+.2f}A", font=self.font, fill=255)

        self.update()

    def draw_status_screen(self, gps_fix: bool, satellites: int,
                          sensor_connected: bool, lap_count: int):
        """
        Draw status screen

        Args:
            gps_fix: GPS has valid fix
            satellites: Number of satellites
            sensor_connected: Sensor is connected
            lap_count: Number of completed laps
        """
        if not self.connected or self.draw is None:
            return

        self.clear()

        self.draw.text((2, 0), "TRACK RAT", font=self.font_large, fill=255)
        self.draw.text((2, 18), f"GPS: {'OK' if gps_fix else 'NO FIX'} ({satellites} sats)", font=self.font, fill=255)
        self.draw.text((2, 30), f"Sensor: {'OK' if sensor_connected else 'DISCONNECTED'}", font=self.font, fill=255)
        self.draw.text((2, 42), f"Laps completed: {lap_count}", font=self.font, fill=255)
        self.draw.text((2, 54), "Press CTRL+C to exit", font=self.font, fill=255)

        self.update()

    def draw_text(self, text: str, x: int = 0, y: int = 0, large: bool = False):
        """Draw text at position"""
        if not self.connected or self.draw is None:
            return

        font = self.font_large if large else self.font
        self.draw.text((x, y), text, font=font, fill=255)

    def update(self):
        """Update display with current image"""
        if not self.connected or self.display is None or self.image is None:
            return

        try:
            self.display.image(self.image)
            self.display.show()
        except Exception as e:
            logger.error(f"Error updating display: {e}")

    def disconnect(self):
        """Clean up display"""
        if self.display is not None:
            try:
                self.display.fill(0)
                self.display.show()
            except:
                pass
        self.connected = False


class ConsoleDisplay:
    """Console-based display for testing without hardware"""

    def __init__(self, width: int = 128, height: int = 64, address: int = 0x3C):
        self.connected = False
        self.last_update = 0.0
        self.update_interval = 1.0  # Update console every second

    def connect(self) -> bool:
        self.connected = True
        logger.info("Console display initialized")
        print("\n" + "="*50)
        print("TRACK RAT - Console Display Mode")
        print("="*50 + "\n")
        return True

    def clear(self):
        pass

    def draw_lap_screen(self, current_lap: str, last_lap: str, best_lap: str,
                       delta: str, speed: float, lateral_g: float, long_g: float):
        """Print lap screen to console"""
        if not self.connected:
            return

        current_time = time.time()
        if current_time - self.last_update < self.update_interval:
            return

        self.last_update = current_time

        print("\033[2J\033[H")  # Clear console
        print("="*50)
        print("TRACK RAT - Lap Timer")
        print("="*50)
        print(f"\nCurrent Lap:    {current_lap}")
        print(f"Last Lap:       {last_lap}")
        print(f"Best Lap:       {best_lap}")
        print(f"Delta:          {delta}")
        print(f"\nSpeed:          {speed:.1f} km/h")
        print(f"Lateral G:      {lateral_g:+.2f}g")
        print(f"Longitudinal G: {long_g:+.2f}g")
        print("\n" + "="*50)
        print("Press CTRL+C to stop")

    def draw_status_screen(self, gps_fix: bool, satellites: int,
                          sensor_connected: bool, lap_count: int):
        """Print status screen to console"""
        if not self.connected:
            return

        print("\n" + "="*50)
        print("System Status")
        print("="*50)
        print(f"GPS Fix:        {'YES' if gps_fix else 'NO'} ({satellites} satellites)")
        print(f"Sensor:         {'Connected' if sensor_connected else 'Disconnected'}")
        print(f"Laps completed: {lap_count}")
        print("="*50 + "\n")

    def draw_text(self, text: str, x: int = 0, y: int = 0, large: bool = False):
        pass

    def update(self):
        pass

    def disconnect(self):
        self.connected = False
        print("\nDisplay disconnected")
