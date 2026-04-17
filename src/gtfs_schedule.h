#ifndef GTFS_SCHEDULE_H
#define GTFS_SCHEDULE_H

#include <Arduino.h>
#include <vector>
#include <map>
#include <functional>

// Progress callback: (phase, percent) - phase is "download", "extract", "parse"
typedef std::function<void(const char* phase, int percent)> GTFSProgressCallback;

// A scheduled arrival time
struct ScheduledArrival {
    int minutesFromMidnight;  // e.g., 11:20am = 11*60+20 = 680
    bool matchedToPrediction; // True if a real-time prediction matched this
};

// Stores schedule data for each service type (day of week)
struct ServiceSchedule {
    std::vector<ScheduledArrival> arrivals;  // All arrivals, sorted by time
    int lastBusMinutes = -1;  // Cache of last bus time, -1 means unknown
};

class GTFSSchedule {
public:
    GTFSSchedule(const char* routeId, const char* stopId);

    // Download and parse GTFS data. Call this periodically (e.g., daily).
    // Optional progress callback for UI updates.
    // Returns true if successful.
    bool fetch(GTFSProgressCallback progressCallback = nullptr);

    // Check if a given arrival time is the last bus for today.
    // arrivalMinutes: minutes from midnight (e.g., 23:30 = 23*60+30 = 1410)
    // tolerance: how close to last bus to trigger (default 30 min)
    bool isLastBus(int arrivalMinutes, int tolerance = 30);

    // Get last bus time for today (minutes from midnight), or -1 if unknown
    int getLastBusMinutes();

    // Get all scheduled arrivals for today (after currentMinutes)
    // Returns arrivals sorted by time
    std::vector<ScheduledArrival> getTodayArrivals(int afterMinutes = 0);

    // Match a prediction time to a scheduled arrival (within tolerance)
    // Returns true if matched, marks the scheduled arrival as matched
    bool matchPrediction(int predictionMinutes, int tolerance = 15);

    // Reset all matched flags (call before processing new predictions)
    void resetMatches();

    // Get unmatched scheduled arrivals for today (buses without predictions)
    std::vector<ScheduledArrival> getUnmatchedArrivals(int afterMinutes = 0);

    // Check if we have valid schedule data
    bool hasSchedule() const;

private:
    String routeId;
    String stopId;

    // Schedule data indexed by day of week (0=Sunday, 1=Monday, ..., 6=Saturday)
    ServiceSchedule scheduleByDay[7];

    bool scheduleLoaded = false;

    // Internal parsing helpers
    bool downloadAndParse(GTFSProgressCallback progressCallback = nullptr);
    void parseTripsAndCalendar(char* calendarBuf, size_t calendarSize,
                               char* tripsBuf, size_t tripsSize,
                               std::map<String, std::vector<int>>& tripIdToDays);
    void parseStopTimes(char* buffer, size_t bufferSize,
                        const std::map<String, std::vector<int>>& tripIdToDays);

    // Get current day of week (0=Sunday)
    int getCurrentDayOfWeek();
};

#endif
