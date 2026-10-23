#include "serial_console.h"
#include "board_config.h"
#include "power_runtime.h"
#include <cstring>

void SerialConsole::beginWifiSetup() {
  if (setupStep != SetupStep::None) return;
  pendingSsid = "";
  setupStep = SetupStep::Ssid;
  Serial.println(F("Wi-Fi setup. Type /cancel to leave setup."));
  Serial.println(F("Enter SSID (case-sensitive), then press Enter:"));
}

void SerialConsole::handleSetupInput(const String &line) {
  if (line == "/cancel") {
    setupStep = SetupStep::None;
    pendingSsid = "";
    Serial.println(F("Wi-Fi setup cancelled; existing settings retained."));
    return;
  }
  if (setupStep == SetupStep::Ssid) {
    // Ignore empty input without repeatedly printing the setup prompt.
    if (line.length() == 0) return;
    if (line.length() > 32) {
      Serial.println(F("SSID must contain 1..32 bytes. Enter SSID:"));
      return;
    }
    pendingSsid = line;
    setupStep = SetupStep::Password;
    Serial.println(F("Enter Wi-Fi password (case-sensitive), then Enter."));
    Serial.println(F("Firmware does not echo it. Empty = open network:"));
    return;
  }
  if (!ControllerNetwork::validPassword(line)) {
    Serial.println(F("Use 8..63 bytes, a 64-digit hex key, or an empty line."));
    Serial.println(F("Enter password again, or /cancel:"));
    return;
  }
  if (!network.setCredentials(pendingSsid, line)) return;
  pendingSsid = "";
  setupStep = SetupStep::None;
  Serial.println(F("Wi-Fi credentials saved across power loss."));
  network.connect();
}

void SerialConsole::printHelp() {
  Serial.print(F("Room Controller, IR GPIO "));
  Serial.print(kIrLed);
  Serial.print(F(", temperature GPIO "));
  Serial.print(kTemperaturePin);
  Serial.println(F(", 115200 8-N-1. Commands:"));
  Serial.println(F("  on / off / power toggle / temp 16..30|up|down"));
  Serial.println(F("  mode auto|cool|dry|fan|next"));
  Serial.println(F("  fan auto|low|medium|high|next"));
  Serial.println(F("  capacity 100|110|80|60|40|next"));
  Serial.println(F("  horizontal|vertical|light|turbo|econo|sleep"));
  Serial.println(F("    on|off|toggle"));
  Serial.println(F("    Turbo expires after 20 minutes; automatic retry waits 5 minutes."));
  Serial.println(F("  state / send / load <24 hexadecimal digits> / help"));
  Serial.println(F("  sensor   (measured temperature on GPIO 22; updates every 5s)"));
  Serial.println(F("  autocool on|off|toggle (AC on in Cool mode; valid sensor required)"));
  Serial.println(F("    Recovery: 5 min settling + 15 min measuring; Off waits 5/10/20/30 min."));
  Serial.println(F("    Automatic IR: 3 transmissions, 2 seconds apart; new commands cancel repeats."));
  Serial.println(F("  wifi setup / wifi status / wifi reconnect"));
  Serial.println(F("  wifi forget"));
  Serial.println(F("  hostname [new-name]   (without .local; saved in flash)"));
  Serial.println(F("  lamp scan / lamp pair <number> / lamp status / lamp forget"));
  Serial.println(F("  lamp connect / lamp reconnect / lamp disconnect / lamp on / lamp off"));
  Serial.println(F("  lamp color <name|#RRGGBB>"));
  Serial.println(F("  lamp brightness 0..100 / lamp effect 0..212 / lamp speed 0..100"));
}

void SerialConsole::processSerialCommand(String line) {
  PowerRuntime::activity();
  if (setupStep != SetupStep::None) {
    handleSetupInput(line);  // Preserve case and spaces in credentials.
    return;
  }
  line.trim();
  if (line.length() == 0) return;
  const int space = line.indexOf(' ');
  String command = space < 0 ? line : line.substring(0, space);
  String value = space < 0 ? "" : line.substring(space + 1);
  command.toLowerCase();
  value.trim();
  if (command == "wifi") {
    value.toLowerCase();
    if (value == "setup") {
      beginWifiSetup();
    } else if (value == "status" || value.length() == 0) {
      network.printStatus();
    } else if (value == "reconnect") {
      network.connect();
    } else if (value == "forget") {
      if (network.forget()) {
        network.connect();  // Disconnect and stop serving.
        Serial.println(F("Saved Wi-Fi credentials forgotten."));
        beginWifiSetup();
      }
    } else {
      printHelp();
    }
    return;
  }
  if (command == "hostname") {
    if (!value.length()) {
      network.printStatus();
      return;
    }
    value.toLowerCase();
    if (!ControllerNetwork::validHostname(value)) {
      Serial.println(F("Use 1..63 letters/digits/hyphens, without .local."));
      Serial.println(F("The first and last character cannot be a hyphen."));
      return;
    }
    if (value == network.hostname()) {
      network.printStatus();
      return;
    }
    if (network.setHostname(value)) {
      Serial.println(F("Hostname saved. Reconnecting with the new name."));
      network.connect();
    }
    return;
  }
  if (command == "help" && !value.length()) {
    printHelp();
    return;
  }
  if (command == "sensor" && !value.length()) {
    printTemperature();
    return;
  }
  if (command == "lamp") {
    value.toLowerCase();
    String message;
    lamp.command(value, true, message);
    Serial.println(message);
    if (!value.length() || value == "status") lamp.printStatus();
    return;
  }
  String message;
  if (acController.command(line, &message, sensor.snapshot())) {
    Serial.println(message);
    printState();
  } else {
    Serial.println(message);
  }
}

void SerialConsole::read() {
  static char line[192];
  static size_t length = 0;
  static bool overflow = false;
  static bool previousCr = false;
  if (PowerRuntime::serialWake(setupActive())) {
    memset(line, 0, sizeof(line));
    length = 0;
    overflow = previousCr = false;
  }

  // A noisy or continuously active serial port must not starve the main loop.
  size_t processed = 0;
  while (Serial.available() && processed++ < 64) {
    const uint8_t c = Serial.read();
    if (c == '\n' && previousCr) {
      previousCr = false;  // CRLF is one line, including password prompts.
      continue;
    }
    previousCr = c == '\r';
    if (c == '\r' || c == '\n') {
      if (overflow) {
        Serial.println(F("Command too long; discarded."));
      } else if (length || setupStep != SetupStep::None) {
        line[length] = '\0';
        processSerialCommand(String(line));
      }
      std::memset(line, 0, sizeof(line));
      length = 0;
      overflow = false;
      // An input burst can contain many short IR commands. Limit execution,
      // not just bytes, so sensors and HTTP get a turn between commands.
      break;
    } else if (!overflow && (c == '\b' || c == 127)) {
      if (length) length--;
    } else if (!overflow && c >= ' ') {
      if (length + 1 < sizeof(line)) line[length++] = c;
      else overflow = true;
    }
  }
}

void SerialConsole::printTemperature() { sensor.printStatus(Serial); }

void SerialConsole::printState() {
  acController.printState();
  printTemperature();
}

