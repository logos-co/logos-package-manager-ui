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
