#pragma once

#include <QObject>

class PackageTypes : public QObject {
    Q_OBJECT

public:
    enum InstallStatus {
        NotInstalled = 0,
        Installing = 1,
        Installed = 2,
        Failed = 3,
        UpgradeAvailable = 4,
        DowngradeAvailable = 5,
        DifferentHash = 6
    };
    Q_ENUM(InstallStatus)

    enum ErrorType {
        NoError = 0,
        InstallationAlreadyInProgress = 2,
        NoPackagesSelected = 3,
        PackageManagerNotConnected = 4,
        UninstallFailed = 5,
        PackageNotUninstallable = 6
    };
    Q_ENUM(ErrorType)

    enum ProgressType {
        IdleState = 0,
        Started = 2,
        InProgress = 3,
        Completed = 4,
        ProgressFailed = 5
    };
    Q_ENUM(ProgressType)

    enum NotAvailableReason {
        Available = 0,
        NoVariantsPublished = 1,
        BuildFlavorMismatch = 2,
        PlatformMismatch = 3
    };
    Q_ENUM(NotAvailableReason)

    // Per-row primary action — resolved against the row's CURRENTLY
    // SELECTED dropdown version, not the catalog's newest. Drives the
    // Action column label and click target.
    //
    // Uninstall is intentionally absent: it lives in the row's overflow
    // ⋮ menu so the primary cell never offers a destructive default.
    //
    // The transient `Installing` state is intentionally NOT a RowAction
    // — it's not something the user can do, it's something the row is
    // already doing. The QML pill checks `installStatus === Installing`
    // independently and overlays the "Installing…" label/spinner. The
    // resolver folds the in-flight case into `NoOp` so the pill is
    // non-clickable while the install is mid-air.
    //
    // `NoOp` is also the terminal "installed and matches selected"
    // state (non-clickable; label "Installed").
    //
    // Mirrored 1:1 in package_manager_ui.rep#ENUM RowAction and on the
    // QML side via the generated PackageManagerUi imports.
    enum RowAction {
        Install      = 0,
        Upgrade      = 1,
        Downgrade    = 2,
        Reinstall    = 3,
        Retry        = 4,
        NotAvailable = 5,
        NoOp         = 6
    };
    Q_ENUM(RowAction)

    explicit PackageTypes(QObject* parent = nullptr) : QObject(parent) {}
};
