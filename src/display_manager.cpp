#include "display_manager.h"
#include "epd_driver.h"
#include "firasans.h"

void DisplayManager::init() {
    framebuffer = (uint8_t*)ps_calloc(sizeof(uint8_t), EPD_WIDTH * EPD_HEIGHT / 2);
    if (!framebuffer) {
        Serial.println("Failed to allocate framebuffer!");
        return;
    }
    epd_init();
    Serial.println("Display initialized");
}

void DisplayManager::clearFramebuffer() {
    if (framebuffer) {
        memset(framebuffer, 0xFF, EPD_WIDTH * EPD_HEIGHT / 2);
    }
}

void DisplayManager::renderToScreen() {
    epd_poweron();
    epd_clear();
    epd_draw_grayscale_image(epd_full_screen(), framebuffer);
    epd_poweroff_all();
}

void DisplayManager::showArrivals(const std::vector<BusArrival>& arrivals) {
    clearFramebuffer();

    int32_t cursor_x = 40;
    int32_t cursor_y = 80;

    // Title
    writeln((GFXfont*)&FiraSans, "Route 6 - 15 Falls Rd", &cursor_x, &cursor_y, framebuffer);

    cursor_y += 30;
    epd_draw_hline(40, cursor_y, EPD_WIDTH - 80, 0, framebuffer);
    cursor_y += 40;

    if (arrivals.empty()) {
        cursor_x = 40;
        writeln((GFXfont*)&FiraSans, "No upcoming arrivals", &cursor_x, &cursor_y, framebuffer);
    } else {
        for (size_t i = 0; i < arrivals.size() && i < 5; i++) {
            cursor_x = 40;

            char line[128];
            if (arrivals[i].minutesAway <= 0) {
                snprintf(line, sizeof(line), "%s  NOW",
                    arrivals[i].formattedTime.c_str());
            } else if (arrivals[i].minutesAway == 1) {
                snprintf(line, sizeof(line), "%s  (1 min)",
                    arrivals[i].formattedTime.c_str());
            } else {
                snprintf(line, sizeof(line), "%s  (%d min)",
                    arrivals[i].formattedTime.c_str(),
                    arrivals[i].minutesAway);
            }

            writeln((GFXfont*)&FiraSans, line, &cursor_x, &cursor_y, framebuffer);
            cursor_y += 60;
        }
    }

    renderToScreen();
}

void DisplayManager::showError(const char* message) {
    clearFramebuffer();

    int32_t cursor_x = 40;
    int32_t cursor_y = EPD_HEIGHT / 2;

    char line[128];
    snprintf(line, sizeof(line), "Error: %s", message);
    writeln((GFXfont*)&FiraSans, line, &cursor_x, &cursor_y, framebuffer);

    renderToScreen();
}

void DisplayManager::showLoading() {
    clearFramebuffer();

    int32_t cursor_x = EPD_WIDTH / 2 - 80;
    int32_t cursor_y = EPD_HEIGHT / 2;

    writeln((GFXfont*)&FiraSans, "Loading...", &cursor_x, &cursor_y, framebuffer);

    renderToScreen();
}
