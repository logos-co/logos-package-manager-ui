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

    // Snapshot Failed-state so a post-install refresh doesn't clobber the
    // error the user just saw. The catalog rebuild has no knowledge of the
    // failed-install attempt — the package isn't on disk, so the scan
    // reports it as not-installed and PackageManagerBackend computes
    // status=NotInstalled with errorMessage="". Without this preservation
    // the Failed row (set by updatePackageInstallation on line 402 of
    // PackageManagerBackend.cpp) silently reverts the instant
    // finishInstallation → refreshPackages fires, making the install
    // error impossible to inspect. The Failed state survives until the
    // package is successfully (re)installed (new status != NotInstalled,
    // nothing to preserve) or drops out of the catalog entirely.
    struct PriorFailure { QString errorMessage; };
    QHash<QString, PriorFailure> priorFailureByName;
    for (const QVariantMap& pkg : m_packages) {
        int status = pkg.value("installStatus", 0).toInt();
        if (status != static_cast<int>(PackageTypes::Failed)) continue;
        PriorFailure pf{ pkg.value("errorMessage").toString() };
        // Index under both name and moduleName — the incoming row may be
        // keyed by either (install path uses the .lgx package name;
        // uninstall/upgrade paths use moduleName).
        const QString priorName = pkg.value("name").toString();
        const QString priorModuleName = pkg.value("moduleName").toString();
        if (!priorName.isEmpty()) priorFailureByName.insert(priorName, pf);
        if (!priorModuleName.isEmpty()) priorFailureByName.insert(priorModuleName, pf);
    }

    beginResetModel();
    m_packages = packages;

    // Restore selections for packages that still exist; ensure every package has isSelected (bool).
    // Never restore selection for packages without an available variant.
    // Carry forward Failed status + errorMessage for rows that came back
    // NotInstalled (see snapshot above for why).
    for (int i = 0; i < m_packages.size(); ++i) {
        QString name = m_packages[i].value("name").toString();
        QString moduleName = m_packages[i].value("moduleName").toString();
        bool available = m_packages[i].value("isVariantAvailable", false).toBool();
        m_packages[i]["isSelected"] = available && selectedNames.contains(name);

        int status = m_packages[i].value("installStatus", 0).toInt();
        if (status == static_cast<int>(PackageTypes::NotInstalled)) {
            auto it = priorFailureByName.constFind(name);
            if (it == priorFailureByName.constEnd() && !moduleName.isEmpty()) {
                it = priorFailureByName.constFind(moduleName);
            }
            if (it != priorFailureByName.constEnd()) {
                m_packages[i]["installStatus"] = static_cast<int>(PackageTypes::Failed);
                m_packages[i]["errorMessage"] = it->errorMessage;
            }
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
