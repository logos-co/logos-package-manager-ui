// Unit tests for the catalog-row / Local-row transform (src/PackageRowBuilder).
//
// These are pure QVariant in, QVariant out — no Qt host, no IPC, no app. The
// integration suite (tests/ui-tests.mjs) cannot cover the Local row at all:
// its fixture runs logos-standalone-app, which never calls
// package_manager.setUserModulesDirectory, so `getInstalledPackages()` is
// always empty there and no Local row is ever built.

#include <logos_test.h>

#include <QVariantList>
#include <QVariantMap>

#include "PackageRowBuilder.h"

namespace {

// The shape package_manager puts on the wire per installed package
// (PackageManagerImpl::toLogosMap): `dependencies` is a flat name list, and
// `dependencyConstraints` repeats ONLY the entries that declared a version
// range or a signer.
QVariantMap installedRecord(const QVariantList& dependencies,
                            const QVariantList& constraints = {})
{
    QVariantMap inst;
    inst["name"]        = QStringLiteral("token_list_ui");
    inst["displayName"] = QStringLiteral("Token Lists");
    inst["version"]     = QStringLiteral("0.1.0");
    inst["type"]        = QStringLiteral("ui_qml");
    inst["category"]    = QStringLiteral("wallet");
    inst["installType"] = QStringLiteral("user");
    QVariantMap hashes;
    hashes["root"] = QStringLiteral("6550574b0e76");
    inst["hashes"] = hashes;
    inst["dependencies"] = dependencies;
    if (!constraints.isEmpty()) inst["dependencyConstraints"] = constraints;
    return inst;
}

QVariantMap constraint(const QString& name, const QString& version)
{
    QVariantMap c;
    c["name"]    = name;
    c["version"] = version;
    return c;
}

}  // namespace

// The regression. A package no catalog publishes has the Local row as its
// ONLY surface, and this used to be hardcoded to an empty list — so the
// details panel said "Dependencies: None" for every one of them.
LOGOS_TEST(local_row_carries_the_installed_dependencies) {
    const QVariantMap row = packagerow::buildLocalPackageRow(
        installedRecord({QStringLiteral("token_list_module")}));

    const QStringList deps = row.value("dependencies").toStringList();
    LOGOS_ASSERT_EQ(deps.size(), 1);
    LOGOS_ASSERT_EQ(deps.at(0), QStringLiteral("token_list_module"));
}

// The version range survives the split wire shape: package_manager sends the
// name in `dependencies` and the range alongside in `dependencyConstraints`,
// and the row has to put them back together.
LOGOS_TEST(local_row_rejoins_a_constrained_dependency) {
    const QVariantMap row = packagerow::buildLocalPackageRow(
        installedRecord({QStringLiteral("token_list_module")},
                        {constraint(QStringLiteral("token_list_module"),
                                    QStringLiteral("~0.1.0"))}));

    const QStringList deps = row.value("dependencies").toStringList();
    LOGOS_ASSERT_EQ(deps.size(), 1);
    LOGOS_ASSERT_EQ(deps.at(0), QStringLiteral("token_list_module ~0.1.0"));
}

// `dependencies` is the edge list; a constraint naming something absent from
// it must not invent an edge, and the declared order is preserved.
LOGOS_TEST(local_row_takes_its_edges_from_dependencies_alone) {
    const QVariantMap row = packagerow::buildLocalPackageRow(
        installedRecord({QStringLiteral("eth_rpc_module"),
                         QStringLiteral("keystore_module")},
                        {constraint(QStringLiteral("keystore_module"),
                                    QStringLiteral("~0.1.0")),
                         constraint(QStringLiteral("never_declared"),
                                    QStringLiteral("~9.9.9"))}));

    const QStringList deps = row.value("dependencies").toStringList();
    LOGOS_ASSERT_EQ(deps.size(), 2);
    LOGOS_ASSERT_EQ(deps.at(0), QStringLiteral("eth_rpc_module"));
    LOGOS_ASSERT_EQ(deps.at(1), QStringLiteral("keystore_module ~0.1.0"));
}

LOGOS_TEST(local_row_with_no_declared_dependencies_stays_empty) {
    const QVariantMap row = packagerow::buildLocalPackageRow(installedRecord({}));
    LOGOS_ASSERT_TRUE(row.value("dependencies").toStringList().isEmpty());
}

// A catalog row keeps the manifest's object form rendering identically — the
// two paths share one renderer precisely so they can't drift apart again.
LOGOS_TEST(catalog_row_renders_an_object_form_dependency_the_same_way) {
    QVariantMap dep;
    dep["name"]    = QStringLiteral("token_list_module");
    dep["version"] = QStringLiteral("~0.1.0");

    QVariantMap manifest;
    manifest["name"]         = QStringLiteral("token_list_ui");
    manifest["version"]      = QStringLiteral("0.1.0");
    manifest["type"]         = QStringLiteral("ui_qml");
    manifest["dependencies"] = QVariantList{dep};

    QVariantMap version;
    version["manifest"] = manifest;
    version["rootHash"] = QStringLiteral("6550574b0e76");

    QVariantMap catalogRow;
    catalogRow["name"]     = QStringLiteral("token_list_ui");
    catalogRow["versions"] = QVariantList{version};

    const QVariantMap row = packagerow::buildPackageRow(catalogRow, {}, {});

    const QStringList deps = row.value("dependencies").toStringList();
    LOGOS_ASSERT_EQ(deps.size(), 1);
    LOGOS_ASSERT_EQ(deps.at(0), QStringLiteral("token_list_module ~0.1.0"));
}

// ─── Provenance ───────────────────────────────────────────────────────────────
//
// A catalog can draw packages from other catalogs. Such a row is deliberately
// stamped with the CONFIGURED repository's url — the list groups on that, and
// an aggregate catalog would otherwise render as no section at all — so
// `repository*` alone cannot say whose bytes a row carries. `origin*` is what
// answers that, and it has to survive the transform to reach the details panel.

namespace {

// A catalog row as `package_downloader.getCatalog()` emits it, with the
// repository / origin pair the caller wants to exercise.
QVariantMap provenanceRow(const QVariantList& versions,
                          const QString& originName = {},
                          const QString& originDisplay = {})
{
    QVariantMap row;
    row["name"]                  = QStringLiteral("chat_module");
    row["versions"]              = versions;
    row["repositoryUrl"]         = QStringLiteral("https://distro.example/logos-repo.json");
    row["repositoryName"]        = QStringLiteral("my-distro");
    row["repositoryDisplayName"] = QStringLiteral("My Distro");
    if (!originName.isEmpty()) {
        row["originRepositoryUrl"]         = QStringLiteral("https://b.example/logos-repo.json");
        row["originRepositoryName"]        = originName;
        row["originRepositoryDisplayName"] = originDisplay;
    }
    return row;
}

QVariantMap catalogVersion(const QString& version, const QString& hash,
                           const QString& originName = {})
{
    QVariantMap manifest;
    manifest["name"]    = QStringLiteral("chat_module");
    manifest["version"] = version;
    manifest["type"]    = QStringLiteral("core");
    QVariantMap v;
    v["manifest"] = manifest;
    v["rootHash"] = hash;
    if (!originName.isEmpty()) {
        v["originRepositoryName"] = originName;
        v["originRepositoryUrl"]  =
            QStringLiteral("https://%1.example/logos-repo.json").arg(originName);
    }
    return v;
}

}  // namespace

LOGOS_TEST(a_drawn_in_row_keeps_the_configured_repository_and_names_its_origin) {
    const QVariantMap row = packagerow::buildPackageRow(
        provenanceRow(QVariantList{catalogVersion(QStringLiteral("1.2.0"),
                                                  QStringLiteral("h_b"))},
                      QStringLiteral("team-b"), QStringLiteral("Team B")),
        {}, {});

    LOGOS_ASSERT_EQ(row.value("repositoryName").toString(), QStringLiteral("my-distro"));
    LOGOS_ASSERT_EQ(row.value("originRepositoryName").toString(), QStringLiteral("team-b"));
    LOGOS_ASSERT_EQ(row.value("originRepositoryDisplayName").toString(),
                    QStringLiteral("Team B"));
    LOGOS_ASSERT_EQ(row.value("originRepositoryUrl").toString(),
                    QStringLiteral("https://b.example/logos-repo.json"));
}

// A catalog that draws from nobody has origin == repository by definition, and
// so does every row from a downloader predating the field. One shape for every
// row means the details panel needs no special case for the absence.
LOGOS_TEST(a_row_with_no_origin_falls_back_to_its_repository) {
    const QVariantMap row = packagerow::buildPackageRow(
        provenanceRow(QVariantList{catalogVersion(QStringLiteral("1.2.0"),
                                                  QStringLiteral("h_a"))}),
        {}, {});

    LOGOS_ASSERT_EQ(row.value("originRepositoryName").toString(),
                    row.value("repositoryName").toString());
    LOGOS_ASSERT_EQ(row.value("originRepositoryDisplayName").toString(),
                    row.value("repositoryDisplayName").toString());
    LOGOS_ASSERT_EQ(row.value("originRepositoryUrl").toString(),
                    row.value("repositoryUrl").toString());
}

// Present-but-empty is the shape a source repository that never resolved
// produces. The fallback is still the better answer than a blank label.
LOGOS_TEST(an_empty_origin_is_treated_as_absent) {
    QVariantMap raw = provenanceRow(
        QVariantList{catalogVersion(QStringLiteral("1.2.0"), QStringLiteral("h_a"))});
    raw["originRepositoryName"]        = QString();
    raw["originRepositoryDisplayName"] = QString();

    const QVariantMap row = packagerow::buildPackageRow(raw, {}, {});
    LOGOS_ASSERT_EQ(row.value("originRepositoryName").toString(),
                    QStringLiteral("my-distro"));
}

// After a merge one package's versions can come from several catalogs, so the
// dropdown can offer 1.2.0 from one and 1.0.0 from another.
LOGOS_TEST(each_version_carries_the_catalog_that_published_it) {
    const QVariantMap row = packagerow::buildPackageRow(
        provenanceRow(QVariantList{
                          catalogVersion(QStringLiteral("1.2.0"), QStringLiteral("h_b"),
                                         QStringLiteral("team-b")),
                          catalogVersion(QStringLiteral("1.0.0"), QStringLiteral("h_root"),
                                         QStringLiteral("my-distro")),
                      },
                      QStringLiteral("team-b"), QStringLiteral("Team B")),
        {}, {});

    const QVariantList avail = row.value("availableVersions").toList();
    LOGOS_ASSERT_EQ(avail.size(), 2);
    LOGOS_ASSERT_EQ(avail.at(0).toMap().value("originRepositoryName").toString(),
                    QStringLiteral("team-b"));
    LOGOS_ASSERT_EQ(avail.at(1).toMap().value("originRepositoryName").toString(),
                    QStringLiteral("my-distro"));
}

// A version entry with no origin of its own belongs to whoever published the
// package — not to nobody.
LOGOS_TEST(a_version_without_its_own_origin_inherits_the_packages) {
    const QVariantMap row = packagerow::buildPackageRow(
        provenanceRow(QVariantList{catalogVersion(QStringLiteral("1.2.0"),
                                                  QStringLiteral("h_b"))},
                      QStringLiteral("team-b"), QStringLiteral("Team B")),
        {}, {});

    const QVariantList avail = row.value("availableVersions").toList();
    LOGOS_ASSERT_EQ(avail.size(), 1);
    LOGOS_ASSERT_EQ(avail.at(0).toMap().value("originRepositoryName").toString(),
                    QStringLiteral("team-b"));
}

// Nothing published a Local row anywhere, so the "is this drawn from
// elsewhere" comparison must simply be false rather than undefined.
LOGOS_TEST(a_local_row_reports_itself_as_its_own_origin) {
    const QVariantMap row = packagerow::buildLocalPackageRow(installedRecord({}));
    LOGOS_ASSERT_EQ(row.value("originRepositoryName").toString(),
                    row.value("repositoryName").toString());
    LOGOS_ASSERT_EQ(row.value("originRepositoryDisplayName").toString(),
                    QStringLiteral("local"));
}
