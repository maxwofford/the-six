#include <Arduino.h>
#include "wifi_manager.h"
#include "display_manager.h"
#include "data_fetcher.h"

WiFiManager wifi;
DisplayManager display;
DataFetcher fetcher;

const unsigned long REFRESH_INTERVAL = 2 * 60 * 1000; // 2 minutes
unsigned long lastRefresh = 0;

void fetchAndDisplay() {
    Serial.println("Fetching arrivals...");

    if (!wifi.connect()) {
        display.showError("WiFi failed");
        return;
    }

    std::vector<BusArrival> arrivals;
    if (fetcher.fetch(arrivals)) {
        display.showArrivals(arrivals);
    } else {
        display.showError("Fetch failed");
    }

    wifi.disconnect();
}

void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("The Six - Bus Arrival Display");

    display.init();
    display.showLoading();

    fetchAndDisplay();
    lastRefresh = millis();
}

void loop() {
    unsigned long now = millis();

    if (now - lastRefresh >= REFRESH_INTERVAL) {
        fetchAndDisplay();
        lastRefresh = now;
    }

    delay(1000);
}
