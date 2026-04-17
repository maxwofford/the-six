#ifndef DISPLAY_MANAGER_H
#define DISPLAY_MANAGER_H

#include <Arduino.h>
#include <vector>
#include "epd_driver.h"
#include "data_fetcher.h"

class DisplayManager {
public:
    void init();
    void showArrivals(const std::vector<BusArrival>& arrivals, int minutesSinceUpdate = 0);
    void showError(const char* message, bool smallFont = false);
    void showLoading();
    void showProgress(const char* label, int percent);
    void showProgressTime(const char* label, int elapsedSeconds);
    void showFontTest();
    void showLoadingIndicator(int dots);  // 0=hide, 1-3=show with N dots

private:
    uint8_t* framebuffer = nullptr;
    uint8_t* screenBuffer = nullptr;  // Tracks what's currently on the e-paper
    void clearFramebuffer();
    void renderToScreen();
    void renderPartial(Rect_t area);
};

#endif
