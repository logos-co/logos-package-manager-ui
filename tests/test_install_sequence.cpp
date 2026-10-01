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
