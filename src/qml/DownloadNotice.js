.pragma library

// The notice at the top of the Package Manager, or null for none: why
// downloads may not work right now. The same rules and words as Basecamp's App
// Manager (DownloadFormat.downloadNotice there), except that Logos Storage
// being down while GitHub covers for it is shown here: this is where each
// download's source is visible. Offline: nothing here. Basecamp's banner says
// so over every view and app, and Storage being down is no news without
// internet.
//
// `source` is the download source ("any", "logos", "http"); `storageState`
// package_downloader's ("off", "waiting", "starting", "running", "failed", or
// "" not known).
function notice(online, source, storageState) {
    if (!online)
        return null

    const storageDown = storageState === "failed" || storageState === "off"
    const storageStarting = storageState === "waiting" || storageState === "starting"

    if (source === "logos" && storageDown)
        return { severity: "error", title: qsTr("Logos Storage isn't running"),
                 message: qsTr("Downloads are set to Logos Storage only, so apps can't be "
                               + "downloaded. You can enable GitHub downloads in Settings → "
                               + "Repositories.") }

    if (source === "logos" && storageStarting)
        return { severity: "info", title: qsTr("Starting Logos Storage…"),
                 message: qsTr("Downloads begin once it's running.") }

    if (source === "any" && storageDown)
        return { severity: "info", title: qsTr("Logos Storage isn't running"),
                 message: qsTr("Apps download from GitHub meanwhile. Basecamp tries Logos "
                               + "Storage again each time it starts.") }

    return null
}
