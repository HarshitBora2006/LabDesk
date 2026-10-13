#include "passwordhasher.h"

#include <QByteArray>
#include <QCryptographicHash>
#include <QRandomGenerator>
#include <QStringList>

namespace labdesk::password {

namespace {
constexpr int kIterations = 20000;
const QString kScheme = QStringLiteral("sha256i");

QByteArray derive(const QString& plain, const QByteArray& salt, int iterations)
{
    QByteArray digest = QCryptographicHash::hash(salt + plain.toUtf8(), QCryptographicHash::Sha256);
    for (int i = 1; i < iterations; ++i)
        digest = QCryptographicHash::hash(digest + salt, QCryptographicHash::Sha256);
    return digest;
}

bool constantTimeEquals(const QByteArray& a, const QByteArray& b)
{
    if (a.size() != b.size())
        return false;
    unsigned char diff = 0;
    for (int i = 0; i < a.size(); ++i)
        diff |= static_cast<unsigned char>(a.at(i) ^ b.at(i));
    return diff == 0;
}
}  // namespace

QString hash(const QString& plain)
{
    QByteArray salt(16, '\0');
    for (int i = 0; i < salt.size(); ++i)
        salt[i] = static_cast<char>(QRandomGenerator::system()->bounded(256));
    const QByteArray digest = derive(plain, salt, kIterations);
    return QStringLiteral("%1$%2$%3$%4")
        .arg(kScheme)
        .arg(kIterations)
        .arg(QString::fromLatin1(salt.toHex()))
        .arg(QString::fromLatin1(digest.toHex()));
}

bool verify(const QString& plain, const QString& stored)
{
    const QStringList parts = stored.split(QLatin1Char('$'));
    if (parts.size() != 4 || parts.at(0) != kScheme)
        return false;
    bool okIterations = false;
    const int iterations = parts.at(1).toInt(&okIterations);
    if (!okIterations || iterations < 1 || iterations > 1000000)
        return false;
    const QByteArray salt = QByteArray::fromHex(parts.at(2).toLatin1());
    const QByteArray expected = QByteArray::fromHex(parts.at(3).toLatin1());
    return constantTimeEquals(derive(plain, salt, iterations), expected);
}

}  // namespace labdesk::password
