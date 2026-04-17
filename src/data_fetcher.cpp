#include "data_fetcher.h"
#include <HTTPClient.h>
#include <ArduinoJson.h>

const char* DataFetcher::API_URL = "https://maps.trilliumtransit.com/gtfsmap-realtime/feed/ccta-vt-us/arrivals?stopCode=805574&stopID=805574";

bool DataFetcher::fetch(std::vector<BusArrival>& arrivals) {
    HTTPClient http;
    http.begin(API_URL);
    http.setTimeout(10000);

    int httpCode = http.GET();

    if (httpCode != HTTP_CODE_OK) {
        Serial.printf("HTTP error: %d\n", httpCode);
        http.end();
        return false;
    }

    String payload = http.getString();
    http.end();

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, payload);

    if (error) {
        Serial.printf("JSON parse error: %s\n", error.c_str());
        return false;
    }

    if (doc["status"] != "success") {
        Serial.println("API returned non-success status");
        return false;
    }

    JsonArray data = doc["data"].as<JsonArray>();

    // Only overwrite arrivals after successful parse
    std::vector<BusArrival> newArrivals;
    for (JsonObject arrival : data) {
        BusArrival bus;
        bus.routeId = arrival["route_id"].as<const char*>();
        bus.formattedTime = arrival["formattedTime"].as<const char*>();
        bus.isPrediction = arrival["isPrediction"] | false;

        // Calculate minutes away from unixTime
        unsigned long arrivalTime = arrival["unixTime"];
        unsigned long now = time(nullptr);
        bus.minutesAway = (arrivalTime - now) / 60;

        newArrivals.push_back(bus);
    }

    arrivals = std::move(newArrivals);
    Serial.printf("Fetched %d arrivals\n", arrivals.size());
    return true;
}
