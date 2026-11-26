#pragma once

// Catalog rows and installed packages in the shapes package_downloader and
// package_manager send, and reading a model's roles by name.

#include <QAbstractItemModel>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

namespace fixtures {

inline QVariantMap dep(const QString& name, const QString& version = {})
{
    QVariantMap d{{"name", name}};
    if (!version.isEmpty()) d.insert("version", version);
    return d;
}

// One version; `variants` fill the manifest's `main` (none: availability
// rests on the host's variants being unknown).
inline QVariantMap catalogRow(const QString& repo, const QString& name,
                              const QString& version, const QString& rootHash,
                              const QVariantList& deps = {},
                              const QStringList& variants = {},
                              const QString& type = QStringLiteral("ui_qml"))
{
    QVariantMap main;
    for (const QString& v : variants) main.insert(v, QStringLiteral("lib/entry.so"));
    QVariantMap manifest{{"name", name}, {"version", version}, {"dependencies", deps},
                         {"type", type}};
    if (!variants.isEmpty()) manifest.insert("main", main);
    return {{"name", name},
            {"repositoryUrl", repo},
            {"repositoryName", repo},
            {"type", type},
            {"versions", QVariantList{QVariantMap{{"rootHash", rootHash}, {"manifest", manifest}}}}};
}

inline QVariantMap withCategory(QVariantMap row, const QString& category)
{
    row.insert("category", category);
    return row;
}

inline QVariantMap withDisplayName(QVariantMap row, const QString& displayName,
                                   const QString& description = {})
{
    row.insert("displayName", displayName);
    if (!description.isEmpty()) row.insert("description", description);
    return row;
}

// Prepends an older version, so a row offers two.
inline QVariantMap withOlderVersion(QVariantMap row, const QString& version,
                                    const QString& rootHash)
{
    QVariantList versions = row.value("versions").toList();
    QVariantMap older = versions.first().toMap();
    QVariantMap manifest = older.value("manifest").toMap();
    manifest.insert("version", version);
    older.insert("manifest", manifest);
    older.insert("rootHash", rootHash);
    versions.append(older);
    row.insert("versions", versions);
    return row;
}

inline QVariantMap installed(const QString& name, const QString& version,
                             const QString& rootHash,
                             const QString& installType = QStringLiteral("user"),
                             const QString& category = {})
{
    QVariantMap pkg{{"name", name}, {"version", version},
                    {"hashes", QVariantMap{{"root", rootHash}}}, {"installType", installType}};
    if (!category.isEmpty()) pkg.insert("category", category);
    return pkg;
}

inline int roleId(const QAbstractItemModel& model, const QByteArray& name)
{
    const auto roles = model.roleNames();
    for (auto it = roles.cbegin(); it != roles.cend(); ++it)
        if (it.value() == name) return it.key();
    return -1;
}

inline QVariant at(const QAbstractItemModel& model, int row, const QByteArray& role)
{
    return model.data(model.index(row, 0), roleId(model, role));
}

// The row of `name` in `repo` (any repository when empty), or -1.
inline int rowOf(const QAbstractItemModel& model, const QString& name, const QString& repo = {})
{
    for (int i = 0; i < model.rowCount(); ++i) {
        if (at(model, i, "name").toString() != name) continue;
        if (repo.isEmpty() || at(model, i, "repositoryUrl").toString() == repo) return i;
    }
    return -1;
}

inline QVariant roleOf(const QAbstractItemModel& model, const QString& name,
                       const QByteArray& role, const QString& repo = {})
{
    const int row = rowOf(model, name, repo);
    return row < 0 ? QVariant() : at(model, row, role);
}

inline QStringList names(const QAbstractItemModel& model)
{
    QStringList out;
    for (int i = 0; i < model.rowCount(); ++i) out.append(at(model, i, "name").toString());
    return out;
}

}  // namespace fixtures
