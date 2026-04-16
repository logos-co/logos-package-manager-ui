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
        DependenciesRole,
        IsVariantAvailableRole,
        VersionRole,
        InstalledVersionRole,
        HashRole,
        InstalledHashRole,
        ErrorMessageRole,
        InstallTypeRole
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
    int getCompletedInstallCount() const;

    QVariantMap packageAt(int index) const;

    // Map a backend-internal moduleName (e.g. "wallet_module") back to its
    // user-facing package/display name (e.g. "logos-wallet-module"). Returns
    // an empty string when no package with that moduleName is known —
    // PackageManagerBackend::displayNameForModule (the QML-facing slot)
    // falls back to the moduleName in that case.
    //
    // The backend keeps moduleName as the stable identifier for every IPC
    // call (uninstallPackage/resolveDependents/cascadeUnloadRequested all
    // key on moduleName); this helper is presentation-layer only.
    QString displayNameForModule(const QString& moduleName) const;

    void clearAllSelections();

private:
    QList<QVariantMap> m_packages;
};
