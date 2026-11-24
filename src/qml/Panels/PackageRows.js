.pragma library
.import Logos.PackageManagerUi 1.0 as Pmu

// A package row can be uninstalled when the user installed it and it is on
// disk: a built-in module, or one not installed, can't be.
function canUninstall(r) {
    if (!r || r.installType !== "user") return false
    const s = r.installStatus | 0
    return s === Pmu.PackageManagerUi.Installed
        || s === Pmu.PackageManagerUi.UpgradeAvailable
        || s === Pmu.PackageManagerUi.DowngradeAvailable
        || s === Pmu.PackageManagerUi.DifferentHash
}
