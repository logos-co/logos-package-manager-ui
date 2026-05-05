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
        case SizeRole:                return package.value("size");
        case DateUpdatedRole:         return package.value("dateUpdated");
        default:                      return QVariant();
    }
}

QHash<int, QByteArray> PackageListModel::roleNames() const
{
    return {
        {NameRole,               "name"},
        {ModuleNameRole,         "moduleName"},
        {DescriptionRole,        "description"},
        {TypeRole,               "type"},
        {CategoryRole,           "category"},
        {IsSelectedRole,         "isSelected"},
        {InstallStatusRole,      "installStatus"},
        {DependenciesRole,       "dependencies"},
        {IsVariantAvailableRole, "isVariantAvailable"},
        {VersionRole,            "version"},
        {InstalledVersionRole,   "installedVersion"},
        {HashRole,               "hash"},
        {InstalledHashRole,      "installedHash"},
        {ErrorMessageRole,       "errorMessage"},
        {InstallTypeRole,        "installType"},
        {SizeRole,               "size"},
        {DateUpdatedRole,        "dateUpdated"},
    };
}

// ─────────────────────────── file-local helpers ───────────────────────────

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
    const QStringList prevNames = getSelectedPackageNames();
    const QSet<QString> previouslySelected(prevNames.begin(), prevNames.end());

    beginResetModel();
    m_packages = packages;
    for (QVariantMap& row : m_packages) {
        const QString name = row.value("name").toString();
        const QString moduleName = row.value("moduleName").toString();
        const bool available = row.value("isVariantAvailable", false).toBool();
        row["isSelected"] = available && previouslySelected.contains(name);

        const int status = row.value("installStatus", 0).toInt();
        if (status == static_cast<int>(PackageTypes::NotInstalled)) {
            auto it = m_failedByKey.constFind(name);
            if (it == m_failedByKey.constEnd() && !moduleName.isEmpty())
                it = m_failedByKey.constFind(moduleName);
            if (it != m_failedByKey.constEnd()) {
                row["installStatus"] = static_cast<int>(PackageTypes::Failed);
                row["errorMessage"] = it->errorMessage;
            }
        } else {
            if (!name.isEmpty())       m_failedByKey.remove(name);
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
    for (int i = 0; i < m_packages.size(); ++i) {
        QVariantMap& row = m_packages[i];
        const QString rowName = row.value("name").toString();
        const QString rowModuleName = row.value("moduleName").toString();
        if (rowName != packageName && rowModuleName != packageName) continue;

        row["installStatus"] = status;
        row["errorMessage"] = errorMessage;

        if (status == static_cast<int>(PackageTypes::Failed)) {
            const FailedEntry entry{ errorMessage };
            if (!rowName.isEmpty())       m_failedByKey.insert(rowName, entry);
            if (!rowModuleName.isEmpty()) m_failedByKey.insert(rowModuleName, entry);
        } else {
            if (!rowName.isEmpty())       m_failedByKey.remove(rowName);
            if (!rowModuleName.isEmpty()) m_failedByKey.remove(rowModuleName);
        }

        const QModelIndex modelIndex = createIndex(i, 0);
        emit dataChanged(modelIndex, modelIndex, {InstallStatusRole, ErrorMessageRole});
        emit hasSelectionChanged();
        return;
    }
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
