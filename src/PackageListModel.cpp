#include "PackageListModel.h"
#include "PackageTypes.h"

PackageListModel::PackageListModel(QObject* parent)
    : QAbstractListModel(parent)
{
}

int PackageListModel::rowCount(const QModelIndex& parent) const
{
    if (parent.isValid()) {
        return 0;
    }
    return m_packages.size();
}

QVariant PackageListModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() >= m_packages.size()) {
        return QVariant();
    }

    const QVariantMap& package = m_packages.at(index.row());

    switch (role) {
        case NameRole:
            return package.value("name");
        case ModuleNameRole:
            return package.value("moduleName");
        case DescriptionRole:
            return package.value("description");
        case TypeRole:
            return package.value("type");
        case CategoryRole:
            return package.value("category");
        case IsSelectedRole:
            return package.value("isSelected", false);
        case InstallStatusRole:
            return package.value("installStatus", 0);
        case DependenciesRole:
            return package.value("dependencies");
        case IsVariantAvailableRole:
            return package.value("isVariantAvailable", false);
        case VersionRole:
            return package.value("version");
        case InstalledVersionRole:
            return package.value("installedVersion");
        case HashRole:
            return package.value("hash");
        case InstalledHashRole:
            return package.value("installedHash");
        case ErrorMessageRole:
            return package.value("errorMessage");
        case InstallTypeRole:
            // "embedded" | "user" | "" (not installed). QML gates the Uninstall
            // affordance on installType === "user".
            return package.value("installType");
        default:
            return QVariant();
    }
}

QHash<int, QByteArray> PackageListModel::roleNames() const
{
    QHash<int, QByteArray> roles;
    roles[NameRole] = "name";
    roles[ModuleNameRole] = "moduleName";
    roles[DescriptionRole] = "description";
    roles[TypeRole] = "type";
    roles[CategoryRole] = "category";
    roles[IsSelectedRole] = "isSelected";
    roles[InstallStatusRole] = "installStatus";
    roles[DependenciesRole] = "dependencies";
    roles[IsVariantAvailableRole] = "isVariantAvailable";
    roles[VersionRole] = "version";
    roles[InstalledVersionRole] = "installedVersion";
    roles[HashRole] = "hash";
    roles[InstalledHashRole] = "installedHash";
    roles[ErrorMessageRole] = "errorMessage";
    roles[InstallTypeRole] = "installType";
    return roles;
}

void PackageListModel::setPackages(const QList<QVariantMap>& packages)
{
    // Save currently selected package names before reset
    QStringList selectedNames = getSelectedPackageNames();

    beginResetModel();
    m_packages = packages;

    // Walk incoming rows. For each row:
    //   * Restore selection (only when the row has an available variant).
    //   * If the row came in as NotInstalled AND m_failedByKey has an
    //     entry for its name/moduleName, inject Failed + errorMessage.
    //     This is what makes the banner survive both post-install refresh
    //     (disk scan says not installed) AND category filtering (row was
    //     absent from the previous m_packages, so a per-call snapshot
    //     would have lost it).
    //   * If the row came in as anything else (Installed, Installing,
    //     UpgradeAvailable, ...), the package is no longer in a failed
    //     state — drop the cache entry so it doesn't resurrect Failed
    //     later if the row flips back to NotInstalled (e.g. via a
    //     subsequent uninstall event).
    for (int i = 0; i < m_packages.size(); ++i) {
        QString name = m_packages[i].value("name").toString();
        QString moduleName = m_packages[i].value("moduleName").toString();
        bool available = m_packages[i].value("isVariantAvailable", false).toBool();
        m_packages[i]["isSelected"] = available && selectedNames.contains(name);

        int status = m_packages[i].value("installStatus", 0).toInt();
        if (status == static_cast<int>(PackageTypes::NotInstalled)) {
            auto it = m_failedByKey.constFind(name);
            if (it == m_failedByKey.constEnd() && !moduleName.isEmpty()) {
                it = m_failedByKey.constFind(moduleName);
            }
            if (it != m_failedByKey.constEnd()) {
                m_packages[i]["installStatus"] = static_cast<int>(PackageTypes::Failed);
                m_packages[i]["errorMessage"] = it->errorMessage;
            }
        } else {
            if (!name.isEmpty())       m_failedByKey.remove(name);
            if (!moduleName.isEmpty()) m_failedByKey.remove(moduleName);
        }
    }

    endResetModel();
}

void PackageListModel::updatePackageSelection(int index, bool isSelected)
{
    if (index < 0 || index >= m_packages.size()) {
        return;
    }

    m_packages[index]["isSelected"] = isSelected;
    
    QModelIndex modelIndex = createIndex(index, 0);
    emit dataChanged(modelIndex, modelIndex, {IsSelectedRole});
}

void PackageListModel::updatePackageInstallation(const QString& packageName, int status,
                                                  const QString& errorMessage)
{
    // Callers pass either the catalog `name` (install path uses the .lgx file's
    // package name, which matches the catalog row's name) or the manifest
    // `moduleName` (uninstall / upgrade paths key on moduleName because that's
    // what package_manager_lib's byName map uses on disk). Match against both
    // so neither flow silently no-ops the model update — a stale row would
    // make the user think the operation didn't take effect, prompting a retry
    // that hits a real "Package not found" the second time around.
    for (int i = 0; i < m_packages.size(); ++i) {
        const QString rowName = m_packages[i].value("name").toString();
        const QString rowModuleName = m_packages[i].value("moduleName").toString();
        if (rowName == packageName || rowModuleName == packageName) {
            m_packages[i]["installStatus"] = status;
            m_packages[i]["errorMessage"] = errorMessage;

            // Maintain m_failedByKey in lockstep with the row's status so
            // Failed survives filter drop-outs AND clears cleanly on
            // retry / successful install. Indexed under both identifiers
            // for the same reason setPackages does the dual lookup.
            if (status == static_cast<int>(PackageTypes::Failed)) {
                FailedEntry entry{ errorMessage };
                if (!rowName.isEmpty())       m_failedByKey.insert(rowName,       entry);
                if (!rowModuleName.isEmpty()) m_failedByKey.insert(rowModuleName, entry);
            } else {
                if (!rowName.isEmpty())       m_failedByKey.remove(rowName);
                if (!rowModuleName.isEmpty()) m_failedByKey.remove(rowModuleName);
            }

            QModelIndex modelIndex = createIndex(i, 0);
            emit dataChanged(modelIndex, modelIndex, {InstallStatusRole, ErrorMessageRole});
            break;
        }
    }
}

QStringList PackageListModel::getSelectedPackageNames() const
{
    QStringList names;
    for (const QVariantMap& pkg : m_packages) {
        if (pkg.value("isSelected").toBool()) {
            names.append(pkg.value("name").toString());
        }
    }
    return names;
}

int PackageListModel::getSelectedCount() const
{
    int count = 0;
    for (const QVariantMap& pkg : m_packages) {
        if (pkg.value("isSelected").toBool()) {
            ++count;
        }
    }
    return count;
}

int PackageListModel::getCompletedInstallCount() const
{
    int count = 0;
    for (const QVariantMap& pkg : m_packages) {
        if (!pkg.value("isSelected").toBool()) {
            continue;
        }
        int status = pkg.value("installStatus", 0).toInt();
        if (status == static_cast<int>(PackageTypes::Installed) || status == static_cast<int>(PackageTypes::Failed)) {
            ++count;
        }
    }
    return count;
}

QVariantMap PackageListModel::packageAt(int index) const
{
    if (index < 0 || index >= m_packages.size()) {
        return QVariantMap();
    }
    return m_packages.at(index);
}

QString PackageListModel::displayNameForModule(const QString& moduleName) const
{
    if (moduleName.isEmpty()) return QString();
    for (const QVariantMap& pkg : m_packages) {
        if (pkg.value("moduleName").toString() == moduleName) {
            return pkg.value("name").toString();
        }
    }
    return QString();
}

void PackageListModel::clearFailedRows()
{
    // Drop the persistent cache first — otherwise the next setPackages()
    // would re-inject Failed for any row matching a still-live cache key,
    // defeating the caller's intent (release changed / full reload).
    m_failedByKey.clear();

    // Then flip any currently-visible Failed rows back to NotInstalled +
    // blank errorMessage. We coalesce the resulting dataChanged into a
    // single contiguous range — Qt views handle multi-range updates
    // poorly and a single emit covering the affected span is cheap (the
    // QML view re-evaluates only the changed roles for the rows it paints).
    int firstChanged = -1;
    int lastChanged = -1;
    for (int i = 0; i < m_packages.size(); ++i) {
        const int status = m_packages[i].value("installStatus", 0).toInt();
        if (status != static_cast<int>(PackageTypes::Failed)) continue;
        m_packages[i]["installStatus"] = static_cast<int>(PackageTypes::NotInstalled);
        m_packages[i]["errorMessage"] = QString();
        if (firstChanged < 0) firstChanged = i;
        lastChanged = i;
    }
    if (firstChanged < 0) return;  // nothing was Failed in m_packages
    QModelIndex topLeft = createIndex(firstChanged, 0);
    QModelIndex bottomRight = createIndex(lastChanged, 0);
    emit dataChanged(topLeft, bottomRight, {InstallStatusRole, ErrorMessageRole});
}

void PackageListModel::clearAllSelections()
{
    bool changed = false;
    for (int i = 0; i < m_packages.size(); ++i) {
        if (m_packages[i].value("isSelected").toBool()) {
            m_packages[i]["isSelected"] = false;
            changed = true;
        }
    }
    if (changed && !m_packages.isEmpty()) {
        QModelIndex topLeft = createIndex(0, 0);
        QModelIndex bottomRight = createIndex(m_packages.size() - 1, 0);
        emit dataChanged(topLeft, bottomRight, {IsSelectedRole});
    }
}
