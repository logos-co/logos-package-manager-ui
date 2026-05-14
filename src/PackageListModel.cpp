#include "PackageListModel.h"
#include "PackageTypes.h"

#include <QSet>
#include <utility>

PackageListModel::PackageListModel(QObject* parent)
    : QAbstractListModel(parent)
{
}

int PackageListModel::rowCount(const QModelIndex& parent) const
{
    if (parent.isValid()) return 0;
    return m_packages.size();
}

QVariant PackageListModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() >= m_packages.size()) return QVariant();
    const QVariantMap& package = m_packages.at(index.row());
    switch (role) {
        case NameRole:               return package.value("name");
        case ModuleNameRole:         return package.value("moduleName");
        case DescriptionRole:        return package.value("description");
        case TypeRole:               return package.value("type");
        case CategoryRole:           return package.value("category");
        case IsSelectedRole:         return package.value("isSelected", false);
        case InstallStatusRole:      return package.value("installStatus", 0);
        case DependenciesRole:       return package.value("dependencies");
        case IsVariantAvailableRole: return package.value("isVariantAvailable", false);
        case VersionRole:            return package.value("version");
        case InstalledVersionRole:   return package.value("installedVersion");
        case HashRole:               return package.value("hash");
        case InstalledHashRole:      return package.value("installedHash");
        case ErrorMessageRole:       return package.value("errorMessage");
        case InstallTypeRole:        return package.value("installType");
        case SizeRole:               return package.value("size");
        case DateUpdatedRole:        return package.value("dateUpdated");
        case NotAvailableReasonRole: return package.value("notAvailableReason");

        // ── Multi-repo additions ────────────────────────────────────────
        case RepositoryUrlRole:         return package.value("repositoryUrl");
        case RepositoryNameRole:        return package.value("repositoryName");
        case RepositoryDisplayNameRole: return package.value("repositoryDisplayName");
        // QVariantList of per-version maps. See `availableVersions`
        // construction in PackageManagerBackend::setPackagesFromVariantList.
        case AvailableVersionsRole:      return package.value("availableVersions");
        case SelectedVersionIndexRole:   return package.value("selectedVersionIndex", 0);
        case IsFirstOfSourceRole:        return package.value("isFirstOfSource", false);

        default:                     return QVariant();
    }
}

QHash<int, QByteArray> PackageListModel::roleNames() const
{
    return {
        {NameRole,                    "name"},
        {ModuleNameRole,              "moduleName"},
        {DescriptionRole,             "description"},
        {TypeRole,                    "type"},
        {CategoryRole,                "category"},
        {IsSelectedRole,              "isSelected"},
        {InstallStatusRole,           "installStatus"},
        {DependenciesRole,            "dependencies"},
        {IsVariantAvailableRole,      "isVariantAvailable"},
        {VersionRole,                 "version"},
        {InstalledVersionRole,        "installedVersion"},
        {HashRole,                    "hash"},
        {InstalledHashRole,           "installedHash"},
        {ErrorMessageRole,            "errorMessage"},
        {InstallTypeRole,             "installType"},
        {SizeRole,                    "size"},
        {DateUpdatedRole,             "dateUpdated"},
        {NotAvailableReasonRole,      "notAvailableReason"},
        {RepositoryUrlRole,           "repositoryUrl"},
        {RepositoryNameRole,          "repositoryName"},
        {RepositoryDisplayNameRole,   "repositoryDisplayName"},
        {AvailableVersionsRole,       "availableVersions"},
        {SelectedVersionIndexRole,    "selectedVersionIndex"},
        {IsFirstOfSourceRole,         "isFirstOfSource"},
    };
}

// ─────────────────────────── file-local helpers ───────────────────────────

// Build a stable composite key for selection / failed-state restoration.
// Multi-repo: two repositories can each publish a package called "foo",
// so keying on `name` alone would couple them. The format
// `<repositoryUrl><name>` uses a forbidden-in-URLs byte separator so the
// key is unambiguous and cheap to compare.
static QString rowKey(const QString& repositoryUrl, const QString& name)
{
    return repositoryUrl + QChar(0x01) + name;
}

static QString rowKey(const QVariantMap& pkg)
{
    return rowKey(pkg.value("repositoryUrl").toString(),
                  pkg.value("name").toString());
}

// Centralised row predicates — read by the eligibility-aware getters and
// mirrored by the per-row gating in PackageActionButton.qml.
using RowPredicate = bool (*)(const QVariantMap&);

static bool isInstallableRow(const QVariantMap& pkg)
{
    if (!pkg.value("isVariantAvailable", false).toBool()) return false;
    const int status = pkg.value("installStatus", 0).toInt();
    return status == static_cast<int>(PackageTypes::NotInstalled)
        || status == static_cast<int>(PackageTypes::Failed);
}

static bool isUninstallableRow(const QVariantMap& pkg)
{
    if (pkg.value("installType").toString() != QStringLiteral("user")) return false;
    const int status = pkg.value("installStatus", 0).toInt();
    return status == static_cast<int>(PackageTypes::Installed)
        || status == static_cast<int>(PackageTypes::UpgradeAvailable)
        || status == static_cast<int>(PackageTypes::DowngradeAvailable)
        || status == static_cast<int>(PackageTypes::DifferentHash);
}

// Count selected rows that also satisfy `pred`.
static int countSelectedMatching(const QList<QVariantMap>& rows, RowPredicate pred)
{
    int n = 0;
    for (const QVariantMap& pkg : rows)
        if (pkg.value("isSelected").toBool() && pred(pkg)) ++n;
    return n;
}

// Project selected rows that satisfy `pred` to their `field` value (skipping empties).
static QStringList collectSelectedField(const QList<QVariantMap>& rows,
                                        RowPredicate pred,
                                        const char* field)
{
    QStringList out;
    for (const QVariantMap& pkg : rows) {
        if (!pkg.value("isSelected").toBool()) continue;
        if (!pred(pkg)) continue;
        const QString v = pkg.value(field).toString();
        if (!v.isEmpty()) out.append(v);
    }
    return out;
}

// Apply `mutate` to every row matching `pred`; return the [first, last] indices
// of mutated rows (or {-1,-1} if none). Callers emit dataChanged over the span.
template<typename Pred, typename Mut>
static std::pair<int, int> mutateMatchingRows(QList<QVariantMap>& rows, Pred pred, Mut mutate)
{
    int first = -1, last = -1;
    for (int i = 0; i < rows.size(); ++i) {
        if (!pred(rows[i])) continue;
        mutate(rows[i]);
        if (first < 0) first = i;
        last = i;
    }
    return {first, last};
}

// ─────────────────────────────── mutators ────────────────────────────────

void PackageListModel::setPackages(const QList<QVariantMap>& packages)
{
    // Save selected rows by composite (repo, name) key before reset.
    // Using bare names would mark the WRONG row when two repos publish
    // the same package name.
    QSet<QString> previouslySelectedKeys;
    for (const QVariantMap& pkg : m_packages) {
        if (pkg.value("isSelected").toBool()) previouslySelectedKeys.insert(rowKey(pkg));
    }
    // Snapshot per-row selectedVersionIndex so the user's pick survives a
    // category filter pass or a debounced post-install refresh.
    QHash<QString, int> selectedVersionByKey;
    for (const QVariantMap& pkg : m_packages) {
        const int idx = pkg.value("selectedVersionIndex", 0).toInt();
        if (idx != 0) selectedVersionByKey.insert(rowKey(pkg), idx);
    }

    beginResetModel();
    m_packages = packages;

    // Walk incoming rows. For each row:
    //   * Restore selection (only when the row has an available variant)
    //     using the composite (repo, name) key so two repos' "foo" rows
    //     don't share selection state.
    //   * Restore per-row selectedVersionIndex from the snapshot, clamped
    //     to the new availableVersions length.
    //   * If the row came in as NotInstalled AND m_failedByKey has an
    //     entry for its key/moduleName, inject Failed + errorMessage so
    //     the banner survives category-filter drop-outs and disk-scan
    //     post-install refreshes.
    //   * Otherwise drop the cache entry so Failed doesn't resurrect
    //     on a later flip back to NotInstalled.
    for (QVariantMap& row : m_packages) {
        const QString moduleName = row.value("moduleName").toString();
        const QString key = rowKey(row);
        const bool available = row.value("isVariantAvailable", false).toBool();
        row["isSelected"] = available && previouslySelectedKeys.contains(key);

        if (selectedVersionByKey.contains(key)) {
            const QVariantList avail = row.value("availableVersions").toList();
            int idx = selectedVersionByKey.value(key);
            if (idx < 0 || idx >= avail.size()) idx = 0;
            row["selectedVersionIndex"] = idx;
        }

        const int status = row.value("installStatus", 0).toInt();
        if (status == static_cast<int>(PackageTypes::NotInstalled)) {
            auto it = m_failedByKey.constFind(key);
            // Fallback for install paths that key by moduleName only (gated
            // uninstall/upgrade events). The composite key is preferred
            // but either lookup is accepted for the back-fill.
            if (it == m_failedByKey.constEnd() && !moduleName.isEmpty())
                it = m_failedByKey.constFind(moduleName);
            if (it != m_failedByKey.constEnd()) {
                row["installStatus"] = static_cast<int>(PackageTypes::Failed);
                row["errorMessage"] = it->errorMessage;
            }
        } else {
            if (!key.isEmpty())        m_failedByKey.remove(key);
            if (!moduleName.isEmpty()) m_failedByKey.remove(moduleName);
        }
    }

    endResetModel();
    emit hasSelectionChanged();
}

void PackageListModel::updatePackageSelection(int index, bool isSelected)
{
    if (index < 0 || index >= m_packages.size()) return;
    m_packages[index]["isSelected"] = isSelected;

    const QModelIndex modelIndex = createIndex(index, 0);
    emit dataChanged(modelIndex, modelIndex, {IsSelectedRole});
    emit hasSelectionChanged();
}

void PackageListModel::updatePackageInstallation(const QString& packageName, int status,
                                                  const QString& errorMessage)
{
    // Callers pass either the catalog `name` (install path keys on the .lgx
    // package name, which matches the catalog row's name) or the manifest
    // `moduleName` (uninstall / upgrade paths key on moduleName because
    // that's what package_manager_lib's byName map uses on disk). Match
    // against both so neither flow silently no-ops the model update.
    //
    // In the multi-repo world two rows can share `name` (different repos
    // publishing the same package); update EVERY matching row so the
    // failed/installed badge surfaces on each. The failed cache is
    // indexed by (repo, name) so post-refresh restoration keeps them
    // independent.
    int firstChanged = -1, lastChanged = -1;
    for (int i = 0; i < m_packages.size(); ++i) {
        QVariantMap& row = m_packages[i];
        const QString rowName = row.value("name").toString();
        const QString rowModuleName = row.value("moduleName").toString();
        if (rowName != packageName && rowModuleName != packageName) continue;

        row["installStatus"] = status;
        row["errorMessage"] = errorMessage;

        // Maintain m_failedByKey in lockstep with the row's status.
        // Indexed under both the composite (repo, name) key AND the bare
        // moduleName so either flow's lookup works.
        const QString key = rowKey(row);
        if (status == static_cast<int>(PackageTypes::Failed)) {
            const FailedEntry entry{ errorMessage };
            if (!key.isEmpty())            m_failedByKey.insert(key, entry);
            if (!rowModuleName.isEmpty())  m_failedByKey.insert(rowModuleName, entry);
        } else {
            if (!key.isEmpty())            m_failedByKey.remove(key);
            if (!rowModuleName.isEmpty())  m_failedByKey.remove(rowModuleName);
        }

        if (firstChanged < 0) firstChanged = i;
        lastChanged = i;
    }
    if (firstChanged < 0) return;
    emit dataChanged(createIndex(firstChanged, 0),
                     createIndex(lastChanged, 0),
                     {InstallStatusRole, ErrorMessageRole});
    emit hasSelectionChanged();
}

void PackageListModel::setRowVersion(int index, int versionIndex)
{
    if (index < 0 || index >= m_packages.size()) return;
    const QVariantList avail = m_packages[index].value("availableVersions").toList();
    if (versionIndex < 0 || versionIndex >= avail.size()) versionIndex = 0;

    const int currentIdx = m_packages[index].value("selectedVersionIndex", 0).toInt();
    if (currentIdx == versionIndex) return;
    m_packages[index]["selectedVersionIndex"] = versionIndex;

    // Surface the chosen version's `version` / `rootHash` in the existing
    // VersionRole / HashRole so the rest of the QML (status comparison,
    // details panel, etc.) sees the user's pick rather than the row's
    // initial defaults. The install path reads availableVersions[
    // selectedVersionIndex] directly, but mirroring the fields keeps
    // status-text bindings consistent.
    if (versionIndex < avail.size()) {
        const QVariantMap pick = avail.at(versionIndex).toMap();
        m_packages[index]["version"] = pick.value("version");
        m_packages[index]["hash"]    = pick.value("rootHash");
    }

    const QModelIndex mi = createIndex(index, 0);
    emit dataChanged(mi, mi,
        {SelectedVersionIndexRole, VersionRole, HashRole, InstallStatusRole});
}

// ─────────────────────────────── selectors ───────────────────────────────

QStringList PackageListModel::getSelectedPackageNames() const
{
    QStringList names;
    for (const QVariantMap& pkg : m_packages)
        if (pkg.value("isSelected").toBool())
            names.append(pkg.value("name").toString());
    return names;
}

int PackageListModel::getSelectedCount() const
{
    int n = 0;
    for (const QVariantMap& pkg : m_packages)
        if (pkg.value("isSelected").toBool()) ++n;
    return n;
}

int PackageListModel::getInstallableSelectedCount() const
{
    return countSelectedMatching(m_packages, isInstallableRow);
}

int PackageListModel::getUninstallableSelectedCount() const
{
    return countSelectedMatching(m_packages, isUninstallableRow);
}

QStringList PackageListModel::getInstallableSelectedPackageNames() const
{
    return collectSelectedField(m_packages, isInstallableRow, "name");
}

QStringList PackageListModel::getUninstallableSelectedModuleNames() const
{
    return collectSelectedField(m_packages, isUninstallableRow, "moduleName");
}

QVariantMap PackageListModel::packageAt(int index) const
{
    if (index < 0 || index >= m_packages.size()) return QVariantMap();
    return m_packages.at(index);
}

QString PackageListModel::displayNameForModule(const QString& moduleName) const
{
    if (moduleName.isEmpty()) return QString();
    for (const QVariantMap& pkg : m_packages)
        if (pkg.value("moduleName").toString() == moduleName)
            return pkg.value("name").toString();
    return QString();
}

// ──────────────────────────── selection clears ────────────────────────────

void PackageListModel::clearSelectionsByPackageNames(const QStringList& names)
{
    clearSelectionsBy(names, "name");
}

void PackageListModel::clearSelectionsByModuleNames(const QStringList& moduleNames)
{
    clearSelectionsBy(moduleNames, "moduleName");
}

void PackageListModel::clearSelectionsBy(const QStringList& keys, const char* field)
{
    if (keys.isEmpty() || m_packages.isEmpty()) return;
    const QSet<QString> keySet(keys.begin(), keys.end());

    auto [first, last] = mutateMatchingRows(m_packages,
        [&](const QVariantMap& p) {
            return p.value("isSelected").toBool()
                && keySet.contains(p.value(field).toString());
        },
        [](QVariantMap& p) { p["isSelected"] = false; });

    if (first < 0) return;
    emit dataChanged(createIndex(first, 0), createIndex(last, 0), {IsSelectedRole});
    emit hasSelectionChanged();
}

void PackageListModel::clearAllSelections()
{
    auto [first, last] = mutateMatchingRows(m_packages,
        [](const QVariantMap& p) { return p.value("isSelected").toBool(); },
        [](QVariantMap& p) { p["isSelected"] = false; });

    if (first < 0) return;
    emit dataChanged(createIndex(first, 0), createIndex(last, 0), {IsSelectedRole});
    emit hasSelectionChanged();
}

void PackageListModel::clearFailedRows()
{
    m_failedByKey.clear();

    auto [first, last] = mutateMatchingRows(m_packages,
        [](const QVariantMap& p) {
            return p.value("installStatus", 0).toInt()
                == static_cast<int>(PackageTypes::Failed);
        },
        [](QVariantMap& p) {
            p["installStatus"] = static_cast<int>(PackageTypes::NotInstalled);
            p["errorMessage"] = QString();
        });

    if (first < 0) return;
    emit dataChanged(createIndex(first, 0), createIndex(last, 0),
                     {InstallStatusRole, ErrorMessageRole});
    emit hasSelectionChanged();
}
