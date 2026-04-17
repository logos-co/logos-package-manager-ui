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

    // Wipe Failed rows back to NotInstalled with empty errorMessage AND
    // drop the persistent m_failedByKey cache. Called by
    // PackageManagerBackend on release change and on reload — the Failed
    // banner refers to a specific install attempt against the previously
    // selected release, so once the user picks a different release (or
    // forces a full reload) it's stale and should not survive into the
    // re-fetched catalog. The debounced post-install refresh path does
    // NOT call this — there, preserving Failed is the whole point (see
    // setPackages + the m_failedByKey cache).
    void clearFailedRows();

private:
    struct FailedEntry { QString errorMessage; };

    // Persistent Failed-state cache, keyed by BOTH package name and
    // moduleName (the install path calls updatePackageInstallation with
    // catalog name, the gated uninstall/upgrade path with moduleName — we
    // index under both so lookup works regardless). Kept outside the row
    // list so Failed survives:
    //   * category filtering — a Failed row outside the current category
    //     drops out of m_packages; switching back brings it in with the
    //     cached Failed badge (see setPackages injection).
    //   * post-install catalog refresh — the disk scan reports the package
    //     as not-installed (install failed, nothing on disk), so incoming
    //     status is NotInstalled; we inject Failed from the cache.
    // Cleared on explicit package-list refresh (clearFailedRows called
    // from reload / release change) and invalidated per-entry when
    // setPackages sees the row come back non-NotInstalled (the install
    // eventually succeeded, or it's mid-retry).
    QHash<QString, FailedEntry> m_failedByKey;

    QList<QVariantMap> m_packages;
};
