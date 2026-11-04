#include "SettingsScreens.h"

#include "App.h"
#include "Config.h"
#include "ProfileStore.h"
#include "Power.h"
#include "CameraBle.h"
#include "ObdBle.h"
#include "Settings.h"
#include "SettingsFlow.h"
#include "SleepControl.h"

namespace SettingsScreens {

// Settings screen and the first-boot questions (the rules are in SettingsFlow.h)
static SettingsScreenState settingsUi;
static SetupState setupUi;
static DeviceScreenState engineUi;  // Settings > Engine data (DeviceScreen.h)
static ObdBle::Found engineFound[ObdBle::MAX_FOUND];  // the Find adapter list as last shown
static int engineFoundCount = 0;
static DeviceScreenState cameraUi;  // Settings > Camera
static CameraBle::Found cameraFound[CameraBle::MAX_FOUND];
static int cameraFoundCount = 0;

static bool listDragged = false;  // the Settings list is following a finger

static Key toKey(InputEvent ev) {
  switch (ev) {
    case InputEvent::TapLeft: return Key::Left;
    case InputEvent::TapCenter: return Key::Center;
    case InputEvent::TapRight: return Key::Right;
    case InputEvent::TapHeader: return Key::Header;
    case InputEvent::Hold: return Key::Hold;
    case InputEvent::BootShort: return Key::BootShort;
    case InputEvent::BootLong: return Key::BootLong;
    default: return Key::None;
  }
}

void open() {
  settingsUi.begin(input.touchAvailable());
  settingsUi.setPeople(Profiles::get());
  enterState(AppState::SettingsScreen);
}

void onInputList(InputEvent ev) {
  SettingsData s = Settings::get();
  settingsUi.setPeople(Profiles::get());
  const bool tapped = ev == InputEvent::TapLeft || ev == InputEvent::TapCenter || ev == InputEvent::TapRight;
  SettingsScreenState::Result r = tapped ? settingsUi.tap(input.lastTapX(), input.lastTapY(), s)
                                         : settingsUi.handle(toKey(ev), s);
  if (r == SettingsScreenState::Result::Changed) {
    Settings::set(s);
    applySettings();
  } else if (r == SettingsScreenState::Result::Pick) {  // takes effect at the next session
    Profiles::select(settingsUi.pickKind, settingsUi.pickId);
  } else if (r == SettingsScreenState::Result::Exit) {
    enterState(AppState::Menu);
  } else if (r == SettingsScreenState::Result::PowerOff) {
    SleepControl::powerOff();
  } else if (r == SettingsScreenState::Result::Open && settingsUi.item == SettingItem::Engine) {
    engineUi.begin(input.touchAvailable());
    enterState(AppState::EngineScreen);
  } else if (r == SettingsScreenState::Result::Open && settingsUi.item == SettingItem::Camera) {
    cameraUi.begin(input.touchAvailable());
    enterState(AppState::CameraScreen);
  } else if (r == SettingsScreenState::Result::Open && settingsUi.item == SettingItem::Battery) {
    enterState(AppState::BatteryScreen);
  }
  lastRender = 0;
}

void onInputEngine(InputEvent ev) {
  const bool tapped = ev == InputEvent::TapLeft || ev == InputEvent::TapCenter || ev == InputEvent::TapRight;
  const DeviceScreenState::Action a = tapped ? engineUi.tap(input.lastTapX(), input.lastTapY(), engineFoundCount)
                                             : engineUi.handle(toKey(ev), engineFoundCount);
  if (a == DeviceScreenState::Action::Back) {
    enterState(AppState::SettingsScreen);  // (the list keeps its place)
  } else if (a == DeviceScreenState::Action::Scan) {
    engineFoundCount = 0;
    ObdBle::startScan();
  } else if (a == DeviceScreenState::Action::Choose && engineUi.chosen >= 0 && engineUi.chosen < engineFoundCount) {
    ObdBle::choose(engineFound[engineUi.chosen]);
  } else if (a == DeviceScreenState::Action::Forget) {
    ObdBle::forget();
  }
  lastRender = 0;
}

void onInputCamera(InputEvent ev) {
  const bool tapped = ev == InputEvent::TapLeft || ev == InputEvent::TapCenter || ev == InputEvent::TapRight;
  const DeviceScreenState::Action a = tapped ? cameraUi.tap(input.lastTapX(), input.lastTapY(), cameraFoundCount)
                                             : cameraUi.handle(toKey(ev), cameraFoundCount);
  if (a == DeviceScreenState::Action::Back) {
    enterState(AppState::SettingsScreen);
  } else if (a == DeviceScreenState::Action::Scan) {
    cameraFoundCount = 0;
    CameraBle::startScan();
  } else if (a == DeviceScreenState::Action::Choose && cameraUi.chosen >= 0 && cameraUi.chosen < cameraFoundCount) {
    CameraBle::choose(cameraFound[cameraUi.chosen]);
  } else if (a == DeviceScreenState::Action::Forget) {
    CameraBle::forget();
  }
  lastRender = 0;
}

void onInputBattery() {
  enterState(AppState::SettingsScreen);
  lastRender = 0;
}

void onInputSetup(InputEvent ev) {
  SettingsData s = Settings::get();
  if (setupUi.handle(toKey(ev), s)) {
    Settings::set(s);
    applySettings();  // the screen turns round as soon as the way up is chosen
    if (s.setupDone) {
      DEBUG_PRINTF("Setup: %s, %s\n", settings::upsideDown(s) ? "upside down" : "normal",
                   s.units == SpeedUnits::Mph ? "mph" : "km/h");
      enterState(AppState::GpsWait);
    }
  }
  lastRender = 0;
}

void renderList() {
  settingsUi.setPeople(Profiles::get());  // (the car may have been picked by its VIN meanwhile)
  display.drawSettings(Settings::get(), settingsUi, input.touchAvailable(),
                       gps.hasFix() ? (int64_t)settings::localTimeOfDayMs(gps.lastFix().gpsTimeMs,
                                                                           Settings::get().utcOffsetMin)
                                    : -1);
}

void renderEngine() {
  if (engineUi.page == DeviceScreenState::Page::Find) engineFoundCount = ObdBle::found(engineFound, ObdBle::MAX_FOUND);
  display.drawEngineData(engineUi, ObdBle::status(), engineFound, engineFoundCount, ObdBle::scanning(), input.touchAvailable());
}

void renderCamera() {
  if (cameraUi.page == DeviceScreenState::Page::Find) cameraFoundCount = CameraBle::found(cameraFound, CameraBle::MAX_FOUND);
  display.drawCamera(cameraUi, CameraBle::status(), Settings::get().cameraWithSessions, cameraFound, cameraFoundCount,
                     CameraBle::scanning(), input.touchAvailable());
}

void renderBattery() {
  display.drawBattery(Power::batteryReport(), input.touchAvailable());
}

void renderSetup() {
  display.drawSetup(setupUi.step, setupUi.highlight, input.touchAvailable());
}

bool followDrag(uint32_t now) {
  if (state == AppState::SettingsScreen && input.dragging()) {
    SleepControl::noteActivity(now);
    settingsUi.drag(input.dragDy());
    listDragged = true;
  } else if (listDragged) {
    settingsUi.dragEnd();
    listDragged = false;
    lastRender = 0;  // draw the list where it ended
  }
  return listDragged;
}

}  // namespace SettingsScreens
