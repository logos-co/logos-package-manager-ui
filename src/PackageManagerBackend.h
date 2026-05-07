#pragma once

#include <functional>
#include <QObject>
#include <QTimer>
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
    void installSelected() override;
    void uninstallSelected() override;
    void togglePackage(int index, bool checked) override;
    void installPackage(int index) override;
    void reloadPackage(int index) override;
    void uninstall(int index) override;
    void upgradePackage(int index) override;
    void downgradePackage(int index) override;
    void sidegradePackage(int index) override;
    void requestPackageDetails(int index) override;

private slots:
    void onSelectedReleaseIndexChanged();

private:
    // Mirrors package_manager's UpgradeMode; passed to requestUpgrade as int
    // (avoids carrying an enum across the wire). Keep in sync with module side.
    enum class UpgradeMode { Upgrade = 0, Downgrade = 1, Sidegrade = 2 };

    void refreshPackages();
    void refreshReleases(std::function<void()> onDone);

    // Bulk install pipeline — sequential download+install of N packages,
    // gated by the global isInstalling flag (so the bulk Install button can
    // disable itself during a batch). Used only by installSelected().
    void installNamed(const QStringList& packageNames);

    // Per-row install — runs independently of the global isInstalling
    // flag so multiple per-row clicks can run in parallel
    void installSinglePackageAsync(const QString& packageName);
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

    void processDownloadResults(const QString& releaseTag, const QVariantList& results);
    void installNextPackage(const QString& releaseTag, const QVariantList& results, int index, int completed, int totalPackages);
    void finishInstallation(int completed);

    // Mirror model has*Selection flags into the .rep PROPs.
    // Driven by PackageListModel::hasSelectionChanged; no manual call sites.
    void refreshHasSelection();

    // Shared body for upgrade / downgrade / sidegrade — resolves the row,
    // pins the release tag, forwards to package_manager.requestUpgrade.
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
    void installOnePackage(const QString& releaseTag,
                           const QVariantMap& dl,
                           std::function<void(bool success, const QString& error)> onDone);

    // Empty string means "latest".
    QString currentReleaseTag() const;

    // Connection-readiness predicates — wrap the m_logosAPI null-check + the
    // per-client isConnected() check that nine call sites in the .cpp need to
    // gate IPC against.
    bool clientReady(const char* moduleName) const;
    bool bothClientsReady() const;        // package_downloader AND package_manager
    bool packageManagerReady() const;     // package_manager only

    // Proxy stack: raw rows → filter (search/state/sort) → paging (page slice;
    // exposed via the `packages` Q_PROPERTY).
    PackageListModel*    m_packageModel;
    PackagesFilterProxy* m_packagesFilterProxy;
    PackagesPagingProxy* m_packagesPagingProxy;
    LogosAPI*            m_logosAPI;
    int m_reloadGeneration = 0;
    bool m_suppressReleaseChange = false;

    // Unfiltered catalog for the current release. Slices for category /
    // type filters without a network round-trip. Reset on release change.
    QVariantList m_allPackagesCache;
    QVariantList m_installedPackagesCache;
    QStringList  m_validVariantsCache;

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
