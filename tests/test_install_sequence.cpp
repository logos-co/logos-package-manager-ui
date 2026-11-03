// The per-row install loop (src/InstallSequence) with a fake installer: a
// host-selected optional package may fail without stopping the rest.

#include <logos_test.h>

#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

#include "InstallSequence.h"

namespace {

QVariantMap row(const QString& name) { return {{"name", name}}; }

struct Run {
    QStringList attempted, failed;
    bool lastProceed = true;
};

// Installs `rows` in order; rows named in `failing` fail.
Run run(const QVariantList& rows, const QStringList& failing)
{
    Run r;
    install_sequence::installSequentially(rows,
        [&](const QVariantMap& row, install_sequence::Done done) {
            const QString name = row.value("name").toString();
            r.attempted << name;
            done(!failing.contains(name), failing.contains(name) ? QStringLiteral("boom") : QString());
        },
        [&](int index, bool success, bool proceed, const QString&) {
            if (!success) r.failed << rows[index].toMap().value("name").toString();
            r.lastProceed = proceed;
        });
    return r;
}

} // namespace

LOGOS_TEST(selected_optional_rows_are_tagged) {
    const QVariantList rows = install_sequence::tagOptionalRows(
        {row("dep"), row("app"), row("opt")}, {QVariantMap{{"name", "opt"}, {"optional", true}}});
    LOGOS_ASSERT_TRUE(!rows.at(0).toMap().value("optional").toBool());
    LOGOS_ASSERT_TRUE(!rows.at(1).toMap().value("optional").toBool());
    LOGOS_ASSERT_TRUE(rows.at(2).toMap().value("optional").toBool());
}

LOGOS_TEST(failed_optional_row_does_not_stop_the_batch) {
    const QVariantList rows = install_sequence::tagOptionalRows(
        {row("dep"), row("app"), row("opt"), row("opt2")},
        {QVariantMap{{"name", "opt"}}, QVariantMap{{"name", "opt2"}}});
    const Run r = run(rows, {"opt"});
    LOGOS_ASSERT_EQ(r.attempted, (QStringList{"dep", "app", "opt", "opt2"}));
    LOGOS_ASSERT_EQ(r.failed, QStringList{"opt"});
    LOGOS_ASSERT_TRUE(r.lastProceed);
}

LOGOS_TEST(failed_required_row_stops_the_batch) {
    const QVariantList rows = install_sequence::tagOptionalRows(
        {row("dep"), row("app"), row("opt")}, {QVariantMap{{"name", "opt"}}});
    const Run r = run(rows, {"dep"});
    LOGOS_ASSERT_EQ(r.attempted, QStringList{"dep"});
    LOGOS_ASSERT_TRUE(!r.lastProceed);
}

LOGOS_TEST(untagged_batch_keeps_stopping_at_the_first_failure) {
    const Run r = run({row("a"), row("b"), row("c")}, {"b"});
    LOGOS_ASSERT_EQ(r.attempted, (QStringList{"a", "b"}));
}

// ── Holding an install to what the host approved ──

namespace {

QVariantMap approvedRow(const QString& name, const QString& version, const QString& rootHash)
{
    return {{"name", name}, {"version", version}, {"rootHash", rootHash},
            {"repositoryUrl", "https://repo.example"}};
}

QVariantMap downloaded(const QString& name, const QString& version, const QString& rootHash)
{
    return {{"name", name}, {"version", version}, {"rootHash", rootHash},
            {"path", "/tmp/" + name + ".lgx"}};
}

} // namespace

// Every release the host approved is asked for by itself, pinned: a newer
// release, or the same one signed by someone else, cannot stand in for it.
LOGOS_TEST(approved_releases_are_requested_pinned) {
    QVariantMap app = approvedRow("app", "2.0.0", "a");
    app["signer"] = "did:jwk:signer";
    QVariantMap opt = approvedRow("opt", "1.1.0", "o");
    opt["optional"] = true;
    const QVariantList requests = install_sequence::approvedRequests(
        {approvedRow("dep", "1.0.0", "d"), opt, app});
    LOGOS_ASSERT_EQ(requests.size(), 3);
    LOGOS_ASSERT_EQ(requests.at(0).toMap(), approvedRow("dep", "1.0.0", "d"));
    LOGOS_ASSERT_TRUE(requests.at(1).toMap().value("optional").toBool());
    LOGOS_ASSERT_TRUE(!requests.at(1).toMap().contains("signer"));
    LOGOS_ASSERT_EQ(requests.at(2).toMap(), app);
}

// What was approved installs as before.
LOGOS_TEST(an_approved_download_installs) {
    const QVariantList plan{approvedRow("dep", "1.0.0", "d"), approvedRow("app", "2.0.0", "a")};
    const QVariantList rows{downloaded("dep", "1.0.0", "d"), downloaded("app", "2.0.0", "a")};
    LOGOS_ASSERT_EQ(install_sequence::heldToPlan(rows, plan, "app"), rows);
}

// The download brought a release the user never saw -- the catalogue changed
// after the dialog: nothing is installed, and the row says why.
LOGOS_TEST(a_download_outside_the_plan_installs_nothing) {
    const QVariantList plan{approvedRow("app", "2.0.0", "a")};
    for (const QVariantMap& extra : {downloaded("rln", "0.1.0", "r"),       // a new dependency
                                     downloaded("app", "2.0.0", "other")}) { // the same version, rebuilt
        const QVariantList held = install_sequence::heldToPlan(
            {extra, downloaded("app", "2.0.0", "a")}, plan, "app");
        LOGOS_ASSERT_EQ(held.size(), 1);
        LOGOS_ASSERT_EQ(held.first().toMap().value("name").toString(), QString("app"));
        LOGOS_ASSERT_EQ(held.first().toMap().value("repositoryUrl").toString(), QString("https://repo.example"));
        LOGOS_ASSERT_TRUE(!held.first().toMap().contains("path"));
        LOGOS_ASSERT_TRUE(held.first().toMap().value("error").toString().contains(
            extra.value("name").toString() + " v" + extra.value("version").toString()));
    }
    // An approval of nothing installs nothing.
    LOGOS_ASSERT_EQ(install_sequence::heldToPlan({downloaded("app", "2.0.0", "a")}, {}, "app").size(), 1);
    LOGOS_ASSERT_TRUE(install_sequence::heldToPlan({downloaded("app", "2.0.0", "a")}, {}, "app")
                          .first().toMap().contains("error"));
}

// An upgrade checks what the resolver would fetch before it removes the old
// copy, so the rows it checks have no path yet.
LOGOS_TEST(what_would_be_fetched_is_held_to_the_plan_too) {
    const QVariantList plan{approvedRow("app", "2.0.0", "a")};
    const QVariantMap resolved{{"name", "rln"}, {"version", "0.1.0"}, {"rootHash", "r"},
                               {"url", "https://repo.example/rln.lgx"}};
    LOGOS_ASSERT_TRUE(install_sequence::outsidePlan({resolved}, plan).contains("rln v0.1.0"));
    LOGOS_ASSERT_EQ(install_sequence::outsidePlan({approvedRow("app", "2.0.0", "a")}, plan), QString());
}

// A row that failed to download installs nothing anyway, and keeps its own error.
LOGOS_TEST(a_failed_download_keeps_its_own_error) {
    const QVariantList rows{QVariantMap{{"name", "app"}, {"error", "no candidate matches 'app'"}}};
    LOGOS_ASSERT_EQ(install_sequence::heldToPlan(rows, {approvedRow("app", "2.0.0", "a")}, "app"), rows);
}

// Queued behind an install that brought `dep`, the next one does not fetch
// it again; the package asked for always is, even when it is a reinstall.
LOGOS_TEST(what_is_installed_already_is_not_fetched_again) {
    const QVariantList plan{approvedRow("dep", "1.0.0", "d"), approvedRow("lib", "2.0.0", "l"),
                            approvedRow("app", "2.0.0", "a")};
    const QVariantList installed{
        QVariantMap{{"name", "dep"}, {"version", "1.0.0"}, {"hashes", QVariantMap{{"root", "d"}}}},
        // Another release of lib: the plan's still has to be fetched.
        QVariantMap{{"name", "lib"}, {"version", "1.0.0"}, {"hashes", QVariantMap{{"root", "l1"}}}},
        QVariantMap{{"name", "app"}, {"version", "2.0.0"}, {"hashes", QVariantMap{{"root", "a"}}}}};
    const QVariantList left = install_sequence::stillToInstall(plan, installed, "app");
    LOGOS_ASSERT_EQ(left.size(), 2);
    LOGOS_ASSERT_EQ(left.at(0).toMap().value("name").toString(), QString("lib"));
    LOGOS_ASSERT_EQ(left.at(1).toMap().value("name").toString(), QString("app"));
    // Keyed by moduleName where the installed record has one, as the resolver keys it.
    const QVariantList byModule{QVariantMap{{"name", "Dep"}, {"moduleName", "dep"}, {"version", "1.0.0"},
                                            {"hashes", QVariantMap{{"root", "d"}}}}};
    LOGOS_ASSERT_EQ(install_sequence::stillToInstall(plan, byModule, "app").size(), 2);
}

// The download picks among a release's catalogue entries by root hash alone,
// which covers neither the manifest nor who signed it: each file is checked
// for the signer it was approved with, or for none.
LOGOS_TEST(a_download_must_be_signed_as_approved) {
    QVariantMap signedRow = approvedRow("app", "2.0.0", "a");
    signedRow["signer"] = "did:jwk:a";
    const QVariantList plan{signedRow, approvedRow("dep", "1.0.0", "d")};
    const QVariantMap app = downloaded("app", "2.0.0", "a");
    const QVariantMap dep = downloaded("dep", "1.0.0", "d");
    const auto verified = [](bool isSigned, const QString& did) {
        return QVariantMap{{"isSigned", isSigned}, {"signatureValid", isSigned}, {"packageValid", true},
                           {"signerDid", did}};
    };
    LOGOS_ASSERT_EQ(install_sequence::signerChanged(verified(true, "did:jwk:a"), plan, app), QString());
    LOGOS_ASSERT_TRUE(install_sequence::signerChanged(verified(true, "did:jwk:b"), plan, app).contains("app v2.0.0"));
    LOGOS_ASSERT_TRUE(!install_sequence::signerChanged(verified(false, ""), plan, app).isEmpty());
    QVariantMap forged = verified(true, "did:jwk:a");
    forged["signatureValid"] = false;
    LOGOS_ASSERT_TRUE(!install_sequence::signerChanged(forged, plan, app).isEmpty());
    // Approved unsigned: it must still be.
    LOGOS_ASSERT_EQ(install_sequence::signerChanged(verified(false, ""), plan, dep), QString());
    LOGOS_ASSERT_TRUE(!install_sequence::signerChanged(verified(true, "did:jwk:b"), plan, dep).isEmpty());
    // A check that did not run (no answer) proves nothing.
    LOGOS_ASSERT_TRUE(!install_sequence::signerChanged({}, plan, dep).isEmpty());
}

// Whether an approved install takes the file waiting under its name is the
// approval's to say: a plan never takes it, and a file approval with no file
// waiting installs nothing -- never the catalogue's release in its place.
LOGOS_TEST(the_approval_says_where_the_package_comes_from) {
    using install_sequence::Source;
    const QVariantMap file{{"file", QVariantMap{{"rootHash", "h1"}, {"signer", ""}}}};
    const QVariantMap plan{{"plan", QVariantList{approvedRow("app", "2.0.0", "a")}}};
    LOGOS_ASSERT_TRUE(install_sequence::sourceOf(file, true) == Source::File);
    LOGOS_ASSERT_TRUE(install_sequence::sourceOf(file, false) == Source::Refused);
    LOGOS_ASSERT_TRUE(install_sequence::sourceOf(plan, true) == Source::Catalog);
    LOGOS_ASSERT_TRUE(install_sequence::sourceOf(plan, false) == Source::Catalog);
    // From a host that says neither, as before.
    LOGOS_ASSERT_TRUE(install_sequence::sourceOf({}, true) == Source::File);
    LOGOS_ASSERT_TRUE(install_sequence::sourceOf({}, false) == Source::Catalog);
}

// A local file is installed only while it is the one the host read: the same
// root hash and manifest name and version (the root hash covers neither),
// signed by the same key, or by nobody as approved.
LOGOS_TEST(a_local_file_must_still_be_the_one_approved) {
    const QVariantMap approved{{"rootHash", "h1"}, {"signer", "did:jwk:a"}};
    const QVariantMap same{{"name", "app"}, {"version", "1.0.0"}, {"rootHash", "h1"},
                           {"signatureStatus", "signed"}, {"signerDid", "did:jwk:a"}};
    const auto changed = [](const QVariantMap& inspected, const QVariantMap& approval) {
        return !install_sequence::fileChanged(inspected, approval, "app", "1.0.0").isEmpty();
    };
    LOGOS_ASSERT_TRUE(!changed(same, approved));

    const auto with = [&same](const char* key, const QVariant& value) {
        QVariantMap m = same;
        m[key] = value;
        return m;
    };
    LOGOS_ASSERT_TRUE(changed(with("rootHash", "h2"), approved));
    LOGOS_ASSERT_TRUE(changed(with("signerDid", "did:jwk:b"), approved));
    LOGOS_ASSERT_TRUE(changed(with("signatureStatus", "unsigned"), approved));
    LOGOS_ASSERT_TRUE(changed(with("signatureStatus", "invalid"), approved));
    LOGOS_ASSERT_TRUE(changed(with("error", "Failed to load LGX package"), approved));
    // Same content, another package's manifest.
    LOGOS_ASSERT_TRUE(changed(with("name", "capability_module"), approved));
    LOGOS_ASSERT_TRUE(changed(with("version", "9.9.9"), approved));

    const QVariantMap unsignedApproval{{"rootHash", "h1"}, {"signer", ""}};
    QVariantMap plain = with("signatureStatus", "unsigned");
    plain.remove("signerDid");
    LOGOS_ASSERT_TRUE(!changed(plain, unsignedApproval));
    // Signed since, or a signature that no longer reads: not what was approved.
    LOGOS_ASSERT_TRUE(changed(same, unsignedApproval));
    LOGOS_ASSERT_TRUE(changed(with("signatureStatus", "error"), unsignedApproval));
    // An approval naming no file holds it to nothing that could match.
    LOGOS_ASSERT_TRUE(changed(plain, {}));
}
