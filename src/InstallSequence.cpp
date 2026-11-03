#include "InstallSequence.h"

#include <QCryptographicHash>
#include <QFile>
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

// A release as approved: which package, which build, from which repository.
QString release(const QVariantMap& m)
{
    return QStringList{m.value("name").toString(), m.value("version").toString(),
                       m.value("rootHash").toString(), m.value("repositoryUrl").toString()}
        .join(QChar(0x1f));
}

// An installed copy carries no repository: name, version and root hash.
QString build(const QString& name, const QString& version, const QString& rootHash)
{
    return QStringList{name, version, rootHash}.join(QChar(0x1f));
}

QVariantMap planRow(const QVariantList& plan, const QString& name)
{
    for (const QVariant& v : plan)
        if (v.toMap().value("name").toString() == name) return v.toMap();
    return {};
}

} // namespace

QString malformedPlan(const QVariantList& plan, const QString& name)
{
    bool holdsName = false;
    for (const QVariant& v : plan) {
        const QVariantMap row = v.toMap();
        for (const char* key : {"name", "version", "rootHash", "repositoryUrl"})
            if (row.value(key).toString().isEmpty())
                return QStringLiteral("The approval does not say exactly which release of %1 to "
                                      "install, so nothing was installed.")
                    .arg(row.value("name").toString().isEmpty() ? name : row.value("name").toString());
        holdsName = holdsName || row.value("name").toString() == name;
    }
    if (holdsName) return {};
    return QStringLiteral("The approval does not include %1, so nothing was installed.").arg(name);
}

QVariantList stillToInstall(const QVariantList& plan, const QVariantList& installed,
                            const QString& name)
{
    QSet<QString> onDisk;
    for (const QVariant& v : installed) {
        const QVariantMap m = v.toMap();
        // Keyed as buildInstalledPackagesJson() keys the resolver's installed set.
        const QString module = m.value("moduleName").toString();
        onDisk.insert(build(module.isEmpty() ? m.value("name").toString() : module,
                            m.value("version").toString(),
                            m.value("hashes").toMap().value("root").toString()));
    }
    QVariantList rows;
    for (const QVariant& v : plan) {
        const QVariantMap row = v.toMap();
        const QString key = build(row.value("name").toString(), row.value("version").toString(),
                                  row.value("rootHash").toString());
        if (row.value("name").toString() == name || !onDisk.contains(key))
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

QString incomplete(const QVariantList& rows, const QVariantList& requested)
{
    QSet<QString> arrived;
    for (const QVariant& v : rows) {
        const QVariantMap row = v.toMap();
        const bool optional = planRow(requested, row.value("name").toString()).value("optional").toBool();
        if (!row.contains("error")) arrived.insert(release(row));
        else if (!optional) return row.value("error").toString();
    }
    for (const QVariant& v : requested) {
        const QVariantMap row = v.toMap();
        if (row.value("optional").toBool() || arrived.contains(release(row))) continue;
        return QStringLiteral("%1 v%2 could not be fetched as approved, so nothing was installed. "
                              "Try again to review it.")
            .arg(row.value("name").toString(), row.value("version").toString());
    }
    return {};
}

QVariantList failedRow(const QString& name, const QVariantList& plan, const QString& why)
{
    return {QVariantMap{{"name", name},
                        {"repositoryUrl", planRow(plan, name).value("repositoryUrl")},
                        {"error", why}}};
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
                    const QString& name, const QString& version, const QString& sha256)
{
    const QString signer = approved.value("signer").toString();
    const QString status = inspected.value("signatureStatus").toString();
    // From a host that sends no digest of the whole file, its parts alone.
    const QString digest = approved.value("sha256").toString();
    const bool same = !inspected.contains("error")
        && !approved.value("rootHash").toString().isEmpty()
        && (digest.isEmpty() || digest == sha256)
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

QString sha256Of(const QString& path)
{
    QFile file(path);
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!file.open(QIODevice::ReadOnly) || !hash.addData(&file)) return {};
    return QString::fromLatin1(hash.result().toHex());
}

} // namespace install_sequence
