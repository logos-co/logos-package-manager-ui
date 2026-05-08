#pragma once

#include <functional>
#include <QObject>
#include <QTimer>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>
#include <QStringList>
#include "logos_api.h"
#include "logos_api_client.h"
#include "PackageListModel.h"
#include "PackagesFilterProxy.h"
#include "PackagesPagingProxy.h"
#include "PackageTypes.h"
#include "rep_package_manager_ui_source.h"

// Source-side implementation of the PackageManagerUi .rep interface.
// The `packages` Q_PROPERTY exposes a model proxy stack (raw → filter →
// paging) that ui-host remotes separately because QAbstractItemModel*
// can't flow through a .rep; QML reaches it via logos.model(...).
class PackageManagerBackend : public PackageManagerUiSimpleSource {
    Q_OBJECT
    Q_PROPERTY(QAbstractItemModel* packages READ packages CONSTANT)

public:
    explicit PackageManagerBackend(LogosAPI* logosAPI = nullptr, QObject* parent = nullptr);
    ~PackageManagerBackend() = default;

    QAbstractItemModel* packages() const;

public slots:
    // Overrides of the pure-virtual slots generated from the .rep.
    // See package_manager_ui.rep for per-slot documentation.
    void refreshCatalog() override;
    // Bulk: run each selected row's resolved primary action (the new
    // "Run Actions" header button). Subsumes the old installSelected
    // path AND adds upgrade / downgrade / reinstall to the bulk surface.
    void runSelectedActions() override;
    void installSelected() override;   // kept for back-compat, unwired from UI
    void uninstallSelected() override; // kept for back-compat, unwired from UI
    void togglePackage(int index, bool checked) override;
    void installPackage(int index) override;
    void reloadPackage(int index) override;
    void uninstall(int index) override;
    void upgradePackage(int index) override;
    void downgradePackage(int index) override;
    void sidegradePackage(int index) override;
    void requestPackageDetails(int index) override;
    void installFromFile(QUrl filePath) override;

    // Resolver-confirm responses. See `installDepsConfirmationRequested`
    // in the .rep for the flow. The argument is the opaque requestKey
    // from that signal (repo-scoped + name) — unique per pending
    // request, so a second preview for the same package name (possible
    // across repos) can't clobber the first, and a refresh between click
    // and confirm doesn't drop the dispatch.
    void confirmInstallWithDeps(QString requestKey) override;
    void confirmInstallWithoutDeps(QString requestKey) override;
    void cancelInstallConfirm(QString requestKey) override;

    // Forward to PackageListModel::setRowVersion. Pure proxy — the model
    // owns the clamping, mirror-into-version/hash fields, and dataChanged
    // emission so the QML view repaints without any extra backend logic.
    void setRowVersion(int index, int versionIndex) override;

    // Generated from the .rep as a pure-virtual slot (returns QString).
    // Delegates to the model's lookup; declared on the backend so the Repc
    // replica proxies it — the PackageListModel replica (QAbstractItemModel)
    // can't proxy Q_INVOKABLE methods because they're not part of the
    // QAbstractItemModel interface that gets remoted.
    QString displayNameForModule(QString moduleName) override;

    // Repository management — thin wrappers over package_downloader's
    // multi-repo API. Each kicks off an async invokeRemote and emits
    // repositoryOperationCompleted with the result. addRepository and
    // removeRepository also trigger a refreshRepositories() so the
    // panel's bound list re-renders without an explicit reload.
    void refreshRepositories() override;
    void addRepository(QString url) override;
    void removeRepository(QString url) override;
    void setRepositoryEnabled(QString url, bool enabled) override;

private:
    // Mirrors package_manager's UpgradeMode; passed to requestUpgrade as int
    // (avoids carrying an enum across the wire). Keep in sync with module side.
    enum class UpgradeMode { Upgrade = 0, Downgrade = 1, Sidegrade = 2 };

    void refreshPackages();

    // Bulk install pipeline — sequential download+install of N packages,
    // gated by the global isInstalling flag (so the bulk Install button can
    // disable itself during a batch). Each spec pins the row's repo +
    // dropdown-selected version so the dep resolver doesn't pick the
    // wrong package when two repos publish the same `name`. `includeDeps`
    // controls whether transitive deps returned by the resolver are
    // installed alongside the top-level entries (true) or filtered out
    // (false — "just the requested package(s)").
    void installSpecs(const QList<PackageInstallSpec>& specs,
                      bool includeDeps = true);
    // Legacy name-only wrapper kept for the unwired-but-still-present
    // installSelected() .rep slot. Builds specs with empty repo/version
    // — same loose semantics as before (resolver picks across repos +
    // newest version).
    void installNamed(const QStringList& packageNames);

    // Per-row install — runs independently of the global isInstalling
    // flag so multiple per-row clicks can run in parallel. `repoUrl` /
    // `version` empty = no pin (resolver chooses); set = scope the
    // download to exactly that repo/version (the per-row click path
    // always sets both). `includeDeps` is the same gate installSpecs
    // uses — false skips transitive deps entirely.
    void installSinglePackageAsync(const QString& packageName,
                                   const QString& repoUrl = QString(),
                                   const QString& version = QString(),
                                   bool includeDeps = true);

    // Sequential per-row install. Bulk path uses installNextPackage,
    // which locks isInstalling — per-row stays unlocked so concurrent
    // per-row clicks don't deadlock each other. Each entry runs
    // through installOnePackage, the loop chains to the next on success.
    // Progress signals carry `topLevelName` so the UI banner stays
    // anchored to the row the user clicked, even while transitive deps
    // are mid-install.
    void installResultsSequential(const QVariantList& results,
                                  const QString& topLevelName,
                                  int index);

    // Bulk-mark every entry's row as Installing — fired immediately
    // after the resolver returns so the UI shows the whole in-flight
    // batch at once rather than one-row-at-a-time as the sequential
    // loop reaches each. Skips error rows (those fail before any
    // install runs and need to surface as Failed, not Installing).
    void markEntriesInstalling(const QVariantList& entries);

    // Revert remaining entries (entries[fromIndex .. end]) to
    // NotInstalled. Called when installResultsSequential's loop stops
    // on a failure — entries we marked Installing upfront but never
    // got to need to flip back so the row badge isn't stuck.
    void revertPendingEntries(const QVariantList& entries, int fromIndex);

    // Resolve transitive deps for a (name, repoUrl, version) WITHOUT
    // downloading, then either:
    //   * No transitive changes needed → invoke dispatchPendingAction
    //     immediately with includeDeps=true (single-click experience
    //     for the common case).
    //   * Transitive changes needed → stash a PendingDepConfirm keyed
    //     by `packageName` and emit installDepsConfirmationRequested.
    //     The confirm slots later pick up the pending entry and run the
    //     matching path.
    // `actionKind` is the per-row dispatch (Install / Upgrade / ... —
    // see PendingDepConfirm::Action).
    void runDepPreviewForAction(const QString& packageName,
                                const QString& moduleName,
                                const QString& repoUrl,
                                const QString& version,
                                int actionKind);

    // Run the actual install / upgrade / downgrade / sidegrade for the
    // (packageName, repo, version) the user picked. Called from
    // runDepPreviewForAction (no-changes path) and from
    // confirmInstallWith{,out}Deps (post-dialog). includeDeps controls
    // whether transitive resolver picks are installed or filtered out.
    void dispatchPendingAction(const QString& packageName,
                               const QString& moduleName,
                               const QString& repoUrl,
                               const QString& version,
                               int actionKind,
                               bool includeDeps);

    // Compute the user-facing change list. Walks `resolved` (output of
    // package_downloader.resolveDependencies) for transitive (topLevel
    // != true) entries, looks each up in `installedByName`, and emits
    // a per-row description: {name, action, fromVersion, toVersion,
    // repository}. action is "install" if not installed, otherwise
    // "upgrade" / "downgrade" based on versionCmp.
    QVariantList computeDepChanges(const QVariantList& resolved,
                                   const QHash<QString, QString>& installedByName,
                                   const QHash<QString, QString>& repoUrlToName) const;

    // Serialise m_installedPackagesCache to the
    // [{name, version, rootHash}] shape package_downloader.resolveDependencies
    // expects in its `installedPackagesJson` parameter.
    QString buildInstalledPackagesJson() const;

    void setPackagesFromVariantList(const QVariantList& packagesArray,
                                    const QVariantList& installedPackages,
                                    const QStringList& validVariants);

    // Apply the current category filter to the cached full package list and
    // rebuild the model rows.
    void applyCategoryFilter();

    // Rebuild availableTypes from m_allPackagesCache ("All" + sorted distinct
    // types). Clamps selectedTypeIndex to 0 if the prior pick is gone.
    void recomputeAvailableTypes();

    // Push availableTypes[selectedTypeIndex] into the filter proxy
    // (index 0 / out-of-range / "All" → empty filter).
    void applyTypeFilter();

    void processDownloadResults(const QVariantList& results);
    void installNextPackage(const QVariantList& results, int index, int completed, int totalPackages);
    void finishInstallation(int completed);

    // Publish the model's per-selection action plan into the .rep PROPs
    // (`runnableActionCount`, `actionSummary`). Driven by
    // PackageListModel::hasSelectionChanged on every selection toggle;
    // no manual call sites. Replaces the old refreshHasSelection that
    // published the two has*Selection booleans.
    void refreshActionSummary();

    // Shared body for upgrade / downgrade / sidegrade — resolves the row,
    // pins the per-row target version, forwards to package_manager.requestUpgrade.
    void requestVersionChange(int index, UpgradeMode mode);

    // Cancellation events: filters out "user cancelled" (user initiated it,
    // no toast needed); other reasons surface via cancellationOccurred.
    void subscribePackageManagerCancellationEvents();

    // File-install / file-uninstall events → debounced refreshPackages().
    // Covers both PMU-initiated and Basecamp-Modules-initiated mutations
    // since the module is the common point both flow through.
    void subscribePackageManagerRefreshEvents();

    // upgradeUninstallDone fires after confirmUpgrade removes the old version;
    // PMU then drives the download+install of the new one.
    void subscribePackageManagerUpgradeEvents();

    // Handler for upgradeUninstallDone. Payload keys by moduleName; the
    // catalog `name` (used by the downloader) is resolved via PackageListModel.
    void onUpgradeUninstallDone(const QString& moduleName,
                                const QString& releaseTag,
                                int mode);

    // onDone invoked with (success, errorMsg) regardless of outcome.
    void installOnePackage(const QVariantMap& dl,
                           std::function<void(bool success, const QString& error)> onDone);

    // Connection-readiness predicates — wrap the m_logosAPI null-check + the
    // per-client isConnected() check that nine call sites in the .cpp need to
    // gate IPC against.
    bool clientReady(const char* moduleName) const;
    bool bothClientsReady() const;        // package_downloader AND package_manager
    bool packageManagerReady() const;     // package_manager only

    // (`versionCmp` now lives in `src/RowActionResolver.h` so both this
    // file's buildPackageRow AND PackageListModel::setRowVersion can
    // call it. The per-row Action — surfaced as `rowAction` and bound
    // by the QML ActionPill — has to flip when the user moves the
    // dropdown, which is why the comparator can't stay file-local here
    // anymore.)

    // Build the JSON array passed to
    // package_downloader.downloadResolvedDependencies. Uses QJsonDocument
    // (not plain concat) so a repo URL with special characters can't
    // desynchronise the payload — names are safe, but URLs are
    // user-provided. Empty repositoryUrl / version fields are omitted
    // entirely so the resolver falls back to its default behaviour for
    // unpinned entries.
    static QString buildDepsJson(const QList<PackageInstallSpec>& specs);

    // Proxy stack: raw rows → filter (search/state/sort) → paging (page slice;
    // exposed via the `packages` Q_PROPERTY).
    PackageListModel*    m_packageModel;
    PackagesFilterProxy* m_packagesFilterProxy;
    PackagesPagingProxy* m_packagesPagingProxy;
    LogosAPI*            m_logosAPI;
    int m_reloadGeneration = 0;

    // Unfiltered catalog. Slices for category / type filters without a
    // network round-trip. Reset on each reload.
    QVariantList m_allPackagesCache;
    QVariantList m_installedPackagesCache;
    QStringList  m_validVariantsCache;

    // Per-module upgrade meta captured at requestVersionChange time.
    //   repositoryUrl: scopes the post-uninstall download to the row's
    //     source repo, so a same-named package in another repo doesn't
    //     win the resolver's date-tiebreak.
    //   includeDeps: set false when the user picked "install just the
    //     package" from the dep-confirm dialog; tells onUpgradeUninstallDone
    //     to install only the top-level entry from the resolver and drop
    //     any transitive picks.
    // Drained on use in onUpgradeUninstallDone, keyed by moduleName
    // because that's the identifier package_manager echoes back in the
    // upgradeUninstallDone event.
    struct PendingUpgradeMeta {
        QString repositoryUrl;
        bool    includeDeps = true;
    };
    QHash<QString, PendingUpgradeMeta> m_pendingUpgradeByModule;

    // Pending dep-confirm requests, keyed by an opaque requestKey
    // (repositoryUrl + '\n' + name — see depConfirmKey()). Populated by
    // runDepPreviewForAction when the resolver returns transitive
    // changes; drained by confirmInstallWith{,out}Deps /
    // cancelInstallConfirm. The key is repo-scoped so two rows that
    // share a package name across repos get distinct pending entries
    // (keying by name alone let a second preview clobber the first),
    // and it's value-derived (not a row index) so it survives a model
    // refresh between click and confirm.
    struct PendingDepConfirm {
        enum Action { Install = 0, Upgrade = 1, Downgrade = 2, Sidegrade = 3 };
        QString name;            // catalog name (for display + dispatch)
        QString moduleName;      // runtime identity (for upgrade dispatch)
        QString repositoryUrl;
        QString version;         // dropdown-selected target version
        int     action = Install;
    };
    QHash<QString, PendingDepConfirm> m_pendingDepConfirms;

    // Coalesces N rapid file-install / file-uninstall events into one
    // refreshPackages() — does NOT touch releases or selected-release state.
    QTimer* m_refreshDebounceTimer = nullptr;

    // Defers applyCategoryFilter / applyTypeFilter so click events return
    // immediately (instant local highlight) and rapid clicks coalesce into
    // one apply pass. Pending flags pick which apply* runs when the timer fires.
    QTimer* m_filterApplyTimer = nullptr;
    bool m_categoryFilterPending = false;
    bool m_typeFilterPending = false;
};
