#include "timeutil.h"

#include <QDateTime>

namespace labdesk::timeutil {

namespace {
QDateTime parse(const QString& iso)
{
    QDateTime dt = QDateTime::fromString(iso, Qt::ISODateWithMs);
    if (!dt.isValid())
        dt = QDateTime::fromString(iso, Qt::ISODate);
    return dt;
}
}  // namespace

QString nowUtc()
{
    return QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyy-MM-dd'T'HH:mm:ss.zzz'Z'"));
}

QString toDisplay(const QString& isoUtc)
{
    if (isoUtc.isEmpty())
        return QStringLiteral("-");
    const QDateTime dt = parse(isoUtc);
    if (!dt.isValid())
        return isoUtc;
    return dt.toLocalTime().toString(QStringLiteral("dd MMM yyyy, HH:mm:ss"));
}

QString durationText(const QString& startIsoUtc, const QString& endIsoUtc)
{
    const QDateTime start = parse(startIsoUtc);
    const QDateTime end = endIsoUtc.isEmpty() ? QDateTime::currentDateTimeUtc() : parse(endIsoUtc);
    if (!start.isValid() || !end.isValid())
        return QStringLiteral("-");
    qint64 secs = qMax<qint64>(0, start.secsTo(end));
    const qint64 hours = secs / 3600;
    const qint64 minutes = (secs % 3600) / 60;
    secs %= 60;
    if (hours > 0)
        return QStringLiteral("%1 h %2 min").arg(hours).arg(minutes, 2, 10, QLatin1Char('0'));
    if (minutes > 0)
        return QStringLiteral("%1 min %2 s").arg(minutes).arg(secs, 2, 10, QLatin1Char('0'));
    return QStringLiteral("%1 s").arg(secs);
}

}  // namespace labdesk::timeutil
