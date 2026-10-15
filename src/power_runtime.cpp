#include "power_runtime.h"
#include "power_config.h"
#include <Arduino.h>
#include <esp_pm.h>
#include <esp_sleep.h>
#include <driver/uart.h>

#if ROOM_CONTROLLER_POWER_SAVING && (!CONFIG_PM_ENABLE || !CONFIG_FREERTOS_USE_TICKLESS_IDLE)
#error "Automatic light sleep requires the Arduino + ESP-IDF build and sdkconfig.defaults"
#endif

namespace PowerRuntime {
#if ROOM_CONTROLLER_POWER_SAVING
uint32_t lastUserActivity = 0;
bool cpuBoosted = false;
bool powerManagementReady = false;
esp_pm_lock_handle_t cpuLock = nullptr, awakeLock = nullptr;
uint32_t lastSerialActivity = 0;
bool haveSerialActivity = false;
#endif
void acquireActivePower() {
#if ROOM_CONTROLLER_POWER_SAVING
  if (powerManagementReady && !cpuBoosted) {
    esp_pm_lock_acquire(cpuLock);
    esp_pm_lock_acquire(awakeLock);
    cpuBoosted = true;
  }
#endif
}

void activity() {
#if ROOM_CONTROLLER_POWER_SAVING
  lastUserActivity = millis();
  acquireActivePower();
#endif
}

void begin() {
#if ROOM_CONTROLLER_POWER_SAVING
  // Locks preserve 240 MHz and prevent sleep during IR, BLE, and setup. Keep
  // APB at 80 MHz even at the minimum CPU speed for UART and OneWire timing.
  esp_err_t error = esp_pm_lock_create(ESP_PM_CPU_FREQ_MAX, 0, "controls", &cpuLock);
  if (error == ESP_OK)
    error = esp_pm_lock_create(ESP_PM_NO_LIGHT_SLEEP, 0, "controls", &awakeLock);
  if (error == ESP_OK) error = uart_set_wakeup_threshold(UART_NUM_0, 3);
  if (error == ESP_OK) error = esp_sleep_enable_uart_wakeup(UART_NUM_0);
  const esp_pm_config_esp32_t config = {240, 80, true};
  if (error == ESP_OK) error = esp_pm_configure(&config);
  if (error != ESP_OK) {
    // SDK startup frequency is 240 MHz. Leave it alone if PM cannot start.
    if (cpuLock) { esp_pm_lock_delete(cpuLock); cpuLock = nullptr; }
    if (awakeLock) { esp_pm_lock_delete(awakeLock); awakeLock = nullptr; }
    Serial.printf("[power] Automatic light sleep unavailable: %s\n", esp_err_to_name(error));
    return;
  }
  powerManagementReady = true;
  activity();
  Serial.println(F("[power] Automatic light sleep; CPU 80..240 MHz; Wi-Fi MIN_MODEM"));
  Serial.println(F("[power] Press Enter before serial commands after idle; serial stays awake for 60s."));
#else
  // Keep the opt-in SDK support compiled, but disable DFS and automatic sleep.
#if CONFIG_PM_ENABLE
  const esp_pm_config_esp32_t config = {240, 240, false};
  const esp_err_t error = esp_pm_configure(&config);
  if (error != ESP_OK)
    Serial.printf("[power] Fixed CPU configuration failed: %s\n", esp_err_to_name(error));
#endif
  Serial.println(F("[power] Savings disabled; CPU fixed at 240 MHz; Wi-Fi sleep off"));
#endif
}

void service(bool active) {
#if ROOM_CONTROLLER_POWER_SAVING
  if (!powerManagementReady) return;
  if (active ||
      (haveSerialActivity && millis() - lastSerialActivity < 60000)) {
    acquireActivePower();
  } else if (cpuBoosted && millis() - lastUserActivity >= 5000) {
    // The IDF scheduler/drivers now manage DFS and automatic light sleep;
    // never manually change CPU clocks behind their power locks.
    esp_pm_lock_release(awakeLock);
    esp_pm_lock_release(cpuLock);
    cpuBoosted = false;
  }
#endif
}

bool serialWake(bool setupActive) {
  bool discard = false;
#if ROOM_CONTROLLER_POWER_SAVING
  static bool uartWakeHandled = false;
  const bool uartWake = powerManagementReady &&
      esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_UART;
  if ((uartWake && !uartWakeHandled) || Serial.available()) {
    const bool serialWasIdle = !haveSerialActivity || millis() - lastSerialActivity >= 60000;
    lastSerialActivity = millis();
    haveSerialActivity = true;
    activity();
    if (uartWake && !uartWakeHandled) {
      // UART wake-up loses characters. Discard the wake input so a truncated
      // "lamp off", for example, cannot be interpreted as an AC "off" command.
      // The serial monitor sends a complete line at 115200; let it finish first.
      delay(20);
      uart_flush_input(UART_NUM_0);
      discard = true;
    }
    if (powerManagementReady && !setupActive &&
        (serialWasIdle || (uartWake && !uartWakeHandled)))
      Serial.println(F("Serial awake. Enter your command."));
  }
  uartWakeHandled = uartWake;
#endif
  return discard;
}
}
