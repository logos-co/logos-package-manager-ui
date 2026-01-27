#pragma once

#include <QAbstractListModel>
#include <QVariantMap>
#include <QStringList>

class PackageListModel : public QAbstractListModel {
    Q_OBJECT

public:
    enum PackageRoles {
        NameRole = Qt::UserRole + 1,
        ModuleNameRole,
        DescriptionRole,
        TypeRole,
        CategoryRole,
        IsSelectedRole,
        InstallStatusRole,
        DependenciesRole
    };

    explicit PackageListModel(QObject* parent = nullptr);
    ~PackageListModel() override = default;

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void setPackages(const QList<QVariantMap>& packages);
    void updatePackageSelection(int index, bool isSelected);
    void updatePackageInstallation(const QString& packageName, int status);

    QStringList getSelectedPackageNames() const;
    int getSelectedCount() const;
    int getCompletedInstallCount() const;

    QVariantMap packageAt(int index) const;

    void clearAllSelections();

private:
    QList<QVariantMap> m_packages;
};
