#pragma once
// Uniform result type for service operations: success flag, a message that is
// safe to show to the user, and an optional numeric id (e.g. new session id).

#include <QString>
#include <QtGlobal>

namespace labdesk {

struct OpResult {
    bool ok = false;
    QString message;
    qint64 id = -1;

    static OpResult success(const QString& message = QString(), qint64 id = -1)
    {
        OpResult r;
        r.ok = true;
        r.message = message;
        r.id = id;
        return r;
    }

    static OpResult failure(const QString& message)
    {
        OpResult r;
        r.ok = false;
        r.message = message;
        return r;
    }

    explicit operator bool() const { return ok; }
};

}  // namespace labdesk
