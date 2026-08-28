#pragma once

#include <functional>
#include <QObject>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>
#include <QStringList>
#include "logos_api.h"
#include "logos_api_client.h"
#include "logos_ui_plugin_context.h"
#include "PackageListModel.h"
#include "PackagesFilterProxy.h"
#include "PackagesPagingProxy.h"
#include "PackageTypes.h"
#include "rep_package_manager_ui_source.h"

// Source-side implementation of the PackageManagerUi .rep interface.
// The `packages` Q_PROPERTY exposes a model proxy stack (raw → filter →
// paging) that ui-host remotes separately because QAbstractItemModel*
// can't flow through a .rep; QML reaches it via logos.model(...).
//
// LogosUiPluginContext supplies `modules()` — the Qt-typed wrappers for the
// two declared dependencies (package_manager, package_downloader) — plus the
// onContextReady() hook. The generated view-plugin glue
// (generated_code/package_manager_ui_ui_glue.cpp) owns the LogosAPI and wires
// it in; this class never receives one directly.
class PackageManagerBackend : public PackageManagerUiSimpleSource,
                              public LogosUiPluginContext {
    Q_OBJECT
    Q_PROPERTY(QAbstractItemModel* packages READ packages CONSTANT)

public:
    explicit PackageManagerBackend(QObject* parent = nullptr);
    ~PackageManagerBackend() = default;

    QAbstractItemModel* packages() const;

    // Fires once the generated glue has wired modules(); the typed
    // package_manager / package_downloader surfaces are live, so the initial
    // catalog load and the event subscriptions start from here. Doing it in
    // the constructor would run before the framework hands the dependencies
    // over.
    void onContextReady() override;

public slots:
    // Overrides of the pure-virtual slots generated from the .rep.
    // See package_manager_ui.rep for per-slot documentation.
    void refreshCatalog() override;
    // Install a .lgx the user picked off disk.
    void installLocalPackage(QUrl fileUrl) override;
    // Bulk: run each selected row's resolved primary action (the new
    // "Run Actions" header button). Subsumes the old installSelected
    // path AND adds upgrade / downgrade / reinstall to the bulk surface.
    void runSelectedActions() override;
    void installSelected() override;   // kept for back-compat, unwired from UI
    void uninstallSelected() override; // kept for back-compat, unwired from UI
    void togglePackage(int index, bool checked) override;
    void reloadPackage(int index) override;
    void requestPackageDetails(int index) override;

    // Run an action the host approved. QML calls these from the intent
    // callback; the backend itself knows nothing about intents.
    void performInstall(QString name, QString version, QString repositoryUrl) override;
    void performUpgrade(QString moduleName, QString version, int mode,
                        QString repositoryUrl) override;
    void performUninstall(QStringList names) override;
    void reportActionFailed(QStringList names, QString error) override;

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


private:

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





    // Serialise m_installedPackagesCache to the
    // [{name, version, rootHash}] shape package_downloader.resolveDependencies
    // expects in its `installedPackagesJson` parameter.
    QString buildInstalledPackagesJson() const;

    void setPackagesFromVariantList(const QVariantList& packagesArray,
                                    const QVariantList& installedPackages,
                                    const QStringList& validVariants);

    // Push categories[selectedCategoryIndex] into the filter proxy
    // (index 0 / out-of-range / "All" → empty filter).
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


    // File-install / file-uninstall events → debounced refreshPackages().
    // Covers both PMU-initiated and Basecamp-Modules-initiated mutations
    // since the module is the common point both flow through.
    void subscribePackageManagerRefreshEvents();

    // Auto-refresh the catalog whenever the package_downloader emits catalogChanged.
    void subscribePackageDownloaderEvents();

    // Second half of an approved upgrade: old version gone, download+install
    // the new one. Catalog `name` resolved from moduleName via PackageListModel.
    void onUpgradeUninstallDone(const QString& moduleName,
                                const QString& releaseTag,
                                int mode);

    // onDone invoked with (success, errorMsg) regardless of outcome.
    void installOnePackage(const QVariantMap& dl,
                           std::function<void(bool success, const QString& error)> onDone);

    // Connection-readiness predicates — wrap the context-ready check + the
    // per-client isConnected() check that nine call sites in the .cpp need to
    // gate IPC against.
    bool clientReady(const char* moduleName) const;
    bool bothClientsReady() const;        // package_downloader AND package_manager
    bool packageManagerReady() const;     // package_manager only

    bool resolveRowIdentifier(int proxyRow,
                              QString* outName,
                              QString* outRepoUrl) const;
    int         findPackageRowAtProxyRow(int proxyRow) const;
    QVariantMap findPackageAtProxyRow(int proxyRow) const;

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
    int m_reloadGeneration = 0;

    // Unfiltered catalog. Category / type filters run on the proxy without a
    // network round-trip. Reset on each reload.
    QVariantList m_allPackagesCache;
    QVariantList m_installedPackagesCache;
    QStringList  m_validVariantsCache;

    // Per-module upgrade meta captured at requestVersionChange time.
    //   repositoryUrl: scopes the post-uninstall download to the row's
    //     source repo, so a same-named package in another repo doesn't
    //     win the resolver's date-tiebreak.
    // Drained on use in onUpgradeUninstallDone, keyed by moduleName.
    struct PendingUpgradeMeta {
        QString repositoryUrl;
    };
    QHash<QString, PendingUpgradeMeta> m_pendingUpgradeByModule;

    // Local .lgx files awaiting host approval, keyed by the package name
    // inspectPackage reported (the same name carried in the confirm_install /
    // confirm_upgrade payload, so it's what comes back on approval).
    QHash<QString, QString> m_pendingLocalInstalls;

    // Coalesces N rapid file-install / file-uninstall events into one
    // refreshPackages() — does NOT touch releases or selected-release state.
    QTimer* m_refreshDebounceTimer = nullptr;

    void finishInitialSetup(int attempt = 0);
    bool m_initialSetupComplete = false;

    // Defers applyCategoryFilter / applyTypeFilter so click events return
    // immediately (instant local highlight) and rapid clicks coalesce into
    // one apply pass. Pending flags pick which apply* runs when the timer fires.
    QTimer* m_filterApplyTimer = nullptr;
    bool m_categoryFilterPending = false;
    bool m_typeFilterPending = false;
};
