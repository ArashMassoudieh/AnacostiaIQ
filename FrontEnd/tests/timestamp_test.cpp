#include "DashboardConfig.h"
#include <QTimeZone>
#include <cassert>

int main()
{
    DashboardConfig config;
    assert(config.loadFromData(R"({"timestamp_timezone":"UTC"})"));
    const QTimeZone eastern("America/New_York");
    const QDateTime noon({2026, 10, 30}, {12, 0}, eastern);
    assert(config.apiTimestamp(noon) == "2026-10-30T16:00:00");
    assert(config.readingTimestamp("2026-10-30T16:00:00") == noon);
    assert(config.readingTimestamp("2026-10-30T16:00:00Z") == noon);
    assert(config.readingTimestamp("2026-10-30T12:00:00-04:00") == noon);
    const QDateTime winterNoon({2026, 12, 1}, {12, 0}, eastern);
    assert(config.apiTimestamp(winterNoon) == "2026-12-01T17:00:00");
    assert(config.readingTimestamp("2026-12-01T17:00:00") == winterNoon);
    assert(!config.readingTimestamp("invalid").isValid());
    assert(config.loadFromData(R"({"timestamp_timezone":"UTC","data_start":"2026-10-30T15:31:14Z"})"));
    assert(config.dataStart() == QDateTime({2026, 10, 30}, {11, 31, 14}, eastern));
    assert(!config.dataEnd().isValid());

    // Existing lab/legacy profiles retain their wall-clock convention,
    // including after loading a UTC profile into the same config object.
    assert(config.loadFromData("{}"));
    assert(!config.dataStart().isValid());
    assert(config.apiTimestamp(noon) == "2026-10-30T12:00:00");
    assert(config.readingTimestamp("2026-10-30T12:00:00").timeSpec()
           == Qt::LocalTime);
    assert(config.loadFromData(R"({"data_end":"2026-10-26T23:59:59"})"));
    assert(config.apiTimestamp(config.dataEnd()) == "2026-10-26T23:59:59");
    assert(!config.dataStart().isValid());
}
