#include "display_manager.h"
#include "epd_driver.h"
#include "font_large.h"
#include "font_medium.h"
#include "font_small.h"
#include <ctype.h>

// Convert AM/PM to lowercase am/pm to save horizontal space
String formatTime(const String& time) {
    String result = time;
    result.replace("AM", "am");
    result.replace("PM", "pm");
    return result;
}

void DisplayManager::init() {
    framebuffer = (uint8_t *)ps_calloc(sizeof(uint8_t), EPD_WIDTH * EPD_HEIGHT / 2);
    screenBuffer = (uint8_t *)ps_calloc(sizeof(uint8_t), EPD_WIDTH * EPD_HEIGHT / 2);
    if (!framebuffer || !screenBuffer) {
        Serial.println("Failed to allocate framebuffers!");
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
    // Sync screen buffer so partial updates know what's on display
    if (screenBuffer) {
        memcpy(screenBuffer, framebuffer, EPD_WIDTH * EPD_HEIGHT / 2);
    }
}

void DisplayManager::showArrivals(const std::vector<BusArrival>& arrivals, int minutesSinceUpdate) {
    clearFramebuffer();

    int halfWidth = EPD_WIDTH / 2;

    // Fixed Y positions for layout (display is 540px tall)
    const int timeY = 85;        // Time at top (with padding)
    const int numberY = 320;     // Big number in middle
    const int minLabelY = 420;   // "min" below number
    const int updatedY = 525;    // Updated at bottom

    if (arrivals.empty()) {
        // No arrivals - center message
        int32_t bx = 0, by = 0, bx1, by1, bw, bh;
        get_text_bounds((GFXfont *)&FiraSansMedium, "No buses today", &bx, &by, &bx1, &by1, &bw, &bh, NULL);
        int32_t cursor_x = (EPD_WIDTH - bw) / 2;
        int32_t cursor_y = EPD_HEIGHT / 2;
        writeln((GFXfont *)&FiraSansMedium, "No buses today", &cursor_x, &cursor_y, framebuffer);
    } else if (arrivals.size() == 1 && arrivals[0].isLastBus) {
        // Last bus of the day - show on left with "FINAL BUS" warning on right

        // Left side - time at top
        int32_t cursor_x = 60;
        int32_t cursor_y = timeY;
        writeln((GFXfont *)&FiraSansMedium, formatTime(arrivals[0].formattedTime).c_str(), &cursor_x, &cursor_y, framebuffer);

        // Left side - big number
        char minStr[32];
        if (arrivals[0].minutesAway <= 0) {
            snprintf(minStr, sizeof(minStr), "NOW");
        } else {
            snprintf(minStr, sizeof(minStr), "%d", arrivals[0].minutesAway);
        }
        cursor_x = 60;
        cursor_y = numberY;
        writeln((GFXfont *)&FiraSansBold, minStr, &cursor_x, &cursor_y, framebuffer);

        // Left side - "min" label
        if (arrivals[0].minutesAway > 0) {
            cursor_x = 60;
            cursor_y = minLabelY;
            writeln((GFXfont *)&FiraSansMedium, "min", &cursor_x, &cursor_y, framebuffer);
        }

        // Vertical divider
        epd_draw_vline(halfWidth, 30, EPD_HEIGHT - 80, 0, framebuffer);

        // Right side - Warning icon (triangle with !)
        int triCenterX = halfWidth + (halfWidth / 2);
        int triTopY = 80;
        int triSize = 100;
        int thickness = 6;

        // Outer filled triangle (black)
        epd_fill_triangle(
            triCenterX, triTopY,
            triCenterX - triSize/2, triTopY + triSize,
            triCenterX + triSize/2, triTopY + triSize,
            0, framebuffer);

        // Inner filled triangle (white) to create thick outline
        epd_fill_triangle(
            triCenterX, triTopY + thickness*2,
            triCenterX - triSize/2 + thickness*2, triTopY + triSize - thickness,
            triCenterX + triSize/2 - thickness*2, triTopY + triSize - thickness,
            0xFF, framebuffer);

        // Exclamation mark - thick bar
        int exclaimX = triCenterX - 4;
        int exclaimY = triTopY + 35;
        epd_fill_rect(exclaimX, exclaimY, 8, 40, 0, framebuffer);

        // Exclamation mark - dot
        epd_fill_circle(triCenterX, triTopY + 85, 6, 0, framebuffer);

        // FINAL BUS text (medium size, 100px line height)
        cursor_x = halfWidth + 60;
        cursor_y = 300;
        writeln((GFXfont *)&FiraSansMedium, "FINAL", &cursor_x, &cursor_y, framebuffer);
        cursor_x = halfWidth + 60;
        cursor_y = 410;
        writeln((GFXfont *)&FiraSansMedium, "BUS", &cursor_x, &cursor_y, framebuffer);

    } else if (arrivals.size() == 1 && arrivals[0].isUnknownNext) {
        // Single bus, schedule not loaded yet - show "?" for next bus

        // Left side - time at top
        int32_t cursor_x = 60;
        int32_t cursor_y = timeY;
        writeln((GFXfont *)&FiraSansMedium, formatTime(arrivals[0].formattedTime).c_str(), &cursor_x, &cursor_y, framebuffer);

        // Left side - big number
        char minStr[32];
        if (arrivals[0].minutesAway <= 0) {
            snprintf(minStr, sizeof(minStr), "NOW");
        } else {
            snprintf(minStr, sizeof(minStr), "%d", arrivals[0].minutesAway);
        }
        cursor_x = 60;
        cursor_y = numberY;
        writeln((GFXfont *)&FiraSansBold, minStr, &cursor_x, &cursor_y, framebuffer);

        // Left side - "min" label
        if (arrivals[0].minutesAway > 0) {
            cursor_x = 60;
            cursor_y = minLabelY;
            writeln((GFXfont *)&FiraSansMedium, "min", &cursor_x, &cursor_y, framebuffer);
        }

        // Vertical divider
        epd_draw_vline(halfWidth, 30, EPD_HEIGHT - 80, 0, framebuffer);

        // Right side - large "?" centered
        int32_t bx = 0, by = 0, bx1, by1, bw, bh;
        int rightCenter = halfWidth + halfWidth / 2;
        get_text_bounds((GFXfont *)&FiraSansBold, "?", &bx, &by, &bx1, &by1, &bw, &bh, NULL);
        cursor_x = rightCenter - bw / 2;
        cursor_y = 280;
        writeln((GFXfont *)&FiraSansBold, "?", &cursor_x, &cursor_y, framebuffer);

        // "Next bus?" text
        get_text_bounds((GFXfont *)&FiraSansSmall, "Next bus?", &bx, &by, &bx1, &by1, &bw, &bh, NULL);
        cursor_x = rightCenter - bw / 2;
        cursor_y = 400;
        writeln((GFXfont *)&FiraSansSmall, "Next bus?", &cursor_x, &cursor_y, framebuffer);

    } else if (arrivals.size() == 1) {
        // Single bus (not last bus) - center it
        int centerX = EPD_WIDTH / 2;

        int32_t bx = 0, by = 0, bx1, by1, bw, bh;
        char minStr[32];

        // Time at top (centered)
        String time0 = formatTime(arrivals[0].formattedTime);
        get_text_bounds((GFXfont *)&FiraSansMedium, time0.c_str(), &bx, &by, &bx1, &by1, &bw, &bh, NULL);
        int32_t cursor_x = centerX - bw / 2;
        int32_t cursor_y = timeY;
        writeln((GFXfont *)&FiraSansMedium, time0.c_str(), &cursor_x, &cursor_y, framebuffer);

        // Big number (centered)
        if (arrivals[0].minutesAway <= 0) {
            snprintf(minStr, sizeof(minStr), "NOW");
        } else {
            snprintf(minStr, sizeof(minStr), "%d", arrivals[0].minutesAway);
        }
        bx = 0; by = 0;
        get_text_bounds((GFXfont *)&FiraSansBold, minStr, &bx, &by, &bx1, &by1, &bw, &bh, NULL);
        cursor_x = centerX - bw / 2;
        cursor_y = numberY;
        writeln((GFXfont *)&FiraSansBold, minStr, &cursor_x, &cursor_y, framebuffer);

        // "min" label (centered)
        if (arrivals[0].minutesAway > 0) {
            bx = 0; by = 0;
            get_text_bounds((GFXfont *)&FiraSansMedium, "min", &bx, &by, &bx1, &by1, &bw, &bh, NULL);
            cursor_x = centerX - bw / 2;
            cursor_y = minLabelY;
            writeln((GFXfont *)&FiraSansMedium, "min", &cursor_x, &cursor_y, framebuffer);
        }

    } else {
        // Two buses - one on each side
        // Center of each panel
        int leftCenter = halfWidth / 2;
        int rightCenter = halfWidth + halfWidth / 2;

        int32_t bx = 0, by = 0, bx1, by1, bw, bh;
        char minStr[32];

        // LEFT SIDE - time
        String time0 = formatTime(arrivals[0].formattedTime);
        get_text_bounds((GFXfont *)&FiraSansMedium, time0.c_str(), &bx, &by, &bx1, &by1, &bw, &bh, NULL);
        int32_t cursor_x = leftCenter - bw / 2;
        int32_t cursor_y = timeY;
        writeln((GFXfont *)&FiraSansMedium, time0.c_str(), &cursor_x, &cursor_y, framebuffer);

        // LEFT SIDE - big number
        if (arrivals[0].minutesAway <= 0) {
            snprintf(minStr, sizeof(minStr), "NOW");
        } else {
            snprintf(minStr, sizeof(minStr), "%d", arrivals[0].minutesAway);
        }
        bx = 0; by = 0;
        get_text_bounds((GFXfont *)&FiraSansBold, minStr, &bx, &by, &bx1, &by1, &bw, &bh, NULL);
        cursor_x = leftCenter - bw / 2;
        cursor_y = numberY;
        writeln((GFXfont *)&FiraSansBold, minStr, &cursor_x, &cursor_y, framebuffer);

        // LEFT SIDE - "min" label
        if (arrivals[0].minutesAway > 0) {
            bx = 0; by = 0;
            get_text_bounds((GFXfont *)&FiraSansMedium, "min", &bx, &by, &bx1, &by1, &bw, &bh, NULL);
            cursor_x = leftCenter - bw / 2;
            cursor_y = minLabelY;
            writeln((GFXfont *)&FiraSansMedium, "min", &cursor_x, &cursor_y, framebuffer);
        }

        // Vertical divider
        epd_draw_vline(halfWidth, 30, EPD_HEIGHT - 80, 0, framebuffer);

        // RIGHT SIDE - time
        String time1 = formatTime(arrivals[1].formattedTime);
        bx = 0; by = 0;
        get_text_bounds((GFXfont *)&FiraSansMedium, time1.c_str(), &bx, &by, &bx1, &by1, &bw, &bh, NULL);
        cursor_x = rightCenter - bw / 2;
        cursor_y = timeY;
        writeln((GFXfont *)&FiraSansMedium, time1.c_str(), &cursor_x, &cursor_y, framebuffer);

        // RIGHT SIDE - big number
        if (arrivals[1].minutesAway <= 0) {
            snprintf(minStr, sizeof(minStr), "NOW");
        } else {
            snprintf(minStr, sizeof(minStr), "%d", arrivals[1].minutesAway);
        }
        bx = 0; by = 0;
        get_text_bounds((GFXfont *)&FiraSansBold, minStr, &bx, &by, &bx1, &by1, &bw, &bh, NULL);
        cursor_x = rightCenter - bw / 2;
        cursor_y = numberY;
        writeln((GFXfont *)&FiraSansBold, minStr, &cursor_x, &cursor_y, framebuffer);

        // RIGHT SIDE - "min" label
        if (arrivals[1].minutesAway > 0) {
            bx = 0; by = 0;
            get_text_bounds((GFXfont *)&FiraSansMedium, "min", &bx, &by, &bx1, &by1, &bw, &bh, NULL);
            cursor_x = rightCenter - bw / 2;
            cursor_y = minLabelY;
            writeln((GFXfont *)&FiraSansMedium, "min", &cursor_x, &cursor_y, framebuffer);
        }
    }

    // Show last updated time at bottom left
    int32_t cursor_x = 40;
    int32_t cursor_y = updatedY;
    char timeStr[32];
    if (minutesSinceUpdate == 0) {
        snprintf(timeStr, sizeof(timeStr), "Updated just now");
    } else if (minutesSinceUpdate == 1) {
        snprintf(timeStr, sizeof(timeStr), "Updated 1 min ago");
    } else {
        snprintf(timeStr, sizeof(timeStr), "Updated %d min ago", minutesSinceUpdate);
    }
    writeln((GFXfont *)&FiraSansSmall, timeStr, &cursor_x, &cursor_y, framebuffer);

    renderToScreen();
}

void DisplayManager::showError(const char* message, bool smallFont) {
    clearFramebuffer();

    GFXfont* font = smallFont ? (GFXfont *)&FiraSansSmall : (GFXfont *)&FiraSansMedium;

    int32_t bx = 0, by = 0, bx1, by1, bw, bh;
    get_text_bounds(font, message, &bx, &by, &bx1, &by1, &bw, &bh, NULL);
    int32_t cursor_x = (EPD_WIDTH - bw) / 2;
    int32_t cursor_y = EPD_HEIGHT / 2;

    writeln(font, message, &cursor_x, &cursor_y, framebuffer);

    renderToScreen();
}

void DisplayManager::showLoading() {
    clearFramebuffer();

    int32_t cursor_x = EPD_WIDTH / 2 - 150;
    int32_t cursor_y = EPD_HEIGHT / 2 + 20;

    writeln((GFXfont *)&FiraSansMedium, "Loading...", &cursor_x, &cursor_y, framebuffer);

    renderToScreen();
}

void DisplayManager::showProgress(const char* label, int percent) {
    clearFramebuffer();

    // Label text
    int32_t bx = 0, by = 0, bx1, by1, bw, bh;
    get_text_bounds((GFXfont *)&FiraSansMedium, label, &bx, &by, &bx1, &by1, &bw, &bh, NULL);
    int32_t cursor_x = (EPD_WIDTH - bw) / 2;
    int32_t cursor_y = EPD_HEIGHT / 2 - 60;
    writeln((GFXfont *)&FiraSansMedium, label, &cursor_x, &cursor_y, framebuffer);

    // Progress bar background
    int barWidth = 400;
    int barHeight = 30;
    int barX = (EPD_WIDTH - barWidth) / 2;
    int barY = EPD_HEIGHT / 2;
    epd_draw_rect(barX, barY, barWidth, barHeight, 0, framebuffer);

    // Progress bar fill
    int fillWidth = (barWidth - 4) * percent / 100;
    if (fillWidth > 0) {
        epd_fill_rect(barX + 2, barY + 2, fillWidth, barHeight - 4, 0, framebuffer);
    }

    // Percentage text (smaller font)
    char pctStr[16];
    snprintf(pctStr, sizeof(pctStr), "%d%%", percent);
    get_text_bounds((GFXfont *)&FiraSansSmall, pctStr, &bx, &by, &bx1, &by1, &bw, &bh, NULL);
    cursor_x = (EPD_WIDTH - bw) / 2;
    cursor_y = barY + barHeight + 50;
    writeln((GFXfont *)&FiraSansSmall, pctStr, &cursor_x, &cursor_y, framebuffer);

    renderToScreen();
}

void DisplayManager::showProgressTime(const char* label, int elapsedSeconds) {
    clearFramebuffer();

    // Label text
    int32_t bx = 0, by = 0, bx1, by1, bw, bh;
    get_text_bounds((GFXfont *)&FiraSansMedium, label, &bx, &by, &bx1, &by1, &bw, &bh, NULL);
    int32_t cursor_x = (EPD_WIDTH - bw) / 2;
    int32_t cursor_y = EPD_HEIGHT / 2 - 30;
    writeln((GFXfont *)&FiraSansMedium, label, &cursor_x, &cursor_y, framebuffer);

    // Elapsed time text (smaller font)
    char timeStr[32];
    snprintf(timeStr, sizeof(timeStr), "%ds", elapsedSeconds);
    get_text_bounds((GFXfont *)&FiraSansSmall, timeStr, &bx, &by, &bx1, &by1, &bw, &bh, NULL);
    cursor_x = (EPD_WIDTH - bw) / 2;
    cursor_y = EPD_HEIGHT / 2 + 50;
    writeln((GFXfont *)&FiraSansSmall, timeStr, &cursor_x, &cursor_y, framebuffer);

    renderToScreen();
}

void DisplayManager::showFontTest() {
    clearFramebuffer();

    // Large font test (left side)
    int32_t cursor_x = 40;
    int32_t cursor_y = 100;
    writeln((GFXfont *)&FiraSansBold, "123", &cursor_x, &cursor_y, framebuffer);

    cursor_x = 40;
    cursor_y = 200;
    writeln((GFXfont *)&FiraSansBold, "NOW", &cursor_x, &cursor_y, framebuffer);

    // Medium font test (right side)
    cursor_x = EPD_WIDTH / 2 + 40;
    cursor_y = 80;
    writeln((GFXfont *)&FiraSansMedium, "9:35 AM", &cursor_x, &cursor_y, framebuffer);

    cursor_x = EPD_WIDTH / 2 + 40;
    cursor_y = 140;
    writeln((GFXfont *)&FiraSansMedium, "min", &cursor_x, &cursor_y, framebuffer);

    cursor_x = EPD_WIDTH / 2 + 40;
    cursor_y = 200;
    writeln((GFXfont *)&FiraSansMedium, "FINAL BUS", &cursor_x, &cursor_y, framebuffer);

    // Divider
    epd_draw_vline(EPD_WIDTH / 2, 20, EPD_HEIGHT - 40, 0, framebuffer);

    // Labels at bottom
    cursor_x = 40;
    cursor_y = EPD_HEIGHT - 30;
    writeln((GFXfont *)&FiraSansMedium, "Large (96pt)", &cursor_x, &cursor_y, framebuffer);

    cursor_x = EPD_WIDTH / 2 + 40;
    cursor_y = EPD_HEIGHT - 30;
    writeln((GFXfont *)&FiraSansMedium, "Medium (48pt)", &cursor_x, &cursor_y, framebuffer);

    renderToScreen();
}

// Two-phase partial update: erase old pixels, then draw new pixels
// epd_draw_image expects region-stride buffers (area.width per row), not full-screen
// Area must be 8-pixel aligned on X axis
void DisplayManager::renderPartial(Rect_t area) {
    if (!framebuffer || !screenBuffer) return;

    size_t lineBytes = area.width / 2;
    size_t regionSize = lineBytes * area.height;
    uint8_t* prevRegion = (uint8_t*)malloc(regionSize);
    uint8_t* newRegion = (uint8_t*)malloc(regionSize);
    if (!prevRegion || !newRegion) {
        free(prevRegion);
        free(newRegion);
        return;
    }

    // Extract regions from full-screen buffers into region-stride buffers
    for (int y = 0; y < area.height; y++) {
        int srcIdx = ((area.y + y) * EPD_WIDTH + area.x) / 2;
        int dstIdx = y * lineBytes;
        memcpy(prevRegion + dstIdx, screenBuffer + srcIdx, lineBytes);
        memcpy(newRegion + dstIdx, framebuffer + srcIdx, lineBytes);
    }

    epd_poweron();
    // Phase 1: Erase old black pixels (draw white where screen had black)
    epd_draw_image(area, prevRegion, WHITE_ON_WHITE);
    // Phase 2: Draw new black pixels
    epd_draw_image(area, newRegion, BLACK_ON_WHITE);
    epd_poweroff_all();

    free(prevRegion);
    free(newRegion);

    // Sync screenBuffer for the updated area
    for (int y = area.y; y < area.y + area.height && y < EPD_HEIGHT; y++) {
        int srcIdx = (y * EPD_WIDTH + area.x) / 2;
        memcpy(screenBuffer + srcIdx, framebuffer + srcIdx, lineBytes);
    }
}

void DisplayManager::showLoadingIndicator(int dots) {
    if (!framebuffer || !screenBuffer) return;
    static bool indicatorShown = false;

    // Full area covers "Updating..."
    const int areaX = (EPD_WIDTH - 208) & ~7;
    const int areaY = 500;
    const int areaW = ((EPD_WIDTH - areaX) + 7) & ~7;
    const int areaH = 40;
    const int textX = areaX + 10;
    const int textY = areaY + 28;

    if (dots <= 0) {
        // Hide: clear full area and partial update it away
        for (int y = areaY; y < areaY + areaH; y++)
            for (int x = areaX; x < areaX + areaW && x < EPD_WIDTH; x++) {
                int i = (y * EPD_WIDTH + x) / 2;
                if (x % 2 == 0) framebuffer[i] = (framebuffer[i] & 0x0F) | 0xF0;
                else             framebuffer[i] = (framebuffer[i] & 0xF0) | 0x0F;
            }
        Rect_t full = { .x = areaX, .y = areaY, .width = areaW, .height = areaH };
        renderPartial(full);
        indicatorShown = false;
        return;
    }

    int d = ((dots - 1) % 3) + 1;

    // Measure "Updating" to find where the dots region starts
    int32_t bx = 0, by = 0, bx1, by1, bw, bh;
    get_text_bounds((GFXfont *)&FiraSansSmall, "Updating", &bx, &by, &bx1, &by1, &bw, &bh, NULL);
    int dotsX = (textX + bw + 7) & ~7;  // 8-pixel aligned, round UP to not clip text
    int dotsW = ((areaX + areaW - dotsX) + 7) & ~7;

    if (!indicatorShown) {
        // First time: draw "Updating" + dots, partial update full area
        for (int y = areaY; y < areaY + areaH; y++)
            for (int x = areaX; x < areaX + areaW && x < EPD_WIDTH; x++) {
                int i = (y * EPD_WIDTH + x) / 2;
                if (x % 2 == 0) framebuffer[i] = (framebuffer[i] & 0x0F) | 0xF0;
                else             framebuffer[i] = (framebuffer[i] & 0xF0) | 0x0F;
            }
        char label[16];
        snprintf(label, sizeof(label), "Updating%.*s", d, "...");
        int32_t cx = textX, cy = textY;
        writeln((GFXfont *)&FiraSansSmall, label, &cx, &cy, framebuffer);
        Rect_t full = { .x = areaX, .y = areaY, .width = areaW, .height = areaH };
        renderPartial(full);
        indicatorShown = true;
    } else {
        // Subsequent: only clear and redraw the dots area
        for (int y = areaY; y < areaY + areaH; y++)
            for (int x = dotsX; x < dotsX + dotsW && x < EPD_WIDTH; x++) {
                int i = (y * EPD_WIDTH + x) / 2;
                if (x % 2 == 0) framebuffer[i] = (framebuffer[i] & 0x0F) | 0xF0;
                else             framebuffer[i] = (framebuffer[i] & 0xF0) | 0x0F;
            }
        char dotsStr[4];
        snprintf(dotsStr, sizeof(dotsStr), "%.*s", d, "...");
        int32_t cx = textX + bw, cy = textY;
        writeln((GFXfont *)&FiraSansSmall, dotsStr, &cx, &cy, framebuffer);
        Rect_t dotsRect = { .x = dotsX, .y = areaY, .width = dotsW, .height = areaH };
        renderPartial(dotsRect);
    }
}
