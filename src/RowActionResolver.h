#pragma once

// Single source of truth for the dotted-numeric version compare AND for
// the per-row Action resolution. The Action surfaced in the table's
// Action column must reflect the row's CURRENTLY SELECTED dropdown
// version, so this logic has to be reachable from two places: the
// initial row build in PackageManagerBackend.cpp::buildPackageRow (sets
// it against versions[0]) and PackageListModel::setRowVersion (recomputes
// against the picked version). The previous arrangement kept versionCmp
// file-local in PackageManagerBackend.cpp, which is exactly why
// setRowVersion couldn't recompute — and exactly why the "Upgrade" badge
// kept lying about installed/older picks.
//
// Header-only on purpose: no link-time coupling between the backend and
// the model.

#include <QString>
#include <QStringList>
#include <algorithm>

#include "PackageTypes.h"

namespace rowaction {

// Three-way dotted-numeric compare ("1.2.3" vs "1.2.10"). Missing
// trailing components are treated as 0. Returns -1 / 0 / +1.
//
// Same semantics as the previous file-local versionCmp in
// PackageManagerBackend.cpp — moved here verbatim so both buildPackageRow
// and setRowVersion can use it.
inline int versionCmp(const QString& a, const QString& b)
{
    const QStringList aParts = a.split('.');
    const QStringList bParts = b.split('.');
    const int n = std::max(aParts.size(), bParts.size());
    for (int i = 0; i < n; ++i) {
        const int av = (i < aParts.size()) ? aParts[i].toInt() : 0;
        const int bv = (i < bParts.size()) ? bParts[i].toInt() : 0;
        if (av < bv) return -1;
        if (av > bv) return  1;
    }
    return 0;
}

// Resolve the per-row primary action given (installed, selected,
// transient install state, variant availability). Order matters:
// Failed/Installing/NotAvailable short-circuit before the version
// comparison so a row mid-flight or unsupported on this platform never
// shows a runnable Action.
//
// `currentStatus` is the row's existing installStatus int (the original
// newest-vs-installed comparison) — only consulted for the Failed and
// Installing transient cases. The match between selected and installed
// (the "what would happen if you click?" question) is decided entirely
// by the version/hash comparison below, so the Action is independent of
// what the dropdown was BEFORE the user touched it.
//
// `Installing` does not get its own RowAction value — the QML pill
// overlays "Installing…" off `installStatus` directly, and we fold the
// in-flight case into `NoOp` so the pill is non-clickable.
inline int resolveRowAction(bool isInstalled,
                            bool variantAvailable,
                            int currentStatus,
                            const QString& installedVersion,
                            const QString& installedHash,
                            const QString& selectedVersion,
                            const QString& selectedHash)
{
    if (currentStatus == static_cast<int>(PackageTypes::Failed))
        return static_cast<int>(PackageTypes::Retry);
    if (currentStatus == static_cast<int>(PackageTypes::Installing))
        return static_cast<int>(PackageTypes::NoOp);

    // Installed rows take priority over the variant check: a module
    // already on disk is, by definition, available for *this* platform
    // (it was installable at some point — that's how it got installed).
    // Without this branch ordering, a catalog that's been rebuilt
    // without our platform's variant — e.g. chat_ui dropping darwin-
    // arm64 from its manifest.main — paints an obviously-installed row
    // as "Not available", which contradicts the row's own Installed/
    // DifferentHash details. We surface the user's actual situation:
    // they have it, version comparison decides what (if anything) they
    // can do next.
    if (isInstalled) {
        // No catalog variant for this platform → we can't actuate any
        // version change (a Reinstall would fail at download time
        // since there's nothing to download). NoOp is the honest
        // answer; uninstall stays reachable via the row's button.
        if (!variantAvailable)
            return static_cast<int>(PackageTypes::NoOp);

        // Anomalous: row marked installed but no version info to
        // compare. Collapse to NoOp rather than fabricate an arrow.
        if (installedVersion.isEmpty() || selectedVersion.isEmpty())
            return static_cast<int>(PackageTypes::NoOp);

        const int cmp = versionCmp(installedVersion, selectedVersion);
        if (cmp < 0) return static_cast<int>(PackageTypes::Upgrade);
        if (cmp > 0) return static_cast<int>(PackageTypes::Downgrade);

        // Versions match. Hash mismatch → Reinstall (e.g. release
        // re-signed, or local on-disk drift); both hashes empty / both
        // equal → NoOp.
        if (!selectedHash.isEmpty() && !installedHash.isEmpty()
            && selectedHash != installedHash)
            return static_cast<int>(PackageTypes::Reinstall);
        return static_cast<int>(PackageTypes::NoOp);
    }

    // Not installed. NotAvailable is the gate for "you can't install
    // this on this platform"; otherwise it's a fresh Install.
    if (!variantAvailable)
        return static_cast<int>(PackageTypes::NotAvailable);
    return static_cast<int>(PackageTypes::Install);
}

// Convenience: true iff there's an update strictly newer than what's
// installed (independent of the row's dropdown pick). Drives the small
// marker on the Version cell.
inline bool hasUpdateAvailable(bool isInstalled,
                               const QString& installedVersion,
                               const QString& newestCatalogVersion)
{
    if (!isInstalled) return false;
    if (installedVersion.isEmpty() || newestCatalogVersion.isEmpty()) return false;
    return versionCmp(installedVersion, newestCatalogVersion) < 0;
}

} // namespace rowaction
