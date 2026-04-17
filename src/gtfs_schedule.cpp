#include "gtfs_schedule.h"
#include "transit_config.h"
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <rom/miniz.h>
#include <time.h>
#include <algorithm>
#include <set>

// ZIP file signatures
#define ZIP_LOCAL_HEADER_SIG 0x04034b50
#define ZIP_CENTRAL_DIR_SIG 0x02014b50
#define ZIP_END_CENTRAL_SIG 0x06054b50

struct ZipLocalHeader {
    uint32_t signature;
    uint16_t versionNeeded;
    uint16_t flags;
    uint16_t compression;
    uint16_t modTime;
    uint16_t modDate;
    uint32_t crc32;
    uint32_t compressedSize;
    uint32_t uncompressedSize;
    uint16_t filenameLen;
    uint16_t extraLen;
} __attribute__((packed));

struct ZipCentralDirHeader {
    uint32_t signature;
    uint16_t versionMade;
    uint16_t versionNeeded;
    uint16_t flags;
    uint16_t compression;
    uint16_t modTime;
    uint16_t modDate;
    uint32_t crc32;
    uint32_t compressedSize;
    uint32_t uncompressedSize;
    uint16_t filenameLen;
    uint16_t extraLen;
    uint16_t commentLen;
    uint16_t diskStart;
    uint16_t internalAttr;
    uint32_t externalAttr;
    uint32_t localHeaderOffset;
} __attribute__((packed));

struct ZipEndCentralDir {
    uint32_t signature;
    uint16_t diskNum;
    uint16_t diskStart;
    uint16_t numEntriesDisk;
    uint16_t numEntriesTotal;
    uint32_t centralDirSize;
    uint32_t centralDirOffset;
    uint16_t commentLen;
} __attribute__((packed));

GTFSSchedule::GTFSSchedule(const char* routeId, const char* stopId)
    : routeId(routeId), stopId(stopId) {}

bool GTFSSchedule::hasSchedule() const {
    return scheduleLoaded;
}

int GTFSSchedule::getCurrentDayOfWeek() {
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo)) {
        return 1;  // Default to Monday if time unavailable
    }
    return timeinfo.tm_wday;  // 0=Sunday, 1=Monday, etc.
}

int GTFSSchedule::getLastBusMinutes() {
    if (!scheduleLoaded) return -1;
    int today = getCurrentDayOfWeek();
    return scheduleByDay[today].lastBusMinutes;
}

bool GTFSSchedule::isLastBus(int arrivalMinutes, int tolerance) {
    int lastBus = getLastBusMinutes();
    if (lastBus < 0) return false;  // Unknown schedule
    return (lastBus - arrivalMinutes) <= tolerance && arrivalMinutes <= lastBus;
}

bool GTFSSchedule::fetch(GTFSProgressCallback progressCallback) {
    unsigned long startTime = millis();
    Serial.println("GTFS: Starting download...");
    bool result = downloadAndParse(progressCallback);
    unsigned long elapsed = millis() - startTime;
    Serial.printf("GTFS: Total fetch time: %lu ms (%.1f seconds)\n", elapsed, elapsed / 1000.0);
    return result;
}

// File info from central directory
struct ZipFileInfo {
    uint32_t localHeaderOffset;
    uint32_t compressedSize;
    uint32_t uncompressedSize;
    uint16_t compression;
};

// Find file info in central directory
// Copies data from PSRAM to heap to avoid ROM/PSRAM issues
static bool findFileInZip(uint8_t* zipData, size_t zipSize,
                          const char* targetFilename, ZipFileInfo& info) {
    Serial.printf("GTFS: findFileInZip(%s) zipSize=%u\n", targetFilename, zipSize);
    Serial.printf("GTFS: Free heap=%u, PSRAM=%u\n", ESP.getFreeHeap(), ESP.getFreePsram());
    yield();

    // Copy last 1KB to heap for EOCD search
    size_t tailSize = (zipSize > 1024) ? 1024 : zipSize;
    uint8_t* tailBuf = (uint8_t*)malloc(tailSize);
    if (!tailBuf) {
        Serial.println("GTFS: Failed to alloc tail buffer");
        return false;
    }

    memcpy(tailBuf, zipData + zipSize - tailSize, tailSize);
    yield();

    // Find EOCD in tail buffer
    ZipEndCentralDir eocd;
    bool foundEocd = false;
    for (int i = tailSize - 22; i >= 0; i--) {
        uint32_t sig;
        memcpy(&sig, tailBuf + i, 4);
        if (sig == ZIP_END_CENTRAL_SIG) {
            memcpy(&eocd, tailBuf + i, sizeof(ZipEndCentralDir));
            foundEocd = true;
            break;
        }
    }
    free(tailBuf);

    if (!foundEocd) {
        Serial.println("GTFS: EOCD not found");
        return false;
    }

    Serial.printf("GTFS: Central dir has %d entries at offset %u\n",
                  eocd.numEntriesTotal, eocd.centralDirOffset);
    yield();

    // Copy central directory to heap
    size_t cdSize = eocd.centralDirSize;
    if (cdSize > 100000) cdSize = 100000;  // Limit to 100KB

    uint8_t* cdBuf = (uint8_t*)malloc(cdSize);
    if (!cdBuf) {
        Serial.println("GTFS: Failed to alloc CD buffer");
        return false;
    }

    memcpy(cdBuf, zipData + eocd.centralDirOffset, cdSize);
    yield();

    // Scan central directory from heap buffer
    size_t pos = 0;
    size_t targetLen = strlen(targetFilename);
    bool found = false;

    for (int i = 0; i < eocd.numEntriesTotal && pos + sizeof(ZipCentralDirHeader) < cdSize; i++) {
        ZipCentralDirHeader cdh;
        memcpy(&cdh, cdBuf + pos, sizeof(ZipCentralDirHeader));

        if (cdh.signature != ZIP_CENTRAL_DIR_SIG) break;

        if (cdh.filenameLen == targetLen && pos + sizeof(ZipCentralDirHeader) + targetLen <= cdSize) {
            if (memcmp(cdBuf + pos + sizeof(ZipCentralDirHeader), targetFilename, targetLen) == 0) {
                info.localHeaderOffset = cdh.localHeaderOffset;
                info.compressedSize = cdh.compressedSize;
                info.uncompressedSize = cdh.uncompressedSize;
                info.compression = cdh.compression;
                found = true;
                break;
            }
        }

        pos += sizeof(ZipCentralDirHeader) + cdh.filenameLen + cdh.extraLen + cdh.commentLen;
        if (i % 5 == 0) yield();
    }

    free(cdBuf);
    return found;
}

// Extract a file from ZIP to a PSRAM buffer (caller must free with free())
// Uses heap for decompression chunks to avoid ROM/PSRAM compatibility issues
static char* extractFileToBuffer(uint8_t* zipData, size_t zipSize,
                                  const char* targetFilename, size_t& outSize) {
    Serial.printf("GTFS: Extracting %s\n", targetFilename);
    yield();

    ZipFileInfo info;
    if (!findFileInZip(zipData, zipSize, targetFilename, info)) {
        Serial.printf("GTFS: %s not found in zip\n", targetFilename);
        return nullptr;
    }

    Serial.printf("GTFS: Found %s (comp=%u, uncomp=%u)\n",
                 targetFilename, info.compressedSize, info.uncompressedSize);

    // Get local header to find data start (copy from PSRAM to stack)
    ZipLocalHeader lh;
    memcpy(&lh, zipData + info.localHeaderOffset, sizeof(ZipLocalHeader));

    if (lh.signature != ZIP_LOCAL_HEADER_SIG) {
        Serial.printf("GTFS: Invalid local header sig: 0x%08x\n", lh.signature);
        return nullptr;
    }

    size_t dataStart = info.localHeaderOffset + sizeof(ZipLocalHeader) +
                       lh.filenameLen + lh.extraLen;

    // Check memory - use PSRAM for output
    size_t freePsram = ESP.getFreePsram();
    Serial.printf("GTFS: Free PSRAM: %u, need: %u\n", freePsram, info.uncompressedSize + 1);

    if (info.uncompressedSize + 1 > freePsram) {
        Serial.println("GTFS: Not enough PSRAM!");
        return nullptr;
    }

    // Allocate output buffer in PSRAM
    char* outBuffer = (char*)ps_malloc(info.uncompressedSize + 1);
    if (!outBuffer) {
        Serial.println("GTFS: Output alloc failed");
        return nullptr;
    }

    yield();

    if (info.compression == 0) {
        // No compression - direct copy
        memcpy(outBuffer, zipData + dataStart, info.uncompressedSize);
    } else if (info.compression == 8) {
        // Deflate - decompress in chunks
        const size_t CHUNK_SIZE = 32 * 1024;  // 32KB chunks on heap

        uint8_t* chunkBuf = (uint8_t*)malloc(CHUNK_SIZE);
        if (!chunkBuf) {
            Serial.println("GTFS: Chunk buffer alloc failed");
            free(outBuffer);
            return nullptr;
        }

        tinfl_decompressor decomp;
        tinfl_init(&decomp);

        size_t srcPos = 0;
        size_t dstPos = 0;
        tinfl_status status = TINFL_STATUS_NEEDS_MORE_INPUT;

        Serial.printf("GTFS: Decompressing %u bytes in 32K chunks\n", info.compressedSize);

        while (srcPos < info.compressedSize && status != TINFL_STATUS_DONE) {
            // Copy next chunk from PSRAM to heap
            size_t chunkLen = info.compressedSize - srcPos;
            if (chunkLen > CHUNK_SIZE) chunkLen = CHUNK_SIZE;
            memcpy(chunkBuf, zipData + dataStart + srcPos, chunkLen);

            size_t inBytes = chunkLen;
            size_t outBytes = info.uncompressedSize - dstPos;

            bool lastChunk = (srcPos + chunkLen >= info.compressedSize);

            status = tinfl_decompress(&decomp,
                chunkBuf, &inBytes,
                (uint8_t*)outBuffer, (uint8_t*)outBuffer + dstPos, &outBytes,
                TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF | (lastChunk ? 0 : TINFL_FLAG_HAS_MORE_INPUT));

            srcPos += inBytes;
            dstPos += outBytes;

            if (status < 0) {
                Serial.printf("GTFS: Decompress failed at %u/%u (status %d)\n", srcPos, info.compressedSize, status);
                free(chunkBuf);
                free(outBuffer);
                return nullptr;
            }

            yield();
        }

        free(chunkBuf);
        Serial.printf("GTFS: Decompressed %u bytes\n", dstPos);
    } else {
        Serial.printf("GTFS: Unknown compression %d\n", info.compression);
        free(outBuffer);
        return nullptr;
    }

    outBuffer[info.uncompressedSize] = '\0';
    outSize = info.uncompressedSize;
    Serial.printf("GTFS: Extracted %s (%u bytes)\n", targetFilename, outSize);
    return outBuffer;
}

// Helper to find column index from a char* header line
static int findColumnIndexBuffer(const char* header, size_t headerLen, const char* columnName) {
    int index = 0;
    size_t start = 0;
    size_t colNameLen = strlen(columnName);

    while (start < headerLen) {
        size_t end = start;
        while (end < headerLen && header[end] != ',' && header[end] != '\n' && header[end] != '\r') {
            end++;
        }

        size_t fieldLen = end - start;
        if (fieldLen == colNameLen && memcmp(header + start, columnName, colNameLen) == 0) {
            return index;
        }

        index++;
        start = end + 1;
    }
    return -1;
}

// Helper to get a field from a line by index (returns pointer and length, no heap allocation)
static bool getFieldBuffer(const char* line, size_t lineLen, int index, const char** out, size_t* outLen) {
    int current = 0;
    size_t start = 0;

    while (start < lineLen) {
        size_t end = start;
        while (end < lineLen && line[end] != ',' && line[end] != '\n' && line[end] != '\r') {
            end++;
        }

        if (current == index) {
            *out = line + start;
            *outLen = end - start;
            return true;
        }

        current++;
        start = end + 1;
    }
    return false;
}

// Helper to advance through lines in a buffer
// Returns pointer past the newline, or end if no newline found
// Sets lineLen to length of line (excluding \r\n)
static char* nextLine(char* pos, char* end, size_t& lineLen) {
    char* nl = (char*)memchr(pos, '\n', end - pos);
    if (nl) {
        lineLen = nl - pos;
        if (lineLen > 0 && pos[lineLen - 1] == '\r') lineLen--;
        return nl + 1;
    }
    lineLen = end - pos;
    if (lineLen > 0 && pos[lineLen - 1] == '\r') lineLen--;
    return end;
}

bool GTFSSchedule::downloadAndParse(GTFSProgressCallback progressCallback) {
    WiFiClientSecure client;
    client.setInsecure();

    HTTPClient http;
    http.begin(client, GTFS_FEED_URL);
    http.setTimeout(60000);

    auto reportProgress = [&](const char* phase, int pct) {
        if (progressCallback) progressCallback(phase, pct);
    };

    int httpCode = http.GET();
    if (httpCode != HTTP_CODE_OK) {
        Serial.printf("GTFS: HTTP error %d\n", httpCode);
        http.end();
        return false;
    }

    int contentLength = http.getSize();
    Serial.printf("GTFS: Downloading %d bytes\n", contentLength);

    if (contentLength <= 0 || contentLength > 16 * 1024 * 1024) {
        Serial.println("GTFS: Invalid content length");
        http.end();
        return false;
    }

    // Allocate PSRAM buffer for the zip (~1.6MB)
    uint8_t* zipBuffer = (uint8_t*)ps_malloc(contentLength);
    if (!zipBuffer) {
        Serial.printf("GTFS: Failed to allocate %d bytes in PSRAM\n", contentLength);
        http.end();
        return false;
    }

    // Download to PSRAM buffer
    WiFiClient* stream = http.getStreamPtr();
    size_t bytesRead = 0;
    unsigned long lastProgressUpdate = 0;
    unsigned long lastYield = millis();
    int lastPct = -1;

    reportProgress("Downloading", 0);

    while (bytesRead < (size_t)contentLength && http.connected()) {
        size_t available = stream->available();
        if (available) {
            size_t toRead = min(available, (size_t)(contentLength - bytesRead));
            toRead = min(toRead, (size_t)4096);
            stream->readBytes(zipBuffer + bytesRead, toRead);
            bytesRead += toRead;

            int pct = (bytesRead * 100) / contentLength;
            if (pct != lastPct && (pct % 5 == 0 || bytesRead - lastProgressUpdate > 500000)) {
                Serial.printf("GTFS: Downloaded %d / %d bytes (%d%%)\n", bytesRead, contentLength, pct);
                reportProgress("Downloading", pct);
                lastProgressUpdate = bytesRead;
                lastPct = pct;
            }
        }

        if (millis() - lastYield > 100) {
            yield();
            lastYield = millis();
        }
    }
    http.end();

    Serial.printf("GTFS: Download complete (%d bytes)\n", bytesRead);

    // Extract calendar.txt and trips.txt to PSRAM (small files, ~4KB + ~250KB)
    reportProgress("Extracting", 0);
    unsigned long extractStart = millis();

    size_t calendarSize = 0, tripsSize = 0;
    char* calendarBuf = extractFileToBuffer(zipBuffer, bytesRead, "calendar.txt", calendarSize);
    reportProgress("Extracting", 33);
    char* tripsBuf = extractFileToBuffer(zipBuffer, bytesRead, "trips.txt", tripsSize);
    reportProgress("Extracting", 66);

    if (!calendarBuf || !tripsBuf) {
        Serial.println("GTFS: Failed to extract calendar.txt or trips.txt");
        if (calendarBuf) free(calendarBuf);
        if (tripsBuf) free(tripsBuf);
        free(zipBuffer);
        return false;
    }

    Serial.printf("GTFS: Small files extracted in %lu ms\n", millis() - extractStart);

    // Parse trips and calendar from PSRAM buffers (no heap String copies!)
    reportProgress("Parsing", 0);
    unsigned long parseStart = millis();
    std::map<String, std::vector<int>> tripIdToDays;
    parseTripsAndCalendar(calendarBuf, calendarSize, tripsBuf, tripsSize, tripIdToDays);

    // Free small file buffers before extracting the large one
    free(calendarBuf);
    free(tripsBuf);

    Serial.printf("GTFS: Free heap=%u, PSRAM=%u (after trips/calendar parse)\n",
                  ESP.getFreeHeap(), ESP.getFreePsram());
    reportProgress("Parsing", 25);

    // Clear existing schedule data
    for (int i = 0; i < 7; i++) {
        scheduleByDay[i].arrivals.clear();
        scheduleByDay[i].lastBusMinutes = -1;
    }

    // Extract stop_times.txt to PSRAM (~4.5MB; peak with zip = ~6.1MB of 7MB PSRAM)
    reportProgress("Extracting", 80);
    size_t stopTimesSize = 0;
    char* stopTimesBuf = extractFileToBuffer(zipBuffer, bytesRead, "stop_times.txt", stopTimesSize);

    // Free ZIP buffer now - all files extracted
    free(zipBuffer);
    zipBuffer = nullptr;

    if (!stopTimesBuf) {
        Serial.println("GTFS: Failed to extract stop_times.txt");
        return false;
    }

    Serial.printf("GTFS: stop_times.txt: %u bytes in PSRAM\n", stopTimesSize);
    reportProgress("Parsing", 50);

    // Parse stop_times.txt directly from PSRAM buffer
    parseStopTimes(stopTimesBuf, stopTimesSize, tripIdToDays);

    // Free stop_times buffer
    free(stopTimesBuf);

    reportProgress("Parsing", 75);

    // Sort arrivals by time for each day
    for (int i = 0; i < 7; i++) {
        std::sort(scheduleByDay[i].arrivals.begin(), scheduleByDay[i].arrivals.end(),
            [](const ScheduledArrival& a, const ScheduledArrival& b) {
                return a.minutesFromMidnight < b.minutesFromMidnight;
            });
    }

    reportProgress("Parsing", 100);
    Serial.printf("GTFS: Total parse time: %lu ms\n", millis() - parseStart);

    scheduleLoaded = true;

    // Log results
    const char* dayNames[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    for (int i = 0; i < 7; i++) {
        if (scheduleByDay[i].lastBusMinutes >= 0) {
            int h = scheduleByDay[i].lastBusMinutes / 60;
            int m = scheduleByDay[i].lastBusMinutes % 60;
            Serial.printf("GTFS: %s last bus: %02d:%02d (%d arrivals)\n",
                          dayNames[i], h, m, scheduleByDay[i].arrivals.size());
        }
    }

    return true;
}

void GTFSSchedule::parseTripsAndCalendar(char* calendarBuf, size_t calendarSize,
                                          char* tripsBuf, size_t tripsSize,
                                          std::map<String, std::vector<int>>& tripIdToDays) {
    // --- Parse calendar.txt: service_id → days of week ---
    std::map<String, std::vector<int>> serviceToDays;

    char* pos = calendarBuf;
    char* end = calendarBuf + calendarSize;
    size_t lineLen;

    // Header
    char* next = nextLine(pos, end, lineLen);
    int svcIdCol = findColumnIndexBuffer(pos, lineLen, "service_id");
    int monCol   = findColumnIndexBuffer(pos, lineLen, "monday");
    int tueCol   = findColumnIndexBuffer(pos, lineLen, "tuesday");
    int wedCol   = findColumnIndexBuffer(pos, lineLen, "wednesday");
    int thuCol   = findColumnIndexBuffer(pos, lineLen, "thursday");
    int friCol   = findColumnIndexBuffer(pos, lineLen, "friday");
    int satCol   = findColumnIndexBuffer(pos, lineLen, "saturday");
    int sunCol   = findColumnIndexBuffer(pos, lineLen, "sunday");
    pos = next;

    // Data lines
    while (pos < end) {
        next = nextLine(pos, end, lineLen);
        if (lineLen > 0) {
            const char* svcPtr; size_t svcLen;
            if (getFieldBuffer(pos, lineLen, svcIdCol, &svcPtr, &svcLen)) {
                String serviceId(svcPtr, svcLen);
                std::vector<int> days;
                const char* val; size_t vLen;
                if (getFieldBuffer(pos, lineLen, sunCol, &val, &vLen) && vLen == 1 && val[0] == '1') days.push_back(0);
                if (getFieldBuffer(pos, lineLen, monCol, &val, &vLen) && vLen == 1 && val[0] == '1') days.push_back(1);
                if (getFieldBuffer(pos, lineLen, tueCol, &val, &vLen) && vLen == 1 && val[0] == '1') days.push_back(2);
                if (getFieldBuffer(pos, lineLen, wedCol, &val, &vLen) && vLen == 1 && val[0] == '1') days.push_back(3);
                if (getFieldBuffer(pos, lineLen, thuCol, &val, &vLen) && vLen == 1 && val[0] == '1') days.push_back(4);
                if (getFieldBuffer(pos, lineLen, friCol, &val, &vLen) && vLen == 1 && val[0] == '1') days.push_back(5);
                if (getFieldBuffer(pos, lineLen, satCol, &val, &vLen) && vLen == 1 && val[0] == '1') days.push_back(6);
                serviceToDays[serviceId] = days;
            }
        }
        pos = next;
    }

    Serial.printf("GTFS: Parsed %d service patterns\n", serviceToDays.size());

    // --- Parse trips.txt: trip_id → days for our route ---
    pos = tripsBuf;
    end = tripsBuf + tripsSize;

    // Header
    next = nextLine(pos, end, lineLen);
    int routeIdCol  = findColumnIndexBuffer(pos, lineLen, "route_id");
    int tripIdCol   = findColumnIndexBuffer(pos, lineLen, "trip_id");
    int tripSvcIdCol = findColumnIndexBuffer(pos, lineLen, "service_id");
    pos = next;

    int matchingTrips = 0;

    while (pos < end) {
        next = nextLine(pos, end, lineLen);
        if (lineLen > 0) {
            const char* routePtr; size_t routeLen;
            if (getFieldBuffer(pos, lineLen, routeIdCol, &routePtr, &routeLen) &&
                routeLen == routeId.length() &&
                memcmp(routePtr, routeId.c_str(), routeLen) == 0) {

                const char* tripIdPtr; size_t tripIdLen;
                const char* svcIdPtr; size_t svcIdLen;
                if (getFieldBuffer(pos, lineLen, tripIdCol, &tripIdPtr, &tripIdLen) &&
                    getFieldBuffer(pos, lineLen, tripSvcIdCol, &svcIdPtr, &svcIdLen)) {
                    String serviceId(svcIdPtr, svcIdLen);
                    auto it = serviceToDays.find(serviceId);
                    if (it != serviceToDays.end()) {
                        String tripId(tripIdPtr, tripIdLen);
                        tripIdToDays[tripId] = it->second;
                        matchingTrips++;
                    }
                }
            }
        }
        pos = next;
    }

    Serial.printf("GTFS: Found %d trips for route %s\n", matchingTrips, routeId.c_str());
}

void GTFSSchedule::parseStopTimes(char* buffer, size_t bufferSize,
                                   const std::map<String, std::vector<int>>& tripIdToDays) {
    char* pos = buffer;
    char* end = buffer + bufferSize;
    size_t lineLen;

    // Header
    char* next = nextLine(pos, end, lineLen);
    int tripIdCol  = findColumnIndexBuffer(pos, lineLen, "trip_id");
    int stopIdCol  = findColumnIndexBuffer(pos, lineLen, "stop_id");
    int arrivalCol = findColumnIndexBuffer(pos, lineLen, "arrival_time");

    Serial.printf("GTFS: stop_times columns: trip_id=%d, stop_id=%d, arrival_time=%d\n",
                  tripIdCol, stopIdCol, arrivalCol);
    pos = next;

    int matchedLines = 0;
    int linesProcessed = 0;
    std::set<String> processedTrips;

    while (pos < end) {
        next = nextLine(pos, end, lineLen);
        linesProcessed++;

        if (linesProcessed % 20000 == 0) {
            Serial.printf("GTFS: Processed %d lines...\n", linesProcessed);
            yield();
        }

        if (lineLen > 0) {
            // Check stop_id first — most selective, skips ~99.9% of lines without heap alloc
            const char* stopIdPtr; size_t stopIdLen;
            if (getFieldBuffer(pos, lineLen, stopIdCol, &stopIdPtr, &stopIdLen) &&
                stopIdLen == stopId.length() &&
                memcmp(stopIdPtr, stopId.c_str(), stopIdLen) == 0) {

                // Our stop — now check trip_id against our route's trips
                const char* tripIdPtr; size_t tripIdLen;
                if (getFieldBuffer(pos, lineLen, tripIdCol, &tripIdPtr, &tripIdLen)) {
                    String tripId(tripIdPtr, tripIdLen);

                    auto it = tripIdToDays.find(tripId);
                    if (it != tripIdToDays.end() && !processedTrips.count(tripId)) {
                        processedTrips.insert(tripId);

                        // Parse arrival time (HH:MM or H:MM, GTFS allows >24h)
                        const char* arrPtr; size_t arrLen;
                        if (getFieldBuffer(pos, lineLen, arrivalCol, &arrPtr, &arrLen) && arrLen >= 4) {
                            // Find colon position to handle both H:MM and HH:MM
                            int colonPos = -1;
                            for (size_t i = 0; i < arrLen; i++) {
                                if (arrPtr[i] == ':') { colonPos = i; break; }
                            }
                            if (colonPos >= 1 && colonPos + 2 < (int)arrLen) {
                                int hours = 0;
                                for (int i = 0; i < colonPos; i++) {
                                    hours = hours * 10 + (arrPtr[i] - '0');
                                }
                                int minutes = (arrPtr[colonPos + 1] - '0') * 10 + (arrPtr[colonPos + 2] - '0');
                                int arrivalMinutes = hours * 60 + minutes;

                                const std::vector<int>& days = it->second;
                                for (int day : days) {
                                    ScheduledArrival arrival;
                                    arrival.minutesFromMidnight = arrivalMinutes;
                                    arrival.matchedToPrediction = false;
                                    scheduleByDay[day].arrivals.push_back(arrival);

                                    if (arrivalMinutes > scheduleByDay[day].lastBusMinutes) {
                                        scheduleByDay[day].lastBusMinutes = arrivalMinutes;
                                    }
                                }
                                matchedLines++;
                            }
                        }
                    }
                }
            }
        }

        pos = next;
    }

    Serial.printf("GTFS: Matched %d arrivals at our stop (%d lines total)\n",
                  matchedLines, linesProcessed);
}

std::vector<ScheduledArrival> GTFSSchedule::getTodayArrivals(int afterMinutes) {
    std::vector<ScheduledArrival> result;
    if (!scheduleLoaded) return result;

    int today = getCurrentDayOfWeek();
    for (const auto& arrival : scheduleByDay[today].arrivals) {
        if (arrival.minutesFromMidnight > afterMinutes) {
            result.push_back(arrival);
        }
    }
    return result;
}

bool GTFSSchedule::matchPrediction(int predictionMinutes, int tolerance) {
    if (!scheduleLoaded) return false;

    int today = getCurrentDayOfWeek();
    for (auto& arrival : scheduleByDay[today].arrivals) {
        if (!arrival.matchedToPrediction) {
            int diff = abs(arrival.minutesFromMidnight - predictionMinutes);
            if (diff <= tolerance) {
                arrival.matchedToPrediction = true;
                return true;
            }
        }
    }
    return false;
}

void GTFSSchedule::resetMatches() {
    for (int i = 0; i < 7; i++) {
        for (auto& arrival : scheduleByDay[i].arrivals) {
            arrival.matchedToPrediction = false;
        }
    }
}

std::vector<ScheduledArrival> GTFSSchedule::getUnmatchedArrivals(int afterMinutes) {
    std::vector<ScheduledArrival> result;
    if (!scheduleLoaded) return result;

    int today = getCurrentDayOfWeek();
    for (const auto& arrival : scheduleByDay[today].arrivals) {
        if (!arrival.matchedToPrediction && arrival.minutesFromMidnight > afterMinutes) {
            result.push_back(arrival);
        }
    }
    return result;
}
