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
#include "PackageTypes.h"
#include "rep_package_manager_ui_source.h"

// PackageManagerBackend is the source-side implementation of the
// PackageManagerUi .rep interface. Inheriting from the generated
// PackageManagerUiSimpleSource gives us a single source of truth for the
// properties / signals / slots and a real static metaobject that can be
// remoted via QRemoteObjectNode::enableRemoting() when this module runs as
// a view module.
//
// The PackageListModel* is kept as a subclass-only Q_PROPERTY because
// QAbstractItemModel* can't flow through a .rep — it's remoted separately
// via QRemoteObjectNode::enableRemoting(model, "packages") on the host
// side, and exposed to QML through logos.model(...).
class PackageManagerBackend : public PackageManagerUiSimpleSource {
    Q_OBJECT
    Q_PROPERTY(PackageListModel* packages READ packages CONSTANT)

public:
    explicit PackageManagerBackend(LogosAPI* logosAPI = nullptr, QObject* parent = nullptr);
    ~PackageManagerBackend() = default;

    PackageListModel* packages() const;

public slots:
    // Overrides of the pure-virtual slots generated from the .rep.
    void reload() override;
    void install() override;
    void requestPackageDetails(int index) override;
    void togglePackage(int index, bool checked) override;

    // Uninstall / upgrade / downgrade / sidegrade flow — all thin wrappers
    // over package_manager.requestUninstall / requestUpgrade. PMU is
    // stateless for cascade purposes: the module owns the pending-action
    // protocol, emits beforeUninstall / beforeUpgrade events so Basecamp
    // can drive the cascade dialog, and reports the outcome via the
    // existing uninstallFinished / corePluginUninstalled / uiPluginUninstalled
    // events plus the new uninstallCancelled / upgradeCancelled events.
    //
    // All four take an int index (not a name). The backend reads moduleName
    // from its own PackageListModel via packageAt(index) — avoids QRO's role
    // lazy-fetch race where `model.moduleName` on the replica side can
    // return empty briefly after a model reset.
    void uninstall(int index) override;
    void upgradePackage(int index) override;
    void downgradePackage(int index) override;
    void sidegradePackage(int index) override;

    // Generated from the .rep as a pure-virtual slot (returns QString).
    // Delegates to the model's lookup; declared on the backend so the Repc
    // replica proxies it — the PackageListModel replica (QAbstractItemModel)
    // can't proxy Q_INVOKABLE methods because they're not part of the
    // QAbstractItemModel interface that gets remoted.
    QString displayNameForModule(QString moduleName) override;

private slots:
    void onSelectedReleaseIndexChanged();

private:
    // Matches package_manager's UpgradeMode values — passed to requestUpgrade
    // as an int so the remote call doesn't have to carry an enum over the
    // wire. Keep these integer values in sync with the module side; they're
    // informational for the module today, mostly used to pick the right
    // "Upgrading..." / "Downgrading..." label in the user-facing cancellation
    // messages if needed.
    enum class UpgradeMode { Upgrade = 0, Downgrade = 1, Sidegrade = 2 };

    void refreshPackages();
    void refreshReleases(std::function<void()> onDone);
    void setPackagesFromVariantList(const QVariantList& packagesArray,
                                    const QVariantList& installedPackages,
                                    const QStringList& validVariants);
    void processDownloadResults(const QString& releaseTag, const QVariantList& results);
    void installNextPackage(const QString& releaseTag, const QVariantList& results, int index, int completed, int totalPackages);
    void finishInstallation(int completed);
    void refreshHasSelectedPackages();

    // Common body for upgradePackage / downgradePackage / sidegradePackage —
    // resolves the row, pins the release tag, and forwards to
    // package_manager.requestUpgrade. The module gates the cascade dialog
    // and emits beforeUpgrade; Basecamp's PluginManager catches that event
    // and drives the dialog.
    void requestVersionChange(int index, UpgradeMode mode);

    // Subscribe to package_manager's cancellation events — fires on every
    // cancellation path (user cancelled, ack timeout, errors). PMU filters
    // the "user cancelled" reason to stay silent (user initiated it); other
    // reasons surface as toast messages.
    void subscribePackageManagerCancellationEvents();

    // Subscribe to package_manager's on-disk mutation events (file install /
    // uninstall). Triggers a debounced refreshPackages() so the catalog rows
    // flip status without the user having to click Reload, while avoiding a
    // full reload() that would reset releases/selection. Both PMU-initiated
    // AND Basecamp-Modules-tab-initiated operations reach us through this
    // one path — the module is the common point every install/uninstall
    // passes through, so one subscription covers both directions.
    void subscribePackageManagerRefreshEvents();

    // Subscribe to package_manager's upgradeUninstallDone event, which fires
    // after confirmUpgrade successfully uninstalls the old version. PMU drives
    // the download+install of the new version using its existing infrastructure
    // (package_downloader.downloadPackageAsync → installOnePackage). The row
    // flips to Installing immediately and then to Installed/Failed on completion.
    void subscribePackageManagerUpgradeEvents();

    // Handler for the upgradeUninstallDone event. Downloads the new version
    // from the specified release and installs it, updating the model row and
    // emitting progress signals throughout. The event payload keys the
    // package by moduleName (that's what the gated uninstall/upgrade flow
    // was initiated with); the implementation maps it back to the catalog
    // `name` via PackageListModel::displayNameForModule before calling the
    // downloader and formatting user-visible messages.
    void onUpgradeUninstallDone(const QString& moduleName,
                                const QString& releaseTag,
                                int mode);

    // Install a single package identified by the download-result map. onDone
    // is invoked with (success, errorMsg) regardless of outcome.
    void installOnePackage(const QString& releaseTag,
                           const QVariantMap& dl,
                           std::function<void(bool success, const QString& error)> onDone);

    // Returns the release tag string for the currently selected release index.
    // Empty string means "latest".
    QString currentReleaseTag() const;

    // Three-way version comparator (dotted-numeric semantics).
    // Returns -1 if a < b, 0 if a == b, +1 if a > b. Missing components treated as 0.
    static int versionCmp(const QString& a, const QString& b);

    PackageListModel* m_packageModel;
    LogosAPI* m_logosAPI;
    int m_reloadGeneration = 0;
    bool m_suppressReleaseChange = false;

    // Debounces bursts of file-install / file-uninstall events (e.g., an
    // N-package batch install fires N corePluginFileInstalled events rapid-
    // fire) into a single refreshPackages() call so we don't fire N
    // redundant package-list refreshes. Armed from the event subscribers;
    // each restart cancels the previous pending tick. This coalesces
    // package refresh work only; it does not imply a full reload() of
    // releases or selected-release state, and it keeps network traffic down.
    QTimer* m_refreshDebounceTimer = nullptr;
};
