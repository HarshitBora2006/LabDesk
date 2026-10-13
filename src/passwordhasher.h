#pragma once
// Salted, iterated SHA-256 password hashing using QCryptographicHash.
// Stored format:  sha256i$<iterations>$<salt-hex>$<hash-hex>
//
// Educational-grade: better than plaintext, but a production system should
// use a memory-hard KDF such as Argon2id or bcrypt (see README limitations).

#include <QString>

namespace labdesk::password {

QString hash(const QString& plain);
bool verify(const QString& plain, const QString& stored);

}  // namespace labdesk::password
