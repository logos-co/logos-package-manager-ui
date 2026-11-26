#include "Packages.h"

#include <logos/installer/CatalogClient.h>
#include <logos/installer/InstallProgress.h>
#include <logos/installer/Installer.h>
#include <logos/installer/OnlineWatch.h>
#include <logos/installer/PackageFilter.h>
#include <logos/installer/PackageModel.h>
#include <logos/installer/PagingProxy.h>

#include <QFileInfo>

#include <algorithm>

namespace pmui {

using namespace logos::installer;

namespace {

const QString kAll = QStringLiteral("All");

QString str(const QVariantMap& row, const char* key)
{
    return row.value(QLatin1String(key)).toString();
}

// The word a checked row's action is counted under, or "" for none to run.
QString actionKey(RowAction action)
{
    switch (action) {
    case RowAction::Install:   return QStringLiteral("install");
    case RowAction::Retry:     return QStringLiteral("retry");
    case RowAction::Upgrade:   return QStringLiteral("upgrade");
    case RowAction::Downgrade: return QStringLiteral("downgrade");
    case RowAction::Reinstall: return QStringLiteral("reinstall");
    default:                   return {};
    }
}

}  // namespace

Packages::Packages(QObject* parent)
    : QObject(parent)
    , m_model(new PackageModel(this))
    , m_progress(new InstallProgress(this))
    , m_filter(new PackageFilter(this))
    , m_paging(new PagingProxy(this))
    , m_online(new OnlineWatch(this))
{
    m_model->setProgress(m_progress);
    m_filter->setSourceModel(m_model);
    m_paging->setSourceModel(m_filter);
    // With no sort asked for, by name within each repository's section.
    m_filter->setSortRoleByName(QStringLiteral("name"));

    connect(m_filter, &PackageFilter::categoriesChanged, this, &Packages::takeCategories);
    connect(m_filter, &PackageFilter::typesChanged, this, &Packages::takeTypes);
    connect(m_paging, &PagingProxy::totalCountChanged, this, &Packages::countsChanged);
    connect(m_paging, &PagingProxy::currentPageChanged, this, &Packages::currentPageChanged);
    connect(m_online, &OnlineWatch::onlineChanged, this, &Packages::onlineChanged);
    connect(m_model, &PackageModel::hostVariantChanged, this, &Packages::hostVariantChanged);
    connect(m_model, &PackageModel::selectionChanged, this, &Packages::plan);

    auto recount = [this] {
        countAvailable();
        plan();
    };
    connect(m_model, &QAbstractItemModel::modelReset, this, recount);
    connect(m_model, &QAbstractItemModel::rowsInserted, this, recount);
    connect(m_model, &QAbstractItemModel::rowsRemoved, this, recount);
    connect(m_model, &QAbstractItemModel::dataChanged, this,
            [recount](const QModelIndex&, const QModelIndex&, const QList<int>& roles) {
                // Not for each download sample.
                static const QList<int> moving{
                    PackageModel::DownloadReceivedRole, PackageModel::DownloadTotalRole,
                    PackageModel::DownloadTransportRole, PackageModel::DownloadFellBackRole,
                    PackageModel::PlanDownloadReceivedRole, PackageModel::PlanDownloadTotalRole,
                    PackageModel::PlanDownloadTransportRole, PackageModel::PlanDownloadFellBackRole,
                    PackageModel::PlanInstallStageRole};
                if (!roles.isEmpty()
                    && std::all_of(roles.cbegin(), roles.cend(),
                                   [](int r) { return moving.contains(r); }))
                    return;
                recount();
            });
    takeCategories();
    takeTypes();
}

Packages::~Packages() = default;

void Packages::setModules(PackageModules* packages, InstallModules* installs, Consent* consent)
{
    if (m_catalog) return;
    m_catalog = new CatalogClient(packages, m_model, this);
    if (m_debounceMs >= 0) m_catalog->setDebounceInterval(m_debounceMs);
    m_installer = new Installer(installs, consent, m_progress, this);
    m_installer->setPackages(m_model);
    if (m_clearDelayMs >= 0) m_installer->setClearDelay(m_clearDelayMs);

    connect(m_catalog, &CatalogClient::loadingChanged, this, &Packages::loadingChanged);
    connect(m_catalog, &CatalogClient::repositoriesChanged, this, [this] {
        m_filter->setSourceOrder(m_catalog->sourceOrder());
        emit repositoriesChanged();
    });
    connect(m_catalog, &CatalogClient::downloadSourceChanged, this, &Packages::downloadStateChanged);
    connect(m_catalog, &CatalogClient::storageStateChanged, this, &Packages::downloadStateChanged);
    connect(m_installer, &Installer::busyChanged, this, &Packages::busyChanged);
    connect(m_installer, &Installer::finished, this,
            [this](const QString& name, Installer::Outcome outcome, const QString& error) {
                // package_manager keeps what came with Basecamp.
                if (m_uninstalling.remove(name) && outcome == Installer::Outcome::Failed
                    && error.contains(QStringLiteral("embedded"), Qt::CaseInsensitive)) {
                    emit failed(Error::PackageNotUninstallable);
                    return;
                }
                emit finished(displayNameFor(name), static_cast<int>(outcome), error);
            });
}

void Packages::setDebounceInterval(int ms)
{
    m_debounceMs = ms;
    if (m_catalog) m_catalog->setDebounceInterval(ms);
}

void Packages::setClearDelay(int ms)
{
    m_clearDelayMs = ms;
    if (m_installer) m_installer->setClearDelay(ms);
}

void Packages::load()
{
    if (m_catalog) m_catalog->load();
}

void Packages::refresh()
{
    if (!m_catalog) return;
    // A reload starts over: what failed last time shows what is on disk.
    for (const QString& name : m_progress->activeNames()) {
        if (m_progress->stage(name) == InstallStage::Failed) m_progress->clear(name);
    }
    m_catalog->refresh();
}

void Packages::onPackageInstalled()
{
    if (m_catalog) m_catalog->onPackagesChanged();
}

void Packages::onPackageUninstalled(const QString& moduleName)
{
    // Gone: nothing to run on it.
    for (int row : m_model->selectedRows()) {
        if (m_model->index(row).data(PackageModel::ModuleNameRole).toString() == moduleName)
            m_model->setSelected(row, false);
    }
    if (m_catalog) m_catalog->onPackagesChanged();
}

void Packages::onCatalogChanged()
{
    if (m_catalog) m_catalog->onCatalogChanged();
}

void Packages::onStorageStateChanged(const QString& state, const QString& error)
{
    if (m_catalog) m_catalog->onStorageStateChanged(state, error);
}

void Packages::onSubscriptionArmed()
{
    if (m_catalog) m_catalog->onSubscriptionArmed();
}

void Packages::onDownloadProgress(const QString& name, quint64 received, quint64 total,
                                  const QString& transport)
{
    m_progress->setDownloadProgress(name, received, total, transport);
}

QAbstractItemModel* Packages::page() const
{
    return m_paging;
}

QVariantMap Packages::roleIds() const
{
    QVariantMap ids;
    const QHash<int, QByteArray> names = m_model->roleNames();
    for (auto it = names.cbegin(); it != names.cend(); ++it)
        ids.insert(QString::fromLatin1(it.value()), it.key());
    return ids;
}

void Packages::setSearchText(const QString& text)
{
    if (text == m_filter->searchText()) return;
    m_filter->setSearchText(text);
    emit filtersChanged();
}

QString Packages::searchText() const
{
    return m_filter->searchText();
}

void Packages::setCategoryIndex(int index)
{
    const QString category = index > 0 ? m_categories.value(index) : QString();
    if (category == m_category) return;
    m_category = category;
    m_filter->setCategoryFilter(category);
    emit filtersChanged();
}

int Packages::categoryIndex() const
{
    return m_category.isEmpty() ? 0 : qMax(0, m_categories.indexOf(m_category));
}

QStringList Packages::categories() const
{
    return m_categories;
}

void Packages::setTypeIndex(int index)
{
    const QString type = index > 0 ? m_types.value(index) : QString();
    if (type == m_type) return;
    m_type = type;
    m_filter->setTypeFilter(type);
    emit filtersChanged();
}

int Packages::typeIndex() const
{
    return m_type.isEmpty() ? 0 : qMax(0, m_types.indexOf(m_type));
}

QStringList Packages::types() const
{
    return m_types;
}

void Packages::setInstallState(int state)
{
    if (state == m_filter->installStateFilter()) return;
    m_filter->setInstallStateFilter(state);
    emit filtersChanged();
}

int Packages::installState() const
{
    return m_filter->installStateFilter();
}

void Packages::setShowUnavailable(bool show)
{
    if (show == m_filter->showUnavailable()) return;
    m_filter->setShowUnavailable(show);
    emit filtersChanged();
}

bool Packages::showUnavailable() const
{
    return m_filter->showUnavailable();
}

void Packages::setSort(const QString& roleName, int order)
{
    const QString role = roleName.isEmpty() ? QStringLiteral("name") : roleName;
    if (role == m_filter->sortRoleName() && order == m_filter->sortOrderInt()) return;
    m_filter->setSortRoleByName(role);
    m_filter->setSortOrderInt(order);
    emit filtersChanged();
}

QString Packages::sortRole() const
{
    const QString role = m_filter->sortRoleName();
    return role == QLatin1String("name") ? QString() : role;
}

int Packages::sortOrder() const
{
    return m_filter->sortOrderInt();
}

void Packages::setPageSize(int size)
{
    m_paging->setPageSize(size);
}

int Packages::pageSize() const
{
    return m_paging->pageSize();
}

void Packages::setCurrentPage(int page)
{
    m_paging->setCurrentPage(page);
}

int Packages::currentPage() const
{
    return m_paging->currentPage();
}

int Packages::totalCount() const
{
    return m_paging->totalCount();
}

int Packages::availableHereCount() const
{
    return m_availableHere;
}

bool Packages::loading() const
{
    return m_catalog && m_catalog->isLoading();
}

bool Packages::online() const
{
    return m_online->isOnline();
}

QString Packages::downloadSource() const
{
    return m_catalog ? m_catalog->downloadSource() : QString();
}

QString Packages::storageState() const
{
    return m_catalog ? m_catalog->storageState() : QString();
}

QString Packages::hostVariant() const
{
    return m_model->hostVariant();
}

int Packages::repositoryCount() const
{
    return m_catalog ? m_catalog->repositoryCount() : 0;
}

QVariantMap Packages::repositoryLabels() const
{
    return m_catalog ? m_catalog->repositoryLabels() : QVariantMap{};
}

bool Packages::busy() const
{
    return m_installer && m_installer->isBusy();
}

int Packages::modelRowOf(int pageRow) const
{
    const QModelIndex onPage = m_paging->index(pageRow, 0);
    if (!onPage.isValid()) return -1;
    const QModelIndex inModel = m_filter->mapToSource(m_paging->mapToSource(onPage));
    return inModel.isValid() ? inModel.row() : -1;
}

void Packages::selectVersion(int pageRow, int versionIndex)
{
    const int row = modelRowOf(pageRow);
    if (row >= 0) m_model->selectVersion(row, versionIndex);
}

void Packages::setChecked(int pageRow, bool checked)
{
    const int row = modelRowOf(pageRow);
    if (row < 0) return;
    if (checked) {
        const auto action = static_cast<RowAction>(
            m_model->index(row).data(PackageModel::RowActionRole).toInt());
        if (actionKey(action).isEmpty()) return;
    }
    m_model->setSelected(row, checked);
}

int Packages::runnableCount() const
{
    return m_runnable;
}

QVariantMap Packages::actionSummary() const
{
    return m_summary;
}

QVariantList Packages::actionPlanItems() const
{
    return m_planItems;
}

QVariantMap Packages::details(const QString& name, const QString& repositoryUrl) const
{
    const int row = name.isEmpty() ? -1 : m_model->rowOf(name, repositoryUrl);
    return row >= 0 ? m_model->rowAt(row) : QVariantMap{};
}

QString Packages::displayNameFor(const QString& moduleName) const
{
    for (int row = 0; row < m_model->rowCount(); ++row) {
        const QModelIndex at = m_model->index(row);
        if (at.data(PackageModel::ModuleNameRole).toString() == moduleName
            || at.data(PackageModel::NameRole).toString() == moduleName) {
            const QString label = at.data(PackageModel::DisplayNameRole).toString();
            if (!label.isEmpty()) return label;
        }
    }
    return moduleName;
}

void Packages::install(const QString& name, const QString& version, const QString& repositoryUrl)
{
    if (name.isEmpty()) return;
    if (!m_installer) {
        emit failed(Error::PackageManagerNotConnected);
        return;
    }
    m_installer->install(name, version, repositoryUrl);
}

void Packages::upgrade(const QString& moduleName, const QString& version, int mode,
                       const QString& repositoryUrl)
{
    if (moduleName.isEmpty()) return;
    if (!m_installer) {
        emit failed(Error::PackageManagerNotConnected);
        return;
    }
    m_installer->upgrade(moduleName, version, mode, repositoryUrl);
}

void Packages::uninstall(const QString& moduleName)
{
    if (moduleName.isEmpty()) return;
    if (!m_installer) {
        emit failed(Error::PackageManagerNotConnected);
        return;
    }
    m_uninstalling.insert(moduleName);
    m_installer->uninstall({moduleName});
}

void Packages::installFile(const QString& path)
{
    const QFileInfo file(path);
    if (path.isEmpty() || !file.isFile()
        || file.suffix().compare(QStringLiteral("lgx"), Qt::CaseInsensitive) != 0) {
        emit failed(Error::LocalPackageInvalid);
        return;
    }
    if (!m_installer) {
        emit failed(Error::PackageManagerNotConnected);
        return;
    }
    m_installer->installFile(path);
}

void Packages::takeCategories()
{
    QStringList categories{kAll};
    categories << m_filter->categories();
    if (categories == m_categories) return;
    m_categories = categories;
    // The pick is kept by name; one the catalog no longer has is no filter.
    const bool gone = !m_category.isEmpty() && !m_categories.contains(m_category);
    if (gone) {
        m_category.clear();
        m_filter->setCategoryFilter({});
    }
    emit categoriesChanged();
    if (gone) emit filtersChanged();
}

void Packages::takeTypes()
{
    QStringList types{kAll};
    types << m_filter->types();
    if (types == m_types) return;
    m_types = types;
    const bool gone = !m_type.isEmpty() && !m_types.contains(m_type);
    if (gone) {
        m_type.clear();
        m_filter->setTypeFilter({});
    }
    emit typesChanged();
    if (gone) emit filtersChanged();
}

void Packages::countAvailable()
{
    int here = 0;
    for (int row = 0; row < m_model->rowCount(); ++row) {
        const QModelIndex at = m_model->index(row);
        const bool installed = !at.data(PackageModel::InstalledVersionRole).toString().isEmpty()
                            || !at.data(PackageModel::InstalledHashRole).toString().isEmpty()
                            || !at.data(PackageModel::InstallTypeRole).toString().isEmpty();
        if (installed || at.data(PackageModel::HasInstallableVersionRole).toBool()) ++here;
    }
    if (here == m_availableHere) return;
    m_availableHere = here;
    emit countsChanged();
}

void Packages::plan()
{
    int runnable = 0;
    QVariantMap summary;
    QVariantList items;
    for (int row : m_model->selectedRows()) {
        const QVariantMap r = m_model->rowAt(row);
        const QString key = actionKey(static_cast<RowAction>(r.value(QStringLiteral("rowAction")).toInt()));
        if (key.isEmpty()) continue;
        ++runnable;
        summary.insert(key, summary.value(key).toInt() + 1);
        items.append(QVariantMap{
            {QStringLiteral("name"), str(r, "name")},
            {QStringLiteral("displayName"), str(r, "displayName")},
            {QStringLiteral("action"), key},
            {QStringLiteral("repository"), str(r, "repositoryDisplayName")},
            {QStringLiteral("fromVersion"), str(r, "installedVersion")},
            {QStringLiteral("toVersion"), str(r, "version")},
        });
    }
    if (runnable == m_runnable && summary == m_summary && items == m_planItems) return;
    m_runnable = runnable;
    m_summary = summary;
    m_planItems = items;
    emit selectionChanged();
}

}  // namespace pmui
