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
    return roles;
}

void PackageListModel::setPackages(const QList<QVariantMap>& packages)
{
    // Save currently selected package names before reset
    QStringList selectedNames = getSelectedPackageNames();

    beginResetModel();
    m_packages = packages;

    // Restore selections for packages that still exist; ensure every package has isSelected (bool)
    for (int i = 0; i < m_packages.size(); ++i) {
        QString name = m_packages[i].value("name").toString();
        m_packages[i]["isSelected"] = selectedNames.contains(name);
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

void PackageListModel::updatePackageInstallation(const QString& packageName, int status)
{
    for (int i = 0; i < m_packages.size(); ++i) {
        if (m_packages[i].value("name").toString() == packageName) {
            m_packages[i]["installStatus"] = status;

            QModelIndex modelIndex = createIndex(i, 0);
            emit dataChanged(modelIndex, modelIndex, {InstallStatusRole});
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
