#include "PackageManagerBackend.h"
#include <algorithm>
#include <QDebug>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QPointer>
#include <QSet>
#include <QTimer>
#include <QVariant>
#include "logos_sdk.h"
#include "RowActionResolver.h"   // versionCmp + resolveRowAction (shared with PackageListModel)

constexpr int DOWNLOAD_TIMEOUT_MS = 300000; // 5 minutes

// Serialise install specs to the JSON shape
// downloadResolvedDependencies expects. QJsonDocument handles all the
// escaping that ad-hoc QStringLiteral concat couldn't (a repo URL is
// user-provided and could in theory contain quotes/backslashes/control
// bytes; package names are restricted to a safe charset, which is why
// the older name-only path got away with raw concat). Empty
// repositoryUrl / version fields are omitted entirely so the resolver
// falls back to its default cross-repo / newest-version behaviour
// where the caller didn't pin one.
QString PackageManagerBackend::buildDepsJson(const QList<PackageInstallSpec>& specs)
{
    QJsonArray arr;
    for (const PackageInstallSpec& s : specs) {
        QJsonObject obj;
        obj.insert(QStringLiteral("name"), s.name);
        if (!s.repositoryUrl.isEmpty())
            obj.insert(QStringLiteral("repositoryUrl"), s.repositoryUrl);
        if (!s.version.isEmpty())
            obj.insert(QStringLiteral("version"), s.version);
        arr.append(obj);
    }
    return QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact));
}

PackageManagerBackend::PackageManagerBackend(LogosAPI* logosAPI, QObject* parent)
    : PackageManagerUiSimpleSource(parent)
    , m_packageModel(new PackageListModel(this))
    , m_packagesFilterProxy(new PackagesFilterProxy(this))
    , m_packagesPagingProxy(new PackagesPagingProxy(this))
    , m_logosAPI(logosAPI)
{
    // Initialise base-class properties to sane defaults.
    setSelectedCategoryIndex(0);
    setRunnableActionCount(0);
    setActionSummary(QVariantMap{});
    setActionPlanItems(QVariantList{});
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

    // One-shot wiring: every selection / install-status mutation on the
    // model emits hasSelectionChanged, which rebuilds the bulk action
    // plan and pushes the `runnableActionCount` / `actionSummary` .rep
    // PROPs through this slot. No manual refresh sprinkles at the
    // mutation sites.
    connect(m_packageModel, &PackageListModel::hasSelectionChanged,
            this, &PackageManagerBackend::refreshActionSummary);

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

    if (!m_logosAPI) {
        m_logosAPI = new LogosAPI("core", this);
    }

    // Auto-refresh the catalog on package_manager file mutations. Covers both
    // PMU- and Basecamp-initiated flows since the module is the common point.
    // Targets refreshPackages() (not refreshCatalog) because file mutations
    // don't change releases — going wider would jarringly reset the release combo.
    m_refreshDebounceTimer = new QTimer(this);
    m_refreshDebounceTimer->setSingleShot(true);
    m_refreshDebounceTimer->setInterval(150);
    connect(m_refreshDebounceTimer, &QTimer::timeout,
            this, &PackageManagerBackend::refreshPackages);

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

    QTimer::singleShot(0, this, [this]() { finishInitialSetup(0); });
}

void PackageManagerBackend::finishInitialSetup(int attempt)
{
    if (m_initialSetupComplete) return;
    if (!bothClientsReady()) {
        constexpr int kMaxAttempts = 50;
        if (attempt >= kMaxAttempts) {
            qWarning() << "PackageManagerBackend: modules never became ready — "
                          "event subscriptions and initial catalog load skipped.";
            return;
        }
        QTimer::singleShot(200, this, [this, attempt]() {
            finishInitialSetup(attempt + 1);
        });
        return;
    }
    m_initialSetupComplete = true;

    subscribePackageManagerCancellationEvents();
    subscribePackageManagerRefreshEvents();
    subscribePackageDownloaderEvents();
    subscribePackageManagerUpgradeEvents();

    refreshCatalog();
    refreshRepositories();
}

// ─────────────────────────── file-local helpers ───────────────────────────

// Use the shared dotted-numeric comparator from RowActionResolver.h —
// PackageListModel::setRowVersion needs the same logic to recompute the
// per-row Action when the user moves the dropdown, so keeping versionCmp
// file-local here would force a copy. Bring it into the TU via a using.
using rowaction::versionCmp;

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

// Split a variant string into (base, flavor). Variants are formatted as
// "<os>-<arch>" (release build, no flavor suffix) or "<os>-<arch>-<flavor>"
// where <flavor> is one of a known set ("dev", "portable")
static std::pair<QString, QString> splitVariant(const QString& v)
{
    static const QSet<QString> kKnownFlavors = {
        QStringLiteral("dev"), QStringLiteral("portable")
    };
    const int lastDash = v.lastIndexOf(QLatin1Char('-'));
    if (lastDash <= 0) return {v, QString()};
    const QString trailing = v.mid(lastDash + 1);
    if (kKnownFlavors.contains(trailing)) {
        return {v.left(lastDash), trailing};
    }
    return {v, QString()};
}

// Classify why a package's offered variants don't intersect the platform's
// valid variants. The QML side (ActionPill, when rowAction==NotAvailable)
// maps the enum to user-facing copy via its tooltip.
//   - NoVariantsPublished: nothing offered — nothing to install anywhere.
//   - BuildFlavorMismatch: platform IS offered, wrong flavor (dev/portable/
//     release). User can switch basecamp build flavor to recover.
//   - PlatformMismatch: OS/arch not offered. User can't recover.
static PackageTypes::NotAvailableReason classifyNotAvailable(
    const QStringList& offeredVariants, const QStringList& validVariants)
{
    if (offeredVariants.isEmpty()) return PackageTypes::NoVariantsPublished;

    QSet<QString> userBases;
    for (const QString& v : validVariants) userBases.insert(splitVariant(v).first);
    for (const QString& v : offeredVariants) {
        if (userBases.contains(splitVariant(v).first))
            return PackageTypes::BuildFlavorMismatch;
    }
    return PackageTypes::PlatformMismatch;
}

// Build one model row from one raw catalog row + the installed-by-name index +
// the valid-variants list for this platform. Pure transform; no instance state.
//
// Each catalog row has the multi-repo `index.json` shape produced by
// `package_downloader.getCatalog()`: a `versions[]` array (sorted newest-
// first) where every entry carries the embedded `manifest` for that
// version, plus a small set of header fields (`name`, `description`,
// `type`, `category`, `repositoryUrl`, `repositoryName`, …) that
// `getCatalogJson` lifts from `versions[0].manifest` for convenience.
// We pick `versions[0]` as the selected version (newest); a future
// per-row picker can swap the index without changing this transform.
static QVariantMap buildPackageRow(const QVariantMap& obj,
                                   const QHash<QString, QVariantMap>& installedByName,
                                   const QStringList& validVariants)
{
    QVariantMap pkg;
    const QString name = obj.value("name").toString();

    const QVariantList rawVersions = obj.value("versions").toList();
    QVariantMap selectedVersion;
    if (!rawVersions.isEmpty()) selectedVersion = rawVersions.first().toMap();

    // Manifest of the selected (newest) version — every per-row field
    // not surfaced at the catalog-row top level is read from here.
    const QVariantMap manifest = selectedVersion.value("manifest").toMap();

    QString moduleName = obj.value("moduleName").toString();
    if (moduleName.isEmpty()) moduleName = manifest.value("name").toString();
    if (moduleName.isEmpty()) moduleName = name;

    QString displayName = obj.value("displayName").toString();
    if (displayName.isEmpty()) displayName = manifest.value("display_name").toString();
    if (displayName.isEmpty()) displayName = moduleName;

    pkg["name"] = name;
    pkg["moduleName"] = moduleName;
    pkg["displayName"] = displayName;
    // Header fields: prefer the catalog-row's lifted copy (which
    // getCatalogJson sets from versions[0].manifest), fall back to the
    // manifest itself if the catalog row didn't surface the field.
    pkg["description"] = obj.value("description").toString().isEmpty()
                         ? manifest.value("description").toString()
                         : obj.value("description").toString();
    pkg["type"] = obj.value("type").toString().isEmpty()
                  ? manifest.value("type").toString()
                  : obj.value("type").toString();
    pkg["category"] = obj.value("category").toString().isEmpty()
                      ? manifest.value("category").toString()
                      : obj.value("category").toString();
    pkg["size"] = selectedVersion.value("size");
    pkg["dateUpdated"] = selectedVersion.value("releasedAt").toString();

    pkg["repositoryUrl"]         = obj.value("repositoryUrl").toString();
    pkg["repositoryName"]        = obj.value("repositoryName").toString();
    pkg["repositoryDisplayName"] = obj.value("repositoryDisplayName").toString();

    // Trim each entry of versions[] into a model-friendly shape.
    QVariantList availableVersions;
    for (const QVariant& vv : rawVersions) {
        const QVariantMap vm = vv.toMap();
        const QVariantMap vManifest = vm.value("manifest").toMap();
        QVariantMap entry;
        entry["version"]      = vManifest.value("version").toString();
        entry["rootHash"]     = vm.value("rootHash").toString();
        entry["releasedAt"]   = vm.value("releasedAt").toString();
        entry["publisherRef"] = vm.value("publisherRef").toString();
        entry["url"]          = vm.value("url").toString();
        entry["signed"]       = vm.contains("signature");
        entry["signerDid"]    = vm.value("signature").toMap().value("did").toString();
        entry["manifest"]     = vManifest;
        availableVersions.append(entry);
    }
    pkg["availableVersions"]    = availableVersions;
    pkg["selectedVersionIndex"] = 0;

    // Release version comes from the selected version's manifest; root
    // hash comes from the catalog row's `rootHash` (set per version by
    // the index builder — authoritative over any hash inside the
    // manifest itself).
    const QString releaseVersion = manifest.value("version").toString();
    const QString releaseHash = selectedVersion.value("rootHash").toString();
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

    // Variant availability — true iff any of the package's offered
    // variants intersects this platform's valid-variants list. Variants
    // are the keys of the manifest's `main` map (`{variant: entry_path}`).
    QStringList offeredVariants;
    {
        const QVariantMap mainMap = manifest.value("main").toMap();
        for (auto it = mainMap.constBegin(); it != mainMap.constEnd(); ++it) {
            const QString s = it.key();
            if (!s.isEmpty()) offeredVariants.append(s);
        }
    }
    bool variantAvailable = false;
    for (const QString& s : offeredVariants) {
        if (validVariants.contains(s)) { variantAvailable = true; break; }
    }
    // QML-only ui_qml packages can have an empty `main` map (no backend
    // plugin); they install on every platform.
    if (!variantAvailable && offeredVariants.isEmpty()
        && manifest.value("type").toString() == QLatin1String("ui_qml")) {
        variantAvailable = true;
    }
    pkg["isVariantAvailable"] = variantAvailable;
    pkg["notAvailableReason"] = static_cast<int>(
        variantAvailable ? PackageTypes::Available
                         : classifyNotAvailable(offeredVariants, validVariants));

    // ── Action-column inputs ────────────────────────────────────────
    // `rowAction` is the per-row primary action, resolved against the
    // INITIAL selected version (newest, i.e. versions[0]). It will be
    // recomputed by PackageListModel::setRowVersion() whenever the
    // user moves the dropdown — same helper, same inputs, fresh values.
    //
    // `updateAvailable` is a separate signal that stays put even as the
    // dropdown moves: it reflects "a strictly-newer-than-installed
    // version exists in the catalog", and drives the small marker on
    // the Version cell. Computed once here.
    pkg["rowAction"] = rowaction::resolveRowAction(
        isInstalled, variantAvailable, status,
        installedVersion, installedHash,
        /*selectedVersion=*/releaseVersion,
        /*selectedHash=*/releaseHash);
    pkg["updateAvailable"] = rowaction::hasUpdateAvailable(
        isInstalled, installedVersion, /*newestCatalogVersion=*/releaseVersion);

    // dependencies may be a flat array of names (legacy) or a list mixing
    // plain-string and object entries (new manifest schema). The QML side
    // displays them as a string list; render objects as "name version
    // [signer=…]" so the user can see the constraint.
    QStringList deps;
    QVariantList depsArray = obj.value("dependencies").toList();
    if (depsArray.isEmpty()) depsArray = manifest.value("dependencies").toList();
    for (const QVariant& dep : depsArray) {
        if (dep.canConvert<QVariantMap>() && !dep.toString().size()) {
            const QVariantMap dm = dep.toMap();
            QString s = dm.value("name").toString();
            if (dm.contains("version")) s += QStringLiteral(" ") + dm.value("version").toString();
            if (dm.contains("signer"))
                s += QStringLiteral(" [signer=") + dm.value("signer").toString() + QStringLiteral("]");
            deps.append(s);
        } else {
            deps.append(dep.toString());
        }
    }
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

QAbstractItemModel* PackageManagerBackend::packages() const
{
    return m_packagesPagingProxy;
}

bool PackageManagerBackend::resolveRowIdentifier(int proxyRow,
                                                 QString* outName,
                                                 QString* outRepoUrl) const
{
    if (proxyRow < 0 || !m_packagesPagingProxy || !m_packageModel) return false;
    const QModelIndex idx = m_packagesPagingProxy->index(proxyRow, 0);
    if (!idx.isValid()) return false;
    const QString name = m_packagesPagingProxy
        ->data(idx, PackageListModel::NameRole).toString();
    const QString repoUrl = m_packagesPagingProxy
        ->data(idx, PackageListModel::RepositoryUrlRole).toString();
    if (name.isEmpty()) return false;
    if (outName)    *outName    = name;
    if (outRepoUrl) *outRepoUrl = repoUrl;
    return true;
}

int PackageManagerBackend::findPackageRowAtProxyRow(int proxyRow) const
{
    QString name, repoUrl;
    if (!resolveRowIdentifier(proxyRow, &name, &repoUrl)) return -1;
    return m_packageModel ? m_packageModel->findPackageRow(name, repoUrl) : -1;
}

QVariantMap PackageManagerBackend::findPackageAtProxyRow(int proxyRow) const
{
    const int row = findPackageRowAtProxyRow(proxyRow);
    return row >= 0 ? m_packageModel->packageAt(row) : QVariantMap();
}

void PackageManagerBackend::refreshActionSummary()
{
    if (!m_packageModel) return;
    // Build the plan from current selection state and publish its
    // size + per-action counts. The header's "Run Actions (N)" reads
    // `runnableActionCount`; the confirm-summary popup reads
    // `actionSummary`. Uninstall is never in the plan — the row
    // overflow menu handles it explicitly per-row.
    const PackageActionPlan plan = m_packageModel->buildActionPlanForSelected();
    setRunnableActionCount(plan.total());
    setActionSummary(plan.toSummary());
    // Per-row detail accompanying the category counts. The confirm
    // popup pairs each item with its action header (install / upgrade /
    // …) and shows "name: vFrom → vTo" so the user can verify the
    // exact transitions before confirming. Computed every selection
    // change for the same reason the counts are.
    setActionPlanItems(plan.toItemList());
}

void PackageManagerBackend::refreshCatalog()
{
    if (!bothClientsReady()) {
        qDebug() << "package_downloader or package_manager not connected, cannot refresh catalog";
        return;
    }

    // Full catalog refresh — drop Failed-row state from the previous fetch.
    // Failed marks a previously-attempted install that didn't complete; on a
    // user-initiated reload the previous attempt is stale. (The debounced
    // refresh path leaves Failed alone — file-install events don't justify
    // wiping unrelated user-visible error state.)
    if (m_packageModel) m_packageModel->clearFailedRows();

    setIsLoading(true);

    // Invalidate the downloader's in-memory per-repo index cache BEFORE
    // re-querying. package_downloader caches each repo's index.json for
    // the life of its process and getCatalog() serves that cache, so a
    // bare refreshPackages() here just re-renders the *stale* catalog —
    // modules removed/added upstream wouldn't show until an app restart
    // (the bug the Reload button is supposed to fix). The module's
    // refreshCatalog() maps to lib->refreshCatalogs(), which clears
    // that cache and re-fetches every logos-repo.json; only then is the
    // follow-up getCatalog a real network refresh. clearCaches() runs
    // unconditionally inside refreshCatalogs() even if a repo's metadata
    // fetch errors, so we proceed to refreshPackages() regardless of the
    // reported result. (The debounced file-event path deliberately
    // still calls refreshPackages() directly — a local file mutation
    // doesn't warrant a network round-trip.)
    LogosModules logos(m_logosAPI);
    QPointer<PackageManagerBackend> self(this);
    logos.package_downloader.refreshCatalogAsync([self](QVariantMap r) {
        if (!self) return;
        const QString err = r.value(QStringLiteral("error")).toString();
        if (!err.isEmpty())
            qWarning() << "package_downloader.refreshCatalog reported:" << err;
        self->refreshPackages();
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

    // One round-trip for the catalog (union across every enabled
    // repository); category list is derived from it client-side so
    // subsequent category clicks slice m_allPackagesCache without a
    // network round-trip.
    LogosModules logos(m_logosAPI);
    QPointer<PackageManagerBackend> self(this);
    logos.package_downloader.getCatalogAsync([self, currentGeneration](QVariantList packagesArray) {
        if (!self || self->m_reloadGeneration != currentGeneration) return;

        // Derive categories from the catalog: "All" + sorted distinct
        // (capitalised) values of each package's `category` field.
        QStringList categoryList;
        categoryList << QStringLiteral("All");
        QStringList seen;
        for (const QVariant& v : packagesArray) {
            QString c = v.toMap().value(QStringLiteral("category")).toString();
            if (c.isEmpty()) continue;
            c[0] = c[0].toUpper();
            if (!seen.contains(c)) seen.append(c);
        }
        std::sort(seen.begin(), seen.end());
        categoryList.append(seen);
        self->setCategories(categoryList);

        LogosModules logos2(self->m_logosAPI);
        logos2.package_manager.getInstalledPackagesAsync([self, currentGeneration, packagesArray](QVariantList installedPackages) {
            if (!self || self->m_reloadGeneration != currentGeneration) return;

            LogosModules logos3(self->m_logosAPI);
            logos3.package_manager.getValidVariantsAsync([self, currentGeneration, packagesArray, installedPackages](QVariant result) {
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

    // Group rows by source: the hardcoded default repository always
    // comes first (priority 0), then any user-added repos sorted by
    // their canonical name (priority 1). Within each source rows sort
    // by package name. The QML uses `isFirstOfSource` (tagged below)
    // to draw a section header above the first row of each group
    // instead of a per-row Source column.
    //
    // Default-repo identification is by `repositoryName` matching the
    // canonical "logos-modules-official" string baked into logos-repo.json
    // — avoids pulling in package_downloader_lib.h just for the URL
    // constant. If the canonical name ever moves, the constant in the
    // lib AND this match string need to update together.
    auto sourcePriority = [](const QVariantMap& row) -> int {
        const QString n = row.value("repositoryName").toString();
        return n == QLatin1String("logos-modules-official") ? 0 : 1;
    };
    auto sourceKey = [](const QVariantMap& row) -> QString {
        // Use displayName when present (human label like "Logos Official"),
        // canonical name otherwise, falling back to URL so two unresolved
        // repos still sort stably.
        const QString dn = row.value("repositoryDisplayName").toString();
        if (!dn.isEmpty()) return dn;
        const QString n = row.value("repositoryName").toString();
        if (!n.isEmpty()) return n;
        return row.value("repositoryUrl").toString();
    };
    std::stable_sort(packages.begin(), packages.end(),
        [&](const QVariantMap& a, const QVariantMap& b) {
            const int pa = sourcePriority(a);
            const int pb = sourcePriority(b);
            if (pa != pb) return pa < pb;
            const QString ka = sourceKey(a);
            const QString kb = sourceKey(b);
            const int c = ka.compare(kb, Qt::CaseInsensitive);
            if (c != 0) return c < 0;
            return a.value("name").toString().compare(
                b.value("name").toString(), Qt::CaseInsensitive) < 0;
        });

    // Tag each row's `isFirstOfSource` — true when the row's
    // (priority, sourceKey) tuple differs from the previous row's.
    // The QML rowDelegate reads this to render a section header.
    int prevPriority = -1;
    QString prevKey;
    for (QVariantMap& row : packages) {
        const int p = sourcePriority(row);
        const QString k = sourceKey(row);
        row["isFirstOfSource"] = (p != prevPriority) || (k != prevKey);
        prevPriority = p;
        prevKey = k;
    }

    // setPackages emits hasSelectionChanged; the connected slot
    // (refreshActionSummary) rebuilds the bulk action plan and pushes
    // `runnableActionCount` + `actionSummary` to the .rep PROPs.
    m_packageModel->setPackages(packages);
}

void PackageManagerBackend::processDownloadResults(const QVariantList& results)
{
    if (!packageManagerReady()) {
        qWarning() << "package_manager not connected, cannot install downloaded packages";
        finishInstallation(0);
        return;
    }

    int totalPackages = m_packageModel ? m_packageModel->getSelectedCount() : results.size();
    installNextPackage(results, 0, 0, totalPackages);
}

void PackageManagerBackend::installOnePackage(const QVariantMap& dl,
                                               std::function<void(bool success, const QString& error)> onDone)
{
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

void PackageManagerBackend::installNextPackage(const QVariantList& results, int index, int completed, int totalPackages)
{
    while (index < results.size()) {
        QVariantMap dl = results[index].toMap();
        QString packageName = dl.value("name").toString();

        // Bubble the per-package outcome back up through the model + progress
        // signal. installOnePackage handles both the download-failed fast path
        // (empty filePath) and the actual installPlugin IPC call.
        QPointer<PackageManagerBackend> self(this);
        installOnePackage(dl,
            [self, results, packageName, index, completed, totalPackages](bool success, const QString& err) {
                if (!self) return;

                int newCompleted = completed + 1;
                if (success) {
                    self->m_packageModel->updatePackageInstallation(
                        packageName, static_cast<int>(PackageTypes::Installed));
                    // Per-package deselect on success — the action is done,
                    // so the row should drop out of the selection. Failed
                    // rows stay selected so the user can retry via the
                    // bulk "Run Actions" button: the Failed status flips
                    // rowAction to Retry and keeps the row counted in
                    // runnableActionCount, so a re-confirm replays the
                    // failed installs.
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

                self->installNextPackage(results, index + 1, newCompleted, totalPackages);
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

void PackageManagerBackend::runSelectedActions()
{
    // The new bulk-action path: each selected row contributes its
    // resolved primary action. Builds the plan once (the model already
    // knows each row's `rowAction`), then dispatches in two passes:
    //
    //   1. Install + Retry rows → one batched `installNamed` call
    //      (single dependency-resolving download, sequential install
    //      under the global isInstalling flag — same path the old
    //      bulk Install button used).
    //   2. Upgrade / Downgrade / Reinstall rows → per-row
    //      `requestVersionChange` calls (each pins the row's currently
    //      selected version + the matching UpgradeMode). These run
    //      independently of `isInstalling`, matching how the per-row
    //      action button works today.
    //
    // Uninstall is intentionally NEVER in the plan: destructive actions
    // stay an explicit per-row gesture via the row's overflow menu.
    if (!m_packageModel) return;
    if (!bothClientsReady()) {
        emit errorOccurred(static_cast<int>(PackageTypes::PackageManagerNotConnected));
        return;
    }
    const PackageActionPlan plan = m_packageModel->buildActionPlanForSelected();
    if (plan.isEmpty()) {
        emit errorOccurred(static_cast<int>(PackageTypes::NoPackagesSelected));
        return;
    }
    if (!plan.installSpecs.isEmpty()) {
        installSpecs(plan.installSpecs);
    }
    for (const auto& vc : plan.versionChanges) {
        requestVersionChange(vc.first, static_cast<UpgradeMode>(vc.second));
    }
}

void PackageManagerBackend::installPackage(int index)
{
    const QVariantMap pkg = findPackageAtProxyRow(index);
    if (pkg.isEmpty()) {
        qWarning() << "PackageManagerBackend::installPackage no row at proxy index" << index;
        return;
    }
    const QString name = pkg.value("name").toString();
    if (name.isEmpty()) {
        qWarning() << "PackageManagerBackend::installPackage missing name at proxy index" << index;
        return;
    }
    // Dep preview first — if the resolver surfaces transitive changes,
    // the user gets the dep-confirm dialog before anything downloads.
    // No-changes path proceeds silently (single-click experience for
    // the common case). The row's source repo + selected version flow
    // through so a same-named package in another repo can't sneak in.
    runDepPreviewForAction(name,
                           pkg.value("moduleName").toString(),
                           pkg.value("repositoryUrl").toString(),
                           pkg.value("version").toString(),
                           static_cast<int>(PendingDepConfirm::Install));
}

void PackageManagerBackend::installSinglePackageAsync(const QString& packageName,
                                                       const QString& repoUrl,
                                                       const QString& version,
                                                       bool includeDeps)
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

    // Drive the download through the dep-resolving channel. The
    // resolver returns deps-first then the top-level; whether we
    // install transitives in addition to the top-level is the caller's
    // choice via `includeDeps`. true (default) closes a long-standing
    // quirk where a per-row install silently dropped new transitive
    // deps — the old code only installed the entry matching by name,
    // leaving freshly-required deps on disk-as-download but not
    // registered.
    //
    // Build with QJsonDocument so a repo URL containing characters that
    // would otherwise need JSON escaping (quotes, backslashes, control
    // bytes) doesn't desynchronise the payload. Plain string concat
    // worked when only the package name was involved (names are
    // restricted to a safe charset), but repo URLs are user-provided.
    PackageInstallSpec spec; spec.name = packageName;
    spec.repositoryUrl = repoUrl; spec.version = version;
    const QString depsJson = buildDepsJson({spec});
    LogosModules logos(m_logosAPI);
    QPointer<PackageManagerBackend> self(this);
    logos.package_downloader.downloadResolvedDependenciesAsync(depsJson, buildInstalledPackagesJson(),
        [self, packageName, includeDeps](QVariantList results) {
            if (!self) return;
            // Filter to top-level entries when the caller asked for
            // "just the package". The resolver may still have
            // downloaded transitives (the request goes out before this
            // filter), but we don't install them — matches the
            // user's explicit choice from the dep-confirm dialog.
            QVariantList toInstall;
            QVariantMap fallbackMatch;
            for (const QVariant& v : results) {
                const QVariantMap m = v.toMap();
                const bool isTop = m.value("topLevel").toBool();
                if (isTop || includeDeps) toInstall.append(m);
                if (m.value("name").toString() == packageName) fallbackMatch = m;
            }
            if (toInstall.isEmpty()) {
                // No usable entries — surface the resolver's error row
                // (or fabricate one named for the requested package so
                // the model's Failed-by-name update finds the row).
                QVariantMap dlResult = fallbackMatch.isEmpty()
                    ? (results.isEmpty() ? QVariantMap{} : results.last().toMap())
                    : fallbackMatch;
                if (dlResult.value("name").toString().isEmpty())
                    dlResult["name"] = packageName;
                toInstall.append(dlResult);
            }
            // Flip every dep row's status to Installing up front so the
            // user sees them as "in flight" the moment the dep-confirm
            // dialog closes, not one-by-one as the sequential install
            // loop reaches each. Skip error rows — those didn't make it
            // to the install step and need to surface as failures, not
            // hang in Installing.
            self->markEntriesInstalling(toInstall);
            self->installResultsSequential(toInstall, packageName, 0);
        }, Timeout(DOWNLOAD_TIMEOUT_MS));
}

void PackageManagerBackend::installResultsSequential(const QVariantList& results,
                                                     const QString& topLevelName,
                                                     int index)
{
    // Per-row install pipeline counterpart to installNextPackage. The
    // bulk path locks isInstalling and emits Completed at the end via
    // finishInstallation; per-row stays unlocked so concurrent per-row
    // clicks don't deadlock each other. We emit progress events keyed
    // on the top-level name so the UI shows the user's clicked row as
    // the one being acted on, even when the loop is iterating
    // transitive deps in between.
    if (index >= results.size()) return;
    const QVariantMap dl = results[index].toMap();
    const QString depName = dl.value("name").toString();
    QPointer<PackageManagerBackend> self(this);
    installOnePackage(dl,
        [self, results, topLevelName, depName, index](bool success, const QString& err) {
            if (!self) return;
            if (success) {
                self->m_packageModel->updatePackageInstallation(
                    depName, static_cast<int>(PackageTypes::Installed));
            } else {
                // Failure attribution: the model's row for the
                // failing entry takes the Failed status. The progress
                // event also names the top-level so the UI's
                // "Installing X…" banner flips to "X failed because
                // dep Y failed" once we surface that on the QML side.
                self->m_packageModel->updatePackageInstallation(
                    depName, static_cast<int>(PackageTypes::Failed), err);
                // Earlier we marked EVERY entry in `results` as
                // Installing so the row badges reflect the in-flight
                // batch immediately. The loop stops here on failure;
                // revert remaining entries to NotInstalled so they
                // don't stay stuck on Installing forever (the
                // subsequent refreshPackages via the debounce timer
                // would eventually correct them, but the window between
                // failure and refresh would be visibly wrong).
                self->revertPendingEntries(results, index + 1);
            }
            const bool isLast = (index + 1) >= results.size();
            emit self->installationProgressUpdated(
                success ? (isLast ? static_cast<int>(PackageTypes::Completed)
                                  : static_cast<int>(PackageTypes::InProgress))
                        : static_cast<int>(PackageTypes::ProgressFailed),
                topLevelName, index + 1, results.size(), success,
                success ? QString() : err);
            if (success && !isLast)
                self->installResultsSequential(results, topLevelName, index + 1);
        });
}

void PackageManagerBackend::markEntriesInstalling(const QVariantList& entries)
{
    if (!m_packageModel) return;
    for (const QVariant& v : entries) {
        const QVariantMap m = v.toMap();
        // Error rows have no install to run — skip; the per-entry
        // failure surface will pick them up via the install loop's
        // own Failed update. Marking them Installing first would
        // briefly flip the badge to the wrong state.
        if (m.contains("error")) continue;
        const QString name = m.value("name").toString();
        if (name.isEmpty()) continue;
        m_packageModel->updatePackageInstallation(
            name, static_cast<int>(PackageTypes::Installing));
    }
}

void PackageManagerBackend::revertPendingEntries(const QVariantList& entries, int fromIndex)
{
    if (!m_packageModel) return;
    for (int i = fromIndex; i < entries.size(); ++i) {
        const QString name = entries[i].toMap().value("name").toString();
        if (name.isEmpty()) continue;
        // NotInstalled is the safe rollback target — the next catalog
        // refresh would compute the right status from disk regardless,
        // but until then the Action column would surface "Install"
        // (correct: the user can retry).
        m_packageModel->updatePackageInstallation(
            name, static_cast<int>(PackageTypes::NotInstalled));
    }
}

void PackageManagerBackend::reloadPackage(int index)
{
    // TODO: load/unload the plugin via logoscore.
    Q_UNUSED(index);
    qWarning() << "PackageManagerBackend::reloadPackage not implemented (TODO: logoscore load/unload).";
}

void PackageManagerBackend::installSpecs(const QList<PackageInstallSpec>& specs,
                                         bool includeDeps)
{
    if (isInstalling()) {
        emit errorOccurred(static_cast<int>(PackageTypes::InstallationAlreadyInProgress));
        return;
    }

    if (specs.isEmpty()) {
        emit errorOccurred(static_cast<int>(PackageTypes::NoPackagesSelected));
        return;
    }

    if (!bothClientsReady()) {
        emit errorOccurred(static_cast<int>(PackageTypes::PackageManagerNotConnected));
        return;
    }

    setIsInstalling(true);

    emit installationProgressUpdated(
        static_cast<int>(PackageTypes::Started), "", 0, specs.size(), true, "");

    for (const PackageInstallSpec& spec : specs) {
        m_packageModel->updatePackageInstallation(
            spec.name, static_cast<int>(PackageTypes::Installing));
    }

    // Pack the specs into the JSON-array shape expected by
    // `downloadResolvedDependenciesAsync`. The downloader resolves
    // transitive deps from the catalog, pins each, and downloads in
    // deps-first order. `includeDeps` (default true) feeds the result
    // straight into processDownloadResults; false filters out non-
    // topLevel entries before installation so the user gets "just the
    // packages I selected" semantics.
    const QString depsJson = buildDepsJson(specs);

    LogosModules logos(m_logosAPI);
    QPointer<PackageManagerBackend> self(this);
    logos.package_downloader.downloadResolvedDependenciesAsync(depsJson, buildInstalledPackagesJson(),
        [self, includeDeps](QVariantList results) {
            if (!self) return;
            if (!includeDeps) {
                QVariantList filtered;
                for (const QVariant& v : results) {
                    const QVariantMap m = v.toMap();
                    if (m.value("topLevel").toBool() || m.contains("error"))
                        filtered.append(m);
                }
                self->processDownloadResults(filtered);
            } else {
                self->processDownloadResults(results);
            }
        }, Timeout(DOWNLOAD_TIMEOUT_MS));
}

void PackageManagerBackend::installNamed(const QStringList& packageNames)
{
    // Legacy name-only entry point — kept for the unwired-but-present
    // installSelected() .rep slot. Build specs with empty repo/version
    // (resolver picks cross-repo + newest, same as before).
    QList<PackageInstallSpec> specs;
    specs.reserve(packageNames.size());
    for (const QString& n : packageNames) {
        PackageInstallSpec s; s.name = n; specs.append(s);
    }
    installSpecs(specs);
}

QString PackageManagerBackend::buildInstalledPackagesJson() const
{
    // package_manager.getInstalledPackages returns entries shaped like
    //   { name, moduleName, version, hashes: { root }, installType, ... }
    // The resolver only needs (name, version) for its range check; we
    // also pass rootHash for parity with the rest of the install plumbing
    // even though the current short-circuit doesn't read it.
    QJsonArray arr;
    for (const QVariant& v : m_installedPackagesCache) {
        const QVariantMap m = v.toMap();
        const QString name = m.value("moduleName").toString().isEmpty()
                             ? m.value("name").toString()
                             : m.value("moduleName").toString();
        const QString version = m.value("version").toString();
        if (name.isEmpty() || version.isEmpty()) continue;
        QJsonObject o;
        o.insert(QStringLiteral("name"), name);
        o.insert(QStringLiteral("version"), version);
        const QString rootHash = m.value("hashes").toMap().value("root").toString();
        if (!rootHash.isEmpty()) o.insert(QStringLiteral("rootHash"), rootHash);
        arr.append(o);
    }
    return QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact));
}

QVariantList PackageManagerBackend::computeDepChanges(
    const QVariantList& resolved,
    const QHash<QString, QString>& installedByName,
    const QHash<QString, QString>& repoUrlToName) const
{
    QVariantList out;
    for (const QVariant& v : resolved) {
        const QVariantMap m = v.toMap();
        if (m.contains("error")) continue;            // surfaced separately
        if (m.value("topLevel").toBool()) continue;   // user's own pick
        const QString name = m.value("name").toString();
        const QString to   = m.value("version").toString();
        const QString from = installedByName.value(name);

        QVariantMap c;
        c.insert(QStringLiteral("name"),        name);
        c.insert(QStringLiteral("toVersion"),   to);
        c.insert(QStringLiteral("fromVersion"), from);
        // Prefer the human-readable repo display name when known.
        // repoUrlToName is the caller's map (catalog → repositoryDisplayName);
        // fall back to the raw URL when the catalog hasn't surfaced the
        // friendly label for this repo yet.
        const QString url = m.value("repositoryUrl").toString();
        c.insert(QStringLiteral("repository"),
                 repoUrlToName.contains(url) ? repoUrlToName.value(url) : url);
        if (from.isEmpty()) {
            c.insert(QStringLiteral("action"), QStringLiteral("install"));
        } else {
            const int cmp = rowaction::versionCmp(from, to);
            c.insert(QStringLiteral("action"),
                     cmp < 0 ? QStringLiteral("upgrade") : QStringLiteral("downgrade"));
        }
        out.append(c);
    }
    return out;
}

void PackageManagerBackend::runDepPreviewForAction(const QString& packageName,
                                                   const QString& moduleName,
                                                   const QString& repoUrl,
                                                   const QString& version,
                                                   int actionKind)
{
    if (!bothClientsReady()) {
        emit errorOccurred(static_cast<int>(PackageTypes::PackageManagerNotConnected));
        return;
    }

    PackageInstallSpec spec;
    spec.name = packageName;
    spec.repositoryUrl = repoUrl;
    spec.version = version;
    const QString depsJson      = buildDepsJson({spec});
    const QString installedJson = buildInstalledPackagesJson();

    // repoUrl → repositoryDisplayName index — built once per preview
    // from the current catalog cache so the dialog's "from repo X" line
    // is the human label the rest of the UI uses.
    QHash<QString, QString> repoUrlToName;
    for (const QVariant& v : m_allPackagesCache) {
        const QVariantMap m = v.toMap();
        const QString u = m.value("repositoryUrl").toString();
        const QString n = m.value("repositoryDisplayName").toString();
        if (!u.isEmpty() && !n.isEmpty() && !repoUrlToName.contains(u))
            repoUrlToName.insert(u, n);
    }
    // Installed snapshot indexed by name for from-version lookup.
    QHash<QString, QString> installedByName;
    for (const QVariant& v : m_installedPackagesCache) {
        const QVariantMap m = v.toMap();
        const QString n = m.value("moduleName").toString().isEmpty()
                          ? m.value("name").toString()
                          : m.value("moduleName").toString();
        const QString ver = m.value("version").toString();
        if (!n.isEmpty() && !ver.isEmpty()) installedByName.insert(n, ver);
    }
    LogosModules logos(m_logosAPI);
    QPointer<PackageManagerBackend> self(this);
    logos.package_downloader.resolveDependenciesAsync(depsJson, installedJson,
        [self, packageName, moduleName, repoUrl, version, actionKind,
         installedByName, repoUrlToName]
        (QVariantList resolved) {
            if (!self) return;
            const QVariantList changes = self->computeDepChanges(
                resolved, installedByName, repoUrlToName);
            // Route the confirmation to the HOST (basecamp) instead of showing
            // PMU's own dialog: serialise the resolved change list and hand it
            // to the package_manager gate (requestInstall / requestUpgrade).
            // basecamp subscribes to beforeInstall / beforeUpgrade and shows
            // ONE dialog — titled for the action and listing these `changes` —
            // so the user no longer sees a PMU dialog followed by a basecamp
            // one. An install must still never happen silently, but that
            // confirmation now lives entirely in the host. The old in-PMU
            // InstallDepsConfirm path (installDepsConfirmationRequested +
            // confirmInstallWith{,out}Deps) is retired and never emitted.
            const QString changesJson = QString::fromUtf8(
                QJsonDocument(QJsonArray::fromVariantList(changes))
                    .toJson(QJsonDocument::Compact));
            self->dispatchPendingAction(packageName, moduleName, repoUrl,
                                        version, actionKind, changesJson);
        });
}

void PackageManagerBackend::dispatchPendingAction(const QString& packageName,
                                                  const QString& moduleName,
                                                  const QString& repoUrl,
                                                  const QString& version,
                                                  int actionKind,
                                                  const QString& depChangesJson)
{
    // Central dispatch: route the action through the package_manager gate so
    // the HOST (basecamp) owns the confirmation dialog. `depChangesJson` is the
    // resolved transitive change list, echoed into the module's beforeInstall /
    // beforeUpgrade event so the host dialog can list it. On approval the module
    // emits installApproved / upgradeUninstallDone; PMU then runs the actual
    // download+install (onInstallApproved / onUpgradeUninstallDone). Deps are
    // always included now — the host dialog is confirm-or-cancel, with no
    // "just the package" split — so PendingUpgradeMeta.includeDeps stays true.
    LogosModules logos(m_logosAPI);
    QPointer<PackageManagerBackend> self(this);
    switch (static_cast<PendingDepConfirm::Action>(actionKind)) {
    case PendingDepConfirm::Install:
        logos.package_manager.requestInstallAsync(packageName, version, repoUrl, depChangesJson,
            [self](QVariantMap result) {
                if (!self) return;
                if (!result.value("success", false).toBool())
                    emit self->errorOccurred(
                        static_cast<int>(PackageTypes::InstallationAlreadyInProgress));
            });
        return;
    case PendingDepConfirm::Upgrade:
    case PendingDepConfirm::Downgrade:
    case PendingDepConfirm::Sidegrade: {
        if (!moduleName.isEmpty()) {
            PendingUpgradeMeta meta;
            meta.repositoryUrl = repoUrl;
            meta.includeDeps   = true;
            m_pendingUpgradeByModule.insert(moduleName, meta);
        }
        const int mode = (actionKind == PendingDepConfirm::Downgrade) ? 1
                       : (actionKind == PendingDepConfirm::Sidegrade) ? 2 : 0;
        logos.package_manager.requestUpgradeAsync(moduleName, version, mode, depChangesJson,
            [self](QVariantMap result) {
                if (!self) return;
                if (!result.value("success", false).toBool())
                    emit self->errorOccurred(
                        static_cast<int>(PackageTypes::UninstallFailed));
            });
        return;
    }
    }
}

// Retired: the in-PMU dep-confirm dialog is superseded by the host gate — the
// confirmation now lives in basecamp (see runDepPreviewForAction, which routes
// straight to the package_manager gate). These .rep slots remain so the
// generated interface stays satisfied, but the signal that drove them
// (installDepsConfirmationRequested) is no longer emitted, so they never run.
// Drain any stale pending entry defensively and do nothing else.
void PackageManagerBackend::confirmInstallWithDeps(QString requestKey)
{
    m_pendingDepConfirms.remove(requestKey);
}

void PackageManagerBackend::confirmInstallWithoutDeps(QString requestKey)
{
    m_pendingDepConfirms.remove(requestKey);
}

void PackageManagerBackend::cancelInstallConfirm(QString requestKey)
{
    // No-op beyond dropping the pending entry. No backend state was
    // mutated yet (the resolver call was a pure preview), so cancel is
    // equivalent to "never happened".
    m_pendingDepConfirms.remove(requestKey);
}

void PackageManagerBackend::requestPackageDetails(int index)
{
    const QVariantMap pkg = findPackageAtProxyRow(index);
    if (pkg.isEmpty()) return;
    emit packageDetailsLoaded(pkg);
}

void PackageManagerBackend::togglePackage(int index, bool checked)
{
    // Guardrail: only runnable rows participate in the bulk
    // "Run Actions" selection. NoOp / NotAvailable rows have nothing
    // for runSelectedActions to dispatch, so a check on one of those
    // would just count toward `runnableActionCount` without ever
    // running anything — confusing in the header label, confusing in
    // the confirm-summary. The QML side also hides the checkbox for
    // these rows, but we enforce it here so out-of-band selections
    // (keyboard, scripted tests, future shift-click) can't sneak in.
    if (!m_packageModel) return;
    const int row = findPackageRowAtProxyRow(index);
    if (row < 0) return;
    if (checked) {
        const QVariantMap pkg = m_packageModel->packageAt(row);
        const int action = pkg.value(QStringLiteral("rowAction"),
                              static_cast<int>(PackageTypes::NoOp)).toInt();
        if (action == static_cast<int>(PackageTypes::NoOp)
            || action == static_cast<int>(PackageTypes::NotAvailable))
            return;
    }
    m_packageModel->updatePackageSelection(row, checked);
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

    const QVariantMap pkg = findPackageAtProxyRow(index);
    if (pkg.isEmpty()) {
        qWarning() << "PackageManagerBackend::uninstall no row at proxy index" << index;
        return;
    }
    const QString name = pkg.value("moduleName").toString();
    if (name.isEmpty()) {
        qWarning() << "PackageManagerBackend::uninstall: row has no moduleName at proxy index" << index;
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
    const int row = findPackageRowAtProxyRow(index);
    if (row >= 0) requestVersionChange(row, UpgradeMode::Upgrade);
}

void PackageManagerBackend::downgradePackage(int index)
{
    const int row = findPackageRowAtProxyRow(index);
    if (row >= 0) requestVersionChange(row, UpgradeMode::Downgrade);
}

void PackageManagerBackend::sidegradePackage(int index)
{
    const int row = findPackageRowAtProxyRow(index);
    if (row >= 0) requestVersionChange(row, UpgradeMode::Sidegrade);
}

void PackageManagerBackend::setRowVersion(int index, int versionIndex)
{
    if (!m_packageModel) return;
    const int row = findPackageRowAtProxyRow(index);
    if (row >= 0) m_packageModel->setRowVersion(row, versionIndex);
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

    // Dep preview before the upgrade flow kicks off. If the resolver
    // surfaces transitive changes, the user picks via the dep-confirm
    // dialog BEFORE package_manager.requestUpgrade fires — keeps the
    // "should I install foo's new dep bar?" decision separate from the
    // later "should I unload the loaded dependents of foo?" cascade
    // dialog that beforeUpgrade triggers. dispatchPendingAction handles
    // the actual requestUpgrade call once the preview resolves; the
    // mode int and repoUrl ride through PendingUpgradeMeta keyed on
    // moduleName so onUpgradeUninstallDone can pick them up after the
    // package_manager round-trip.
    const QString name        = pkg.value("name").toString();
    const QString targetVersion = pkg.value("version").toString();
    const QString repoUrl     = pkg.value("repositoryUrl").toString();
    const int actionKind = (mode == UpgradeMode::Downgrade) ? PendingDepConfirm::Downgrade
                         : (mode == UpgradeMode::Sidegrade) ? PendingDepConfirm::Sidegrade
                         :                                    PendingDepConfirm::Upgrade;
    runDepPreviewForAction(name, moduleName, repoUrl, targetVersion, actionKind);
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

    // Fresh-install gate cancellation (host declined, or the ack timed out with
    // no host listening). "user cancelled" is filtered by the helper — no toast
    // when the user themselves clicked Cancel.
    subscribe("installCancelled",
        [](const QJsonObject& obj, const QString& reason) {
            return QStringLiteral("Install of '%1' cancelled: %2")
                       .arg(obj.value("name").toString(), reason);
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

void PackageManagerBackend::subscribePackageDownloaderEvents()
{
    if (!clientReady("package_downloader")) return;

    LogosModules logos(m_logosAPI);
    QPointer<PackageManagerBackend> self(this);
    logos.package_downloader.on("catalogChanged", [self](const QVariantList&) {
        if (!self) return;
        if (self->m_refreshDebounceTimer) self->m_refreshDebounceTimer->start();
        self->refreshRepositories();
    });
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

    // installApproved fires after the host confirms a fresh catalog install
    // through the gate. Payload: { name, releaseTag, repositoryUrl }. PMU runs
    // the actual download+install — the sibling of onUpgradeUninstallDone for
    // the no-old-version-to-remove case.
    logos.package_manager.on("installApproved",
        [self](const QVariantList& data) {
            if (!self) return;
            const QJsonObject obj = parseEventPayload(data);
            if (obj.isEmpty()) return;
            self->onInstallApproved(obj.value("name").toString(),
                                    obj.value("releaseTag").toString(),
                                    obj.value("repositoryUrl").toString());
        });
}

void PackageManagerBackend::onInstallApproved(const QString& name,
                                              const QString& releaseTag,
                                              const QString& repositoryUrl)
{
    // Host approved the gated install — run the real download+install. The
    // gate's whole job was the confirmation; deps are always included (the
    // host dialog is confirm-or-cancel, no "just the package" split).
    installSinglePackageAsync(name, repositoryUrl, releaseTag, /*includeDeps=*/true);
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

    // Download the new version via the dependency-resolving channel.
    // The resolver keys on the catalog name (displayName), not the
    // backend moduleName; we wrap the single name into the JSON-array
    // shape and unwrap the matching result entry below.
    //
    // `releaseTag` is the user's pinned target version — package_manager
    // captured it from requestUpgrade's `releaseTag` arg and re-emits
    // it in the upgradeUninstallDone payload. Forwarding it as the
    // `version` constraint in the dep entry is what makes Downgrade
    // actually downgrade: without this, lgpd's dependency resolver
    // picks the newest catalog version (so a Downgrade to 1.0.0 would
    // download 1.0.1 right back, exactly the symptom that started
    // this fix). An empty releaseTag means "any version" — lgpd
    // falls through to the latest, which is correct for the
    // bare-upgrade case (no pin requested).
    //
    // The repo pin + the user's dep-confirm choice were stashed in
    // dispatchPendingAction; drain them now. repositoryUrl scopes the
    // resolver to the row's source repo (two repos publishing the same
    // name + version would otherwise tie). includeDeps controls whether
    // transitive resolver picks get installed alongside the top-level
    // — false means the user chose "install just the package" from the
    // dep-confirm dialog and we drop transitive entries before handing
    // the results to the install loop. Missing entry = "no preview ran"
    // (older path or never-stashed); default to includeDeps=true so the
    // upgrade behaves like a normal install.
    const PendingUpgradeMeta meta = m_pendingUpgradeByModule.take(moduleName);
    PackageInstallSpec spec;
    spec.name          = displayName;
    spec.repositoryUrl = meta.repositoryUrl;  // empty = no pin (bare upgrade)
    spec.version       = releaseTag;          // empty = newest matching
    const QString depsJson = buildDepsJson({spec});

    LogosModules logos(m_logosAPI);
    QPointer<PackageManagerBackend> self(this);
    logos.package_downloader.downloadResolvedDependenciesAsync(depsJson, buildInstalledPackagesJson(),
        [self, displayName, mode, includeDeps = meta.includeDeps]
        (QVariantList results) {
            if (!self) return;
            // Filter to top-level entries when the user opted out of
            // deps. Without this, an upgrade with a new transitive dep
            // would install BOTH the dep and the package even when the
            // user explicitly clicked "just the package" — which would
            // also silently fix the pre-fix quirk where the upgrade
            // path only installed the top-level by accident.
            QVariantList toInstall;
            for (const QVariant& v : results) {
                const QVariantMap m = v.toMap();
                if (m.value("topLevel").toBool() || includeDeps || m.contains("error"))
                    toInstall.append(m);
            }
            if (toInstall.isEmpty() && !results.isEmpty())
                toInstall.append(results.last().toMap());

            // Up-front Installing for every dep row, same rationale as
            // the install path: visible state during the sequential
            // loop, not lazy per-entry transitions the user might
            // miss if any one finishes too fast to register.
            self->markEntriesInstalling(toInstall);
            self->installResultsSequential(toInstall, displayName, 0);
            // Refresh is driven by the corePluginFileInstalled event
            // package_manager emits per file, which arms the debounce
            // timer — same path the install flow uses. No explicit
            // refreshPackages() here so we don't race the sequential
            // loop's mid-flight model writes.
            Q_UNUSED(mode);
        }, Timeout(DOWNLOAD_TIMEOUT_MS));
}

// ── Repositories panel ─────────────────────────────────────────────────────
//
// Proxies the multi-repo API exposed by package_downloader. The QML panel
// binds to `repositories` (a QVariantList) and never speaks to the
// downloader module directly; that keeps the panel logic on one side of
// the QRO replica boundary.

void PackageManagerBackend::refreshRepositories()
{
    if (!m_logosAPI || !m_logosAPI->getClient("package_downloader")->isConnected()) {
        setRepositories(QVariantList{});
        setRepositoriesLoading(false);
        return;
    }
    setRepositoriesLoading(true);
    LogosModules logos(m_logosAPI);
    QPointer<PackageManagerBackend> self(this);
    logos.package_downloader.listRepositoriesAsync(
        [self](QVariantList list) {
            if (!self) return;
            self->setRepositories(list);
            self->setRepositoriesLoading(false);
        });
}

void PackageManagerBackend::addRepository(QString url)
{
    if (!m_logosAPI || !m_logosAPI->getClient("package_downloader")->isConnected()) {
        emit repositoryOperationCompleted(
            QStringLiteral("add"), url, false,
            QStringLiteral("package_downloader is not connected"));
        return;
    }
    LogosModules logos(m_logosAPI);
    QPointer<PackageManagerBackend> self(this);
    const QString u = url;  // capture by value
    logos.package_downloader.addRepositoryAsync(
        u, [self, u](QVariantMap r) {
            if (!self) return;
            const bool ok  = r.value(QStringLiteral("success")).toBool();
            const QString err = r.value(QStringLiteral("error")).toString();
            emit self->repositoryOperationCompleted(
                QStringLiteral("add"), u, ok, err);
        });
}

void PackageManagerBackend::removeRepository(QString url)
{
    if (!m_logosAPI || !m_logosAPI->getClient("package_downloader")->isConnected()) {
        emit repositoryOperationCompleted(
            QStringLiteral("remove"), url, false,
            QStringLiteral("package_downloader is not connected"));
        return;
    }
    LogosModules logos(m_logosAPI);
    QPointer<PackageManagerBackend> self(this);
    const QString u = url;
    logos.package_downloader.removeRepositoryAsync(
        u, [self, u](QVariantMap r) {
            if (!self) return;
            const bool ok  = r.value(QStringLiteral("success")).toBool();
            const QString err = r.value(QStringLiteral("error")).toString();
            emit self->repositoryOperationCompleted(
                QStringLiteral("remove"), u, ok, err);
        });
}

void PackageManagerBackend::setRepositoryEnabled(QString url, bool enabled)
{
    if (!m_logosAPI || !m_logosAPI->getClient("package_downloader")->isConnected()) {
        emit repositoryOperationCompleted(
            QStringLiteral("setEnabled"), url, false,
            QStringLiteral("package_downloader is not connected"));
        return;
    }
    LogosModules logos(m_logosAPI);
    QPointer<PackageManagerBackend> self(this);
    const QString u = url;
    logos.package_downloader.setRepositoryEnabledAsync(
        u, enabled, [self, u](QVariantMap r) {
            if (!self) return;
            const bool ok  = r.value(QStringLiteral("success")).toBool();
            const QString err = r.value(QStringLiteral("error")).toString();
            emit self->repositoryOperationCompleted(
                QStringLiteral("setEnabled"), u, ok, err);
        });
}

QString PackageManagerBackend::displayNameForModule(QString moduleName)
{
    // Thin delegate to the local model. Needs to live here (not on the model
    // directly as a Q_INVOKABLE) because QAbstractItemModel replicas proxy
    // only the QAbstractItemModel interface — Q_INVOKABLE methods on the
    // concrete subclass don't cross the wire. Slots declared in the .rep do.
    if (!m_packageModel) return moduleName;
    const QString display = m_packageModel->displayNameForModule(moduleName);
    return display.isEmpty() ? moduleName : display;
}
