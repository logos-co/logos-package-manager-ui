import QtQuick

// The enums the views read from the backend's replica, for the view tests,
// which run without it. tst_PackageManager checks them against
// src/package_manager_ui.rep.
QtObject {
    enum InstallStatus { NotInstalled = 0, Installing = 1, Installed = 2, Failed = 3, UpgradeAvailable = 4, DowngradeAvailable = 5, DifferentHash = 6 }
    enum ErrorType { NoError = 0, NoPackagesSelected = 3, PackageManagerNotConnected = 4, UninstallFailed = 5, PackageNotUninstallable = 6, LocalPackageInvalid = 7 }
    enum ProgressType { IdleState = 0, Started = 2, InProgress = 3, Completed = 4, ProgressFailed = 5 }
    enum NotAvailableReason { Available = 0, NoVariantsPublished = 1, BuildFlavorMismatch = 2, PlatformMismatch = 3, NotOnLogosStorage = 4, NotOverHttp = 5 }
    enum RowAction { Install = 0, Upgrade = 1, Downgrade = 2, Reinstall = 3, Retry = 4, NotAvailable = 5, NoOp = 6 }
    enum DownloadSource { NoSource = 0, Builtin = 1, GitHub = 2, Storage = 3, LocalFile = 4 }
}
