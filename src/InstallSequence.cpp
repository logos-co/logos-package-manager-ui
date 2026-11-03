#include "InstallSequence.h"

#include <QSet>
#include <QStringList>

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

Source sourceOf(const QVariantMap& approval, bool fileWaiting)
{
    if (approval.contains("file")) return fileWaiting ? Source::File : Source::Refused;
    if (approval.contains("plan")) return Source::Catalog;
    return fileWaiting ? Source::File : Source::Catalog;
}

QVariantList approvedRequests(const QVariantList& plan)
{
    QVariantList requests;
    for (const QVariant& v : plan) {
        const QVariantMap row = v.toMap();
        QVariantMap request{{"name", row.value("name").toString()}};
        for (const char* key : {"version", "rootHash", "repositoryUrl", "signer"}) {
            const QString value = row.value(key).toString();
            if (!value.isEmpty()) request.insert(key, value);
        }
        if (row.value("optional").toBool()) request.insert("optional", true);
        requests.append(request);
    }
    return requests;
}

namespace {

QString release(const QString& name, const QVariantMap& m, const QString& rootHash)
{
    return QStringList{name, m.value("version").toString(), rootHash}.join(QChar(0x1f));
}

QString release(const QVariantMap& m)
{
    return release(m.value("name").toString(), m, m.value("rootHash").toString());
}

} // namespace

QVariantList stillToInstall(const QVariantList& plan, const QVariantList& installed,
                            const QString& name)
{
    QSet<QString> onDisk;
    for (const QVariant& v : installed) {
        const QVariantMap m = v.toMap();
        // Keyed as buildInstalledPackagesJson() keys the resolver's installed set.
        const QString module = m.value("moduleName").toString();
        onDisk.insert(release(module.isEmpty() ? m.value("name").toString() : module, m,
                              m.value("hashes").toMap().value("root").toString()));
    }
    QVariantList rows;
    for (const QVariant& v : plan) {
        const QVariantMap row = v.toMap();
        if (row.value("name").toString() == name || !onDisk.contains(release(row)))
            rows.append(row);
    }
    return rows;
}

QString outsidePlan(const QVariantList& rows, const QVariantList& plan)
{
    QSet<QString> approved;
    for (const QVariant& v : plan) approved.insert(release(v.toMap()));
    for (const QVariant& v : rows) {
        const QVariantMap row = v.toMap();
        if (row.contains("error") || approved.contains(release(row))) continue;
        return QStringLiteral("What this installs changed after you approved it (%1 v%2), "
                              "so nothing was installed. Try again to review it.")
            .arg(row.value("name").toString(), row.value("version").toString());
    }
    return {};
}

QVariantList failedRow(const QString& name, const QVariantList& plan, const QString& why)
{
    QString repositoryUrl;
    for (const QVariant& p : plan)
        if (p.toMap().value("name").toString() == name)
            repositoryUrl = p.toMap().value("repositoryUrl").toString();
    return {QVariantMap{{"name", name}, {"repositoryUrl", repositoryUrl}, {"error", why}}};
}

QVariantList heldToPlan(const QVariantList& rows, const QVariantList& plan, const QString& name)
{
    const QString why = outsidePlan(rows, plan);
    return why.isEmpty() ? rows : failedRow(name, plan, why);
}

QString signerChanged(const QVariantMap& verified, const QVariantList& plan, const QVariantMap& row)
{
    QString signer;
    for (const QVariant& v : plan)
        if (release(v.toMap()) == release(row))
            signer = v.toMap().value("signer").toString();
    // A check that did not run proves nothing either way.
    const bool held = verified.value("packageValid").toBool()
        && (signer.isEmpty()
                ? !verified.value("isSigned").toBool()
                : verified.value("isSigned").toBool() && verified.value("signatureValid").toBool()
                      && verified.value("signerDid").toString() == signer);
    if (held) return {};
    return QStringLiteral("%1 v%2 is not signed as you approved it, so nothing was installed. "
                          "Try again to review it.")
        .arg(row.value("name").toString(), row.value("version").toString());
}

QString fileChanged(const QVariantMap& inspected, const QVariantMap& approved,
                    const QString& name, const QString& version)
{
    const QString signer = approved.value("signer").toString();
    const QString status = inspected.value("signatureStatus").toString();
    const bool same = !inspected.contains("error")
        && !approved.value("rootHash").toString().isEmpty()
        && inspected.value("name").toString() == name
        && inspected.value("version").toString() == version
        && inspected.value("rootHash").toString() == approved.value("rootHash").toString()
        && (signer.isEmpty() ? status == QLatin1String("unsigned")
                             : status == QLatin1String("signed")
                                   && inspected.value("signerDid").toString() == signer);
    if (same) return {};
    return QStringLiteral("The file changed after you approved it, so it was not installed. "
                          "Open it again to review it.");
}

} // namespace install_sequence
