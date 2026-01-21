#ifndef DISPLAY_MANAGER_H
#define DISPLAY_MANAGER_H

#include <Arduino.h>
#include <vector>
#include "data_fetcher.h"

class DisplayManager {
public:
    void init();
    void showArrivals(const std::vector<BusArrival>& arrivals);
    void showError(const char* message);
    void showLoading();

private:
    uint8_t* framebuffer = nullptr;
    void clearFramebuffer();
    void renderToScreen();
};

#endif
