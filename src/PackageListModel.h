#pragma once

#include <QAbstractListModel>
#include <QHash>
#include <QString>
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
        DependenciesRole,
        IsVariantAvailableRole,
        VersionRole,
        InstalledVersionRole,
        HashRole,
        InstalledHashRole,
        ErrorMessageRole,
        InstallTypeRole,
        SizeRole,
        DateUpdatedRole
    };

    explicit PackageListModel(QObject* parent = nullptr);
    ~PackageListModel() override = default;

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void setPackages(const QList<QVariantMap>& packages);
    void updatePackageSelection(int index, bool isSelected);
    void updatePackageInstallation(const QString& packageName, int status,
                                   const QString& errorMessage = QString());

    QStringList getSelectedPackageNames() const;
    int getSelectedCount() const;

    int getInstallableSelectedCount() const;
    int getUninstallableSelectedCount() const;
    QStringList getInstallableSelectedPackageNames() const;
    QStringList getUninstallableSelectedModuleNames() const;
    void clearSelectionsByPackageNames(const QStringList& names);
    void clearSelectionsByModuleNames(const QStringList& moduleNames);

    QVariantMap packageAt(int index) const;
    QString displayNameForModule(const QString& moduleName) const;
    void clearAllSelections();
    void clearFailedRows();

signals:
    void hasSelectionChanged();

private:
    void clearSelectionsBy(const QStringList& keys, const char* field);

    struct FailedEntry { QString errorMessage; };
    
    QHash<QString, FailedEntry> m_failedByKey;

    QList<QVariantMap> m_packages;
};
