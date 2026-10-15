#pragma once
#include "ac_controller.h"
#include "temperature_sensor.h"
#include "lotus_lamp.h"
#include "controller_network.h"

class SerialConsole {
 public:
  SerialConsole(AcController& ac, TemperatureSensor& sensor,
                LotusLampController& lamp, ControllerNetwork& network)
      : acController(ac), sensor(sensor), lamp(lamp), network(network) {}
  void read();
  void beginWifiSetup();
  void printHelp();
  void printState();
  bool setupActive() const { return setupStep != SetupStep::None; }
 private:
  AcController& acController;
  TemperatureSensor& sensor;
  LotusLampController& lamp;
  ControllerNetwork& network;
  enum class SetupStep { None, Ssid, Password };
  SetupStep setupStep = SetupStep::None;
  String pendingSsid;
  void handleSetupInput(const String& line);
  void processSerialCommand(String line);
  void printTemperature();
};
