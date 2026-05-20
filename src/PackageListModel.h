#pragma once

#include <QAbstractListModel>
#include <QHash>
#include <QList>
#include <QPair>
#include <QString>
#include <QVariantMap>
#include <QStringList>

// Bulk "Run Actions" plan. Built from the model's selected-row state by
// buildActionPlanForSelected(); consumed by
// PackageManagerBackend::runSelectedActions() — installs (+ retries)
// batch through downloadResolvedDependencies, version changes dispatch
// per-row to package_manager.requestUpgrade.
//
// Per-action counts are tracked explicitly (rather than derived) so the
// confirm-summary popup can label retries separately from fresh
// installs, and upgrade/downgrade/reinstall separately within the
// versionChanges list (whose entries only carry (index, mode)).
struct PackageActionPlan {
    // Catalog `name`s for downloadResolvedDependencies — both Install
    // and Retry rows go here, since they share the same execution path.
    QStringList installNames;
    // (modelRowIndex, UpgradeMode-as-int) for each Upgrade / Downgrade /
    // Reinstall row. Mode is the same int the per-row backend slot
    // already uses, so each entry forwards straight to
    // PackageManagerBackend::requestVersionChange.
    QList<QPair<int, int>> versionChanges;

    // Counts (zero by default). install + retry == installNames.size().
    int installCount   = 0;
    int retryCount     = 0;
    int upgradeCount   = 0;
    int downgradeCount = 0;
    int reinstallCount = 0;

    int total() const { return installNames.size() + versionChanges.size(); }
    bool isEmpty() const { return total() == 0; }

    // { install: N, upgrade: N, downgrade: N, reinstall: N, retry: N }
    // — only non-zero counts; the confirm-summary popup renders one
    // line per key in QML.
    QVariantMap toSummary() const;
};

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
        IsFirstOfSourceRole,

        // The row's primary action (PackageTypes::RowAction enum int),
        // resolved by RowActionResolver::resolveRowAction AGAINST THE
        // SELECTED dropdown version — not the catalog newest. Recomputed
        // by setRowVersion() and updatePackageInstallation() so the
        // Action column always reflects "what happens if you click".
        // This is the role the QML ActionPill binds to.
        RowActionRole,

        // True iff a strictly-newer-than-installed version exists in
        // `availableVersions`, regardless of the user's dropdown pick.
        // Drives the small marker on the Version cell. Computed once
        // per buildPackageRow (no dropdown coupling).
        UpdateAvailableRole
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

    // Build the bulk "Run Actions" plan from the current selection.
    // Walks every selected row, reads its rowAction, and projects:
    //   - Install / Retry      → installNames (single batched download)
    //   - Upgrade / Downgrade  → versionChanges with the matching mode
    //   - Reinstall            → versionChanges with mode = Sidegrade
    //   - NoOp / NotAvailable  → ignored (the pill is non-clickable for
    //                            these too, so they shouldn't end up here
    //                            via the per-row click path either)
    // Uninstall is never in the plan — the row overflow ⋮ menu handles
    // it explicitly per-row.
    PackageActionPlan buildActionPlanForSelected() const;

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
