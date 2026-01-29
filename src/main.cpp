/**
 * Track Rat - Main Application
 * ESP32-S3 GPS Lap Timer with Offline Track Database
 *
 * Hardware:
 * - ESP32-S3 N16R8 (16MB Flash, 8MB PSRAM)
 * - Beitian BE-252Q GPS Module (UART)
 * - ST7789 2.8" TFT Display 240x320 (built-in)
 * - SD Card Reader (built-in)
 * - 3x Push Buttons for navigation
 */

#include <Arduino.h>
#include <SD.h>
#include "Config.h"
#include "TrackDatabase.h"
#include "GPSHandler.h"
#include "LapTimer.h"
#include "DisplayManager.h"

// Global components
TrackDatabase trackDB;
GPSHandler gps;
LapTimer lapTimer;
DisplayManager display;

// Button states
bool buttonSelectPressed = false;
bool buttonNextPressed = false;
bool buttonPrevPressed = false;
unsigned long lastButtonPress = 0;

// Application state
enum AppState {
  APP_INIT,
  APP_GPS_WAIT,
  APP_TRACK_SELECT,
  APP_TRACK_CONFIRM,
  APP_RUNNING,
  APP_FINISHED
};

AppState appState = APP_INIT;
Track* selectedTrack = nullptr;

// Function prototypes
void handleButtons();
void updateStateMachine();
void transitionToState(AppState newState);

void setup() {
  // Initialize serial for debugging
  Serial.begin(115200);
  delay(1000);

  DEBUG_PRINTLN("\n\n=================================");
  DEBUG_PRINTLN("Track Rat - GPS Lap Timer");
  DEBUG_PRINTLN("ESP32-S3 N16R8 Edition");
  DEBUG_PRINTLN("=================================\n");

  // Initialize buttons
  pinMode(BUTTON_SELECT_PIN, INPUT_PULLUP);
  pinMode(BUTTON_NEXT_PIN, INPUT_PULLUP);
  pinMode(BUTTON_PREV_PIN, INPUT_PULLUP);

  DEBUG_PRINTLN("Initializing TFT display...");
  if (!display.begin()) {
    DEBUG_PRINTLN("ERROR: Display initialization failed!");
    while (1) delay(100);
  }
  display.setMode(MODE_BOOT);
  display.showBootScreen();

  DEBUG_PRINTLN("Initializing track database...");
  if (!trackDB.begin()) {
    DEBUG_PRINTLN("ERROR: Track database initialization failed!");
    display.showMessage("DB Error!", 3000);
    while (1) delay(100);
  }

  if (trackDB.getTrackCount() == 0) {
    DEBUG_PRINTLN("ERROR: No tracks in database!");
    display.showMessage("No tracks!", 3000);
    while (1) delay(100);
  }

  DEBUG_PRINTF("Loaded %d tracks\n", trackDB.getTrackCount());

  DEBUG_PRINTLN("Initializing GPS...");
  if (!gps.begin(GPS_RX_PIN, GPS_TX_PIN, GPS_BAUD)) {
    DEBUG_PRINTLN("ERROR: GPS initialization failed!");
    display.showMessage("GPS Error!", 3000);
    while (1) delay(100);
  }

  // Connect components to display
  display.setComponents(&trackDB, &lapTimer, &gps);

  DEBUG_PRINTLN("Initialization complete!");
  delay(2000);

  // Transition to GPS wait state
  transitionToState(APP_GPS_WAIT);
}

void loop() {
  // Update GPS data
  gps.update();

  // Handle button presses
  handleButtons();

  // Update state machine
  updateStateMachine();

  // Update display
  display.update();

  // Small delay to prevent CPU spinning
  delay(10);
}

void handleButtons() {
  unsigned long now = millis();

  // Debounce check
  if (now - lastButtonPress < DEBOUNCE_DELAY) {
    return;
  }

  // Check SELECT button
  bool selectPressed = (digitalRead(BUTTON_SELECT_PIN) == LOW);
  if (selectPressed && !buttonSelectPressed) {
    buttonSelectPressed = true;
    lastButtonPress = now;

    DEBUG_PRINTLN("SELECT button pressed");

    // Handle based on current state
    switch (appState) {
      case APP_TRACK_SELECT:
        // Confirm track selection
        transitionToState(APP_TRACK_CONFIRM);
        break;

      case APP_TRACK_CONFIRM:
        // Start lap timer
        transitionToState(APP_RUNNING);
        break;

      case APP_RUNNING:
        // Pause/Stop
        if (lapTimer.getState() == STATE_RUNNING) {
          lapTimer.pause();
          display.showMessage("PAUSED", 1000);
        } else if (lapTimer.getState() == STATE_PAUSED) {
          lapTimer.resume();
          display.showMessage("RESUMED", 1000);
        }
        break;

      case APP_FINISHED:
        // Return to track selection
        transitionToState(APP_TRACK_SELECT);
        break;

      default:
        break;
    }
  } else if (!selectPressed) {
    buttonSelectPressed = false;
  }

  // Check NEXT button
  bool nextPressed = (digitalRead(BUTTON_NEXT_PIN) == LOW);
  if (nextPressed && !buttonNextPressed) {
    buttonNextPressed = true;
    lastButtonPress = now;

    DEBUG_PRINTLN("NEXT button pressed");

    if (appState == APP_TRACK_SELECT) {
      display.nextTrack();
    }
  } else if (!nextPressed) {
    buttonNextPressed = false;
  }

  // Check PREV button
  bool prevPressed = (digitalRead(BUTTON_PREV_PIN) == LOW);
  if (prevPressed && !buttonPrevPressed) {
    buttonPrevPressed = true;
    lastButtonPress = now;

    DEBUG_PRINTLN("PREV button pressed");

    if (appState == APP_TRACK_SELECT) {
      display.previousTrack();
    }
  } else if (!prevPressed) {
    buttonPrevPressed = false;
  }
}

void updateStateMachine() {
  switch (appState) {
    case APP_INIT:
      // Handled in setup()
      break;

    case APP_GPS_WAIT:
      if (gps.hasFix() && gps.getSatellites() >= 4) {
        DEBUG_PRINTLN("GPS fix acquired!");

        // Auto-detect nearest track
        int nearestTrack = trackDB.findNearestTrack(
          gps.getLatitude(),
          gps.getLongitude()
        );

        if (nearestTrack >= 0) {
          // Found a nearby track
          DEBUG_PRINTF("Auto-selected nearest track: %d\n", nearestTrack);
          trackDB.setCurrentTrack(nearestTrack);

          // Show confirm screen
          transitionToState(APP_TRACK_CONFIRM);
        } else {
          // No nearby track, let user select
          DEBUG_PRINTLN("No nearby track, manual selection required");
          transitionToState(APP_TRACK_SELECT);
        }
      }
      break;

    case APP_TRACK_SELECT:
      // User is selecting a track via buttons
      // Transition handled by button press
      break;

    case APP_TRACK_CONFIRM:
      // User confirming track selection
      // Transition handled by button press
      break;

    case APP_RUNNING:
      // Update lap timer
      if (selectedTrack && gps.hasFix()) {
        lapTimer.update(
          gps.getLatitude(),
          gps.getLongitude(),
          gps.getSpeed(),
          gps.getTimeMillis()
        );
      }

      // Check for GPS loss
      if (!gps.hasFix()) {
        DEBUG_PRINTLN("WARNING: GPS fix lost during session!");
        display.showMessage("GPS LOST", 2000);
      }
      break;

    case APP_FINISHED:
      // Session ended, showing summary
      // Transition handled by button press
      break;
  }
}

void transitionToState(AppState newState) {
  DEBUG_PRINTF("State transition: %d -> %d\n", appState, newState);

  // Exit current state
  switch (appState) {
    case APP_RUNNING:
      // Save lap data
      if (lapTimer.getLapCount() > 0) {
        String filename = "/session_" + String(millis()) + ".json";
        lapTimer.saveLaps(filename);
        DEBUG_PRINTF("Session data saved to: %s\n", filename.c_str());
      }
      break;

    default:
      break;
  }

  // Enter new state
  appState = newState;

  switch (newState) {
    case APP_INIT:
      display.setMode(MODE_BOOT);
      break;

    case APP_GPS_WAIT:
      display.setMode(MODE_GPS_WAITING);
      display.showMessage("Waiting for GPS", 1500);
      break;

    case APP_TRACK_SELECT:
      display.setMode(MODE_TRACK_SELECTION);
      // Auto-detect nearest track as suggestion
      if (gps.hasFix()) {
        int nearest = trackDB.findNearestTrack(
          gps.getLatitude(),
          gps.getLongitude()
        );
        if (nearest >= 0) {
          trackDB.setCurrentTrack(nearest);
        }
      }
      break;

    case APP_TRACK_CONFIRM:
      display.setMode(MODE_TRACK_CONFIRM);
      selectedTrack = trackDB.getTrack(display.getSelectedTrackIndex());
      if (selectedTrack) {
        trackDB.setCurrentTrack(display.getSelectedTrackIndex());
        DEBUG_PRINTF("Track confirmed: %s\n", selectedTrack->name.c_str());
      }
      break;

    case APP_RUNNING:
      display.setMode(MODE_LAP_TIMING);
      if (selectedTrack) {
        lapTimer.reset();
        lapTimer.begin(selectedTrack, MIN_LAP_TIME, MAX_LAP_TIME);
        lapTimer.start();
        DEBUG_PRINTLN("Lap timer started!");
        display.showMessage("GO!", 1000);
      }
      break;

    case APP_FINISHED:
      display.setMode(MODE_SESSION_SUMMARY);
      lapTimer.stop();
      DEBUG_PRINTLN("Session finished!");
      break;
  }
}
