#include "PackageRowBuilder.h"

#include <utility>

#include <QSet>

#include "PackageTypes.h"
#include "RowActionResolver.h"

namespace packagerow {

// Use the shared dotted-numeric comparator from RowActionResolver.h —
// PackageListModel::setRowVersion needs the same logic to recompute the
// per-row Action when the user moves the dropdown, so keeping versionCmp
// file-local here would force a copy. Bring it into the TU via a using.
using rowaction::versionCmp;

namespace {

// Split a variant string into (base, flavor). Variants are formatted as
// "<os>-<arch>" (release build, no flavor suffix) or "<os>-<arch>-<flavor>"
// where <flavor> is one of a known set ("dev", "portable")
std::pair<QString, QString> splitVariant(const QString& v)
{
    static const QSet<QString> kKnownFlavors = {
        QStringLiteral("dev"), QStringLiteral("portable")
    };
    const int lastDash = v.lastIndexOf(QLatin1Char('-'));
    if (lastDash <= 0) return {v, QString()};
    const QString trailing = v.mid(lastDash + 1);
    if (kKnownFlavors.contains(trailing)) {
        return {v.left(lastDash), trailing};
    }
    return {v, QString()};
}

// Classify why a package's offered variants don't intersect the platform's
// valid variants. The QML side (ActionPill, when rowAction==NotAvailable)
// maps the enum to user-facing copy via its tooltip.
//   - NoVariantsPublished: nothing offered — nothing to install anywhere.
//   - BuildFlavorMismatch: platform IS offered, wrong flavor (dev/portable/
//     release). User can switch basecamp build flavor to recover.
//   - PlatformMismatch: OS/arch not offered. User can't recover.
PackageTypes::NotAvailableReason classifyNotAvailable(
    const QStringList& offeredVariants, const QStringList& validVariants)
{
    if (offeredVariants.isEmpty()) return PackageTypes::NoVariantsPublished;

    QSet<QString> userBases;
    for (const QString& v : validVariants) userBases.insert(splitVariant(v).first);
    for (const QString& v : offeredVariants) {
        if (userBases.contains(splitVariant(v).first))
            return PackageTypes::BuildFlavorMismatch;
    }
    return PackageTypes::PlatformMismatch;
}

// Read an `origin*` field, falling back when the producer did not set one.
// An empty string counts as absent: the downloader stamps these
// unconditionally, so present-but-empty means the source repository never
// resolved, and the fallback is still the better answer.
QString originOf(const QVariantMap& src, const char* key, const QString& fallback)
{
    const QString v = src.value(QLatin1String(key)).toString();
    return v.isEmpty() ? fallback : v;
}

// A downloaded package installed before sources were recorded has none and
// counts as GitHub.
PackageTypes::DownloadSource downloadSourceOf(const QString& installType,
                                              const QString& source)
{
    if (installType.isEmpty()) {
        return PackageTypes::NoSource;
    }

    if (installType != QStringLiteral("user")) {
        return PackageTypes::Builtin;
    }

    if (source.startsWith(QStringLiteral("logos:"))) {
        return PackageTypes::Storage;
    }

    if (source.startsWith(QStringLiteral("file:"))) {
        return PackageTypes::LocalFile;
    }

    return PackageTypes::GitHub;
}

}  // namespace

// An entry is a plain name or an object carrying a version range and/or a
// signer DID; QML wants a string list, so an object becomes "name version
// [signer=…]". Shared, so a catalog row and a Local row read the same.
QStringList renderDependencies(const QVariantList& depsArray)
{
    QStringList deps;
    for (const QVariant& dep : depsArray) {
        if (dep.canConvert<QVariantMap>() && !dep.toString().size()) {
            const QVariantMap dm = dep.toMap();
            QString s = dm.value("name").toString();
            if (dm.contains("version")) s += QStringLiteral(" ") + dm.value("version").toString();
            if (dm.contains("signer"))
                s += QStringLiteral(" [signer=") + dm.value("signer").toString() + QStringLiteral("]");
            deps.append(s);
        } else {
            deps.append(dep.toString());
        }
    }
    return deps;
}

// Build one model row from one raw catalog row + the installed-by-name index +
// the valid-variants list for this platform. Pure transform; no instance state.
//
// Each catalog row has the multi-repo `index.json` shape produced by
// `package_downloader.getCatalog()`: a `versions[]` array (sorted newest-
// first) where every entry carries the embedded `manifest` for that
// version, plus a small set of header fields (`name`, `description`,
// `type`, `category`, `repositoryUrl`, `repositoryName`, …) that
// `getCatalogJson` lifts from `versions[0].manifest` for convenience.
// We pick `versions[0]` as the selected version (newest); a future
// per-row picker can swap the index without changing this transform.
QVariantMap buildPackageRow(const QVariantMap& obj,
                                   const QHash<QString, QVariantMap>& installedByName,
                                   const QStringList& validVariants)
{
    QVariantMap pkg;
    const QString name = obj.value("name").toString();

    const QVariantList rawVersions = obj.value("versions").toList();
    QVariantMap selectedVersion;
    if (!rawVersions.isEmpty()) selectedVersion = rawVersions.first().toMap();

    // Manifest of the selected (newest) version — every per-row field
    // not surfaced at the catalog-row top level is read from here.
    const QVariantMap manifest = selectedVersion.value("manifest").toMap();

    QString moduleName = obj.value("moduleName").toString();
    if (moduleName.isEmpty()) moduleName = manifest.value("name").toString();
    if (moduleName.isEmpty()) moduleName = name;

    QString displayName = obj.value("displayName").toString();
    if (displayName.isEmpty()) displayName = manifest.value("display_name").toString();
    if (displayName.isEmpty()) displayName = moduleName;

    pkg["name"] = name;
    pkg["moduleName"] = moduleName;
    pkg["displayName"] = displayName;
    // Header fields: prefer the catalog-row's lifted copy (which
    // getCatalogJson sets from versions[0].manifest), fall back to the
    // manifest itself if the catalog row didn't surface the field.
    pkg["description"] = obj.value("description").toString().isEmpty()
                         ? manifest.value("description").toString()
                         : obj.value("description").toString();
    pkg["type"] = obj.value("type").toString().isEmpty()
                  ? manifest.value("type").toString()
                  : obj.value("type").toString();
    pkg["category"] = obj.value("category").toString().isEmpty()
                      ? manifest.value("category").toString()
                      : obj.value("category").toString();

    pkg["repositoryUrl"]         = obj.value("repositoryUrl").toString();
    pkg["repositoryName"]        = obj.value("repositoryName").toString();
    pkg["repositoryDisplayName"] = obj.value("repositoryDisplayName").toString();

    // Where the package is actually PUBLISHED, which is not always the
    // repository the user configured. A catalog can draw packages from other
    // catalogs (logos-repo.json `includesUrl`), and a drawn-in package is
    // deliberately stamped with the CONFIGURED repository's url — the section
    // headers group on that, and an aggregate catalog would otherwise render
    // as no section at all. So `repository*` is the shelf it sits on and
    // `origin*` is who put it there; only the second answers "who am I
    // trusting for these bytes".
    //
    // Falls back to `repository*` rather than staying empty: a catalog that
    // draws from nobody has origin == repository by definition, and so does
    // every row from a downloader predating the field. One shape for every
    // row means no consumer has to special-case the absence.
    pkg["originRepositoryUrl"] =
        originOf(obj, "originRepositoryUrl", pkg["repositoryUrl"].toString());
    pkg["originRepositoryName"] =
        originOf(obj, "originRepositoryName", pkg["repositoryName"].toString());
    pkg["originRepositoryDisplayName"] =
        originOf(obj, "originRepositoryDisplayName",
                 pkg["repositoryDisplayName"].toString());

    // Trim each entry of versions[] into a model-friendly shape.
    QVariantList availableVersions;
    for (const QVariant& vv : rawVersions) {
        const QVariantMap vm = vv.toMap();
        const QVariantMap vManifest = vm.value("manifest").toMap();
        QVariantMap entry;
        entry["version"]      = vManifest.value("version").toString();
        entry["rootHash"]     = vm.value("rootHash").toString();
        entry["releasedAt"]   = vm.value("releasedAt").toString();
        entry["size"]         = vm.value("size");
        entry["publisherRef"] = vm.value("publisherRef").toString();
        entry["url"]          = vm.value("url").toString();

        QStringList urls = vm.value("urls").toStringList();

        if (urls.isEmpty()) {
            // Fall back to the single `url` if `urls` is empty.
            urls.append(vm.value("url").toString());
        }

        bool onStorage = false;
        bool onGitHub = false;

        for (const QString& url : urls) {
            if (url.startsWith(QStringLiteral("logos:"))) {
                onStorage = true;
            } else if (!url.isEmpty()) {
                onGitHub = true;
            }
        }

        // Only the transports the download source allows. A downloader that
        // predates the setting sends no `allowedSources`: all are allowed.
        const bool hasAllowed = vm.contains(QStringLiteral("allowedSources"));
        const QStringList allowed = vm.value(QStringLiteral("allowedSources")).toStringList();

        QVariantList sources;

        if (onStorage && (!hasAllowed || allowed.contains(QStringLiteral("logos")))) {
            sources.append(static_cast<int>(PackageTypes::Storage));
        }

        if (onGitHub && (!hasAllowed || allowed.contains(QStringLiteral("http")))) {
            sources.append(static_cast<int>(PackageTypes::GitHub));
        }

        entry["sources"]      = sources;
        entry["sourceAvailable"] = vm.value(QStringLiteral("sourceAvailable"), true).toBool();
        entry["sourceUnavailableReason"] = static_cast<int>(
            vm.value(QStringLiteral("requiredSource")).toString() == QLatin1String("http")
                ? PackageTypes::NotOverHttp : PackageTypes::NotOnLogosStorage);
        entry["signed"]       = vm.contains("signature");
        entry["signerDid"]    = vm.value("signature").toMap().value("did").toString();
        entry["manifest"]     = vManifest;
        // Per version, not just per package: after a merge one package's
        // versions can come from several catalogs, so the dropdown can offer
        // 1.2.0 from one and 1.0.0 from another. Mirrored onto the row by
        // PackageListModel::setRowVersion as the user moves the picker.
        entry["originRepositoryUrl"] =
            originOf(vm, "originRepositoryUrl", pkg["originRepositoryUrl"].toString());
        entry["originRepositoryName"] =
            originOf(vm, "originRepositoryName", pkg["originRepositoryName"].toString());
        availableVersions.append(entry);
    }
    pkg["availableVersions"]    = availableVersions;
    pkg["selectedVersionIndex"] = 0;

    // Release version comes from the selected version's manifest; root
    // hash comes from the catalog row's `rootHash` (set per version by
    // the index builder — authoritative over any hash inside the
    // manifest itself).
    const QString releaseVersion = manifest.value("version").toString();
    const QString releaseHash = selectedVersion.value("rootHash").toString();
    pkg["version"] = releaseVersion;
    pkg["hash"] = releaseHash;

    // Cross-reference against the on-disk install state.
    QString installedVersion;
    QString installedHash;
    QString installType;
    QString downloadSource;
    const bool isInstalled = installedByName.contains(moduleName);
    if (isInstalled) {
        const QVariantMap& inst = installedByName[moduleName];
        installedVersion = inst.value("version").toString();
        installedHash = inst.value("hashes").toMap().value("root").toString();
        // "embedded" or "user" — QML gates Uninstall on installType === "user".
        installType = inst.value("installType").toString();
        downloadSource = inst.value("source").toString();
    }
    pkg["installedVersion"] = installedVersion;
    pkg["installedHash"] = installedHash;
    pkg["installType"] = installType;
    pkg["downloadSource"] = static_cast<int>(downloadSourceOf(installType, downloadSource));
    rowaction::applyPickedSizeAndDate(pkg, 0);

    // Resolve install status. Embedded vs user doesn't change the status itself —
    // the QML side gates the Uninstall button on installType separately.
    int status = static_cast<int>(PackageTypes::NotInstalled);
    if (isInstalled) {
        if (releaseVersion.isEmpty() || installedVersion.isEmpty()) {
            // No version info to compare — assume same.
            status = static_cast<int>(PackageTypes::Installed);
        } else {
            const int cmp = versionCmp(installedVersion, releaseVersion);
            if (cmp < 0)      status = static_cast<int>(PackageTypes::UpgradeAvailable);
            else if (cmp > 0) status = static_cast<int>(PackageTypes::DowngradeAvailable);
            else if (!releaseHash.isEmpty() && !installedHash.isEmpty()
                     && releaseHash != installedHash)
                              status = static_cast<int>(PackageTypes::DifferentHash);
            else              status = static_cast<int>(PackageTypes::Installed);
        }
    }
    pkg["installStatus"] = status;
    pkg["errorMessage"] = QString();

    // Variant availability — true iff any of the package's offered
    // variants intersects this platform's valid-variants list. Variants
    // are the keys of the manifest's `main` map (`{variant: entry_path}`).
    QStringList offeredVariants;
    {
        const QVariantMap mainMap = manifest.value("main").toMap();
        for (auto it = mainMap.constBegin(); it != mainMap.constEnd(); ++it) {
            const QString s = it.key();
            if (!s.isEmpty()) offeredVariants.append(s);
        }
    }
    bool variantAvailable = false;
    for (const QString& s : offeredVariants) {
        if (validVariants.contains(s)) { variantAvailable = true; break; }
    }
    // QML-only ui_qml packages can have an empty `main` map (no backend
    // plugin); they install on every platform.
    if (!variantAvailable && offeredVariants.isEmpty()
        && manifest.value("type").toString() == QLatin1String("ui_qml")) {
        variantAvailable = true;
    }
    pkg["isVariantAvailable"] = variantAvailable;
    pkg["variantNotAvailableReason"] = static_cast<int>(
        variantAvailable ? PackageTypes::Available
                         : classifyNotAvailable(offeredVariants, validVariants));
    // The newest version may be one the download source cannot serve.
    rowaction::applyPickedSourceAvailability(pkg, 0);

    // ── Action-column inputs ────────────────────────────────────────
    // `rowAction` is the per-row primary action, resolved against the
    // INITIAL selected version (newest, i.e. versions[0]). It will be
    // recomputed by PackageListModel::setRowVersion() whenever the
    // user moves the dropdown — same helper, same inputs, fresh values.
    //
    // `updateAvailable` is a separate signal that stays put even as the
    // dropdown moves: it reflects "a strictly-newer-than-installed
    // version exists in the catalog", and drives the small marker on
    // the Version cell. Computed once here.
    pkg["rowAction"] = rowaction::resolveRowAction(
        isInstalled, variantAvailable && pkg.value("isSourceAvailable").toBool(), status,
        installedVersion, installedHash,
        /*selectedVersion=*/releaseVersion,
        /*selectedHash=*/releaseHash);
    pkg["updateAvailable"] = rowaction::hasUpdateAvailable(
        isInstalled, installedVersion, /*newestCatalogVersion=*/releaseVersion);

    // The catalog row lifts `dependencies` from versions[0].manifest; fall
    // back to the manifest itself for index entries that didn't.
    QVariantList depsArray = obj.value("dependencies").toList();
    if (depsArray.isEmpty()) depsArray = manifest.value("dependencies").toList();
    pkg["dependencies"] = renderDependencies(depsArray);

    return pkg;
}

// Build a "Local" row for an installed package that has no catalog entry.
// Rendered under a synthetic "Local" section that sits below all real repos
// in the grouped list. No versions/rowAction — the row exists only to show
// that the module is present on disk; upgrades reappear when a repo publishes
// it.
QVariantMap buildLocalPackageRow(const QVariantMap& installed)
{
    QVariantMap pkg;
    const QString name = installed.value("name").toString();
    QString moduleName = installed.value("moduleName").toString();
    if (moduleName.isEmpty()) moduleName = name;

    QString displayName = installed.value("displayName").toString();
    if (displayName.isEmpty()) displayName = moduleName;

    pkg["name"]        = name;
    pkg["moduleName"]  = moduleName;
    pkg["displayName"] = displayName;
    pkg["description"] = installed.value("description").toString();
    pkg["type"]        = installed.value("type").toString();
    // Preserve the module's own category (Networking / Chat / …). "Local" is
    // a repo-slot label, not a category — the Categories sidebar reflects
    // real values, not this synthetic bucket.
    pkg["category"]    = installed.value("category").toString();
    pkg["size"]        = 0;
    pkg["dateUpdated"] = QString();

    pkg["repositoryUrl"]         = QString();
    pkg["repositoryName"]        = QStringLiteral("local");
    pkg["repositoryDisplayName"] = QStringLiteral("local");
    // Nothing published a Local row anywhere; origin mirrors the slot so the
    // "is this drawn from elsewhere" comparison is simply false.
    pkg["originRepositoryUrl"]         = QString();
    pkg["originRepositoryName"]        = QStringLiteral("local");
    pkg["originRepositoryDisplayName"] = QStringLiteral("local");

    pkg["availableVersions"]    = QVariantList{};
    pkg["selectedVersionIndex"] = 0;

    const QString installedVersion = installed.value("version").toString();
    const QString installedHash    = installed.value("hashes").toMap().value("root").toString();
    pkg["version"]          = installedVersion;
    pkg["hash"]             = installedHash;
    pkg["installedVersion"] = installedVersion;
    pkg["installedHash"]    = installedHash;
    const QString installType = installed.value("installType").toString();
    pkg["installType"]      = installType;
    pkg["downloadSource"]   = static_cast<int>(downloadSourceOf(
                                  installType, installed.value("source").toString()));
    pkg["installStatus"]    = static_cast<int>(PackageTypes::Installed);
    pkg["errorMessage"]     = QString();
    pkg["isVariantAvailable"]   = true;
    pkg["notAvailableReason"]   = static_cast<int>(PackageTypes::Available);
    pkg["rowAction"]        = static_cast<int>(PackageTypes::NoOp);
    pkg["updateAvailable"]  = false;
    // From the installed record: a Local row is the only surface a package
    // outside every catalog has, and hardcoding this empty read as
    // "Dependencies: None". `dependencies` is a flat name list and
    // `dependencyConstraints` repeats just the entries that declared a range
    // or a signer, so re-join them to render what a catalog row renders.
    QVariantMap constraintByName;
    for (const QVariant& c : installed.value("dependencyConstraints").toList()) {
        const QVariantMap cm = c.toMap();
        constraintByName.insert(cm.value("name").toString(), cm);
    }
    QVariantList depsArray;
    for (const QVariant& dep : installed.value("dependencies").toList()) {
        const QString depName = dep.toString();
        depsArray.append(constraintByName.contains(depName)
                         ? constraintByName.value(depName) : QVariant(depName));
    }
    pkg["dependencies"] = renderDependencies(depsArray);
    return pkg;
}

}  // namespace packagerow
