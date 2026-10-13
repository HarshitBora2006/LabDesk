#pragma once
// All timestamps are stored as UTC ISO-8601 with milliseconds
// ("2026-10-13T09:15:02.481Z") and converted to local time only for display.

#include <QString>

namespace labdesk::timeutil {

QString nowUtc();
QString toDisplay(const QString& isoUtc);           // local time, or "-" if empty/invalid
QString durationText(const QString& startIsoUtc,     // "1 h 05 min", "42 s", ...
                     const QString& endIsoUtc = QString());

}  // namespace labdesk::timeutil
