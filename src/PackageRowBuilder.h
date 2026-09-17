#pragma once

// The catalog row → model row transform, lifted out of PackageManagerBackend
// so it can be tested without a Qt host, an IPC client or a running app.
// Pure functions over QVariant: no instance state, no I/O.

#include <QHash>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

namespace packagerow {

// Render a manifest `dependencies` array for display. An entry is a plain
// name or an object carrying a version range and/or a signer DID.
QStringList renderDependencies(const QVariantList& depsArray);

// One model row from one raw catalog row, cross-referenced against the
// installed-by-name index and this platform's valid variants.
QVariantMap buildPackageRow(const QVariantMap& obj,
                            const QHash<QString, QVariantMap>& installedByName,
                            const QStringList& validVariants);

// One model row for an installed package that NO catalog publishes.
QVariantMap buildLocalPackageRow(const QVariantMap& installed);

}  // namespace packagerow
