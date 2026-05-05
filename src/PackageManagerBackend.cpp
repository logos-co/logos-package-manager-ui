#include "PackageManagerBackend.h"
#include <algorithm>
#include <QDebug>
#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QPointer>
#include <QSet>
#include <QTimer>
#include <QVariant>
#include "logos_sdk.h"

constexpr int DOWNLOAD_TIMEOUT_MS = 300000; // 5 minutes

PackageManagerBackend::PackageManagerBackend(LogosAPI* logosAPI, QObject* parent)
    : PackageManagerUiSimpleSource(parent)
    , m_packageModel(new PackageListModel(this))
    , m_packagesFilterProxy(new PackagesFilterProxy(this))
    , m_packagesPagingProxy(new PackagesPagingProxy(this))
    , m_logosAPI(logosAPI)
{
    // Initialise base-class properties to sane defaults.
    setSelectedCategoryIndex(0);
    setReleases(QStringList{"latest"});
    setSelectedReleaseIndex(0);
    setHasInstallableSelection(false);
    setHasUninstallableSelection(false);
    setIsInstalling(false);
    setIsLoading(false);

    // Filter / sort / pagination defaults — initial values must match
    // the proxies' defaults so the first-ever *Changed signal doesn't
    // fight them. PackagesPagingProxy defaults to pageSize=20, page=1.
    setSearchText(QString());
    setInstallStateFilter(0);
    setPageSize(20);
    setCurrentPage(1);
    setSortRole(QString());
    setSortOrder(Qt::AscendingOrder);
    setTotalCount(0);

    // Stack: raw model → filter+sort proxy → paging proxy. ui-host's
    // dynamic remoting scans backend Q_PROPERTYs of QAbstractItemModel*
    // and remotes each — exposing the *paging* proxy as `packages`
    // means QML's logos.model("package_manager_ui","packages") gets
    // exactly the current page's rows. Filter / sort happens upstream
    // in the filter proxy; the paging proxy slices the post-filter set.
    m_packagesFilterProxy->setSourceModel(m_packageModel);
    m_packagesPagingProxy->setSourceModel(m_packagesFilterProxy);

    // One-shot wiring: every selection / install-status mutation on the model
    // emits hasSelectionChanged, which pushes the has*Selection .rep PROPs
    // through this slot. No manual refresh sprinkles needed at the mutation sites.
    connect(m_packageModel, &PackageListModel::hasSelectionChanged,
            this, &PackageManagerBackend::refreshHasSelection);

    // Forward .rep PROP changes to the proxy that owns each concern.
    // Filter / sort lives in m_packagesFilterProxy; pageSize / page
    // in m_packagesPagingProxy. The paging proxy auto-resets to page 1
    // when its source's row set changes (i.e. filter / sort flip), so
    // the backend doesn't have to coordinate that interaction.
    connect(this, &PackageManagerUiSimpleSource::searchTextChanged,
            this, [this]() { m_packagesFilterProxy->setSearchText(searchText()); });
    connect(this, &PackageManagerUiSimpleSource::installStateFilterChanged,
            this, [this]() { m_packagesFilterProxy->setInstallStateFilter(installStateFilter()); });
    connect(this, &PackageManagerUiSimpleSource::sortRoleChanged,
            this, [this]() { m_packagesFilterProxy->setSortRoleByName(sortRole()); });
    connect(this, &PackageManagerUiSimpleSource::sortOrderChanged,
            this, [this]() { m_packagesFilterProxy->setSortOrderInt(sortOrder()); });
    connect(this, &PackageManagerUiSimpleSource::pageSizeChanged,
            this, [this]() { m_packagesPagingProxy->setPageSize(pageSize()); });
    connect(this, &PackageManagerUiSimpleSource::currentPageChanged,
            this, [this]() { m_packagesPagingProxy->setCurrentPage(currentPage()); });

    // Mirror the paging proxy's notifications back into our PROPs so
    // QML stays in sync. totalCount = filtered row count; currentPage
    // mirrors the auto-reset the proxy applies on filter / sort flip.
    connect(m_packagesPagingProxy, &PackagesPagingProxy::totalCountChanged,
            this, [this]() { setTotalCount(m_packagesPagingProxy->totalCount()); });
    connect(m_packagesPagingProxy, &PackagesPagingProxy::currentPageChanged,
            this, [this](int page) { setCurrentPage(page); });

    // Category change is a pure client-side filter over the cached full
    // catalog — no network round-trip. refreshPackages() already fetches
    // every package (category="All"); applyCategoryFilter() slices that
    // cache down to the user's pick and rebuilds the model rows. Routed
    // through the filter-apply debounce so a click feels instant in QML
    // (the click handler returns immediately) and rapid clicks coalesce
    // into a single applyCategoryFilter pass.
    connect(this, &PackageManagerUiSimpleSource::selectedCategoryIndexChanged,
            this, [this]() {
                m_categoryFilterPending = true;
                m_filterApplyTimer->start();
            });

    connect(this, &PackageManagerUiSimpleSource::selectedTypeIndexChanged,
            this, [this]() {
                m_typeFilterPending = true;
                m_filterApplyTimer->start();
            });

    // Changing the release triggers a partial reload (no setRelease needed —
    // the tag is passed directly to each downloader call).
    connect(this, &PackageManagerUiSimpleSource::selectedReleaseIndexChanged,
            this, &PackageManagerBackend::onSelectedReleaseIndexChanged);

    if (!m_logosAPI) {
        m_logosAPI = new LogosAPI("core", this);
    }

    // Wire up the uninstall/upgrade cancellation event handlers. The module
    // fires these on every cancellation path (user cancel + ack timeout +
    // error paths); we filter the "user cancelled" reason to stay silent and
    // surface the others as toast messages via the existing progress-update
    // signal the QML layer already renders.
    subscribePackageManagerCancellationEvents();

    // Auto-refresh the catalog on package_manager file mutations. Covers both
    // PMU- and Basecamp-initiated flows since the module is the common point.
    // Targets refreshPackages() (not refreshCatalog) because file mutations
    // don't change releases — going wider would jarringly reset the release combo.
    m_refreshDebounceTimer = new QTimer(this);
    m_refreshDebounceTimer->setSingleShot(true);
    m_refreshDebounceTimer->setInterval(150);
    connect(m_refreshDebounceTimer, &QTimer::timeout,
            this, &PackageManagerBackend::refreshPackages);
    subscribePackageManagerRefreshEvents();

    // Filter-apply debounce — see header comment. 30ms is short enough to
    // feel instant for a single click but long enough to coalesce the
    // bursts that arrive when the user clicks several categories / types
    // in rapid succession.
    m_filterApplyTimer = new QTimer(this);
    m_filterApplyTimer->setSingleShot(true);
    m_filterApplyTimer->setInterval(30);
    connect(m_filterApplyTimer, &QTimer::timeout, this, [this]() {
        // Apply only what's actually pending — a type-only click should
        // not trigger a full applyCategoryFilter (which calls setPackages
        // and resets the model). Reset the flags first so a re-entrant
        // signal during apply* doesn't cause a double-fire.
        const bool catPending  = m_categoryFilterPending;
        const bool typePending = m_typeFilterPending;
        m_categoryFilterPending = false;
        m_typeFilterPending     = false;
        if (catPending)  applyCategoryFilter();
        if (typePending) applyTypeFilter();
    });

    // Listen for upgrade-uninstall-done events so PMU can drive the
    // download+install step automatically. Without this, confirmUpgrade only
    // removes the old version and the user would have to manually re-install.
    subscribePackageManagerUpgradeEvents();

    // Defer the first catalog refresh until the event loop starts so
    // ui-host can signal ready and the Package Manager tab appears
    // immediately.
    QTimer::singleShot(0, this, &PackageManagerBackend::refreshCatalog);
}

// ─────────────────────────── file-local helpers ───────────────────────────

// Three-way dotted-numeric version compare. Missing components = 0.
// Returns -1 if a<b, 0 if equal, +1 if a>b.
static int versionCmp(const QString& a, const QString& b)
{
    const QStringList aParts = a.split('.');
    const QStringList bParts = b.split('.');
    const int n = std::max(aParts.size(), bParts.size());
    for (int i = 0; i < n; ++i) {
        const int av = (i < aParts.size()) ? aParts[i].toInt() : 0;
        const int bv = (i < bParts.size()) ? bParts[i].toInt() : 0;
        if (av < bv) return -1;
        if (av > bv) return 1;
    }
    return 0;
}

// Decode a package_manager event payload (a single JSON-encoded string in
// `data.first()`) into a QJsonObject. Returns an empty object on any failure
// (no payload, parse error, or non-object root) — callers test isEmpty().
static QJsonObject parseEventPayload(const QVariantList& data)
{
    if (data.isEmpty()) return {};
    const QByteArray payload = data.first().toString().toUtf8();
    QJsonParseError parseErr{};
    const QJsonDocument doc = QJsonDocument::fromJson(payload, &parseErr);
    if (parseErr.error != QJsonParseError::NoError || !doc.isObject()) return {};
    return doc.object();
}

// Build one model row from one raw catalog row + the installed-by-name index +
// the valid-variants list for this platform. Pure transform; no instance state.
static QVariantMap buildPackageRow(const QVariantMap& obj,
                                   const QHash<QString, QVariantMap>& installedByName,
                                   const QStringList& validVariants)
{
    QVariantMap pkg;
    const QString name = obj.value("name").toString();
    const QString moduleName = obj.value("moduleName").toString();
    pkg["name"] = name;
    pkg["moduleName"] = moduleName;
    pkg["description"] = obj.value("description").toString();
    pkg["type"] = obj.value("type").toString();
    pkg["category"] = obj.value("category").toString();

    // Release version + hash from manifest, falling back to top-level (older
    // list.json schemas).
    const QVariantMap manifest = obj.value("manifest").toMap();
    QString releaseVersion = manifest.value("version").toString();
    if (releaseVersion.isEmpty()) releaseVersion = obj.value("version").toString();
    const QString releaseHash = manifest.value("hashes").toMap().value("root").toString();
    pkg["version"] = releaseVersion;
    pkg["hash"] = releaseHash;

    // Cross-reference against the on-disk install state.
    QString installedVersion;
    QString installedHash;
    QString installType;
    const bool isInstalled = installedByName.contains(moduleName);
    if (isInstalled) {
        const QVariantMap& inst = installedByName[moduleName];
        installedVersion = inst.value("version").toString();
        installedHash = inst.value("hashes").toMap().value("root").toString();
        // "embedded" or "user" — QML gates Uninstall on installType === "user".
        installType = inst.value("installType").toString();
    }
    pkg["installedVersion"] = installedVersion;
    pkg["installedHash"] = installedHash;
    pkg["installType"] = installType;

    // Resolve install status. Embedded vs user doesn't change the status itself —
    // the QML side gates the Uninstall button on installType separately.
    int status = static_cast<int>(PackageTypes::NotInstalled);
    if (isInstalled) {
        if (releaseVersion.isEmpty() || installedVersion.isEmpty()) {
            // No version info to compare — assume same.
            status = static_cast<int>(PackageTypes::Installed);
        } else {
            const int cmp = versionCmp(installedVersion, releaseVersion);
            if (cmp < 0)      status = static_cast<int>(PackageTypes::UpgradeAvailable);
            else if (cmp > 0) status = static_cast<int>(PackageTypes::DowngradeAvailable);
            else if (!releaseHash.isEmpty() && !installedHash.isEmpty()
                     && releaseHash != installedHash)
                              status = static_cast<int>(PackageTypes::DifferentHash);
            else              status = static_cast<int>(PackageTypes::Installed);
        }
    }
    pkg["installStatus"] = status;
    pkg["errorMessage"] = QString();

    // Variant availability — true iff any of the package's offered variants
    // is in this platform's valid-variants list.
    bool variantAvailable = false;
    const QVariantList packageVariants = obj.value("variants").toList();
    for (const QVariant& pv : packageVariants) {
        if (validVariants.contains(pv.toString())) { variantAvailable = true; break; }
    }
    pkg["isVariantAvailable"] = variantAvailable;

    QStringList deps;
    const QVariantList depsArray = obj.value("dependencies").toList();
    for (const QVariant& dep : depsArray) deps.append(dep.toString());
    pkg["dependencies"] = deps;

    return pkg;
}

// ──────────────────── connection-readiness predicates ────────────────────

bool PackageManagerBackend::clientReady(const char* moduleName) const
{
    if (!m_logosAPI) return false;
    LogosAPIClient* c = m_logosAPI->getClient(moduleName);
    return c && c->isConnected();
}

bool PackageManagerBackend::bothClientsReady() const
{
    return clientReady("package_downloader") && clientReady("package_manager");
}

bool PackageManagerBackend::packageManagerReady() const
{
    return clientReady("package_manager");
}

QString PackageManagerBackend::currentReleaseTag() const
{
    const QStringList& tags = releases();
    const int idx = selectedReleaseIndex();
    if (idx <= 0 || idx >= tags.size()) {
        // Index 0 is "latest", and out-of-range also defaults to "latest".
        // Pass empty string so the lib resolves to "latest".
        return QString();
    }
    return tags.at(idx);
}

QAbstractItemModel* PackageManagerBackend::packages() const
{
    return m_packagesPagingProxy;
}

void PackageManagerBackend::refreshHasSelection()
{
    if (!m_packageModel) return;
    setHasInstallableSelection(m_packageModel->getInstallableSelectedCount() > 0);
    setHasUninstallableSelection(m_packageModel->getUninstallableSelectedCount() > 0);
}

void PackageManagerBackend::refreshCatalog()
{
    if (!bothClientsReady()) {
        qDebug() << "package_downloader or package_manager not connected, cannot refresh catalog";
        return;
    }

    // Full catalog refresh — drop Failed-row state from the previous fetch.
    // Failed referred to an attempt against the previous release context, so
    // refetching from scratch makes those errors stale. (The debounced refresh
    // path leaves Failed alone — file-install events don't change releases.)
    if (m_packageModel) m_packageModel->clearFailedRows();

    setIsLoading(true);

    QPointer<PackageManagerBackend> self(this);
    refreshReleases([self]() {
        if (!self) return;
        self->refreshPackages();
    });
}

void PackageManagerBackend::refreshReleases(std::function<void()> onDone)
{
    if (!clientReady("package_downloader")) {
        if (onDone) onDone();
        return;
    }

    LogosModules logos(m_logosAPI);
    QPointer<PackageManagerBackend> self(this);
    logos.package_downloader.getReleasesAsync([self, onDone](QVariantList releaseList) {
        if (!self) return;
        QStringList tags;
        tags << QStringLiteral("latest");
        for (const QVariant& v : releaseList) {
            QVariantMap rel = v.toMap();
            QString tag = rel.value("tag_name").toString();
            if (!tag.isEmpty()) {
                tags << tag;
            }
        }
        self->setReleases(tags);

        // Reset to "latest" on every full reload. Suppress the change handler so
        // the slot doesn't kick off another partial reload while we're already in
        // the middle of one (the caller's onDone callback will handle that).
        self->m_suppressReleaseChange = true;
        self->setSelectedReleaseIndex(0);
        self->m_suppressReleaseChange = false;

        if (onDone) onDone();
    });
}

void PackageManagerBackend::refreshPackages()
{
    if (!bothClientsReady()) {
        qDebug() << "package_downloader or package_manager not connected, cannot refresh packages";
        setIsLoading(false);
        return;
    }

    ++m_reloadGeneration;
    const int currentGeneration = m_reloadGeneration;
    setIsLoading(true);

    const QString tag = currentReleaseTag();

    // Fetch with category="All" once; subsequent category clicks slice
    // m_allPackagesCache locally with no network round-trip.
    LogosModules logos(m_logosAPI);
    QPointer<PackageManagerBackend> self(this);
    logos.package_downloader.getCategoriesAsync(tag, [self, currentGeneration, tag](QVariant result) {
        if (!self || self->m_reloadGeneration != currentGeneration) return;
        QStringList categoryList = result.toStringList();
        self->setCategories(categoryList);

        LogosModules logos2(self->m_logosAPI);
        logos2.package_downloader.getPackagesAsync(tag, QStringLiteral("All"), [self, currentGeneration](QVariantList packagesArray) {
            if (!self || self->m_reloadGeneration != currentGeneration) return;

            LogosModules logos3(self->m_logosAPI);
            logos3.package_manager.getInstalledPackagesAsync([self, currentGeneration, packagesArray](QVariantList installedPackages) {
                if (!self || self->m_reloadGeneration != currentGeneration) return;

                LogosModules logos4(self->m_logosAPI);
                logos4.package_manager.getValidVariantsAsync([self, currentGeneration, packagesArray, installedPackages](QVariant result) {
                    if (!self || self->m_reloadGeneration != currentGeneration) return;
                    QStringList validVariants = result.toStringList();
                    self->m_allPackagesCache = packagesArray;
                    self->m_installedPackagesCache = installedPackages;
                    self->m_validVariantsCache = validVariants;
                    // Type list is derived from the unfiltered cache, not from
                    // the category-filtered slice, so switching categories
                    // doesn't make Type tabs flicker in/out.
                    self->recomputeAvailableTypes();
                    self->applyCategoryFilter();
                    self->setIsLoading(false);
                });
            });
        });
    });
}

void PackageManagerBackend::applyCategoryFilter()
{
    // Pure client-side filter over m_allPackagesCache. No network work.
    // When the selected index is out of bounds (e.g. release change
    // surfaced a different categories list), .value(idx, "All") falls
    // back to the "no filter" pseudo-category.
    //
    // Comparisons are case-insensitive: category labels originate from
    // package manifests authored by many hands, so "Networking" /
    // "networking" / "NETWORKING" should all bucket together rather
    // than silently diverge based on capitalisation. The "All" sentinel
    // is matched case-insensitively for the same reason.
    const QString selected = categories().value(selectedCategoryIndex(), QStringLiteral("All"));

    QVariantList filtered;
    if (selected.isEmpty() || selected.compare(QStringLiteral("All"), Qt::CaseInsensitive) == 0) {
        filtered = m_allPackagesCache;
    } else {
        filtered.reserve(m_allPackagesCache.size());
        for (const QVariant& v : m_allPackagesCache) {
            const QVariantMap pkg = v.toMap();
            if (pkg.value("category").toString().compare(selected, Qt::CaseInsensitive) == 0) {
                filtered.append(v);
            }
        }
    }

    setPackagesFromVariantList(filtered, m_installedPackagesCache, m_validVariantsCache);
}

void PackageManagerBackend::recomputeAvailableTypes()
{
    // "All" is always at index 0; real types follow alphabetically. Using a
    // QSet to dedupe keeps this O(N) over the cache; the final sort is over
    // the small set of distinct types (typically 2-4 entries).
    QSet<QString> distinct;
    for (const QVariant& v : m_allPackagesCache) {
        const QString t = v.toMap().value("type").toString();
        if (!t.isEmpty()) distinct.insert(t);
    }
    QStringList sorted(distinct.begin(), distinct.end());
    std::sort(sorted.begin(), sorted.end());

    QStringList types;
    types.reserve(sorted.size() + 1);
    types.append(QStringLiteral("All"));
    types.append(sorted);

    // Preserve the user's pick across refreshes when possible: remember
    // the selected type *string* before we overwrite the list, then
    // re-resolve it against the new list. If it's gone (e.g. the only
    // package of that type was uninstalled and a refresh is in flight
    // for a release that doesn't have it either), fall back to "All".
    const QStringList prev = availableTypes();
    const int prevIdx = selectedTypeIndex();
    const QString prevSelectedType = (prevIdx >= 0 && prevIdx < prev.size())
                                         ? prev.at(prevIdx) : QString();

    setAvailableTypes(types);

    int newIdx = 0;
    if (!prevSelectedType.isEmpty()) {
        const int found = types.indexOf(prevSelectedType);
        if (found >= 0) newIdx = found;
    }
    if (newIdx != selectedTypeIndex()) {
        setSelectedTypeIndex(newIdx);          // emits selectedTypeIndexChanged
    } else {
        // Index is the same but the underlying list might have changed
        // (e.g. "ui" stayed at index 1 but a `ui_qml` entry slid in at
        // index 2). Push the resolved string into the proxy regardless
        // so the filter stays consistent.
        applyTypeFilter();
    }
}

void PackageManagerBackend::applyTypeFilter()
{
    if (!m_packagesFilterProxy) return;
    const QStringList types = availableTypes();
    const int idx = selectedTypeIndex();
    QString typeFilter;
    if (idx > 0 && idx < types.size()) {
        const QString t = types.at(idx);
        // Index 0 is the "All" sentinel; treat any literal "All" cell
        // anywhere in the list as no-filter for safety (the catalog is
        // unlikely to ship a type literally named "All", but if it does
        // the user-facing semantics are still "show everything").
        if (t.compare(QStringLiteral("All"), Qt::CaseInsensitive) != 0)
            typeFilter = t;
    }
    m_packagesFilterProxy->setTypeFilter(typeFilter);
}

void PackageManagerBackend::onSelectedReleaseIndexChanged()
{
    if (m_suppressReleaseChange) return;
    if (!clientReady("package_downloader")) return;

    if (m_packageModel) {
        // Drop Failed-row state from the previously selected release — the
        // banner refers to an install attempt against THAT release's package,
        // and the user has just switched contexts. See clearFailedRows() for
        // the broader rationale.
        m_packageModel->clearFailedRows();

        // Drop selection too. A ticked checkbox represents intent to act on
        // a specific catalog row; switching releases swaps the catalog out
        // from under the user, so the previous intent is no longer well-
        // defined (the row may not exist in the new release, may carry a
        // different version, may have a different variant availability).
        // Cleared *before* refreshPackages() runs so the setPackages
        // selection-restore pass (which keys on getSelectedPackageNames)
        // doesn't carry stale ticks into the new catalog.
        m_packageModel->clearAllSelections();

        // The model's isSelected role just flipped false on every row, but
        // LogosTable maintains its own selectedIndices array as QML-side
        // state — without this signal the checkboxes would stay visually
        // ticked even though the model says no row is selected. QML hooks
        // selectionsCleared and resets the table's selection array.
        emit selectionsCleared();
    }

    // No setRelease call needed — the release tag is passed directly to each
    // downloader method call. Just kick off a catalog reload.
    refreshPackages();
}

void PackageManagerBackend::setPackagesFromVariantList(const QVariantList& packagesArray,
                                                        const QVariantList& installedPackages,
                                                        const QStringList& validVariants)
{
    // Index installed packages by moduleName for O(1) lookup in buildPackageRow.
    QHash<QString, QVariantMap> installedByName;
    for (const QVariant& val : installedPackages) {
        const QVariantMap obj = val.toMap();
        const QString installedName = obj.value("name").toString();
        if (!installedName.isEmpty()) installedByName.insert(installedName, obj);
    }

    QList<QVariantMap> packages;
    packages.reserve(packagesArray.size());
    for (const QVariant& value : packagesArray)
        packages.append(buildPackageRow(value.toMap(), installedByName, validVariants));

    // setPackages emits hasSelectionChanged; the connected slot refreshes
    // hasInstallableSelection / hasUninstallableSelection.
    m_packageModel->setPackages(packages);
}

void PackageManagerBackend::processDownloadResults(const QString& releaseTag, const QVariantList& results)
{
    if (!packageManagerReady()) {
        qWarning() << "package_manager not connected, cannot install downloaded packages";
        finishInstallation(0);
        return;
    }

    int totalPackages = m_packageModel ? m_packageModel->getSelectedCount() : results.size();
    installNextPackage(releaseTag, results, 0, 0, totalPackages);
}

void PackageManagerBackend::installOnePackage(const QString& releaseTag,
                                               const QVariantMap& dl,
                                               std::function<void(bool success, const QString& error)> onDone)
{
    Q_UNUSED(releaseTag);

    QString packageName = dl.value("name").toString();
    QString filePath = dl.value("path").toString();
    QString downloadError = dl.value("error").toString();

    if (filePath.isEmpty()) {
        qWarning() << "Download failed for" << packageName << ":" << downloadError;
        if (onDone) onDone(false, downloadError.isEmpty()
                                       ? QStringLiteral("Download failed")
                                       : downloadError);
        return;
    }

    qDebug() << "Download complete for" << packageName << "at" << filePath << "— installing...";
    LogosModules logos(m_logosAPI);
    QPointer<PackageManagerBackend> self(this);
    logos.package_manager.installPluginAsync(filePath, false,
        [self, packageName, onDone](QVariantMap installResult) {
            if (!self) return;
            bool success = !installResult.value("path").toString().isEmpty()
                        && !installResult.contains("error");
            QString err = installResult.value("error").toString();
            if (onDone) onDone(success, success
                                           ? QString()
                                           : (err.isEmpty()
                                                  ? QStringLiteral("Installation failed")
                                                  : err));
        });
}

void PackageManagerBackend::installNextPackage(const QString& releaseTag, const QVariantList& results, int index, int completed, int totalPackages)
{
    while (index < results.size()) {
        QVariantMap dl = results[index].toMap();
        QString packageName = dl.value("name").toString();

        // Bubble the per-package outcome back up through the model + progress
        // signal. installOnePackage handles both the download-failed fast path
        // (empty filePath) and the actual installPlugin IPC call.
        QPointer<PackageManagerBackend> self(this);
        installOnePackage(releaseTag, dl,
            [self, releaseTag, results, packageName, index, completed, totalPackages](bool success, const QString& err) {
                if (!self) return;

                int newCompleted = completed + 1;
                if (success) {
                    self->m_packageModel->updatePackageInstallation(
                        packageName, static_cast<int>(PackageTypes::Installed));
                    // Per-package deselect on success — the action is done,
                    // so the row should drop out of the selection. Failed
                    // rows stay selected so the user can retry from the
                    // bulk Install button (the Failed status keeps them in
                    // hasInstallableSelection).
                    self->m_packageModel->clearSelectionsByPackageNames({packageName});
                } else {
                    self->m_packageModel->updatePackageInstallation(
                        packageName, static_cast<int>(PackageTypes::Failed), err);
                }
                emit self->installationProgressUpdated(
                    success ? static_cast<int>(PackageTypes::InProgress)
                            : static_cast<int>(PackageTypes::ProgressFailed),
                    packageName, newCompleted, totalPackages, success,
                    success ? "" : err);

                self->installNextPackage(releaseTag, results, index + 1, newCompleted, totalPackages);
            });
        return;
    }

    finishInstallation(completed);
}

void PackageManagerBackend::finishInstallation(int completed)
{
    setIsInstalling(false);
    emit installationProgressUpdated(
        static_cast<int>(PackageTypes::Completed), "", completed, completed, true, "");
    refreshPackages();
}

void PackageManagerBackend::installSelected()
{
    const QStringList names = m_packageModel->getInstallableSelectedPackageNames();
    if (names.isEmpty()) {
        emit errorOccurred(static_cast<int>(PackageTypes::NoPackagesSelected));
        return;
    }
    installNamed(names);
}

void PackageManagerBackend::installPackage(int index)
{
    const QVariantMap pkg = m_packageModel->packageAt(index);
    if (pkg.isEmpty()) {
        qWarning() << "PackageManagerBackend::installPackage invalid index:" << index;
        return;
    }
    const QString name = pkg.value("name").toString();
    if (name.isEmpty()) {
        qWarning() << "PackageManagerBackend::installPackage missing name for index:" << index;
        return;
    }
    installSinglePackageAsync(name);
}

void PackageManagerBackend::installSinglePackageAsync(const QString& packageName)
{
    if (!bothClientsReady()) {
        emit errorOccurred(static_cast<int>(PackageTypes::PackageManagerNotConnected));
        return;
    }
    if (packageName.isEmpty()) {
        emit errorOccurred(static_cast<int>(PackageTypes::NoPackagesSelected));
        return;
    }

    // Mark the row Installing immediately so the per-row button gates itself
    // against double-click while the download is in flight.
    m_packageModel->updatePackageInstallation(
        packageName, static_cast<int>(PackageTypes::Installing));

    emit installationProgressUpdated(
        static_cast<int>(PackageTypes::Started), packageName, 0, 1, true, "");

    const QString tag = currentReleaseTag();
    LogosModules logos(m_logosAPI);
    QPointer<PackageManagerBackend> self(this);
    logos.package_downloader.downloadPackageAsync(tag, packageName,
        [self, tag, packageName](QVariantMap dlResult) {
            if (!self) return;
            self->installOnePackage(tag, dlResult,
                [self, packageName](bool success, const QString& err) {
                    if (!self) return;
                    if (success) {
                        self->m_packageModel->updatePackageInstallation(
                            packageName, static_cast<int>(PackageTypes::Installed));
                        // Drop selection on success
                        self->m_packageModel->clearSelectionsByPackageNames({packageName});
                    } else {
                        self->m_packageModel->updatePackageInstallation(
                            packageName, static_cast<int>(PackageTypes::Failed), err);
                    }
                    emit self->installationProgressUpdated(
                        success ? static_cast<int>(PackageTypes::Completed)
                                : static_cast<int>(PackageTypes::ProgressFailed),
                        packageName, 1, 1, success, success ? QString() : err);
                });
        });
}

void PackageManagerBackend::reloadPackage(int index)
{
    // TODO: load/unload the plugin via logoscore.
    Q_UNUSED(index);
    qWarning() << "PackageManagerBackend::reloadPackage not implemented (TODO: logoscore load/unload).";
}

void PackageManagerBackend::installNamed(const QStringList& packageNames)
{
    if (isInstalling()) {
        emit errorOccurred(static_cast<int>(PackageTypes::InstallationAlreadyInProgress));
        return;
    }

    if (packageNames.isEmpty()) {
        emit errorOccurred(static_cast<int>(PackageTypes::NoPackagesSelected));
        return;
    }

    if (!bothClientsReady()) {
        emit errorOccurred(static_cast<int>(PackageTypes::PackageManagerNotConnected));
        return;
    }

    setIsInstalling(true);

    emit installationProgressUpdated(
        static_cast<int>(PackageTypes::Started), "", 0, packageNames.size(), true, "");

    for (const QString& packageName : packageNames) {
        m_packageModel->updatePackageInstallation(packageName, static_cast<int>(PackageTypes::Installing));
    }

    const QString tag = currentReleaseTag();
    LogosModules logos(m_logosAPI);
    QPointer<PackageManagerBackend> self(this);
    logos.package_downloader.downloadPackagesAsync(tag, packageNames, [self, tag](QVariantList results) {
        if (!self) return;
        self->processDownloadResults(tag, results);
    }, Timeout(DOWNLOAD_TIMEOUT_MS));
}

void PackageManagerBackend::requestPackageDetails(int index)
{
    QVariantMap pkg = m_packageModel->packageAt(index);
    if (pkg.isEmpty()) {
        return;
    }
    emit packageDetailsLoaded(pkg);
}

void PackageManagerBackend::togglePackage(int index, bool checked)
{
    m_packageModel->updatePackageSelection(index, checked);
}

void PackageManagerBackend::uninstallSelected()
{
    if (!packageManagerReady()) {
        emit errorOccurred(static_cast<int>(PackageTypes::PackageManagerNotConnected));
        return;
    }

    const QStringList moduleNames = m_packageModel
        ? m_packageModel->getUninstallableSelectedModuleNames()
        : QStringList{};
    if (moduleNames.isEmpty()) {
        emit errorOccurred(static_cast<int>(PackageTypes::NoPackagesSelected));
        return;
    }

    LogosModules logos(m_logosAPI);
    QPointer<PackageManagerBackend> self(this);
    logos.package_manager.requestMultiUninstallAsync(moduleNames,
        [self, moduleNames](QVariantMap result) {
            if (!self) return;
            if (!result.value("success", false).toBool()) {
                const QString err = result.value("error").toString();
                const int errCode = err.contains("embedded", Qt::CaseInsensitive)
                                        ? static_cast<int>(PackageTypes::PackageNotUninstallable)
                                        : static_cast<int>(PackageTypes::UninstallFailed);
                emit self->errorOccurred(errCode);
            }
        });
}

void PackageManagerBackend::uninstall(int index)
{
    if (!packageManagerReady()) {
        emit errorOccurred(static_cast<int>(PackageTypes::PackageManagerNotConnected));
        return;
    }

    const QVariantMap pkg = m_packageModel->packageAt(index);
    if (pkg.isEmpty()) {
        qWarning() << "PackageManagerBackend::uninstall invalid index:" << index;
        return;
    }
    const QString name = pkg.value("moduleName").toString();
    if (name.isEmpty()) {
        qWarning() << "PackageManagerBackend::uninstall: row has no moduleName at index" << index;
        return;
    }

    LogosModules logos(m_logosAPI);
    QPointer<PackageManagerBackend> self(this);
    logos.package_manager.requestUninstallAsync(name,
        [self](QVariantMap result) {
            if (!self) return;
            if (!result.value("success", false).toBool()) {
                const QString err = result.value("error").toString();
                // Embedded-refusal surfaces distinctly so the QML toast is
                // clearer; every other failure funnels through UninstallFailed.
                const int errCode = err.contains("embedded", Qt::CaseInsensitive)
                                        ? static_cast<int>(PackageTypes::PackageNotUninstallable)
                                        : static_cast<int>(PackageTypes::UninstallFailed);
                emit self->errorOccurred(errCode);
            }
        });
}

void PackageManagerBackend::upgradePackage(int index)
{
    requestVersionChange(index, UpgradeMode::Upgrade);
}

void PackageManagerBackend::downgradePackage(int index)
{
    requestVersionChange(index, UpgradeMode::Downgrade);
}

void PackageManagerBackend::sidegradePackage(int index)
{
    requestVersionChange(index, UpgradeMode::Sidegrade);
}

void PackageManagerBackend::requestVersionChange(int index, UpgradeMode mode)
{
    if (!bothClientsReady()) {
        emit errorOccurred(static_cast<int>(PackageTypes::PackageManagerNotConnected));
        return;
    }

    QVariantMap pkg = m_packageModel->packageAt(index);
    if (pkg.isEmpty()) return;

    const QString moduleName = pkg.value("moduleName").toString();
    if (moduleName.isEmpty()) return;

    const QString releaseTag = currentReleaseTag();

    LogosModules logos(m_logosAPI);
    QPointer<PackageManagerBackend> self(this);
    logos.package_manager.requestUpgradeAsync(moduleName, releaseTag,
        static_cast<int>(mode),
        [self](QVariantMap result) {
            if (!self) return;
            if (!result.value("success", false).toBool()) {
                emit self->errorOccurred(
                    static_cast<int>(PackageTypes::UninstallFailed));
            }
        });
}

void PackageManagerBackend::subscribePackageManagerCancellationEvents()
{
    if (!packageManagerReady()) return;

    static const QString kReasonUserCancelled = QStringLiteral("user cancelled");

    LogosModules logos(m_logosAPI);
    QPointer<PackageManagerBackend> self(this);

    // Subscribe a cancellation event with a per-event toast formatter.
    auto subscribe = [&](const char* eventName,
                         std::function<QString(const QJsonObject&, const QString&)> format) {
        logos.package_manager.on(eventName,
            [self, format](const QVariantList& data) {
                if (!self) return;
                const QJsonObject obj = parseEventPayload(data);
                if (obj.isEmpty()) return;
                const QString reason = obj.value("reason").toString();
                if (reason == kReasonUserCancelled) return;
                const QString name = obj.value("name").toString();
                emit self->cancellationOccurred(name, format(obj, reason));
            });
    };

    subscribe("uninstallCancelled",
        [](const QJsonObject& obj, const QString& reason) {
            return QStringLiteral("Uninstall of '%1' cancelled: %2")
                       .arg(obj.value("name").toString(), reason);
        });

    subscribe("upgradeCancelled",
        [](const QJsonObject& obj, const QString& reason) {
            return QStringLiteral("Upgrade of '%1' (%2) cancelled: %3")
                       .arg(obj.value("name").toString(),
                            obj.value("releaseTag").toString(),
                            reason);
        });
}

void PackageManagerBackend::subscribePackageManagerRefreshEvents()
{
    if (!packageManagerReady()) return;

    LogosModules logos(m_logosAPI);

    QPointer<PackageManagerBackend> self(this);
    auto arm = [self](const QVariantList&) {
        if (!self || !self->m_refreshDebounceTimer) return;
        self->m_refreshDebounceTimer->start();
    };

    auto deselectAndArm = [self](const QVariantList& data) {
        if (!self) return;
        if (!data.isEmpty() && self->m_packageModel) {
            const QString moduleName = data.first().toString();
            if (!moduleName.isEmpty())
                self->m_packageModel->clearSelectionsByModuleNames({moduleName});
        }
        if (self->m_refreshDebounceTimer) self->m_refreshDebounceTimer->start();
    };

    logos.package_manager.on("corePluginFileInstalled", arm);
    logos.package_manager.on("uiPluginFileInstalled",   arm);
    logos.package_manager.on("corePluginUninstalled",   deselectAndArm);
    logos.package_manager.on("uiPluginUninstalled",     deselectAndArm);
}

void PackageManagerBackend::subscribePackageManagerUpgradeEvents()
{
    if (!packageManagerReady()) return;

    LogosModules logos(m_logosAPI);
    QPointer<PackageManagerBackend> self(this);

    // upgradeUninstallDone fires after confirmUpgrade removes the old version.
    // Payload: JSON-encoded { name, releaseTag, mode }. PMU drives the
    // download+install of the new version.
    logos.package_manager.on("upgradeUninstallDone",
        [self](const QVariantList& data) {
            if (!self) return;
            const QJsonObject obj = parseEventPayload(data);
            if (obj.isEmpty()) return;
            self->onUpgradeUninstallDone(obj.value("name").toString(),
                                         obj.value("releaseTag").toString(),
                                         obj.value("mode").toInt());
        });
}

void PackageManagerBackend::onUpgradeUninstallDone(const QString& moduleName,
                                                    const QString& releaseTag,
                                                    int mode)
{
    const QString mapped = m_packageModel
        ? m_packageModel->displayNameForModule(moduleName) : QString();
    const QString displayName = mapped.isEmpty() ? moduleName : mapped;

    if (!bothClientsReady()) {
        const QString msg = QStringLiteral("Cannot complete upgrade of '%1': "
                                           "required modules not connected").arg(displayName);
        emit installationProgressUpdated(
            static_cast<int>(PackageTypes::ProgressFailed),
            displayName, 0, 0, false, msg);
        return;
    }

    if (m_refreshDebounceTimer) m_refreshDebounceTimer->stop();

    m_packageModel->updatePackageInstallation(
        displayName, static_cast<int>(PackageTypes::Installing));

    static const char* modeLabels[] = {"Upgrading", "Downgrading", "Sidegrading"};
    const char* label = (mode >= 0 && mode <= 2) ? modeLabels[mode] : "Upgrading";
    emit installationProgressUpdated(
        static_cast<int>(PackageTypes::InProgress),
        displayName, 0, 1, true,
        QStringLiteral("%1 %2\u2026").arg(label, displayName));

    // Download the new version from the release the user confirmed against.
    // downloadPackageAsync keys on the catalog name (displayName), not the
    // backend moduleName.
    LogosModules logos(m_logosAPI);
    QPointer<PackageManagerBackend> self(this);
    logos.package_downloader.downloadPackageAsync(releaseTag, displayName,
        [self, releaseTag, displayName, mode](QVariantMap dlResult) {
            if (!self) return;
            // installOnePackage expects { name, path, error } — the
            // downloadPackage return already has that shape.
            self->installOnePackage(releaseTag, dlResult,
                [self, displayName, mode](bool success, const QString& err) {
                    if (!self) return;
                    if (success) {
                        self->m_packageModel->updatePackageInstallation(
                            displayName, static_cast<int>(PackageTypes::Installed));
                    } else {
                        self->m_packageModel->updatePackageInstallation(
                            displayName, static_cast<int>(PackageTypes::Failed), err);
                    }

                    static const char* pastLabels[] = {
                        "Upgrade", "Downgrade", "Sidegrade"};
                    const char* opLabel = (mode >= 0 && mode <= 2)
                                              ? pastLabels[mode] : "Upgrade";

                    emit self->installationProgressUpdated(
                        success ? static_cast<int>(PackageTypes::Completed)
                                : static_cast<int>(PackageTypes::ProgressFailed),
                        displayName, 1, 1, success,
                        success ? QStringLiteral("")
                                : QStringLiteral("%1 of '%2' failed: %3")
                                      .arg(opLabel, displayName, err));

                    // Full catalog refresh — pull updated installType,
                    // installedVersion, installedHash from the on-disk scan.
                    self->refreshPackages();
                });
        }, Timeout(DOWNLOAD_TIMEOUT_MS));
}
