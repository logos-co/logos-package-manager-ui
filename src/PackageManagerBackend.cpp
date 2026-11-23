#include "PackageManagerBackend.h"
#include "InstallSequence.h"
#include <algorithm>
#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkInformation>
#include <QPointer>
#include <QSet>
#include <QTemporaryDir>
#include <QTimer>
#include <QUuid>
#include <QVariant>
#include "logos_sdk.h"
#include "PackageRowBuilder.h"   // buildPackageRow / buildLocalPackageRow
#include "RowActionResolver.h"   // versionCmp + resolveRowAction (shared with PackageListModel)

constexpr int DOWNLOAD_TIMEOUT_MS = 300000; // 5 minutes

QVariantList PackageManagerBackend::requestedRows(const QVariantMap& approval,
                                                 const QString& subject) const
{
    return install_sequence::stillToInstall(approval.value(QStringLiteral("plan")).toList(),
                                            m_installedPackagesCache, subject);
}

QString PackageManagerBackend::requestsJson(const QVariantList& requested)
{
    // QJsonDocument escapes what a repository URL may hold.
    const QJsonArray requests =
        QJsonArray::fromVariantList(install_sequence::approvedRequests(requested));
    return QString::fromUtf8(QJsonDocument(requests).toJson(QJsonDocument::Compact));
}

void PackageManagerBackend::holdToApproval(const QVariantList& rows, const QVariantMap& approval,
                                           const QVariantList& requested,
                                           std::function<void(const QString& refused)> then)
{
    // All of it, or none: nothing installs if a required package did not come.
    const QVariantList plan = approval.value(QStringLiteral("plan")).toList();
    QString why = install_sequence::incomplete(rows, requested);
    if (why.isEmpty()) why = install_sequence::outsidePlan(rows, plan);
    if (!why.isEmpty()) {
        then(why);
        return;
    }
    checkSigners(rows, plan, std::move(then));
}

void PackageManagerBackend::checkSigners(const QVariantList& rows, const QVariantList& plan,
                                         std::function<void(const QString& refused)> then, int index)
{
    while (index < rows.size()
           && rows.at(index).toMap().value(QStringLiteral("path")).toString().isEmpty())
        ++index;
    if (index >= rows.size()) {
        then({});
        return;
    }
    // The download picks among a release's catalogue entries by root hash
    // alone, and the root hash does not cover the manifest or its signature.
    constexpr int kVerifyIpcDeadlineMs = 2 * 60 * 1000;
    const QVariantMap row = rows.at(index).toMap();
    QPointer<PackageManagerBackend> self(this);
    modules().package_manager.verifyPackageAsync(row.value(QStringLiteral("path")).toString(),
        [self, rows, plan, then, index, row](QVariantMap verified) {
            if (!self) return;
            const QString why = install_sequence::signerChanged(verified, plan, row);
            if (!why.isEmpty()) {
                then(why);
                return;
            }
            self->checkSigners(rows, plan, then, index + 1);
        }, Timeout(kVerifyIpcDeadlineMs));
}

void PackageManagerBackend::checkLocalFile(const QString& path, const QVariantMap& approval,
                                           const QString& name, const QString& version,
                                           std::function<void(const QString& changed)> then)
{
    const QString sha256 = install_sequence::sha256Of(path);
    modules().package_manager.inspectPackageAsync(path,
        [then, name, version, sha256, approved = approval.value(QStringLiteral("file")).toMap()]
        (QVariantMap inspected) {
            then(install_sequence::fileChanged(inspected, approved, name, version, sha256));
        });
}

PackageManagerBackend::PackageManagerBackend(QObject* parent)
    : PackageManagerUiSimpleSource(parent)
    , m_packageModel(new PackageListModel(this))
    , m_packagesFilterProxy(new PackagesFilterProxy(this))
    , m_packagesPagingProxy(new PackagesPagingProxy(this))
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
    setShowUnavailable(false);
    setPageSize(20);
    setCurrentPage(1);
    setSortRole(QString());
    setSortOrder(Qt::AscendingOrder);
    setTotalCount(0);
    setRepositoryCount(0);

    {
        QVariantMap ids;
        const auto rn = m_packageModel->roleNames();
        for (auto it = rn.cbegin(); it != rn.cend(); ++it)
            ids.insert(QString::fromUtf8(it.value()), int(it.key()));
        setPackageRoleIds(ids);
    }

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
    connect(this, &PackageManagerUiSimpleSource::showUnavailableChanged,
            this, [this]() { m_packagesFilterProxy->setShowUnavailable(showUnavailable()); });
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

    // Category / type changes are pure client-side proxy filters over the
    // cached full catalog — no network round-trip and no model rebuild.
    // Routed through the filter-apply debounce so a click feels instant in
    // QML (the click handler returns immediately) and rapid clicks coalesce
    // into a single apply pass.
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
        // Apply only what's actually pending. Reset the flags first so a
        // re-entrant signal during apply* doesn't cause a double-fire.
        const bool catPending  = m_categoryFilterPending;
        const bool typePending = m_typeFilterPending;
        m_categoryFilterPending = false;
        m_typeFilterPending     = false;
        if (catPending)  applyCategoryFilter();
        if (typePending) applyTypeFilter();
    });
}

void PackageManagerBackend::onContextReady()
{
    // modules() is live from here on. The dependency clients still need a
    // moment to connect, so the setup below keeps its existing retry loop —
    // this hook only replaces the constructor's singleShot kick-off, which
    // could otherwise fire before the framework wired the typed deps.
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
    watchNetwork();

    // package_downloader does nothing until a consumer starts it. Everything
    // below calls into it, so it waits for the answer, whatever it is: one
    // that predates start() refuses the method and is running already.
    LogosModules& logos = modules();
    QPointer<PackageManagerBackend> self(this);
    logos.package_downloader.startAsync([self](QVariantMap r) {
        if (!self) return;
        if (!r.value(QStringLiteral("success")).toBool())
            qWarning() << "package_downloader.start did not succeed:"
                       << r.value(QStringLiteral("error")).toString();

        self->subscribePackageManagerRefreshEvents();
        self->subscribePackageDownloaderEvents();
        self->refreshDownloadStatus();

        self->refreshCatalog();
    });
}


// ──────────────────── connection-readiness predicates ────────────────────

bool PackageManagerBackend::clientReady(const char* moduleName) const
{
    // Guard on the context: modules() is only valid once the generated glue
    // has wired it (isContextReady()), and `api` is the LogosAPI the glue
    // built the typed wrappers from — the same one their clients live on.
    if (!isContextReady()) return false;
    LogosAPIClient* c = modules().api->getClient(moduleName);
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
    LogosModules& logos = modules();
    QPointer<PackageManagerBackend> self(this);
    logos.package_downloader.refreshCatalogAsync([self](QVariantMap r) {
        if (!self) return;
        const QString err = r.value(QStringLiteral("error")).toString();
        if (!err.isEmpty())
            qWarning() << "package_downloader.refreshCatalog reported:" << err;
        self->refreshPackages();
    });
}

void PackageManagerBackend::installLocalPackage(QUrl fileUrl)
{
    if (!fileUrl.isValid() || !fileUrl.isLocalFile()) {
        qWarning() << "installLocalPackage: not a local file URL:" << fileUrl;
        emit errorOccurred(static_cast<int>(PackageTypes::LocalPackageInvalid));
        return;
    }

    const QString filePath = fileUrl.toLocalFile();
    const QFileInfo fi(filePath);
    if (!fi.isFile()
        || fi.suffix().compare(QStringLiteral("lgx"), Qt::CaseInsensitive) != 0) {
        qWarning() << "installLocalPackage: not an .lgx file:" << filePath;
        emit errorOccurred(static_cast<int>(PackageTypes::LocalPackageInvalid));
        return;
    }

    if (!packageManagerReady()) {
        emit errorOccurred(static_cast<int>(PackageTypes::PackageManagerNotConnected));
        return;
    }

    // A copy only this user can change: it is what the host checks and what
    // is installed, while the picked file stays free to change.
    const QString fileLabel = fi.fileName();
    const QString staged = stageLocalFile(filePath);
    if (staged.isEmpty()) {
        emit installationProgressUpdated(static_cast<int>(PackageTypes::ProgressFailed), fileLabel,
                                         0, 0, false,
                                         QStringLiteral("Could not copy %1 to install it").arg(fileLabel));
        return;
    }

    // Inspect first: a local file's name/version only exist inside its
    // manifest, and this is also what picks install vs upgrade below.
    LogosModules& logos = modules();
    QPointer<PackageManagerBackend> self(this);
    logos.package_manager.inspectPackageAsync(staged,
        [self, filePath = staged, fileLabel](QVariantMap info) {
            if (!self) return;

            const QString err  = info.value(QStringLiteral("error")).toString();
            const QString name = info.value(QStringLiteral("name")).toString();
            if (!err.isEmpty() || name.isEmpty()) {
                const QString msg = !err.isEmpty()
                    ? err
                    : QStringLiteral("Could not read a package name from %1").arg(fileLabel);
                qWarning() << "installLocalPackage: inspect failed for" << filePath << ":" << msg;
                self->dropStaged(filePath);
                emit self->installationProgressUpdated(
                    static_cast<int>(PackageTypes::ProgressFailed),
                    fileLabel, 0, 0, false, msg);
                return;
            }

            const QString version = info.value(QStringLiteral("version")).toString();
            const bool alreadyInstalled =
                info.value(QStringLiteral("isAlreadyInstalled")).toBool();

            self->forgetLocal(name);
            self->m_pendingLocalInstalls.insert(name, filePath);

            if (!alreadyInstalled) {
                emit self->localPackageInspected(name, version, 0, /*isUpgrade=*/false, filePath);
                return;
            }

            // mode mirrors the per-row dispatch: 0 upgrade / 1 downgrade /
            // 2 sidegrade (same version — i.e. a reinstall from disk).
            const QString installedVersion =
                info.value(QStringLiteral("installedVersion")).toString();
            const int cmp  = rowaction::versionCmp(version, installedVersion);
            const int mode = (cmp > 0) ? 0 : (cmp < 0) ? 1 : 2;

            emit self->localPackageInspected(name, version, mode, /*isUpgrade=*/true, filePath);
        });
}

QString PackageManagerBackend::stageLocalFile(const QString& path)
{
    if (!m_stagingDir)   // 0700, removed with the backend
        m_stagingDir = std::make_unique<QTemporaryDir>(QDir::tempPath() + QStringLiteral("/pmui-XXXXXX"));
    if (!m_stagingDir->isValid()) return {};
    // A name of its own: two picks of one file are two copies.
    const QString staged = m_stagingDir->filePath(QUuid::createUuid().toString(QUuid::WithoutBraces)
                                                  + QStringLiteral(".lgx"));
    if (!QFile::copy(path, staged)) return {};
    QFile::setPermissions(staged, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    return staged;
}

void PackageManagerBackend::dropStaged(const QString& path)
{
    if (m_stagingDir && !path.isEmpty() && QFileInfo(path).absolutePath() == m_stagingDir->path())
        QFile::remove(path);
}

void PackageManagerBackend::forgetLocal(const QString& name)
{
    if (m_pendingLocalInstalls.contains(name))
        dropStaged(m_pendingLocalInstalls.take(name));
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

    // Repository list first: grouping needs each repo's identity before it
    // can lay out the catalog rows, and it feeds repositoryCount, which
    // gates the empty state.
    LogosModules& logos = modules();
    QPointer<PackageManagerBackend> self(this);
    logos.package_downloader.listRepositoriesAsync(
        [self, currentGeneration](QVariantList repos) {
            if (!self || self->m_reloadGeneration != currentGeneration) return;
            self->applyRepositoryList(repos);
            self->loadCatalog(currentGeneration);
        });
}

void PackageManagerBackend::loadCatalog(int currentGeneration)
{
    // One round-trip for the catalog (union across every enabled
    // repository); the Categories and Types lists are derived from it
    // client-side so subsequent clicks only update the proxy filter — no
    // network round-trip and no model rebuild.
    LogosModules& logos = modules();
    QPointer<PackageManagerBackend> self(this);
    logos.package_downloader.getCatalogAsync([self, currentGeneration](QVariantList packagesArray) {
        if (!self || self->m_reloadGeneration != currentGeneration) return;

        LogosModules& logos2 = self->modules();
        logos2.package_manager.getInstalledPackagesAsync([self, currentGeneration, packagesArray](QVariantList installedPackages) {
            if (!self || self->m_reloadGeneration != currentGeneration) return;

            LogosModules& logos3 = self->modules();
            logos3.package_manager.getValidVariantsAsync([self, currentGeneration, packagesArray, installedPackages](QVariant result) {
                if (!self || self->m_reloadGeneration != currentGeneration) return;
                QStringList validVariants = result.toStringList();
                self->setHostVariant(validVariants.value(0));
                self->m_allPackagesCache = packagesArray;
                self->m_installedPackagesCache = installedPackages;
                self->m_validVariantsCache = validVariants;
                self->setPackagesFromVariantList(self->m_allPackagesCache,
                                                 self->m_installedPackagesCache,
                                                 self->m_validVariantsCache);
                self->applyCategoryFilter();
                self->setIsLoading(false);
            });
        });
    });
}

static QString repositoryLabelFor(const QVariantMap& r,
                                  int sharingName, int sharingOwner)
{
    // Trimmed: a trailing space is invisible on screen but would make two
    // otherwise identical names render at different widths, and it has to
    // match the trimming the collision keys below do.
    QString label = r.value("displayName").toString().trimmed();
    if (label.isEmpty()) label = r.value("name").toString().trimmed();

    const QString owner = r.value("sourceOwner").toString();
    const QString repo  = r.value("sourceRepo").toString();
    const QString host  = r.value("sourceHost").toString();
    // Off GitHub there is no owner segment, so the host identifies it.
    // Both are absent entirely against a downloader older than the
    // source* fields — fall back to the URL, which every entry has.
    // Qualifying with an empty string renders "Logos Official ()", which
    // is worse than not qualifying: it looks broken AND still fails to
    // tell the two repos apart.
    QString shortSource = owner.isEmpty() ? host : owner;
    QString longSource  = (owner.isEmpty() || repo.isEmpty())
                              ? shortSource
                              : owner + QLatin1Char('/') + repo;
    if (shortSource.isEmpty()) shortSource = r.value("url").toString();
    if (longSource.isEmpty())  longSource  = shortSource;

    if (label.isEmpty()) return longSource;   // nothing to qualify
    if (sharingName <= 1) return label;
    if (shortSource.isEmpty()) return label;  // nothing to qualify WITH
    return QStringLiteral("%1 (%2)").arg(label,
                                         sharingOwner > 1 ? longSource : shortSource);
}

// Repository identity for the grouping pass, keyed by URL — the registry's
// own key and the only field a remote can't choose. `repositoryLabels`
// carries URL → header label over to QML, whose ListView sections group on
// the key alone and so can't reach any other field.
void PackageManagerBackend::applyRepositoryList(const QVariantList& repos)
{
    m_defaultRepositoryUrl.clear();

    // Two passes: a label depends on how many OTHER repos contest the name,
    // so the counts must all exist before any label is built.
    // Comparison keys only — the rendered label keeps the repo's own
    // spelling. Trimmed and case-folded so neither a stray space nor a
    // different capitalisation of the same GitHub account (which GitHub
    // treats as one account) can slip past the collision check and leave
    // two repos rendering the same bare name.
    auto nameOf  = [](const QVariantMap& r) {
        return r.value("displayName").toString().trimmed().toLower();
    };
    auto pairOf = [&](const QVariantMap& r) {
        return nameOf(r) + QChar(0x01)
             + r.value("sourceOwner").toString().trimmed().toLower();
    };
    QHash<QString, int> byName, byNameAndOwner;
    for (const QVariant& v : repos) {
        const QVariantMap r = v.toMap();
        byName[nameOf(r)]++;
        byNameAndOwner[pairOf(r)]++;
    }

    QVariantMap labels;
    for (const QVariant& v : repos) {
        const QVariantMap r = v.toMap();
        const QString url = r.value("url").toString();
        if (url.isEmpty()) continue;
        if (r.value("isDefault").toBool()) m_defaultRepositoryUrl = url;
        labels.insert(url, repositoryLabelFor(r, byName.value(nameOf(r)),
                                              byNameAndOwner.value(pairOf(r))));
    }
    // Lowercase deliberately: basecamp's app manager renders the same
    // synthetic bucket, and its tests pin that spelling. Keeping the two
    // surfaces identical is the point of this whole labelling path.
    labels.insert(QStringLiteral("local"), tr("local"));
    setRepositoryLabels(labels);
    setRepositoryCount(repos.size());
}

void PackageManagerBackend::applyCategoryFilter()
{
    if (!m_packagesFilterProxy) return;
    const QString selected =
        categories().value(selectedCategoryIndex(), QStringLiteral("All"));
    QString categoryFilter;
    if (!selected.isEmpty()
        && selected.compare(QStringLiteral("All"), Qt::CaseInsensitive) != 0) {
        categoryFilter = selected;
    }
    m_packagesFilterProxy->setCategoryFilter(categoryFilter);
}

void PackageManagerBackend::recomputeCategories(const QList<QVariantMap>& rows)
{
    QStringList categoryList;
    categoryList << QStringLiteral("All");
    QStringList seen;
    for (const QVariantMap& row : rows) {
        if (rowaction::isUnavailableOnThisPlatform(row)) continue;
        QString c = row.value(QStringLiteral("category")).toString();
        if (c.isEmpty()) continue;
        c[0] = c[0].toUpper();
        if (!seen.contains(c)) seen.append(c);
    }
    std::sort(seen.begin(), seen.end());
    categoryList.append(seen);
    setCategories(categoryList);
}

void PackageManagerBackend::recomputeAvailableTypes(const QList<QVariantMap>& rows)
{
    // "All" is always at index 0; real types follow alphabetically. Using a
    // QSet to dedupe keeps this O(N) over the rows; the final sort is over
    // the small set of distinct types (typically 2-4 entries).
    QSet<QString> distinct;
    for (const QVariantMap& row : rows) {
        if (rowaction::isUnavailableOnThisPlatform(row)) continue;
        const QString t = row.value("type").toString();
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
    packages.reserve(packagesArray.size() + installedPackages.size());
    QSet<QString> catalogModuleNames;
    catalogModuleNames.reserve(packagesArray.size());
    for (const QVariant& value : packagesArray) {
        const QVariantMap row = packagerow::buildPackageRow(
            value.toMap(), installedByName, validVariants);
        catalogModuleNames.insert(row.value("moduleName").toString());
        packages.append(row);
    }

    // Any USER-installed package the catalog doesn't publish gets a
    // synthetic "Local"-repo row so it still shows up in the grouped list.
    // Embedded packages ship inside the app bundle — they're already
    // discoverable through the built-in module surface, so listing them
    // under Local would double-count and mislead.
    for (const QVariant& val : installedPackages) {
        const QVariantMap inst = val.toMap();
        const QString name = inst.value("name").toString();
        if (name.isEmpty()) continue;
        if (inst.value("installType").toString() != QLatin1String("user")) continue;
        QString moduleName = inst.value("moduleName").toString();
        if (moduleName.isEmpty()) moduleName = name;
        if (catalogModuleNames.contains(moduleName)) continue;
        packages.append(packagerow::buildLocalPackageRow(inst));
    }

    // Group rows by source. The key is the repo's logos-repo.json URL —
    // unique, unlike the manifest's name/displayName, which a repo that
    // copies another's manifest shares (and one in the wild does).
    // Order: built-in repo (isDefault, from the registry — never a name
    // match, which any repo could spoof), then user repos by display
    // name, then the synthetic "local" bucket.
    const QString localKey = QStringLiteral("local");
    auto sourceKey = [&](const QVariantMap& row) -> QString {
        const QString url = row.value("repositoryUrl").toString();
        if (!url.isEmpty()) return url;
        // No URL: a local row, or a repo whose manifest never resolved —
        // key those by name so they still get separate sections.
        const QString n = row.value("repositoryName").toString();
        return n.isEmpty() ? localKey : n;
    };
    auto sourcePriority = [&](const QVariantMap& row) -> int {
        const QString key = sourceKey(row);
        if (key == localKey) return 2;
        return (!m_defaultRepositoryUrl.isEmpty() && key == m_defaultRepositoryUrl)
                   ? 0 : 1;
    };
    std::stable_sort(packages.begin(), packages.end(),
        [&](const QVariantMap& a, const QVariantMap& b) {
            const int pa = sourcePriority(a);
            const int pb = sourcePriority(b);
            if (pa != pb) return pa < pb;
            const int c = a.value("repositoryDisplayName").toString().compare(
                b.value("repositoryDisplayName").toString(), Qt::CaseInsensitive);
            if (c != 0) return c < 0;
            // Same display name, different repo: keep each one's rows in
            // a contiguous run instead of interleaving them by package
            // name under a single header.
            const int ck = sourceKey(a).compare(sourceKey(b), Qt::CaseInsensitive);
            if (ck != 0) return ck < 0;
            return a.value("name").toString().compare(
                b.value("name").toString(), Qt::CaseInsensitive) < 0;
        });

    // Stamp each row's key, and collect the keys in the order their
    // groups appear so the proxy can hold that order across a re-sort.
    QStringList sourceOrder;
    for (QVariantMap& row : packages) {
        const QString k = sourceKey(row);
        row["sourceKey"] = k;
        if (sourceOrder.isEmpty() || sourceOrder.last() != k)
            sourceOrder.append(k);
    }
    m_packagesFilterProxy->setSourceOrder(sourceOrder);

    // Facets before the model: both narrow the sidebar to what the rows can
    // actually offer, and recomputeAvailableTypes ends by re-applying the
    // resolved type filter to the proxy.
    recomputeCategories(packages);
    recomputeAvailableTypes(packages);

    int availableHere = 0;
    for (const QVariantMap& row : packages) {
        if (!rowaction::isUnavailableOnThisPlatform(row)) ++availableHere;
    }
    setAvailableHereCount(availableHere);

    // setPackages emits hasSelectionChanged; the connected slot
    // (refreshActionSummary) rebuilds the bulk action plan and pushes
    // `runnableActionCount` + `actionSummary` to the .rep PROPs.
    m_packageModel->setPackages(packages);
}

void PackageManagerBackend::installOnePackage(const QVariantMap& dl,
                                               std::function<void(bool success, const QString& error)> onDone)
{
    QString packageName = dl.value("name").toString();
    QString filePath = dl.value("path").toString();
    QString downloadError = dl.value("error").toString();
    const QString expectedVersion = dl.value("version").toString();
    const QString expectedHash = dl.value("rootHash").toString();
    const QString source = dl.value("source").toString();

    if (filePath.isEmpty()) {
        qWarning() << "Download failed for" << packageName << ":" << downloadError;
        if (onDone) onDone(false, downloadError.isEmpty()
                                       ? QStringLiteral("Download failed")
                                       : downloadError);
        return;
    }

    qDebug() << "Download complete for" << packageName << "at" << filePath << "— installing...";
    LogosModules& logos = modules();
    QPointer<PackageManagerBackend> self(this);
    // Installing was left on the default 20 s IPC deadline while DOWNLOADING
    // gets five minutes -- backwards. Downloading is network-bound and can be
    // retried; installing is disk-bound over a payload package_manager reads,
    // gunzips and tar-parses three times and Merkle-hashes twice, then extracts
    // and copies. Blowing the deadline does not cancel any of that: the files
    // still install and only the reply is abandoned, so the user is told the
    // install failed when it did not.
    constexpr int kInstallIpcDeadlineMs = 5 * 60 * 1000;
    // `installPluginAsyncResult`, not `installPluginAsync`: the plain async
    // wrapper hands the callback a bare QVariantMap, so a transport failure is
    // indistinguishable from a provider that legitimately returned an empty
    // one. AsyncResult<T> carries the value and the error together.
    logos.package_manager.installPluginAsyncResult(filePath, false, source,
        [self, filePath, packageName, expectedVersion, expectedHash, onDone](logos::AsyncResult<QVariantMap> r) {
            if (!self) return;
            // A staged copy of a picked file has done its job either way.
            self->dropStaged(filePath);
            // Transport-level failure FIRST. On a timeout `value` is
            // default-constructed, so reading it as an install verdict is the
            // exact mistake this channel removes -- and the honest thing to
            // tell the user is that the deadline expired, not that the install
            // failed: blowing it cancels nothing that was already underway.
            if (!r.ok()) {
                qWarning() << "installPlugin failed for" << packageName << ":"
                           << QString::fromStdString(r.error.code)
                           << QString::fromStdString(r.error.message);
                const QString detail = QString::fromStdString(r.error.message);
                if (onDone) onDone(false,
                    QStringLiteral("%1 — the package may in fact be installed; check before retrying")
                        .arg(detail.isEmpty()
                                 ? QStringLiteral("package_manager did not reply")
                                 : detail));
                return;
            }
            const QVariantMap installResult = r.value;
            // Success is the ABSENCE of an "error" key, not the presence of a
            // non-empty "path". package_manager reports a QML-only ui_qml
            // package's install location as the installed module directory,
            // but an older package_manager left "path" empty for those (they
            // ship no backend library) — which this read as failure and drew a
            // red RETRY on a package that had installed perfectly.
            // `contains("path")` is still required so that a malformed reply
            // does not read as success: every real installPlugin reply carries
            // the key, even when its value is empty. (A DROPPED reply no longer
            // reaches here at all — r.ok() catches it above.)
            bool success = installResult.contains("path")
                        && !installResult.contains("error");
            QString err = installResult.value("error").toString();
            if (!success) {
                if (onDone) onDone(false, err.isEmpty()
                                              ? QStringLiteral("Installation failed")
                                              : err);
                return;
            }
            // Reported success. That is the installer's word for it — check
            // the disk before the row is allowed to claim the new version.
            self->verifyInstalledArtifact(packageName, expectedVersion,
                                          expectedHash, onDone);
        },
        Timeout(kInstallIpcDeadlineMs));
}

void PackageManagerBackend::verifyInstalledArtifact(
        const QString& packageName,
        const QString& expectedVersion,
        const QString& expectedHash,
        std::function<void(bool success, const QString& error)> onDone)
{
    if (expectedVersion.isEmpty() || !packageManagerReady()) {
        if (onDone) onDone(true, QString());
        return;
    }

    constexpr int kVerifyIpcDeadlineMs = 60 * 1000;

    const QString diskKey = m_packageModel
        ? m_packageModel->moduleNameForPackage(packageName)
        : packageName;

    LogosModules& logos = modules();
    QPointer<PackageManagerBackend> self(this);
    logos.package_manager.getInstalledPackagesAsyncResult(
        [self, packageName, diskKey, expectedVersion, expectedHash, onDone]
        (logos::AsyncResult<QVariantList> r) {
            if (!self) return;
            if (!r.ok()) {
                // Couldn't read the disk. Say so in the log and pass the
                // install through — see the fail-open note on the decl.
                qWarning() << "post-install verification skipped for" << packageName
                           << "— could not read the installed set:"
                           << QString::fromStdString(r.error.message);
                if (onDone) onDone(true, QString());
                return;
            }

            QVariantMap found;
            for (const QVariant& v : r.value) {
                const QVariantMap inst = v.toMap();
                const QString name = inst.value("name").toString();
                if (name == diskKey || name == packageName) {
                    found = inst;
                    break;
                }
            }

            if (found.isEmpty()) {
                if (onDone) onDone(false,
                    QStringLiteral("The installer reported success, but %1 is not "
                                   "installed on disk").arg(packageName));
                return;
            }

            const QString onDisk = found.value("version").toString();
            if (!onDisk.isEmpty()
                && rowaction::versionCmp(onDisk, expectedVersion) != 0) {
                if (onDone) onDone(false,
                    QStringLiteral("The installer reported success, but v%1 is still "
                                   "on disk — v%2 was not written. Uninstall %3 and "
                                   "install it again.")
                        .arg(onDisk, expectedVersion, packageName));
                return;
            }

            const QString onDiskHash =
                found.value("hashes").toMap().value("root").toString();
            if (!expectedHash.isEmpty() && !onDiskHash.isEmpty()
                && onDiskHash != expectedHash) {
                qWarning() << "post-install hash mismatch for" << packageName
                           << "at v" << onDisk << "— on disk" << onDiskHash
                           << "expected" << expectedHash;
            }

            if (onDone) onDone(true, QString());
        }, Timeout(kVerifyIpcDeadlineMs));
}

void PackageManagerBackend::runSelectedActions()
{
    // Unreachable: the bulk surface (checkbox column + "Run Actions") is off in
    // QML. It also cannot work as written any more — confirmation is raised by
    // QML now, and a backend slot has no way to ask for one. Reviving bulk
    // means building the plan in QML and raising ONE combined confirm intent
    // for the batch, not restoring this.
    qWarning() << "runSelectedActions: bulk actions are not wired";
    emit errorOccurred(static_cast<int>(PackageTypes::NoPackagesSelected));
}


// Two installs at once resolved a shared dependency twice: the one still
// downloading was not on disk yet, so the second fetched and installed it
// again, and both downloads drew on the same row. So they queue instead.
void PackageManagerBackend::installEnded()
{
    if (m_queuedInstalls.isEmpty()) {
        setIsInstalling(false);
        return;
    }

    // The lock stays held until the next starts, so nothing overtakes it.
    QPointer<PackageManagerBackend> self(this);
    auto startNext = [self]() {
        if (!self) return;
        self->setIsInstalling(false);
        while (self && !self->isInstalling() && !self->m_queuedInstalls.isEmpty())
            self->m_queuedInstalls.takeFirst()();
    };

    if (!packageManagerReady()) {
        startNext();
        return;
    }
    modules().package_manager.getInstalledPackagesAsync(
        [self, startNext](QVariantList installed) {
            if (!self) return;
            self->m_installedPackagesCache = installed;
            startNext();
        });
}

void PackageManagerBackend::installSinglePackageAsync(const QString& packageName,
                                                       const QString& repoUrl,
                                                       const QVariantMap& approval)
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
    // against double-click while the download is in flight. Scoped to the
    // clicked row's repo — another repo publishing the same name is not
    // installing anything.
    setIsInstalling(true);

    m_packageModel->updateRowInstallation(
        packageName, repoUrl, static_cast<int>(PackageTypes::Installing));

    emit installationProgressUpdated(
        static_cast<int>(PackageTypes::Started), packageName, 0, 1, true, "");

    // Drive the download through the dep-resolving channel: the resolver
    // returns deps-first, then the package, and all of it is installed.
    const QVariantList requested = requestedRows(approval, packageName);
    const QString depsJson = requestsJson(requested);
    LogosModules& logos = modules();
    QPointer<PackageManagerBackend> self(this);
    logos.package_downloader.downloadResolvedDependenciesAsync(depsJson, buildInstalledPackagesJson(),
        [self, packageName, approval, requested](QVariantList downloaded) {
            if (!self) return;
            downloaded = install_sequence::tagOptionalRows(
                downloaded, approval.value(QStringLiteral("optionalPackages")).toList());
            self->holdToApproval(downloaded, approval, requested,
                [self, packageName, approval, downloaded](const QString& refused) {
                    if (!self) return;
                    const QVariantList plan = approval.value(QStringLiteral("plan")).toList();
                    QVariantList results = refused.isEmpty()
                        ? downloaded
                        : install_sequence::failedRow(packageName, plan, refused);
                    // So the row clicked never stays Installing.
                    if (results.isEmpty())
                        results = install_sequence::failedRow(packageName, plan,
                            QStringLiteral("Nothing approved was fetched, so nothing was installed."));
                    // Every row Installing at once, not one by one as the loop
                    // reaches it; an error row is skipped and fails in the loop.
                    self->markEntriesInstalling(results);
                    self->installResultsSequential(results, packageName, 0);
                });
        }, Timeout(DOWNLOAD_TIMEOUT_MS));
}

void PackageManagerBackend::installResultsSequential(const QVariantList& results,
                                                     const QString& topLevelName,
                                                     int index)
{
    // Per-row install pipeline. Every path into it holds the install lock,
    // given up (installEnded) when the last entry is done or a failure stops
    // the batch. We emit progress events keyed on the top-level name so the UI
    // shows the user's clicked row as the one being acted on, even when the
    // loop is iterating transitive deps in between.

    // Nothing to install (a download that answered nothing): the loop below
    // would never report back, and the lock would stay taken for good.
    if (index >= results.size()) {
        installEnded();
        return;
    }

    QPointer<PackageManagerBackend> self(this);
    install_sequence::installSequentially(results,
        [self](const QVariantMap& row, install_sequence::Done done) {
            if (self) self->installOnePackage(row, done);
        },
        [self, results, topLevelName](int index, bool success, bool proceed, const QString& err) {
            if (!self) return;
            const QVariantMap dl = results[index].toMap();
            const QString depName = dl.value("name").toString();
            if (success) {
                self->m_packageModel->updatePackageInstallation(
                    depName, static_cast<int>(PackageTypes::Installed));
            } else {
                // The failing entry's row takes the Failed status. Every entry
                // was marked Installing up front, so when the batch stops here
                // the rest revert to NotInstalled rather than stay Installing.
                self->m_packageModel->updateRowInstallation(
                    depName, dl.value("repositoryUrl").toString(),
                    static_cast<int>(PackageTypes::Failed), err);
                if (!proceed) self->revertPendingEntries(results, index + 1);
            }
            const bool isLast = (index + 1) >= results.size();
            if (isLast || !proceed) self->installEnded();
            emit self->installationProgressUpdated(
                proceed ? (isLast ? static_cast<int>(PackageTypes::Completed)
                                  : static_cast<int>(PackageTypes::InProgress))
                        : static_cast<int>(PackageTypes::ProgressFailed),
                topLevelName, index + 1, results.size(), proceed,
                success ? QString() : err);
        }, index);
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
        m_packageModel->updateRowInstallation(
            name, m.value("repositoryUrl").toString(),
            static_cast<int>(PackageTypes::Installing));
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




void PackageManagerBackend::performInstall(QString name, QString version,
                                           QString repositoryUrl, QVariantMap approval)
{
    // Refused before it can queue: an approval that holds nothing never waits
    // its turn. A queued install comes back here when its turn comes.
    const QString refused =
        install_sequence::refusal(approval, m_pendingLocalInstalls.contains(name), name);
    if (!refused.isEmpty()) {
        m_packageModel->updateRowInstallation(name, repositoryUrl,
                                              static_cast<int>(PackageTypes::Failed), refused);
        emit installationProgressUpdated(static_cast<int>(PackageTypes::ProgressFailed),
                                         name, 0, 0, false, refused);
        return;
    }

    // Behind the install that is running, in the order asked; the row says
    // "Queued" meanwhile.
    if (isInstalling()) {
        m_packageModel->queueRowInstallation(name, repositoryUrl);
        QPointer<PackageManagerBackend> self(this);
        m_queuedInstalls.append([self, name, version, repositoryUrl, approval]() {
            if (!self) return;
            self->performInstall(name, version, repositoryUrl, approval);
            // Still queued: it stopped before it started (a module not
            // connected). The disk says what the row really is.
            if (self && self->m_packageModel->isRowQueued(name, repositoryUrl))
                self->refreshPackages();
        });
        return;
    }

    // Local .lgx: the file is already on disk, nothing to download.
    if (approval.contains(QStringLiteral("file"))) {
        setIsInstalling(true);
        installLocalFile(name, name, version, m_pendingLocalInstalls.take(name), approval);
        return;
    }
    installSinglePackageAsync(name, repositoryUrl, approval);
}

void PackageManagerBackend::installLocalFile(const QString& name, const QString& topLevelName,
                                             const QString& version, const QString& path,
                                             const QVariantMap& approval)
{
    QVariantMap entry{
        {QStringLiteral("name"), name},
        {QStringLiteral("path"), path},
        {QStringLiteral("source"), QUrl::fromLocalFile(path).toString()},
    };
    markEntriesInstalling({entry});
    // Checked again right before installing: what the host approved is what it
    // read, not the path.
    QPointer<PackageManagerBackend> self(this);
    checkLocalFile(path, approval, name, version,
        [self, entry, path, topLevelName](const QString& changed) mutable {
            if (!self) return;
            if (!changed.isEmpty()) {
                self->dropStaged(path);
                entry.remove(QStringLiteral("path"));
                entry.insert(QStringLiteral("error"), changed);
            }
            self->installResultsSequential({entry}, topLevelName, 0);
        });
}

void PackageManagerBackend::performUpgrade(QString moduleName, QString version, int mode,
                                           QString repositoryUrl, QVariantMap approval)
{
    const QString mapped = m_packageModel
        ? m_packageModel->displayNameForModule(moduleName) : QString();
    const QString displayName = mapped.isEmpty() ? moduleName : mapped;

    // Refused before it can queue or touch anything. A queued upgrade comes
    // back here when its turn comes.
    const QString refused = install_sequence::refusal(
        approval, m_pendingLocalInstalls.contains(moduleName), moduleName);
    if (!refused.isEmpty()) {
        emit cancellationOccurred(displayName,
            QStringLiteral("Upgrade of '%1' failed: %2").arg(displayName, refused));
        return;
    }
    if (!packageManagerReady()) {
        emit errorOccurred(static_cast<int>(PackageTypes::PackageManagerNotConnected));
        return;
    }
    // Queued before anything is removed: the old copy stays until its turn.
    if (isInstalling()) {
        QPointer<PackageManagerBackend> self(this);
        m_queuedInstalls.append([self, moduleName, version, mode, repositoryUrl, approval]() {
            if (self) self->performUpgrade(moduleName, version, mode, repositoryUrl, approval);
        });
        return;
    }
    setIsInstalling(true);

    // Travels with this upgrade to the end, file included, so nothing else
    // changes it midway: not a cancel elsewhere, nor another request for the
    // same package.
    PendingUpgradeMeta upgrade;
    upgrade.repositoryUrl = repositoryUrl;
    upgrade.approval = approval;
    if (approval.contains(QStringLiteral("file")))
        upgrade.stagedPath = m_pendingLocalInstalls.take(moduleName);

    // Nothing is removed until its replacement is here, and is the approved one.
    QPointer<PackageManagerBackend> self(this);
    prepareReplacement(moduleName, displayName, version, upgrade,
        [self, moduleName, version, mode, displayName, upgrade](
            const QString& refused, const QVariantList& downloaded) mutable {
            if (!self) return;
            if (!refused.isEmpty()) {
                self->installEnded();
                self->dropStaged(upgrade.stagedPath);
                emit self->cancellationOccurred(displayName,
                    QStringLiteral("Upgrade of '%1' failed: %2").arg(displayName, refused));
                // The row says what is on disk again.
                self->refreshPackages();
                return;
            }
            upgrade.downloaded = downloaded;
            self->removeForUpgrade(moduleName, version, mode, displayName, upgrade);
        });
}

void PackageManagerBackend::prepareReplacement(
    const QString& moduleName, const QString& displayName, const QString& version,
    const PendingUpgradeMeta& meta,
    std::function<void(const QString& refused, const QVariantList& downloaded)> then)
{
    const QVariantMap& approval = meta.approval;
    const QString refused =
        install_sequence::refusal(approval, !meta.stagedPath.isEmpty(), moduleName);
    if (!refused.isEmpty()) {
        then(refused, {});
        return;
    }
    if (approval.contains(QStringLiteral("file"))) {
        checkLocalFile(meta.stagedPath, approval, moduleName, version,
                       [then](const QString& changed) { then(changed, {}); });
        return;
    }

    m_packageModel->updateRowInstallation(displayName, meta.repositoryUrl,
                                          static_cast<int>(PackageTypes::Installing));
    const QVariantList requested = requestedRows(approval, moduleName);
    QPointer<PackageManagerBackend> self(this);
    modules().package_downloader.downloadResolvedDependenciesAsync(
        requestsJson(requested), buildInstalledPackagesJson(),
        [self, then, approval, requested](QVariantList downloaded) {
            if (!self) return;
            if (downloaded.isEmpty()) {
                then(QStringLiteral("package_downloader did not answer"), {});
                return;
            }
            downloaded = install_sequence::tagOptionalRows(
                downloaded, approval.value(QStringLiteral("optionalPackages")).toList());
            self->holdToApproval(downloaded, approval, requested,
                [then, downloaded](const QString& refused) {
                    then(refused, refused.isEmpty() ? downloaded : QVariantList{});
                });
        }, Timeout(DOWNLOAD_TIMEOUT_MS));
}

void PackageManagerBackend::removeForUpgrade(const QString& moduleName, const QString& version,
                                             int mode, const QString& displayName,
                                             const PendingUpgradeMeta& upgrade)
{
    // The host has unloaded the old version. An embedded copy stays in the
    // application directory and the new user-directory copy takes priority.
    // Remove only a user-installed copy; its replacement is already checked.
    LogosModules& logos = modules();
    QPointer<PackageManagerBackend> self(this);
    logos.package_manager.getInstalledPackagesAsync(
        [self, moduleName, version, mode, displayName, upgrade](QVariantList installed) {
            if (!self) return;
            for (const QVariant& item : installed) {
                const QVariantMap pkg = item.toMap();
                if (pkg.value(QStringLiteral("name")).toString() == moduleName
                    && pkg.value(QStringLiteral("installType")).toString()
                           == QLatin1String("embedded")) {
                    self->onUpgradeUninstallDone(moduleName, version, mode, upgrade);
                    return;
                }
            }
            self->modules().package_manager.uninstallPackageAsync(moduleName,
                [self, moduleName, version, mode, displayName, upgrade](QVariantMap result) {
                    if (!self) return;
                    if (!result.value("success", false).toBool()) {
                        self->installEnded();
                        self->dropStaged(upgrade.stagedPath);
                        emit self->cancellationOccurred(displayName,
                            QStringLiteral("Upgrade of '%1' failed: could not remove the "
                                           "installed version — %2")
                                .arg(displayName, result.value("error").toString()));
                        return;
                    }
                    self->onUpgradeUninstallDone(moduleName, version, mode, upgrade);
                });
        });
}

void PackageManagerBackend::performUninstall(QStringList names)
{
    if (!packageManagerReady()) {
        emit errorOccurred(static_cast<int>(PackageTypes::PackageManagerNotConnected));
        return;
    }

    LogosModules& logos = modules();
    QPointer<PackageManagerBackend> self(this);
    for (const QString& name : names) {
        logos.package_manager.uninstallPackageAsync(name,
            [self](QVariantMap result) {
                if (!self) return;
                if (result.value("success", false).toBool()) return;
                const QString err = result.value("error").toString();
                // Embedded-refusal surfaces distinctly so the QML toast is
                // clearer; every other failure funnels through UninstallFailed.
                emit self->errorOccurred(
                    err.contains("embedded", Qt::CaseInsensitive)
                        ? static_cast<int>(PackageTypes::PackageNotUninstallable)
                        : static_cast<int>(PackageTypes::UninstallFailed));
            });
    }
}

void PackageManagerBackend::reportActionFailed(QStringList names, QString error)
{
    if (names.isEmpty()) return;

    // Drop a local .lgx staged for a request that will not run — including on
    // `cancelled` — or a later install of the same package could pick it up.
    // An upgrade already under way took its file with it.
    QStringList labels;
    for (const QString& name : names) {
        forgetLocal(name);
        const QString mapped = m_packageModel
            ? m_packageModel->displayNameForModule(name) : QString();
        labels.append(mapped.isEmpty() ? name : mapped);
    }

    // The user's own cancel needs no toast — the click already said it.
    if (error == QLatin1String("cancelled")) return;

    emit cancellationOccurred(names.first(),
        QStringLiteral("'%1' was not changed: %2")
            .arg(labels.join(QStringLiteral(", ")), error));
}

void PackageManagerBackend::requestPackageDetailsFor(QString name, QString repositoryUrl)
{
    // A row index would be read against the list as it is when this arrives,
    // after a search or sort the view had not seen yet.
    const int row = m_packageModel ? m_packageModel->findPackageRow(name, repositoryUrl) : -1;
    if (row < 0) return;
    emit packageDetailsLoaded(m_packageModel->packageAt(row));
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
    // Unreachable, same as runSelectedActions — kept only so the .rep surface
    // stays stable for out-of-tree callers.
    qWarning() << "uninstallSelected: bulk uninstall is not wired";
    emit errorOccurred(static_cast<int>(PackageTypes::NoPackagesSelected));
}





void PackageManagerBackend::setRowVersion(int index, int versionIndex)
{
    if (!m_packageModel) return;
    const int row = findPackageRowAtProxyRow(index);
    if (row >= 0) m_packageModel->setRowVersion(row, versionIndex);
}


void PackageManagerBackend::subscribePackageManagerRefreshEvents()
{
    if (!packageManagerReady()) return;

    LogosModules& logos = modules();

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

void PackageManagerBackend::watchNetwork()
{
    // Basecamp's test switch: 1 offline, 0 online (a sandbox has no network).
    const QByteArray forced = qgetenv("BASECAMP_FORCE_OFFLINE");
    if (forced == "1" || forced == "0") {
        setOnline(forced == "0");
        return;
    }

    // The platform's own backend first. By features alone Qt may take glib,
    // which outside Linux never sees the connection go.
    const bool loaded = QNetworkInformation::loadDefaultBackend()
        || QNetworkInformation::loadBackendByFeatures(QNetworkInformation::Feature::Reachability);
    if (!loaded) {
        qInfo() << "PackageManagerBackend: no reachability backend among"
                << QNetworkInformation::availableBackends() << "- assuming online";
        return;
    }

    qInfo() << "PackageManagerBackend: reachability backend"
            << QNetworkInformation::instance()->backendName()
            << "of" << QNetworkInformation::availableBackends();

    // Local: a network, but no internet behind it.
    auto apply = [this]() {
        const auto r = QNetworkInformation::instance()->reachability();
        qInfo() << "PackageManagerBackend: reachability" << r;
        setOnline(r != QNetworkInformation::Reachability::Disconnected
                  && r != QNetworkInformation::Reachability::Local);
    };
    connect(QNetworkInformation::instance(), &QNetworkInformation::reachabilityChanged,
            this, apply);
    apply();
}

// Raw calls rather than the typed wrappers: a downloader that predates these
// methods answers nothing, and the values stay "".
void PackageManagerBackend::refreshDownloadStatus()
{
    if (!clientReady("package_downloader")) return;

    QPointer<PackageManagerBackend> self(this);
    modules().package_downloader.getDownloadSourceAsync([self](QString source) {
        if (self) self->setDownloadSource(source);
    });
    refreshStorageState();
}

void PackageManagerBackend::refreshStorageState()
{
    if (!clientReady("package_downloader")) return;

    QPointer<PackageManagerBackend> self(this);
    modules().package_downloader.getStorageStateAsync([self](QVariantMap result) {
        if (!self) return;
        const QString state = result.value("state").toString();
        self->setStorageState(state);
        if (state == QStringLiteral("waiting") || state == QStringLiteral("starting")) {
            QTimer::singleShot(2000, self, [self]() {
                if (self) self->refreshStorageState();
            });
        }
    });
}

void PackageManagerBackend::subscribePackageDownloaderEvents()
{
    if (!clientReady("package_downloader")) return;

    LogosModules& logos = modules();
    QPointer<PackageManagerBackend> self(this);
    logos.package_downloader.on("catalogChanged", [self](const QVariantList&) {
        if (!self) return;
        if (self->m_refreshDebounceTimer) self->m_refreshDebounceTimer->start();
        // A change of download source is announced this way too.
        self->refreshDownloadStatus();
    });

    logos.package_downloader.on("storageStateChanged", [self](const QVariantList& data) {
        if (!self) return;
        self->setStorageState(data.value(0).toString());
    });
    // Can arrive off the GUI thread.
    logos.package_downloader.onSubscriptionStatus([self](logos::SubStatus status, std::uint64_t) {
        if (status != logos::SubStatus::Armed) return;
        QMetaObject::invokeMethod(QCoreApplication::instance(), [self]() {
            if (self) self->refreshStorageState();
        }, Qt::QueuedConnection);
    });

    // Live byte progress for whatever is currently downloading. Payload is
    // [packageName, received, total] — see package_downloader's
    // `logos_events: downloadProgress`. Fires a few times a second per
    // download and lands straight on the model row, which is why
    // updateDownloadProgress emits only the two progress roles.
    logos.package_downloader.on("downloadProgress", [self](const QVariantList& data) {
        if (!self || !self->m_packageModel) return;
        if (data.size() < 3) {
            qWarning() << "package_downloader.downloadProgress: expected "
                          "[name, received, total, transport], got" << data.size() << "args";
            return;
        }
        // The transport is the fourth; a downloader that predates it sends three.
        self->m_packageModel->updateDownloadProgress(data.at(0).toString(),
                                                     data.at(1).toULongLong(),
                                                     data.at(2).toULongLong(),
                                                     data.value(3).toString());
    });
}

void PackageManagerBackend::onUpgradeUninstallDone(const QString& moduleName,
                                                    const QString& releaseTag,
                                                    int mode,
                                                    const PendingUpgradeMeta& upgrade)
{
    const QString mapped = m_packageModel
        ? m_packageModel->displayNameForModule(moduleName) : QString();
    const QString displayName = mapped.isEmpty() ? moduleName : mapped;

    if (!bothClientsReady()) {
        installEnded();
        dropStaged(upgrade.stagedPath);
        const QString msg = QStringLiteral("Cannot complete upgrade of '%1': "
                                           "required modules not connected").arg(displayName);
        emit installationProgressUpdated(
            static_cast<int>(PackageTypes::ProgressFailed),
            displayName, 0, 0, false, msg);
        return;
    }

    if (m_refreshDebounceTimer) m_refreshDebounceTimer->stop();

    // Scoped to the repo the upgrade came from, so another repo's copy of the
    // same package isn't badged. Empty (a local file) falls back to the
    // name-wide update.
    m_packageModel->updateRowInstallation(
        displayName, upgrade.repositoryUrl, static_cast<int>(PackageTypes::Installing));

    static const char* modeLabels[] = {"Upgrading", "Downgrading", "Sidegrading"};
    const char* label = (mode >= 0 && mode <= 2) ? modeLabels[mode] : "Upgrading";
    emit installationProgressUpdated(
        static_cast<int>(PackageTypes::InProgress),
        displayName, 0, 1, true,
        QStringLiteral("%1 %2\u2026").arg(label, displayName));

    // Local .lgx: the host has unloaded the old copy. A user-installed copy
    // was removed; an embedded copy remains as the fallback. The replacement
    // is the file this upgrade took — installed directly, checked once more.
    if (upgrade.approval.contains(QStringLiteral("file"))) {
        installLocalFile(moduleName, displayName, releaseTag, upgrade.stagedPath,
                         upgrade.approval);
        return;
    }

    // Approved and fetched before the old copy was removed (prepareReplacement):
    // install that, and nothing else.
    if (upgrade.downloaded.isEmpty()) {
        installResultsSequential(
            {QVariantMap{{QStringLiteral("name"), moduleName},
                         {QStringLiteral("error"),
                          QStringLiteral("Nothing approved was fetched, so nothing was installed.")}}},
            displayName, 0);
        return;
    }
    markEntriesInstalling(upgrade.downloaded);
    installResultsSequential(upgrade.downloaded, displayName, 0);
}

// ── Navigation ─────────────────────────────────────────────────────────────

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
