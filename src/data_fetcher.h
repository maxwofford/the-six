#ifndef DATA_FETCHER_H
#define DATA_FETCHER_H

#include <Arduino.h>
#include <vector>

struct BusArrival {
    String routeId;
    String formattedTime;
    int minutesAway;
    bool isPrediction;    // True if this is a real-time prediction (vs scheduled)
    bool isLastBus;       // True if this is the last bus of the day
    bool isScheduled;     // True if this is from GTFS schedule (no live tracking)
    bool isUnknownNext;   // True if we don't have schedule data to know if another bus is coming
};

class DataFetcher {
public:
    bool fetch(std::vector<BusArrival>& arrivals);

private:
    static const char* API_URL;
};

#endif
