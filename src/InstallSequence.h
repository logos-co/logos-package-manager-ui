#pragma once

#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <functional>

// The per-row install loop, without the backend's IPC or model. A row of a
// host-selected optional package (`optional: true`) may fail without stopping
// the rest of the batch; any other failure stops it.
namespace install_sequence {

using Done = std::function<void(bool success, const QString& error)>;
using InstallOne = std::function<void(const QVariantMap& row, Done done)>;
// After each row: whether it succeeded, and whether the batch continues.
using OnRow = std::function<void(int index, bool success, bool proceed, const QString& error)>;

// Marks the rows named by `optionalPackages` (resolver request objects).
QVariantList tagOptionalRows(QVariantList rows, const QVariantList& optionalPackages);

void installSequentially(QVariantList rows, InstallOne installOne, OnRow onRow, int index = 0);

} // namespace install_sequence
