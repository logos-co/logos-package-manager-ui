#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

class QAbstractItemModel;

namespace logos::installer {
class CatalogClient;
class Consent;
class InstallModules;
class InstallProgress;
class Installer;
class OnlineWatch;
class PackageFilter;
class PackageModel;
class PackageModules;
class PagingProxy;
}  // namespace logos::installer

namespace pmui {

// What the Package Manager's page shows and does, on basecamp-installer-common:
// every repository's packages and the ones installed from a file, filtered,
// sorted and paged as the page asks; a package's details; and the installs,
// upgrades and uninstalls the person asks for, each of which Basecamp approves
// before anything happens. No module calls of its own: the backend hands it the
// modules it reads and installs through, and Basecamp's consent.
class Packages : public QObject {
    Q_OBJECT

public:
    // Why a request didn't go ahead, for the page to say: the .rep's ErrorType.
    enum class Error {
        PackageManagerNotConnected = 4,
        PackageNotUninstallable = 6,
        LocalPackageInvalid = 7,
    };

    explicit Packages(QObject* parent = nullptr);
    ~Packages() override;

    // Once, before load().
    void setModules(logos::installer::PackageModules* packages,
                    logos::installer::InstallModules* installs,
                    logos::installer::Consent* consent);
    // For tests: how soon a burst of events reloads, and how long a finished
    // install's row says Installed.
    void setDebounceInterval(int ms);
    void setClearDelay(int ms);

    // The first load, and Reload: every repository fetched again, and the
    // rows that failed last time show what is on disk.
    void load();
    void refresh();

    // The modules' events, as the backend receives them.
    void onPackageInstalled();
    void onPackageUninstalled(const QString& moduleName);
    void onCatalogChanged();
    void onStorageStateChanged(const QString& state, const QString& error);
    void onSubscriptionArmed();
    void onDownloadProgress(const QString& name, quint64 received, quint64 total,
                            const QString& transport);

    // The page of rows the list shows, and its roles by name.
    QAbstractItemModel* page() const;
    QVariantMap roleIds() const;

    void setSearchText(const QString& text);
    QString searchText() const;
    // Into categories(); 0 is "All". A category the catalog no longer has is
    // no filter.
    void setCategoryIndex(int index);
    int categoryIndex() const;
    QStringList categories() const;
    // Into types(); 0 is "All".
    void setTypeIndex(int index);
    int typeIndex() const;
    QStringList types() const;
    // logos::installer::PackageFilter::InstallState.
    void setInstallState(int state);
    int installState() const;
    void setShowUnavailable(bool show);
    bool showUnavailable() const;
    // A role by name, "" for the catalog's order; Qt::SortOrder.
    void setSort(const QString& roleName, int order);
    QString sortRole() const;
    int sortOrder() const;
    void setPageSize(int size);
    int pageSize() const;
    void setCurrentPage(int page);
    int currentPage() const;

    // The rows every page holds, and those this host can install or has.
    int totalCount() const;
    int availableHereCount() const;

    bool loading() const;
    // The catalog has been read once; what the last read couldn't read, or "".
    bool loaded() const;
    QString loadError() const;
    bool online() const;
    QString downloadSource() const;
    QString storageState() const;
    QString hostVariant() const;
    int repositoryCount() const;
    // Repository URL to its label, and "local" to its own.
    QVariantMap repositoryLabels() const;
    // A request is running, or asking.
    bool busy() const;

    // The row at `pageRow` of the page: its version picked, or its checkbox.
    // Only a row with something to run can be checked.
    void selectVersion(int pageRow, int versionIndex);
    void setChecked(int pageRow, bool checked);
    // The checked rows' plan: how many can run, and by action
    // ({install: n, upgrade: n, …}, none at 0), and each row of it
    // ({name, displayName, action, repository, fromVersion, toVersion}).
    int runnableCount() const;
    QVariantMap actionSummary() const;
    QVariantList actionPlanItems() const;

    // Every role of the package's row, by name; empty when there is none.
    QVariantMap details(const QString& name, const QString& repositoryUrl) const;
    // The label of the package installed as `moduleName`, or the name itself.
    QString displayNameFor(const QString& moduleName) const;

    // An intent this app provides, delivered through intents_module:
    // packages.show and packages.install {name}. Both only show the package:
    // no click here was behind the request, so the person's own click on
    // Install is what asks Basecamp.
    static bool provides(const QString& intent);

    // What the person asked for. Each asks Basecamp first; how it ended comes
    // as finished(). `mode`: 0 upgrade, 1 downgrade, 2 the same version again.
    void install(const QString& name, const QString& version, const QString& repositoryUrl);
    void upgrade(const QString& moduleName, const QString& version, int mode,
                 const QString& repositoryUrl);
    void uninstall(const QString& moduleName);
    // A .lgx the person picked; refused at once (failed()) if it isn't one.
    void installFile(const QString& path);

signals:
    void loadingChanged();
    void loadStateChanged();
    // Every read that failed, `error` as loadError() has it.
    void loadFailed(const QString& error);
    void onlineChanged();
    void downloadStateChanged();
    void hostVariantChanged();
    void repositoriesChanged();
    void categoriesChanged();
    void typesChanged();
    void filtersChanged();
    void countsChanged();
    void currentPageChanged();
    void selectionChanged();
    void busyChanged();
    // `outcome`: logos::installer::Installer::Outcome as an int; `name` the
    // package's label. An uninstall of a package that came with Basecamp
    // is failed(PackageNotUninstallable) at once instead.
    void finished(const QString& name, int outcome, const QString& error);
    void changeEnded(const QString& change);
    void failed(pmui::Packages::Error error);

private:
    int modelRowOf(int pageRow) const;
    // How what is installed as `moduleName` was ("user", "embedded"), or ""
    // when no row says.
    QString installTypeOf(const QString& moduleName) const;
    void takeCategories();
    void takeTypes();
    void countAvailable();
    void plan();

    logos::installer::PackageModel* m_model;
    logos::installer::InstallProgress* m_progress;
    logos::installer::PackageFilter* m_filter;
    logos::installer::PagingProxy* m_paging;
    logos::installer::OnlineWatch* m_online;
    logos::installer::CatalogClient* m_catalog = nullptr;
    logos::installer::Installer* m_installer = nullptr;
    int m_debounceMs = -1;
    int m_clearDelayMs = -1;

    QString m_category;
    QStringList m_categories;
    QString m_type;
    QStringList m_types;
    int m_availableHere = 0;

    int m_runnable = 0;
    QVariantMap m_summary;
    QVariantList m_planItems;
};

}  // namespace pmui
