#pragma once

#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <functional>

// The per-row install loop, without the backend's IPC or model. A row of a
// host-selected optional package (`optional: true`) may fail without stopping
// the rest of the batch; any other failure stops it.
namespace install_sequence {

using Done = std::function<void(bool success, const QString& error)>;
using InstallOne = std::function<void(const QVariantMap& row, Done done)>;
// After each row: whether it succeeded, and whether the batch continues.
using OnRow = std::function<void(int index, bool success, bool proceed, const QString& error)>;

// Marks the rows named by `optionalPackages` (resolver request objects).
QVariantList tagOptionalRows(QVariantList rows, const QVariantList& optionalPackages);

void installSequentially(QVariantList rows, InstallOne installOne, OnRow onRow, int index = 0);

// Where an approved install comes from. A file the host read ({file}) must
// still be the one waiting under the name, or nothing is installed; a plan
// ({plan}) is always the catalogue's, whatever file waits under the name.
// From a host that says neither, the waiting file if there is one.
enum class Source { File, Catalog, Refused };
Source sourceOf(const QVariantMap& approval, bool fileWaiting);

// The resolver requests that hold a download to `plan`, the releases the host
// approved: each pinned to the version, root hash, repository and signer it
// was approved with.
QVariantList approvedRequests(const QVariantList& plan);

// `plan` without what is installed already as approved (name, version and
// root hash, against getInstalledPackages() rows), except `name`, the package
// asked for: an install queued behind another must not fetch again what that
// one installed.
QVariantList stillToInstall(const QVariantList& plan, const QVariantList& installed,
                            const QString& name);

// Why `rows` -- the resolver's, or what it downloaded -- hold a release `plan`
// does not, or empty when they hold none. A failed row is not looked at.
QString outsidePlan(const QVariantList& rows, const QVariantList& plan);

// One failed row for `name` saying `why`, so that nothing is installed.
QVariantList failedRow(const QString& name, const QVariantList& plan, const QString& why);

// `rows`, or failedRow() when outsidePlan() finds something.
QVariantList heldToPlan(const QVariantList& rows, const QVariantList& plan, const QString& name);

// Why the downloaded `row`, as verifyPackage() describes it in `verified`, is
// not signed as `plan` approved it -- by its signer, or by nobody -- or empty
// when it is.
QString signerChanged(const QVariantMap& verified, const QVariantList& plan, const QVariantMap& row);

// Why the file inspectPackage() describes as `inspected` is not the one the
// host approved ({rootHash, signer}) as `name` at `version`, or empty when it is.
QString fileChanged(const QVariantMap& inspected, const QVariantMap& approved,
                    const QString& name, const QString& version);

} // namespace install_sequence
