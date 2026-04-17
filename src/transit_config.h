#ifndef TRANSIT_CONFIG_H
#define TRANSIT_CONFIG_H

// Transit configuration
// Change these values to configure for a different stop/route

// Route 6 - Shelburne Road
static const char* ROUTE_ID = "19140";

// Stop: Falls Road at Shelburne Shopping Park
static const char* STOP_ID = "805574";

// GTFS feed URL for schedule data
static const char* GTFS_FEED_URL = "https://data.trilliumtransit.com/gtfs/ccta-vt-us/ccta-vt-us.zip";

// How often to refresh GTFS schedule data (milliseconds)
// Default: once per day (schedules don't change frequently)
static const unsigned long GTFS_REFRESH_INTERVAL = 24 * 60 * 60 * 1000;

// Tolerance for "last bus" detection (minutes)
// If arrival is within this many minutes of the last scheduled bus, show warning
static const int LAST_BUS_TOLERANCE = 30;

#endif
