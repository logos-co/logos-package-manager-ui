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
        DateUpdatedRole,
        NotAvailableReasonRole,

        // Multi-repo additions. Row identity is now (RepositoryUrl, Name)
        // rather than just Name — two repos can each publish a package
        // called "foo" and we render both as distinct rows.
        RepositoryUrlRole,           // canonical URL of the repo's logos-repo.json
        RepositoryNameRole,          // canonical id from logos-repo.json#name
        RepositoryDisplayNameRole,   // human-friendly badge label

        // Per-row version selector. `availableVersions` is an array of
        // { version, rootHash, releasedAt, url, signed, manifest } —
        // populated from the new index.json schema, newest first.
        // `selectedVersionIndex` is the index the user picked; defaults
        // to 0 (latest). Version/Hash columns and the install path read
        // (version, rootHash) from `availableVersions[selectedVersionIndex]`.
        AvailableVersionsRole,
        SelectedVersionIndexRole,

        // True for the first row of each source group when the model is
        // sorted by (sourcePriority, sourceName, name). The QML uses it
        // to render a section header above the row instead of repeating
        // the source label on every row. Computed by
        // PackageManagerBackend::setPackagesFromVariantList after its
        // group-ordered sort. Filter-proxy reslicing can stale the
        // value when the first row of a source gets filtered out, but
        // the default unfiltered view is always correct.
        IsFirstOfSourceRole
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

    // Pick a different version on a single row. Clamps `versionIndex`
    // to the row's `availableVersions` length; out-of-range or negative
    // values reset to 0 (latest). Triggers a dataChanged on the version /
    // hash / status roles so the visible columns refresh in one shot
    // (status can flip between Installed / UpgradeAvailable / etc. when
    // the selected release version moves relative to the installed one).
    void setRowVersion(int index, int versionIndex);

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
