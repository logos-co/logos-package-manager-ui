#pragma once

#include <QString>

#include <string>

namespace pmui {

// A download or an install can take minutes; the rest answers at once.
constexpr int kReadMs = 30 * 1000;
constexpr int kVerifyMs = 2 * 60 * 1000;
constexpr int kInstallMs = 5 * 60 * 1000;

// One package. package_downloader ends a stalled fetch itself (60 s without
// progress over Logos Storage, 600 s in all over HTTP) but not a slow one that
// keeps going, so this only catches a downloader that stopped answering.
constexpr int kDownloadMs = 30 * 60 * 1000;

// What a failed call to `module` tells the user: a timeout as a timeout, else
// the module's own words.
inline QString callFailure(const QString& module, const std::string& code,
                           const std::string& message, int timeoutMs)
{
    if (code == "timeout") {
        const QString within = timeoutMs % (60 * 1000) != 0
            ? QStringLiteral("%1 s").arg(timeoutMs / 1000)
            : QStringLiteral("%1 min").arg(timeoutMs / (60 * 1000));
        return QStringLiteral("%1 did not answer within %2.").arg(module, within);
    }
    if (!message.empty()) return QString::fromStdString(message);
    return QStringLiteral("%1 did not reply.").arg(module);
}

}  // namespace pmui
