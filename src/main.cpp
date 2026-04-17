#include <Arduino.h>
#include <WiFi.h>
#include <time.h>
#include <algorithm>
#include <rom/miniz.h>
#include "wifi_manager.h"
#include "display_manager.h"
#include "data_fetcher.h"
#include "gtfs_schedule.h"
#include "transit_config.h"
#include "secrets.h"

#ifndef BOARD_HAS_PSRAM
#error "Please enable PSRAM!"
#endif

WiFiManager wifi;
DisplayManager display;
DataFetcher fetcher;
GTFSSchedule schedule(ROUTE_ID, STOP_ID);

// Walking time to bus stop (subtracted from arrival times)
const int WALKING_TIME_OFFSET = 2;  // minutes

// Display updates every 1 minute (to update "X min ago" and countdown)
const unsigned long DISPLAY_INTERVAL = 1 * 60 * 1000;
unsigned long lastDisplayUpdate = 0;

// Data fetch intervals based on next bus proximity
const unsigned long FETCH_INTERVAL_URGENT = 1 * 60 * 1000;   // Bus < 10 min: every 1 min
const unsigned long FETCH_INTERVAL_SOON = 5 * 60 * 1000;     // Bus < 30 min: every 5 min
const unsigned long FETCH_INTERVAL_LATER = 15 * 60 * 1000;   // Bus > 60 min: every 15 min
unsigned long lastFetch = 0;

// Cached arrivals and fetch timestamp
std::vector<BusArrival> cachedArrivals;
unsigned long fetchTimestamp = 0;  // millis() when data was fetched

// NTP settings (Eastern Time)
const char* ntpServer = "pool.ntp.org";
const long gmtOffset_sec = -5 * 3600;  // EST = UTC-5
const int daylightOffset_sec = 3600;    // +1 hour for DST

void syncTime() {
    Serial.println("Syncing time with NTP...");
    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);

    struct tm timeinfo;
    int retries = 0;
    while (!getLocalTime(&timeinfo) && retries < 10) {
        Serial.println("Waiting for time...");
        delay(1000);
        retries++;
    }

    if (retries < 10) {
        Serial.println(&timeinfo, "Time synced: %A, %B %d %Y %H:%M:%S");
    } else {
        Serial.println("Failed to sync time!");
    }
}

#ifdef DEMO_MODE

void runDemo() {
    Serial.println("runDemo() starting...");
    Serial.printf("Before init - Heap: %u, PSRAM: %u\n", ESP.getFreeHeap(), ESP.getFreePsram());
    display.init();
    Serial.println("Display initialized");

    #if DEMO_MODE == 1
    // Demo: No arrivals
    Serial.println("Demo: No arrivals");
    std::vector<BusArrival> empty;
    display.showArrivals(empty);

    #elif DEMO_MODE == 2
    // Demo: Arrival in 1 hour
    Serial.println("Demo: Arrival in 1 hour");
    std::vector<BusArrival> arrivals;
    BusArrival bus1;
    bus1.routeId = "6";
    bus1.formattedTime = "10:30 AM";
    bus1.minutesAway = 60;
    bus1.isPrediction = true;
    bus1.isLastBus = false;
    arrivals.push_back(bus1);

    BusArrival bus2;
    bus2.routeId = "6";
    bus2.formattedTime = "11:00 AM";
    bus2.minutesAway = 90;
    bus2.isPrediction = true;
    bus2.isLastBus = false;
    arrivals.push_back(bus2);

    display.showArrivals(arrivals);

    #elif DEMO_MODE == 3
    // Demo: Arrival in 5 min
    Serial.println("Demo: Arrival in 5 min");
    std::vector<BusArrival> arrivals;
    BusArrival bus1;
    bus1.routeId = "6";
    bus1.formattedTime = "9:35 AM";
    bus1.minutesAway = 5;
    bus1.isPrediction = true;
    bus1.isLastBus = false;
    arrivals.push_back(bus1);

    BusArrival bus2;
    bus2.routeId = "6";
    bus2.formattedTime = "10:55 AM";
    bus2.minutesAway = 80;
    bus2.isPrediction = true;
    bus2.isLastBus = false;
    arrivals.push_back(bus2);

    display.showArrivals(arrivals);

    #elif DEMO_MODE == 4
    // Demo: No WiFi
    Serial.println("Demo: No WiFi");
    display.showError("No WiFi");

    #elif DEMO_MODE == 5
    // Demo: Last bus
    Serial.println("Demo: Last bus");
    std::vector<BusArrival> arrivals;
    BusArrival bus1;
    bus1.routeId = "6";
    bus1.formattedTime = "11:45 PM";
    bus1.minutesAway = 12;
    bus1.isPrediction = true;
    bus1.isLastBus = true;  // This is the last bus!
    arrivals.push_back(bus1);
    display.showArrivals(arrivals);

    #elif DEMO_MODE == 6
    // Demo: Font test
    Serial.println("Demo: Font test");
    display.showFontTest();

    #elif DEMO_MODE == 7
    // Demo: GTFS timing test - shows progress and results on display
    Serial.println("Demo: GTFS timing test");
    Serial.printf("Initial: Heap=%dK PSRAM=%dK\n", ESP.getFreeHeap() / 1024, ESP.getFreePsram() / 1024);

    display.showProgressTime("Connecting", 0);

    unsigned long connectStart = millis();
    // Show elapsed time while connecting
    while (!WiFi.isConnected()) {
        if (!wifi.connect()) {
            display.showError("WiFi failed");
            return;
        }
        display.showProgressTime("Connecting", (millis() - connectStart) / 1000);
    }
    syncTime();

    Serial.printf("After WiFi: PSRAM=%dK\n", ESP.getFreePsram() / 1024);

    unsigned long startTime = millis();

    // Progress callback updates the display
    auto progressCb = [](const char* phase, int pct) {
        Serial.printf("%s %d%% (PSRAM=%dK)\n", phase, pct, ESP.getFreePsram() / 1024);
        display.showProgress(phase, pct);
    };

    bool success = schedule.fetch(progressCb);
    unsigned long elapsed = millis() - startTime;
    wifi.disconnect();

    // Show final result
    char msg[32];
    snprintf(msg, sizeof(msg), "%s %.1fs", success ? "Done" : "FAIL", elapsed / 1000.0);
    display.showError(msg);
    Serial.printf("Result: %s, PSRAM=%dK\n", msg, ESP.getFreePsram() / 1024);

    // Stay here forever - don't let watchdog or anything restart us
    while (true) {
        delay(10000);
        yield();
    }

    #elif DEMO_MODE == 8
    // Demo: Memory benchmark (use small font)
    Serial.println("BENCHMARK MODE 8");

    // Test 1: Show memory
    char msg[64];
    snprintf(msg, sizeof(msg), "Heap:%uK PSRAM:%uK",
             ESP.getFreeHeap()/1024, ESP.getFreePsram()/1024);
    display.showError(msg, true);
    Serial.println(msg);
    delay(3000);

    // Test 2: PSRAM alloc
    display.showError("Testing PSRAM alloc...", true);
    delay(500);

    size_t maxPsram = 0;
    for (size_t sz = 1; sz <= 8; sz++) {
        void* p = ps_malloc(sz * 1024 * 1024);
        if (p) {
            maxPsram = sz;
            free(p);
            Serial.printf("PSRAM %uMB: OK\n", sz);
        } else {
            Serial.printf("PSRAM %uMB: FAIL\n", sz);
            break;
        }
        yield();
    }
    snprintf(msg, sizeof(msg), "PSRAM max: %uMB", maxPsram);
    display.showError(msg, true);
    Serial.println(msg);
    delay(3000);

    // Test 3: Heap alloc
    display.showError("Testing heap alloc...", true);
    delay(500);

    size_t maxHeap = 0;
    for (size_t sz = 50; sz <= 350; sz += 50) {
        void* p = malloc(sz * 1024);
        if (p) {
            maxHeap = sz;
            free(p);
            Serial.printf("Heap %uK: OK\n", sz);
        } else {
            Serial.printf("Heap %uK: FAIL\n", sz);
            break;
        }
        yield();
    }
    snprintf(msg, sizeof(msg), "Heap max: %uK", maxHeap);
    display.showError(msg, true);
    Serial.println(msg);
    delay(3000);

    // Final summary
    snprintf(msg, sizeof(msg), "PSRAM:%uMB Heap:%uK", maxPsram, maxHeap);
    display.showError(msg, true);
    Serial.println("=== Benchmark done ===");

    while (true) {
        delay(10000);
        yield();
    }

    #endif
}

void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println("The Six - DEMO MODE");
    runDemo();
}

void loop() {
    delay(1000);
}

#else
// Production mode

// Store original minutesAway values at fetch time
std::vector<int> originalMinutesAway;

// Track last GTFS schedule refresh by calendar day (tm_yday)
int lastGTFSFetchDay = -1;

// Calculate fetch interval based on soonest bus arrival
unsigned long calculateFetchInterval() {
    if (originalMinutesAway.empty()) {
        return FETCH_INTERVAL_URGENT;  // No data, retry every 1 min
    }

    // Adjust for time elapsed since fetch
    int minutesSinceFetch = (millis() - fetchTimestamp) / 60000;
    int soonest = max(0, originalMinutesAway[0] - minutesSinceFetch);

    if (soonest < 10) {
        Serial.println("Fetch interval: 1 min (bus < 10 min away)");
        return FETCH_INTERVAL_URGENT;
    } else if (soonest < 30) {
        Serial.println("Fetch interval: 5 min (bus < 30 min away)");
        return FETCH_INTERVAL_SOON;
    } else {
        Serial.println("Fetch interval: 15 min (bus > 30 min away)");
        return FETCH_INTERVAL_LATER;
    }
}

// Check if GTFS schedule needs refresh (first boot + daily at 3am)
bool shouldFetchGTFS() {
    if (!schedule.hasSchedule()) return true;  // Never loaded

    struct tm timeinfo;
    if (!getLocalTime(&timeinfo)) return false;

    // Refresh daily after 3am
    if (timeinfo.tm_hour >= 3 && timeinfo.tm_yday != lastGTFSFetchDay) {
        return true;
    }
    return false;
}

// Fetch GTFS and record the day
void fetchGTFSSchedule() {
    Serial.println("Fetching GTFS schedule...");
    int dotState = 1;
    display.showLoadingIndicator(dotState);

    auto progressCb = [&dotState](const char* phase, int pct) {
        dotState++;
        display.showLoadingIndicator(dotState);
    };

    if (schedule.fetch(progressCb)) {
        struct tm timeinfo;
        if (getLocalTime(&timeinfo)) {
            lastGTFSFetchDay = timeinfo.tm_yday;
        }
        Serial.println("GTFS schedule loaded");
    } else {
        Serial.println("GTFS schedule fetch failed (will retry later)");
    }

    display.showLoadingIndicator(0);
}

// Convert arrival time to minutes from midnight
int arrivalToMinutesFromMidnight(const BusArrival& arrival) {
    // Parse formattedTime like "11:28am" or "5:30pm"
    String timeStr = arrival.formattedTime;
    int colonPos = timeStr.indexOf(':');
    if (colonPos < 0) return -1;

    int hours = timeStr.substring(0, colonPos).toInt();
    int minutes = timeStr.substring(colonPos + 1, colonPos + 3).toInt();

    // Check for pm (but not 12pm)
    if (timeStr.indexOf("pm") >= 0 && hours != 12) {
        hours += 12;
    }
    // 12am is midnight (0 hours)
    if (timeStr.indexOf("am") >= 0 && hours == 12) {
        hours = 0;
    }

    return hours * 60 + minutes;
}

// Get current time as minutes from midnight
int getCurrentMinutesFromMidnight() {
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo)) {
        return 0;
    }
    return timeinfo.tm_hour * 60 + timeinfo.tm_min;
}

// Format minutes from midnight to display string (e.g., "11:28am")
String formatMinutesToTime(int minutes) {
    int hours = minutes / 60;
    int mins = minutes % 60;
    bool isPM = hours >= 12;

    if (hours > 12) hours -= 12;
    if (hours == 0) hours = 12;

    char buf[16];
    snprintf(buf, sizeof(buf), "%d:%02d%s", hours, mins, isPM ? "pm" : "am");
    return String(buf);
}

// Tolerance for matching predictions to schedule (minutes)
const int SCHEDULE_MATCH_TOLERANCE = 15;

// Process cached arrivals with current schedule data
// Sets isLastBus, isUnknownNext, adds unmatched scheduled arrivals
void processArrivals() {
    fetchTimestamp = millis();

    schedule.resetMatches();
    int currentMinutes = getCurrentMinutesFromMidnight();
    bool hasSchedule = schedule.hasSchedule();

    // Mark predictions and match them to schedule
    for (auto& arrival : cachedArrivals) {
        arrival.isScheduled = false;
        arrival.isUnknownNext = false;

        int arrivalMinutes = arrivalToMinutesFromMidnight(arrival);

        if (hasSchedule) {
            schedule.matchPrediction(arrivalMinutes, SCHEDULE_MATCH_TOLERANCE);
            arrival.isLastBus = schedule.isLastBus(arrivalMinutes, LAST_BUS_TOLERANCE);
            if (arrival.isLastBus) {
                Serial.printf("Last bus detected: %s\n", arrival.formattedTime.c_str());
            }
        } else {
            arrival.isLastBus = false;
        }
    }

    if (hasSchedule) {
        // Add unmatched scheduled arrivals (buses the API didn't predict)
        auto unmatchedArrivals = schedule.getUnmatchedArrivals(currentMinutes);
        for (const auto& scheduled : unmatchedArrivals) {
            BusArrival bus;
            bus.routeId = ROUTE_ID;
            bus.formattedTime = formatMinutesToTime(scheduled.minutesFromMidnight);
            bus.minutesAway = scheduled.minutesFromMidnight - currentMinutes;
            bus.isPrediction = false;
            bus.isScheduled = true;
            bus.isLastBus = schedule.isLastBus(scheduled.minutesFromMidnight, LAST_BUS_TOLERANCE);
            bus.isUnknownNext = false;

            if (bus.minutesAway > 0 && bus.minutesAway < 120) {
                cachedArrivals.push_back(bus);
                Serial.printf("Added scheduled bus: %s (%d min)\n",
                             bus.formattedTime.c_str(), bus.minutesAway);
            }
        }
    }

    // Sort all arrivals by minutesAway
    std::sort(cachedArrivals.begin(), cachedArrivals.end(),
        [](const BusArrival& a, const BusArrival& b) {
            return a.minutesAway < b.minutesAway;
        });

    // Limit to first 2 arrivals for display
    if (cachedArrivals.size() > 2) {
        cachedArrivals.resize(2);
    }

    // If only 1 arrival and no schedule data, mark as unknown next
    if (cachedArrivals.size() == 1 && !hasSchedule) {
        cachedArrivals[0].isUnknownNext = true;
    }

    // Store original minutesAway values
    originalMinutesAway.clear();
    for (const auto& arrival : cachedArrivals) {
        originalMinutesAway.push_back(arrival.minutesAway);
    }

    Serial.printf("Final arrivals: %d (schedule %s)\n",
                  cachedArrivals.size(), hasSchedule ? "loaded" : "pending");
}

// Fetch fresh arrival data from API (does not fetch GTFS)
bool fetchArrivals() {
    Serial.println("Fetching arrivals...");

    if (!wifi.connect()) {
        Serial.println("WiFi failed");
        return false;
    }

    // Sync time on first fetch
    static bool timesynced = false;
    if (!timesynced) {
        syncTime();
        timesynced = true;
    }

    bool success = fetcher.fetch(cachedArrivals);
    wifi.disconnect();

    if (success) {
        processArrivals();
    } else {
        Serial.println("Fetch failed");
    }

    return success;
}

// Full fetch: arrivals + GTFS if needed
bool fetchData() {
    Serial.println("Fetching arrivals...");

    if (!wifi.connect()) {
        Serial.println("WiFi failed");
        return false;
    }

    static bool timesynced = false;
    if (!timesynced) {
        syncTime();
        timesynced = true;
    }

    // Check if GTFS needs refresh (daily at 3am)
    if (shouldFetchGTFS()) {
        fetchGTFSSchedule();
    }

    bool success = fetcher.fetch(cachedArrivals);
    wifi.disconnect();

    if (success) {
        processArrivals();
    } else {
        Serial.println("Fetch failed");
    }

    return success;
}

// Update display with current data
void updateDisplay() {
    int minutesSinceUpdate = (millis() - fetchTimestamp) / 60000;

    // Create display copy with adjusted times (subtract walking offset)
    std::vector<BusArrival> displayArrivals = cachedArrivals;
    for (size_t i = 0; i < displayArrivals.size(); i++) {
        displayArrivals[i].minutesAway = max(0, originalMinutesAway[i] - minutesSinceUpdate - WALKING_TIME_OFFSET);
    }

    display.showArrivals(displayArrivals, minutesSinceUpdate);
    Serial.printf("Display updated (%d min since fetch)\n", minutesSinceUpdate);
}

void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println("The Six - Bus Arrival Display");

    display.init();
    display.showLoading();

    // Step 1: Get arrivals on screen fast (no GTFS yet)
    bool gotArrivals = fetchArrivals();
    if (gotArrivals) {
        updateDisplay();
    }

    // Step 2: Download GTFS schedule in background (~77-145s)
    // Display already showing arrivals; "Updating..." indicator via partial refresh
    if (wifi.connect()) {
        syncTime();
        fetchGTFSSchedule();

        // Retry arrivals if initial fetch failed (WiFi still up)
        if (!gotArrivals) {
            Serial.println("Retrying arrival fetch...");
            if (fetcher.fetch(cachedArrivals)) {
                processArrivals();
                gotArrivals = true;
            }
        }

        wifi.disconnect();

        // Re-process arrivals with schedule data and refresh display
        if (!cachedArrivals.empty()) {
            processArrivals();
            updateDisplay();
        }
    }

    if (!gotArrivals && cachedArrivals.empty()) {
        display.showError("Fetch failed");
    }

    lastFetch = millis();
    lastDisplayUpdate = millis();
}

void loop() {
    unsigned long now = millis();

    // Check if we need to fetch new data
    unsigned long fetchInterval = calculateFetchInterval();
    if (now - lastFetch >= fetchInterval) {
        if (fetchData()) {
            updateDisplay();
        }
        lastFetch = now;
        lastDisplayUpdate = now;
    }
    // Check if we need to update display (every minute)
    else if (now - lastDisplayUpdate >= DISPLAY_INTERVAL) {
        updateDisplay();
        lastDisplayUpdate = now;
    }

    delay(1000);
}

#endif
