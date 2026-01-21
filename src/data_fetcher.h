#ifndef DATA_FETCHER_H
#define DATA_FETCHER_H

#include <Arduino.h>
#include <vector>

struct BusArrival {
    String routeId;
    String formattedTime;
    int minutesAway;
    bool isPrediction;
};

class DataFetcher {
public:
    bool fetch(std::vector<BusArrival>& arrivals);

private:
    static const char* API_URL;
};

#endif
