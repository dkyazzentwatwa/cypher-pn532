#pragma once

#include <Arduino.h>
#include <Preferences.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <esp_system.h>

namespace CypherPuter {
static constexpr const char* kPrefsNamespace = "cyputeros";
static constexpr const char* kReturnFlag = "returnOnce";
static constexpr const char* kBootToApp = "bootToApp";

inline void markReturnToLauncher() {
  Preferences prefs;
  if (prefs.begin(kPrefsNamespace, false)) {
    prefs.putBool(kReturnFlag, true);
    prefs.putBool(kBootToApp, false);
    prefs.end();
  }
}

inline bool setLauncherBootPartition() {
  const esp_partition_t* launcher = esp_partition_find_first(
      ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_0, "app0");
  if (!launcher) {
    launcher = esp_partition_find_first(
        ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_0, nullptr);
  }
  return launcher && esp_ota_set_boot_partition(launcher) == ESP_OK;
}

inline void returnToLauncher(uint32_t delayMs = 250) {
  markReturnToLauncher();
  setLauncherBootPartition();
  if (delayMs > 0) {
    delay(delayMs);
  }
  ESP.restart();
}
}  // namespace CypherPuter

namespace CypherOs = CypherPuter;

inline void cypherPuterReturnToLauncher(uint32_t delayMs = 250) {
  CypherPuter::returnToLauncher(delayMs);
}

inline void cypherOsReturnToLauncher(uint32_t delayMs = 250) {
  CypherPuter::returnToLauncher(delayMs);
}
