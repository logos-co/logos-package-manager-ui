#include "InstallSequence.h"

#include <QSet>

namespace install_sequence {

QVariantList tagOptionalRows(QVariantList rows, const QVariantList& optionalPackages)
{
    QSet<QString> names;
    for (const QVariant& request : optionalPackages) names.insert(request.toMap().value("name").toString());
    for (QVariant& row : rows) {
        QVariantMap m = row.toMap();
        if (names.contains(m.value("name").toString())) { m["optional"] = true; row = m; }
    }
    return rows;
}

void installSequentially(QVariantList rows, InstallOne installOne, OnRow onRow, int index)
{
    if (index >= rows.size()) return;
    const QVariantMap row = rows[index].toMap();
    installOne(row, [rows, installOne, onRow, index, optional = row.value("optional").toBool()]
                    (bool success, const QString& error) {
        const bool proceed = success || optional;
        onRow(index, success, proceed, error);
        if (proceed) installSequentially(rows, installOne, onRow, index + 1);
    });
}

} // namespace install_sequence
