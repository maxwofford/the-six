#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <Arduino.h>

class WiFiManager {
  public:
    bool connect();
    void disconnect();
};

#endif