#include "PackageManagerBackend.h"

#include "ModuleCalls.h"
#include "Packages.h"
#include <logos/installer/Installer.h>
#include <logos/installer/IntentConsent.h>

#include "logos_sdk.h"

#include <QCoreApplication>
#include <QDebug>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>

using pmui::Packages;

namespace {

const QString kIntentsModule = QStringLiteral("intents_module");

// intents_module answers a request or a result at once; the person's answer
// comes later, as settled.
constexpr int kIntentCallMs = 10 * 1000;

}  // namespace

PackageManagerBackend::PackageManagerBackend(QObject* parent)
    : PackageManagerUiSimpleSource(parent)
    , m_packages(new Packages(this))
{
    setPackageRoleIds(m_packages->roleIds());
    mirror();

    connect(m_packages, &Packages::loadingChanged, this, [this] { setIsLoading(m_packages->loading()); });
    connect(m_packages, &Packages::loadStateChanged, this, [this] {
        setLoaded(m_packages->loaded());
        setLoadError(m_packages->loadError());
    });
    connect(m_packages, &Packages::loadFailed, this,
            [this](const QString& error) { emit loadFailed(error); });
    connect(m_packages, &Packages::onlineChanged, this, [this] { setOnline(m_packages->online()); });
    connect(m_packages, &Packages::downloadStateChanged, this, [this] {
        setDownloadSource(m_packages->downloadSource());
        setStorageState(m_packages->storageState());
    });
    connect(m_packages, &Packages::hostVariantChanged, this,
            [this] { setHostVariant(m_packages->hostVariant()); });
    connect(m_packages, &Packages::repositoriesChanged, this, [this] {
        setRepositoryLabels(m_packages->repositoryLabels());
        setRepositoryCount(m_packages->repositoryCount());
    });
    connect(m_packages, &Packages::categoriesChanged, this, [this] {
        setCategories(m_packages->categories());
        setSelectedCategoryIndex(m_packages->categoryIndex());
    });
    connect(m_packages, &Packages::typesChanged, this, [this] {
        setAvailableTypes(m_packages->types());
        setSelectedTypeIndex(m_packages->typeIndex());
    });
    // The view sets the filters; a category or type the catalog no longer has
    // is dropped here.
    connect(m_packages, &Packages::filtersChanged, this, [this] {
        setSelectedCategoryIndex(m_packages->categoryIndex());
        setSelectedTypeIndex(m_packages->typeIndex());
    });
    connect(m_packages, &Packages::countsChanged, this, [this] {
        setTotalCount(m_packages->totalCount());
        setAvailableHereCount(m_packages->availableHereCount());
    });
    connect(m_packages, &Packages::currentPageChanged, this,
            [this] { setCurrentPage(m_packages->currentPage()); });
    connect(m_packages, &Packages::selectionChanged, this, &PackageManagerBackend::mirrorSelection);
    connect(m_packages, &Packages::busyChanged, this, [this] { setIsInstalling(m_packages->busy()); });
    connect(m_packages, &Packages::finished, this,
            [this](const QString& name, int outcome, const QString& error) {
                using Outcome = logos::installer::Installer::Outcome;
                switch (static_cast<Outcome>(outcome)) {
                case Outcome::Done:
                    emit installationProgressUpdated(Completed, name, 1, 1, true, {});
                    break;
                case Outcome::Failed:
                    emit installationProgressUpdated(ProgressFailed, name, 0, 0, false, error);
                    break;
                case Outcome::Cancelled:
                    // The person's own cancel: the click already said it.
                    break;
                }
            });
    connect(m_packages, &Packages::failed, this,
            [this](Packages::Error error) { emit errorOccurred(static_cast<int>(error)); });

    // What the view pushes.
    connect(this, &PackageManagerUiSimpleSource::searchTextChanged, this,
            [this] { m_packages->setSearchText(searchText()); });
    connect(this, &PackageManagerUiSimpleSource::selectedCategoryIndexChanged, this,
            [this] { m_packages->setCategoryIndex(selectedCategoryIndex()); });
    connect(this, &PackageManagerUiSimpleSource::selectedTypeIndexChanged, this,
            [this] { m_packages->setTypeIndex(selectedTypeIndex()); });
    connect(this, &PackageManagerUiSimpleSource::installStateFilterChanged, this,
            [this] { m_packages->setInstallState(installStateFilter()); });
    connect(this, &PackageManagerUiSimpleSource::showUnavailableChanged, this,
            [this] { m_packages->setShowUnavailable(showUnavailable()); });
    connect(this, &PackageManagerUiSimpleSource::sortRoleChanged, this,
            [this] { m_packages->setSort(sortRole(), sortOrder()); });
    connect(this, &PackageManagerUiSimpleSource::sortOrderChanged, this,
            [this] { m_packages->setSort(sortRole(), sortOrder()); });
    connect(this, &PackageManagerUiSimpleSource::pageSizeChanged, this, [this] {
        m_packages->setPageSize(pageSize());
        setPageSize(m_packages->pageSize());
    });
    connect(this, &PackageManagerUiSimpleSource::currentPageChanged, this,
            [this] { m_packages->setCurrentPage(currentPage()); });
}

PackageManagerBackend::~PackageManagerBackend()
{
    // It holds m_calls; nothing may reach the modules through it after.
    delete m_packages;
    m_packages = nullptr;
}

QAbstractItemModel* PackageManagerBackend::packages() const
{
    return m_packages->page();
}

void PackageManagerBackend::onContextReady()
{
    LogosModules& logos = modules();
    m_calls = std::make_unique<pmui::ModuleCalls>(logos);

    // Basecamp's consent, asked through intents_module, which the shell hosts.
    auto intents = logos.bind_intents(kIntentsModule);
    m_consent = new logos::installer::IntentConsent(
        [intents](const QString& intent, const QString& params,
                  std::function<void(const QString&)> done) mutable {
            intents.requestAsync(intent, params, [done](const QString& reply) { done(reply); },
                                 Timeout(kIntentCallMs));
        },
        [intents](const QString& handle, const QString& receipt,
                  std::function<void(const QString&)> done) mutable {
            intents.resultAsync(handle, receipt, [done](const QString& reply) { done(reply); },
                                Timeout(kIntentCallMs));
        },
        this);
    m_request = [intents](const QString& intent) mutable {
        intents.requestAsync(intent, QStringLiteral("{}"), [intent](const QString& reply) {
            if (!QJsonDocument::fromJson(reply.toUtf8()).object().value(QStringLiteral("ok")).toBool())
                qWarning() << "PMUI:" << intent << "refused:" << reply;
        }, Timeout(kIntentCallMs));
    };
    m_respond = [intents](const QString& dispatch, bool ok, const QString& error) mutable {
        intents.respondAsync(dispatch, ok, QStringLiteral("{}"), error, [](const QString&) {},
                             Timeout(kIntentCallMs));
    };

    // Events can arrive off this thread, and no module may be called from
    // inside one: each is handled from the event loop.
    QPointer<PackageManagerBackend> self(this);
    auto later = [self](std::function<void(PackageManagerBackend*)> handle) {
        QMetaObject::invokeMethod(QCoreApplication::instance(), [self, handle] {
            if (self && self->m_packages) handle(self.data());
        }, Qt::QueuedConnection);
    };
    intents.onSettled([later](const QString& handle) {
        later([handle](PackageManagerBackend* b) { b->m_consent->onSettled(handle); });
    });
    intents.onSubscriptionStatus([later](logos::SubStatus status, std::uint64_t generation) {
        // Armed again after the first time: intents_module started over.
        if (status == logos::SubStatus::Armed && generation > 1)
            later([](PackageManagerBackend* b) { b->m_consent->onRestarted(); });
    });

    m_packages->setModules(m_calls.get(), m_calls.get(), m_consent);

    auto installed = [later](const QVariantList&) {
        later([](PackageManagerBackend* b) { b->m_packages->onPackageInstalled(); });
    };
    auto uninstalled = [later](const QVariantList& data) {
        const QString moduleName = data.value(0).toString();
        later([moduleName](PackageManagerBackend* b) { b->m_packages->onPackageUninstalled(moduleName); });
    };
    logos.package_manager.on(QStringLiteral("corePluginFileInstalled"), installed);
    logos.package_manager.on(QStringLiteral("uiPluginFileInstalled"), installed);
    logos.package_manager.on(QStringLiteral("corePluginUninstalled"), uninstalled);
    logos.package_manager.on(QStringLiteral("uiPluginUninstalled"), uninstalled);
    logos.package_downloader.on(QStringLiteral("catalogChanged"), [later](const QVariantList&) {
        later([](PackageManagerBackend* b) { b->m_packages->onCatalogChanged(); });
    });
    logos.package_downloader.on(QStringLiteral("storageStateChanged"), [later](const QVariantList& data) {
        const QString state = data.value(0).toString();
        const QString error = data.value(1).toString();
        later([state, error](PackageManagerBackend* b) { b->m_packages->onStorageStateChanged(state, error); });
    });
    // [name, received, total, transport]; a downloader before transports sends three.
    logos.package_downloader.on(QStringLiteral("downloadProgress"), [later](const QVariantList& data) {
        if (data.size() < 3) return;
        const QString name = data.at(0).toString();
        const quint64 received = data.at(1).toULongLong();
        const quint64 total = data.at(2).toULongLong();
        const QString transport = data.value(3).toString();
        later([=](PackageManagerBackend* b) { b->m_packages->onDownloadProgress(name, received, total, transport); });
    });
    logos.package_downloader.onSubscriptionStatus([later](logos::SubStatus status, std::uint64_t) {
        if (status == logos::SubStatus::Armed)
            later([](PackageManagerBackend* b) { b->m_packages->onSubscriptionArmed(); });
    });

    m_packages->load();
}

QString PackageManagerBackend::deliver(const QString& dispatch, const QString& intent,
                                       const QString& paramsJson, const QString&)
{
    if (!Packages::provides(intent) || !m_respond) return QStringLiteral("{\"ok\":false}");
    const QString name = QJsonDocument::fromJson(paramsJson.toUtf8()).object()
                             .value(QStringLiteral("name")).toString();
    m_revealDispatches.insert(dispatch);
    emit revealRequested(dispatch, intent, name);
    return QStringLiteral("{\"ok\":true}");
}

void PackageManagerBackend::answerReveal(QString dispatch, bool ok, QString error)
{
    // Only what was delivered here, once each.
    if (!m_revealDispatches.remove(dispatch) || !m_respond) return;
    m_respond(dispatch, ok, ok ? QString() : error);
}

void PackageManagerBackend::refreshCatalog()
{
    m_packages->refresh();
}

void PackageManagerBackend::installLocalPackage(QUrl fileUrl)
{
    if (!fileUrl.isValid() || !fileUrl.isLocalFile()) {
        emit errorOccurred(LocalPackageInvalid);
        return;
    }
    m_packages->installFile(fileUrl.toLocalFile());
}

void PackageManagerBackend::runSelectedActions()
{
    // The bulk surface is off in QML. Reviving it means raising ONE combined
    // confirm intent for the batch.
    qWarning() << "runSelectedActions: bulk actions are not wired";
    emit errorOccurred(NoPackagesSelected);
}

void PackageManagerBackend::uninstallSelected()
{
    qWarning() << "uninstallSelected: bulk uninstall is not wired";
    emit errorOccurred(NoPackagesSelected);
}

void PackageManagerBackend::togglePackage(int index, bool checked)
{
    m_packages->setChecked(index, checked);
}

void PackageManagerBackend::reloadPackage(int index)
{
    // TODO: load/unload the plugin via logoscore.
    Q_UNUSED(index);
    qWarning() << "PackageManagerBackend::reloadPackage not implemented (TODO: logoscore load/unload).";
}

void PackageManagerBackend::setRowVersion(int index, int versionIndex)
{
    m_packages->selectVersion(index, versionIndex);
}

void PackageManagerBackend::requestPackageDetailsFor(QString name, QString repositoryUrl)
{
    // By name and repository: a row index would be read against the list as it
    // is when this arrives, after a search or sort the view had not seen yet.
    const QVariantMap details = m_packages->details(name, repositoryUrl);
    if (!details.isEmpty()) emit packageDetailsLoaded(details);
}

QString PackageManagerBackend::displayNameForModule(QString moduleName)
{
    return m_packages->displayNameFor(moduleName);
}

void PackageManagerBackend::confirmInstall(QString name, QString version, QString repositoryUrl)
{
    m_packages->install(name, version, repositoryUrl);
}

void PackageManagerBackend::confirmUpgrade(QString moduleName, QString version, int mode,
                                           QString repositoryUrl)
{
    m_packages->upgrade(moduleName, version, mode, repositoryUrl);
}

void PackageManagerBackend::confirmUninstall(QString moduleName)
{
    m_packages->uninstall(moduleName);
}

void PackageManagerBackend::manageRepositories()
{
    // Nothing waits on the answer: the shell shows Settings, or doesn't.
    if (m_request) m_request(QStringLiteral("basecamp.repositories.manage"));
}

void PackageManagerBackend::mirror()
{
    setIsLoading(m_packages->loading());
    setLoaded(m_packages->loaded());
    setLoadError(m_packages->loadError());
    setOnline(m_packages->online());
    setDownloadSource(m_packages->downloadSource());
    setStorageState(m_packages->storageState());
    setHostVariant(m_packages->hostVariant());
    setRepositoryLabels(m_packages->repositoryLabels());
    setRepositoryCount(m_packages->repositoryCount());
    setCategories(m_packages->categories());
    setAvailableTypes(m_packages->types());
    setTotalCount(m_packages->totalCount());
    setAvailableHereCount(m_packages->availableHereCount());
    setIsInstalling(m_packages->busy());
    setSearchText(m_packages->searchText());
    setSelectedCategoryIndex(m_packages->categoryIndex());
    setSelectedTypeIndex(m_packages->typeIndex());
    setInstallStateFilter(m_packages->installState());
    setShowUnavailable(m_packages->showUnavailable());
    setSortRole(m_packages->sortRole());
    setSortOrder(m_packages->sortOrder());
    setPageSize(m_packages->pageSize());
    setCurrentPage(m_packages->currentPage());
    mirrorSelection();
}

void PackageManagerBackend::mirrorSelection()
{
    setRunnableActionCount(m_packages->runnableCount());
    setActionSummary(m_packages->actionSummary());
    setActionPlanItems(m_packages->actionPlanItems());
}
