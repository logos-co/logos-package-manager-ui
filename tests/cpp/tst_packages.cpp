// The Package Manager's page on fake package modules and a fake Basecamp:
// every repository's packages, filtered, sorted and paged; the checked rows'
// plan; and what each action asks Basecamp before anything changes.

#include "check.h"
#include "fakes.h"
#include "fixtures.h"

#include "Packages.h"

#include <logos/installer/Installer.h>
#include <logos/installer/PackageFilter.h>
#include <logos/installer/Types.h>

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSignalSpy>
#include <QTemporaryDir>

using namespace fixtures;
using logos::installer::Consent;
using logos::installer::Installer;
using logos::installer::PackageFilter;
using logos::installer::RowAction;
using pmui::Packages;

namespace {

const QString kDefault = QStringLiteral("https://default");
const QString kMine = QStringLiteral("https://mine");
const QStringList kHere{QStringLiteral("linux-x86_64")};

QVariantMap pkg(const QString& repo, const QString& name, const QString& version,
                const QString& hash)
{
    return catalogRow(repo, name, version, hash, {}, kHere, QStringLiteral("core"));
}

// Not for this host: a build for another platform only.
QVariantMap elsewhere(const QString& repo, const QString& name, const QString& hash)
{
    return catalogRow(repo, name, QStringLiteral("1.0.0"), hash, {},
                      {QStringLiteral("darwin-arm64")}, QStringLiteral("core"));
}

QVariantMap withSize(QVariantMap row, qint64 size)
{
    QVariantList versions = row.value("versions").toList();
    QVariantMap newest = versions.first().toMap();
    newest.insert("size", size);
    versions[0] = newest;
    row.insert("versions", versions);
    return row;
}

int outcome(Installer::Outcome o) { return static_cast<int>(o); }

// package_manager refusing every uninstall with `refusal`.
struct Refusing : fakes::Modules {
    QString refusal;
    void uninstallPackage(const QString& name, std::function<void(const QVariantMap&)> done) override
    {
        calls << "uninstall " + name;
        done({{"success", false}, {"error", refusal}});
    }
};

// A download that ends only when told to.
struct Holding : fakes::Modules {
    std::function<void()> finish;
    void downloadPinned(const QString& repositoryUrl, const QString& name, const QString& version,
                        const QString& rootHash, std::function<void(const QVariantMap&)> done) override
    {
        finish = [=] { fakes::Modules::downloadPinned(repositoryUrl, name, version, rootHash, done); };
    }
};

template <typename Modules = fakes::Modules>
struct PageOf {
    Modules modules;
    fakes::Basecamp basecamp;
    Packages packages;
    struct Finished { QString name; int outcome; QString error; };
    QList<Finished> finished;
    QList<Packages::Error> failures;

    PageOf()
    {
        // The default repository is first whatever its name.
        modules.repositories = {
            QVariantMap{{"url", kMine}, {"displayName", "Mine"}},
            QVariantMap{{"url", kDefault}, {"displayName", "Zed"}, {"isDefault", true}},
        };
        packages.setDebounceInterval(0);
        packages.setClearDelay(0);
        packages.setModules(&modules, &modules, &basecamp);
        QObject::connect(&packages, &Packages::finished, &packages,
                         [this](const QString& name, int o, const QString& error) {
                             finished.append({name, o, error});
                         });
        QObject::connect(&packages, &Packages::failed, &packages,
                         [this](Packages::Error e) { failures.append(e); });
    }

    QStringList names() const { return fixtures::names(*packages.page()); }
    QVariant role(const QString& name, const QByteArray& r) const
    {
        return roleOf(*packages.page(), name, r);
    }
    QString lastIntent() const { return basecamp.asked.isEmpty() ? QString() : basecamp.asked.last().intent; }
    QVariantMap lastParams() const { return basecamp.asked.isEmpty() ? QVariantMap() : basecamp.asked.last().params; }
    QStringList changes() const { return modules.calls.filter(QRegularExpression("^(install|uninstall) ")); }
    // Basecamp approving what is asked: the catalog's release, or for an
    // uninstall the names asked.
    void approveAll()
    {
        basecamp.decide = [this](const QString&, const QVariantMap& params) {
            if (params.contains("names")) return fakes::approve({{"names", params.value("names")}});
            const QString name = params.value("name").toString();
            const QString version = params.value("version").toString();
            for (const QVariant& v : modules.catalog) {
                const QVariantMap row = v.toMap();
                if (row.value("name").toString() != name) continue;
                for (const QVariant& r : row.value("versions").toList()) {
                    const QVariantMap release = r.toMap();
                    if (release.value("manifest").toMap().value("version").toString() != version) continue;
                    return fakes::approve({{"plan", QVariantList{fakes::release(
                        row.value("repositoryUrl").toString(), name, version,
                        release.value("rootHash").toString())}}});
                }
            }
            return fakes::approve({});
        };
    }
};
using Page = PageOf<>;

}  // namespace

// ── The list ─────────────────────────────────────────────────────────────

TEST(rows_come_by_repository_the_default_first_and_local_last_each_by_name) {
    Page p;
    p.modules.catalog = {pkg(kMine, "notes", "1.0.0", "N"),
                         pkg(kDefault, "wallet", "1.0.0", "W"),
                         pkg(kDefault, "Chat", "1.0.0", "C")};
    // Installed by the user, published by no repository.
    p.modules.addInstalled("scratch", "0.1.0", "S");
    p.packages.load();
    CHECK_EQ(p.names(), (QStringList{"Chat", "wallet", "notes", "scratch"}));
    CHECK_EQ(p.role("scratch", "sourceKey").toString(), QStringLiteral("local"));
    CHECK_EQ(p.packages.totalCount(), 4);
    CHECK_EQ(p.packages.repositoryCount(), 2);
    CHECK_EQ(p.packages.repositoryLabels().value(kDefault).toString(), QStringLiteral("Zed"));
    CHECK_EQ(p.packages.repositoryLabels().value("local").toString(), QStringLiteral("local"));
    CHECK_EQ(p.packages.hostVariant(), QStringLiteral("linux-x86_64"));
    CHECK(!p.packages.loading());
}

TEST(the_filters_narrow_the_rows_and_the_count) {
    Page p;
    p.modules.catalog = {withDisplayName(pkg(kDefault, "wallet", "1.0.0", "W"), "Wallet", "Keeps your keys"),
                         pkg(kDefault, "chat", "1.0.0", "C"),
                         catalogRow(kDefault, "chat_ui", "1.0.0", "U", {}, kHere, "ui_qml"),
                         elsewhere(kDefault, "far", "F")};
    p.modules.addInstalled("chat", "1.0.0", "C");
    p.packages.load();
    CHECK_EQ(p.names(), (QStringList{"chat", "chat_ui", "wallet"}));

    p.packages.setSearchText("keys");
    CHECK_EQ(p.names(), QStringList{"wallet"});
    CHECK_EQ(p.packages.totalCount(), 1);
    p.packages.setSearchText({});

    p.packages.setInstallState(PackageFilter::Installed);
    CHECK_EQ(p.names(), QStringList{"chat"});
    p.packages.setInstallState(PackageFilter::NotInstalled);
    CHECK_EQ(p.names(), (QStringList{"chat_ui", "wallet"}));
    p.packages.setInstallState(PackageFilter::AnyInstallState);

    CHECK_EQ(p.packages.types(), (QStringList{"All", "core", "ui_qml"}));
    p.packages.setTypeIndex(2);
    CHECK_EQ(p.names(), QStringList{"chat_ui"});
    CHECK_EQ(p.packages.typeIndex(), 2);
    p.packages.setTypeIndex(0);

    // What this host can't install, asked for.
    p.packages.setShowUnavailable(true);
    CHECK_EQ(p.names(), (QStringList{"chat", "chat_ui", "far", "wallet"}));
}

TEST(what_this_host_has_or_can_install_is_counted) {
    Page p;
    p.modules.catalog = {pkg(kDefault, "here", "1.0.0", "H"),
                         elsewhere(kDefault, "there", "T"),
                         elsewhere(kDefault, "kept", "K")};
    p.modules.addInstalled("kept", "1.0.0", "K");
    p.packages.load();
    CHECK_EQ(p.packages.availableHereCount(), 2);
    CHECK_EQ(p.names(), (QStringList{"here", "kept"}));
}

TEST(a_category_stays_picked_by_name_and_one_that_went_is_dropped) {
    Page p;
    p.modules.catalog = {withCategory(pkg(kDefault, "wallet", "1.0.0", "W"), "wallet"),
                         withCategory(pkg(kDefault, "chat", "1.0.0", "C"), "chat")};
    p.packages.load();
    CHECK_EQ(p.packages.categories(), (QStringList{"All", "Chat", "Wallet"}));
    p.packages.setCategoryIndex(2);
    CHECK_EQ(p.names(), QStringList{"wallet"});

    // A new category comes before it: still Wallet.
    p.modules.catalog.append(withCategory(pkg(kDefault, "apps", "1.0.0", "A"), "apps"));
    p.packages.onCatalogChanged();
    SPIN(20);
    CHECK_EQ(p.packages.categories(), (QStringList{"All", "Apps", "Chat", "Wallet"}));
    CHECK_EQ(p.packages.categoryIndex(), 3);
    CHECK_EQ(p.names(), QStringList{"wallet"});

    QSignalSpy filters(&p.packages, &Packages::filtersChanged);
    p.modules.catalog.removeFirst();
    p.packages.onCatalogChanged();
    SPIN(20);
    CHECK_EQ(p.packages.categories(), (QStringList{"All", "Apps", "Chat"}));
    CHECK_EQ(p.packages.categoryIndex(), 0);
    CHECK(filters.count() > 0);
    CHECK_EQ(p.names(), (QStringList{"apps", "chat"}));
}

TEST(rows_sort_by_a_column_within_each_repository_and_back_by_name) {
    Page p;
    p.modules.catalog = {withSize(pkg(kDefault, "b", "1.0.0", "B"), 30),
                         withSize(pkg(kDefault, "a", "1.0.0", "A"), 10),
                         withSize(pkg(kMine, "c", "1.0.0", "C"), 20)};
    p.packages.load();
    CHECK_EQ(p.names(), (QStringList{"a", "b", "c"}));
    p.packages.setSort("size", Qt::DescendingOrder);
    CHECK_EQ(p.names(), (QStringList{"b", "a", "c"}));
    CHECK_EQ(p.packages.sortRole(), QStringLiteral("size"));
    CHECK_EQ(p.packages.sortOrder(), static_cast<int>(Qt::DescendingOrder));
    // No column: by name, not in the catalog's order.
    p.packages.setSort({}, Qt::AscendingOrder);
    CHECK_EQ(p.names(), (QStringList{"a", "b", "c"}));
    CHECK_EQ(p.packages.sortRole(), QString());
}

TEST(the_page_is_a_slice_and_a_filter_starts_it_over) {
    Page p;
    for (const char* n : {"a", "b", "c", "d", "e"})
        p.modules.catalog.append(pkg(kDefault, n, "1.0.0", QString(n).toUpper()));
    p.packages.load();
    p.packages.setPageSize(2);
    CHECK_EQ(p.names(), (QStringList{"a", "b"}));
    CHECK_EQ(p.packages.totalCount(), 5);
    p.packages.setCurrentPage(3);
    CHECK_EQ(p.packages.currentPage(), 3);
    CHECK_EQ(p.names(), QStringList{"e"});
    p.packages.setSearchText("d");
    CHECK_EQ(p.packages.currentPage(), 1);
    CHECK_EQ(p.names(), QStringList{"d"});
}

// ── Rows of the page ─────────────────────────────────────────────────────

TEST(the_version_is_picked_on_the_row_the_page_shows) {
    Page p;
    p.modules.catalog = {pkg(kDefault, "a", "1.0.0", "A"),
                         withOlderVersion(pkg(kDefault, "b", "2.0.0", "B2"), "1.0.0", "B1")};
    p.modules.addInstalled("b", "2.0.0", "B2");
    p.packages.load();
    p.packages.setSearchText("b");
    p.packages.selectVersion(0, 1);
    CHECK_EQ(p.role("b", "version").toString(), QStringLiteral("1.0.0"));
    CHECK_EQ(p.role("b", "rowAction").toInt(), toInt(RowAction::Downgrade));
    CHECK_EQ(p.role("a", "version"), QVariant());
}

TEST(only_a_row_with_something_to_run_is_checked_and_the_plan_counts_them) {
    Page p;
    p.modules.catalog = {withDisplayName(pkg(kDefault, "a", "1.0.0", "A"), "Alpha"),
                         withOlderVersion(pkg(kDefault, "b", "2.0.0", "B2"), "1.0.0", "B1"),
                         pkg(kDefault, "c", "1.0.0", "C")};
    p.modules.addInstalled("b", "1.0.0", "B1");
    p.modules.addInstalled("c", "1.0.0", "C");
    p.packages.load();

    // The page's first row, as the view sees it.
    p.packages.setSearchText("b");
    p.packages.setChecked(0, true);
    CHECK_EQ(p.packages.runnableCount(), 1);
    CHECK_EQ(p.packages.actionSummary(), (QVariantMap{{"upgrade", 1}}));
    const QVariantMap item = p.packages.actionPlanItems().value(0).toMap();
    CHECK_EQ(item.value("name").toString(), QStringLiteral("b"));
    CHECK_EQ(item.value("action").toString(), QStringLiteral("upgrade"));
    CHECK_EQ(item.value("fromVersion").toString(), QStringLiteral("1.0.0"));
    CHECK_EQ(item.value("toVersion").toString(), QStringLiteral("2.0.0"));
    CHECK_EQ(item.value("repository").toString(), p.role("b", "repositoryDisplayName").toString());
    p.packages.setSearchText({});

    // c is current: nothing to run, so it can't be checked.
    p.packages.setChecked(2, true);
    CHECK_EQ(p.role("c", "isSelected").toBool(), false);
    p.packages.setChecked(0, true);
    CHECK_EQ(p.packages.runnableCount(), 2);
    CHECK_EQ(p.packages.actionSummary(), (QVariantMap{{"install", 1}, {"upgrade", 1}}));
    CHECK_EQ(p.packages.actionPlanItems().value(0).toMap().value("displayName").toString(),
             QStringLiteral("Alpha"));

    // Uninstalled: nothing to run on it is left checked, though it could be
    // installed again.
    p.modules.installed.remove("b");
    p.packages.onPackageUninstalled("b");
    CHECK_EQ(p.packages.runnableCount(), 1);
    SPIN(20);
    CHECK_EQ(p.role("b", "rowAction").toInt(), toInt(RowAction::Install));
    CHECK_EQ(p.packages.runnableCount(), 1);
    CHECK_EQ(p.packages.actionSummary(), (QVariantMap{{"install", 1}}));
}

TEST(details_are_found_by_name_and_repository) {
    Page p;
    p.modules.catalog = {withDisplayName(pkg(kDefault, "notes", "1.0.0", "N1"), "Notes"),
                         withDisplayName(pkg(kMine, "notes", "2.0.0", "N2"), "My Notes")};
    p.packages.load();
    CHECK_EQ(p.packages.details("notes", kMine).value("version").toString(), QStringLiteral("2.0.0"));
    CHECK_EQ(p.packages.details("notes", kDefault).value("version").toString(), QStringLiteral("1.0.0"));
    CHECK(p.packages.details("nothing", {}).isEmpty());
    CHECK_EQ(p.packages.displayNameFor("notes"), QStringLiteral("Notes"));
    CHECK_EQ(p.packages.displayNameFor("nothing"), QStringLiteral("nothing"));
}

// ── Actions ──────────────────────────────────────────────────────────────

TEST(an_install_asks_basecamp_first_and_says_how_it_ended) {
    Page p;
    p.modules.catalog = {withDisplayName(pkg(kDefault, "wallet", "1.0.0", "W"), "Wallet")};
    p.modules.serve("wallet", "1.0.0", "W");
    p.packages.load();
    p.packages.install("wallet", "1.0.0", kDefault);
    CHECK_EQ(p.lastIntent(), QString(Consent::kConfirmInstall));
    CHECK_EQ(p.lastParams().value("name").toString(), QStringLiteral("wallet"));
    CHECK_EQ(p.lastParams().value("version").toString(), QStringLiteral("1.0.0"));
    CHECK_EQ(p.lastParams().value("repositoryUrl").toString(), kDefault);
    CHECK(p.packages.busy());
    CHECK(p.changes().isEmpty());

    p.basecamp.waiting.takeFirst()(
        fakes::approve({{"plan", QVariantList{fakes::release(kDefault, "wallet", "1.0.0", "W")}}}));
    CHECK_EQ(p.changes(), QStringList{"install wallet"});
    CHECK_EQ(p.finished.size(), 1);
    CHECK_EQ(p.finished.last().name, QStringLiteral("Wallet"));
    CHECK_EQ(p.finished.last().outcome, outcome(Installer::Outcome::Done));
    CHECK(!p.packages.busy());
}

TEST(an_upgrade_names_its_way) {
    Page p;
    p.modules.catalog = {withOlderVersion(pkg(kDefault, "wallet", "2.0.0", "W2"), "1.0.0", "W1")};
    p.modules.addInstalled("wallet", "2.0.0", "W2");
    p.packages.load();
    p.packages.upgrade("wallet", "1.0.0", 1, kDefault);
    CHECK_EQ(p.lastIntent(), QString(Consent::kConfirmUpgrade));
    CHECK_EQ(p.lastParams().value("version").toString(), QStringLiteral("1.0.0"));
    CHECK_EQ(p.lastParams().value("mode").toInt(), 1);
}

TEST(a_refusal_says_why_and_a_cancel_says_nothing_more) {
    Page p;
    p.modules.catalog = {withDisplayName(pkg(kDefault, "wallet", "1.0.0", "W"), "Wallet")};
    p.packages.load();
    p.basecamp.decide = [](const QString&, const QVariantMap&) { return fakes::refuse("unavailable"); };
    p.packages.install("wallet", "1.0.0", kDefault);
    CHECK_EQ(p.finished.size(), 1);
    CHECK_EQ(p.finished.last().name, QStringLiteral("Wallet"));
    CHECK_EQ(p.finished.last().outcome, outcome(Installer::Outcome::Failed));
    CHECK(p.finished.last().error.contains("unavailable"));
    SPIN(10);

    p.basecamp.decide = [](const QString&, const QVariantMap&) { return fakes::refuse("cancelled"); };
    p.packages.upgrade("wallet", "1.0.0", 2, kDefault);
    CHECK_EQ(p.finished.size(), 2);
    CHECK_EQ(p.finished.last().outcome, outcome(Installer::Outcome::Cancelled));
    CHECK(p.changes().isEmpty());
    CHECK(p.failures.isEmpty());
}

TEST(an_uninstall_asks_basecamp_then_removes) {
    Page p;
    p.modules.catalog = {pkg(kDefault, "wallet", "1.0.0", "W")};
    p.modules.addInstalled("wallet", "1.0.0", "W");
    p.packages.load();
    p.packages.uninstall("wallet");
    CHECK_EQ(p.lastIntent(), QString(Consent::kConfirmUninstall));
    CHECK_EQ(p.lastParams().value("names").toStringList(), QStringList{"wallet"});
    CHECK(p.changes().isEmpty());
    p.basecamp.waiting.takeFirst()(fakes::approve({{"names", QStringList{"wallet"}}}));
    CHECK_EQ(p.changes(), QStringList{"uninstall wallet"});
    CHECK_EQ(p.finished.last().outcome, outcome(Installer::Outcome::Done));
}

// Known by its install type, not by the words package_manager refuses it
// with: a package that came with Basecamp can't go, and Basecamp isn't
// asked.
TEST(a_package_that_came_with_basecamp_cant_be_uninstalled_and_says_so) {
    PageOf<Refusing> p;
    p.modules.refusal = "Cannot uninstall embedded package: wallet";
    p.modules.catalog = {pkg(kDefault, "wallet", "1.0.0", "W"), pkg(kDefault, "notes", "1.0.0", "N")};
    p.modules.addInstalled("wallet", "1.0.0", "W", "embedded");
    p.modules.addInstalled("notes", "1.0.0", "N");
    p.packages.load();
    p.approveAll();
    p.packages.uninstall("wallet");
    CHECK_EQ(p.failures.size(), 1);
    CHECK_EQ(static_cast<int>(p.failures.first()), static_cast<int>(Packages::Error::PackageNotUninstallable));
    CHECK(p.basecamp.asked.isEmpty());
    CHECK(p.finished.isEmpty());

    // The user's own: what package_manager says is what it said, "embedded"
    // in it or not.
    p.modules.refusal = "Cannot uninstall: an embedded copy is in use";
    p.packages.uninstall("notes");
    CHECK_EQ(p.failures.size(), 1);
    CHECK_EQ(p.finished.size(), 1);
    CHECK_EQ(p.finished.last().outcome, outcome(Installer::Outcome::Failed));
    CHECK(p.finished.last().error.contains("an embedded copy is in use"));
}

TEST(a_local_file_is_shown_to_basecamp_and_anything_else_is_refused_at_once) {
    Page p;
    p.packages.load();
    QTemporaryDir dir;
    const QString notes = dir.filePath("notes.txt");
    const QString lgx = dir.filePath("scratch.lgx");
    for (const QString& path : {notes, lgx}) {
        QFile f(path);
        CHECK(f.open(QIODevice::WriteOnly));
        f.write("scratch|0.1.0|S|");
    }
    p.packages.installFile(notes);
    p.packages.installFile(dir.filePath("missing.lgx"));
    p.packages.installFile({});
    CHECK_EQ(p.failures.size(), 3);
    for (Packages::Error e : p.failures)
        CHECK_EQ(static_cast<int>(e), static_cast<int>(Packages::Error::LocalPackageInvalid));
    CHECK(p.basecamp.asked.isEmpty());

    p.packages.installFile(lgx);
    CHECK_EQ(p.lastIntent(), QString(Consent::kConfirmInstall));
    CHECK_EQ(p.lastParams().value("name").toString(), QStringLiteral("scratch"));
    CHECK(!p.lastParams().value("localPath").toString().isEmpty());
    // A private copy, not the picked file.
    CHECK(p.lastParams().value("localPath").toString() != lgx);
}

TEST(reload_fetches_every_repository_again_and_forgets_what_failed) {
    Page p;
    p.modules.catalog = {pkg(kDefault, "wallet", "1.0.0", "W")};
    p.packages.load();
    // Approved, but nothing serves it: the download fails.
    p.approveAll();
    p.packages.install("wallet", "1.0.0", kDefault);
    CHECK_EQ(p.finished.last().outcome, outcome(Installer::Outcome::Failed));
    CHECK_EQ(p.role("wallet", "rowAction").toInt(), toInt(RowAction::Retry));

    // A load after a package change keeps it; Reload starts over.
    p.packages.onPackageInstalled();
    SPIN(20);
    CHECK_EQ(p.role("wallet", "rowAction").toInt(), toInt(RowAction::Retry));
    CHECK(!p.modules.calls.contains("refreshCatalog"));
    p.packages.refresh();
    CHECK(p.modules.calls.contains("refreshCatalog"));
    CHECK_EQ(p.role("wallet", "rowAction").toInt(), toInt(RowAction::Install));
}

// A read that failed is not an empty catalog: it says so, and the next good
// one clears it. The page said "No repositories configured".
TEST(a_failed_load_says_so_until_one_reads_everything) {
    Page p;
    p.modules.catalog = {pkg(kDefault, "wallet", "1.0.0", "W")};
    p.modules.failing << "getCatalog";
    QSignalSpy failed(&p.packages, &Packages::loadFailed);
    p.packages.load();
    CHECK(!p.packages.loaded());
    CHECK_EQ(p.packages.loadError(), QStringLiteral("the catalog"));
    CHECK_EQ(failed.count(), 1);
    CHECK_EQ(failed.at(0).at(0).toString(), QStringLiteral("the catalog"));
    p.modules.failing.clear();
    p.packages.refresh();
    CHECK(p.packages.loaded());
    CHECK_EQ(p.packages.loadError(), QString());
    CHECK_EQ(p.names(), QStringList{"wallet"});
    // A reload that fails keeps the list, and says so again.
    p.modules.failing << "getCatalog";
    p.packages.refresh();
    CHECK_EQ(failed.count(), 2);
    CHECK(p.packages.loaded());
    CHECK_EQ(p.names(), QStringList{"wallet"});
}

TEST(without_the_package_modules_nothing_is_asked) {
    Packages packages;
    QList<Packages::Error> failures;
    QObject::connect(&packages, &Packages::failed, &packages,
                     [&failures](Packages::Error e) { failures.append(e); });
    packages.install("wallet", "1.0.0", kDefault);
    packages.upgrade("wallet", "1.0.0", 0, kDefault);
    packages.uninstall("wallet");
    CHECK_EQ(failures.size(), 3);
    for (Packages::Error e : failures)
        CHECK_EQ(static_cast<int>(e), static_cast<int>(Packages::Error::PackageManagerNotConnected));
    CHECK(!packages.busy());
}

// ── The downloader ───────────────────────────────────────────────────────

TEST(the_download_notice_reads_the_downloader) {
    Page p;
    p.modules.downloadSource = "logos";
    p.modules.storage = {{"state", "starting"}};
    QSignalSpy changed(&p.packages, &Packages::downloadStateChanged);
    p.packages.load();
    CHECK_EQ(p.packages.downloadSource(), QStringLiteral("logos"));
    CHECK_EQ(p.packages.storageState(), QStringLiteral("starting"));
    p.packages.onStorageStateChanged("failed", "no peers");
    CHECK_EQ(p.packages.storageState(), QStringLiteral("failed"));
    CHECK(changed.count() > 0);
}

// What intents_module delivers here: packages.show and packages.install, as
// on master, and nothing else, as metadata.json declares them. Building on
// basecamp-installer-common dropped packages.install.
TEST(it_provides_packages_show_and_packages_install_as_it_declares) {
    CHECK(Packages::provides(QStringLiteral("packages.show")));
    CHECK(Packages::provides(QStringLiteral("packages.install")));
    CHECK(!Packages::provides(QStringLiteral("apps.search")));
    CHECK(!Packages::provides(QStringLiteral("basecamp.packages.confirm_install")));
    CHECK(!Packages::provides(QString()));

    QFile metadata(QStringLiteral(PMUI_SOURCE_DIR "/../metadata.json"));
    CHECK(metadata.open(QIODevice::ReadOnly));
    QStringList declared;
    const QJsonArray provides =
        QJsonDocument::fromJson(metadata.readAll()).object().value(QStringLiteral("provides")).toArray();
    for (const QJsonValue& v : provides) declared << v.toObject().value(QStringLiteral("intent")).toString();
    declared.sort();
    CHECK_EQ(declared, (QStringList{QStringLiteral("packages.install"), QStringLiteral("packages.show")}));
}

TEST(download_progress_shows_on_the_row_installing) {
    PageOf<Holding> p;
    p.modules.catalog = {pkg(kDefault, "wallet", "1.0.0", "W")};
    p.modules.serve("wallet", "1.0.0", "W");
    p.packages.load();
    p.approveAll();
    p.packages.install("wallet", "1.0.0", kDefault);
    CHECK(p.modules.finish);
    p.packages.onDownloadProgress("wallet", 10, 100, "http");
    CHECK_EQ(p.role("wallet", "installStatus").toInt(),
             toInt(logos::installer::InstallStatus::Installing));
    CHECK_EQ(p.role("wallet", "downloadReceived").toULongLong(), 10ull);
    CHECK_EQ(p.role("wallet", "downloadTotal").toULongLong(), 100ull);
    CHECK_EQ(p.role("wallet", "downloadTransport").toString(), QStringLiteral("http"));
    p.modules.finish();
    CHECK_EQ(p.changes(), QStringList{"install wallet"});
    CHECK_EQ(p.finished.last().outcome, outcome(Installer::Outcome::Done));
}

CHECK_MAIN()
