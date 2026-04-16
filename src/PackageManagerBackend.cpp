#include "PackageManagerBackend.h"
#include <QDebug>
#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QPointer>
#include <QSet>
#include <QTimer>
#include <QVariant>
#include "logos_sdk.h"

PackageManagerBackend::PackageManagerBackend(LogosAPI* logosAPI, QObject* parent)
    : PackageManagerUiSimpleSource(parent)
    , m_packageModel(new PackageListModel(this))
    , m_logosAPI(logosAPI)
{
    // Initialise base-class properties to sane defaults.
    setSelectedCategoryIndex(0);
    setReleases(QStringList{"latest"});
    setSelectedReleaseIndex(0);
    setHasSelectedPackages(false);
    setIsInstalling(false);
    setIsLoading(false);

    // Changing the selected category triggers a partial reload (catalog only).
    connect(this, &PackageManagerUiSimpleSource::selectedCategoryIndexChanged,
            this, [this]() { refreshPackages(); });

    // Changing the release triggers a partial reload (no setRelease needed —
    // the tag is passed directly to each downloader call).
    connect(this, &PackageManagerUiSimpleSource::selectedReleaseIndexChanged,
            this, &PackageManagerBackend::onSelectedReleaseIndexChanged);

    if (!m_logosAPI) {
        m_logosAPI = new LogosAPI("core", this);
    }

    // Wire up the uninstall/upgrade cancellation event handlers. The module
    // fires these on every cancellation path (user cancel + ack timeout +
    // error paths); we filter the "user cancelled" reason to stay silent and
    // surface the others as toast messages via the existing progress-update
    // signal the QML layer already renders.
    subscribePackageManagerCancellationEvents();

    // Auto-refresh the catalog whenever package_manager's on-disk state
    // changes. This covers the user-reported bug where a Basecamp-initiated
    // uninstall (Modules tab) didn't flip the PMU row status — the events
    // fire on BOTH PMU- and Basecamp-initiated flows, so one subscription
    // keeps the catalog consistent in both directions. Uses a single debounce
    // timer so a batch install of N packages produces a single refresh.
    //
    // Target is refreshPackages() (catalog only), NOT reload(): a file
    // mutation only changes per-package installed state, it does not change
    // which releases exist on the server. Going through reload() would
    // re-fetch the release list AND reset the selected-release combo to
    // "latest" — both jarring side-effects for the user. The release list
    // is only refreshed on construction + explicit Reload-button clicks.
    m_refreshDebounceTimer = new QTimer(this);
    m_refreshDebounceTimer->setSingleShot(true);
    m_refreshDebounceTimer->setInterval(150);
    connect(m_refreshDebounceTimer, &QTimer::timeout,
            this, &PackageManagerBackend::refreshPackages);
    subscribePackageManagerRefreshEvents();

    // Listen for upgrade-uninstall-done events so PMU can drive the
    // download+install step automatically. Without this, confirmUpgrade only
    // removes the old version and the user would have to manually re-install.
    subscribePackageManagerUpgradeEvents();

    // Defer the first reload until the event loop starts so ui-host can signal
    // ready and the Package Manager tab appears immediately.
    QTimer::singleShot(0, this, &PackageManagerBackend::reload);
}

int PackageManagerBackend::versionCmp(const QString& a, const QString& b)
{
    const QStringList aParts = a.split('.');
    const QStringList bParts = b.split('.');
    const int n = std::max(aParts.size(), bParts.size());
    for (int i = 0; i < n; ++i) {
        const int av = (i < aParts.size()) ? aParts[i].toInt() : 0;
        const int bv = (i < bParts.size()) ? bParts[i].toInt() : 0;
        if (av < bv) return -1;
        if (av > bv) return 1;
    }
    return 0;
}

QString PackageManagerBackend::currentReleaseTag() const
{
    const QStringList& tags = releases();
    const int idx = selectedReleaseIndex();
    if (idx <= 0 || idx >= tags.size()) {
        // Index 0 is "latest", and out-of-range also defaults to "latest".
        // Pass empty string so the lib resolves to "latest".
        return QString();
    }
    return tags.at(idx);
}

PackageListModel* PackageManagerBackend::packages() const
{
    return m_packageModel;
}

QString PackageManagerBackend::displayNameForModule(QString moduleName)
{
    // Thin delegate to the local model. Needs to live here (not on the model
    // directly as a Q_INVOKABLE) because QAbstractItemModel replicas proxy
    // only the QAbstractItemModel interface — Q_INVOKABLE methods on the
    // concrete subclass don't cross the wire. Slots declared in the .rep do.
    if (!m_packageModel) return moduleName;
    QString display = m_packageModel->displayNameForModule(moduleName);
    return display.isEmpty() ? moduleName : display;
}

void PackageManagerBackend::refreshHasSelectedPackages()
{
    setHasSelectedPackages(m_packageModel->getSelectedCount() > 0);
}

void PackageManagerBackend::reload()
{
    if (!m_logosAPI
        || !m_logosAPI->getClient("package_downloader")->isConnected()
        || !m_logosAPI->getClient("package_manager")->isConnected()) {
        qDebug() << "package_downloader or package_manager not connected, cannot reload packages";
        return;
    }

    setIsLoading(true);

    QPointer<PackageManagerBackend> self(this);
    refreshReleases([self]() {
        if (!self) return;
        self->refreshPackages();
    });
}

void PackageManagerBackend::refreshReleases(std::function<void()> onDone)
{
    if (!m_logosAPI || !m_logosAPI->getClient("package_downloader")->isConnected()) {
        if (onDone) onDone();
        return;
    }

    LogosModules logos(m_logosAPI);
    QPointer<PackageManagerBackend> self(this);
    logos.package_downloader.getReleasesAsync([self, onDone](QVariantList releaseList) {
        if (!self) return;
        QStringList tags;
        tags << QStringLiteral("latest");
        for (const QVariant& v : releaseList) {
            QVariantMap rel = v.toMap();
            QString tag = rel.value("tag_name").toString();
            if (!tag.isEmpty()) {
                tags << tag;
            }
        }
        self->setReleases(tags);

        // Reset to "latest" on every full reload. Suppress the change handler so
        // the slot doesn't kick off another partial reload while we're already in
        // the middle of one (the caller's onDone callback will handle that).
        self->m_suppressReleaseChange = true;
        self->setSelectedReleaseIndex(0);
        self->m_suppressReleaseChange = false;

        if (onDone) onDone();
    });
}

void PackageManagerBackend::refreshPackages()
{
    if (!m_logosAPI
        || !m_logosAPI->getClient("package_downloader")->isConnected()
        || !m_logosAPI->getClient("package_manager")->isConnected()) {
        qDebug() << "package_downloader or package_manager not connected, cannot refresh packages";
        setIsLoading(false);
        return;
    }

    ++m_reloadGeneration;
    const int currentGeneration = m_reloadGeneration;
    setIsLoading(true);

    const QString tag = currentReleaseTag();

    LogosModules logos(m_logosAPI);
    QPointer<PackageManagerBackend> self(this);
    logos.package_downloader.getCategoriesAsync(tag, [self, currentGeneration, tag](QVariant result) {
        if (!self || self->m_reloadGeneration != currentGeneration) return;
        QStringList categoryList = result.toStringList();
        self->setCategories(categoryList);

        QString selectedCategory = categoryList.value(self->selectedCategoryIndex(), "All");

        LogosModules logos2(self->m_logosAPI);
        logos2.package_downloader.getPackagesAsync(tag, selectedCategory, [self, currentGeneration](QVariantList packagesArray) {
            if (!self || self->m_reloadGeneration != currentGeneration) return;

            LogosModules logos3(self->m_logosAPI);
            logos3.package_manager.getInstalledPackagesAsync([self, currentGeneration, packagesArray](QVariantList installedPackages) {
                if (!self || self->m_reloadGeneration != currentGeneration) return;

                LogosModules logos4(self->m_logosAPI);
                logos4.package_manager.getValidVariantsAsync([self, currentGeneration, packagesArray, installedPackages](QVariant result) {
                    if (!self || self->m_reloadGeneration != currentGeneration) return;
                    QStringList validVariants = result.toStringList();
                    self->setPackagesFromVariantList(packagesArray, installedPackages, validVariants);
                    self->setIsLoading(false);
                });
            });
        });
    });
}

void PackageManagerBackend::onSelectedReleaseIndexChanged()
{
    if (m_suppressReleaseChange) return;
    if (!m_logosAPI || !m_logosAPI->getClient("package_downloader")->isConnected()) {
        return;
    }

    // No setRelease call needed — the release tag is passed directly to each
    // downloader method call. Just kick off a catalog reload.
    refreshPackages();
}

void PackageManagerBackend::setPackagesFromVariantList(const QVariantList& packagesArray,
                                                        const QVariantList& installedPackages,
                                                        const QStringList& validVariants)
{
    QHash<QString, QVariantMap> installedByName;
    for (const QVariant& val : installedPackages) {
        QVariantMap obj = val.toMap();
        QString installedName = obj.value("name").toString();
        if (!installedName.isEmpty()) {
            installedByName.insert(installedName, obj);
        }
    }

    QList<QVariantMap> packages;
    for (const QVariant& value : packagesArray) {
        QVariantMap obj = value.toMap();
        QVariantMap pkg;
        QString name = obj.value("name").toString();
        QString moduleName = obj.value("moduleName").toString();
        pkg["name"] = name;
        pkg["moduleName"] = moduleName;
        pkg["description"] = obj.value("description").toString();
        pkg["type"] = obj.value("type").toString();
        pkg["category"] = obj.value("category").toString();

        // Pull release version + hash from manifest if present, otherwise fall
        // back to the top-level version (older list.json schemas).
        QVariantMap manifest = obj.value("manifest").toMap();
        QString releaseVersion = manifest.value("version").toString();
        if (releaseVersion.isEmpty()) {
            releaseVersion = obj.value("version").toString();
        }
        QString releaseHash = manifest.value("hashes").toMap().value("root").toString();

        pkg["version"] = releaseVersion;
        pkg["hash"] = releaseHash;

        QString installedVersion;
        QString installedHash;
        QString installType;
        bool isInstalled = installedByName.contains(moduleName);
        if (isInstalled) {
            const QVariantMap& inst = installedByName[moduleName];
            installedVersion = inst.value("version").toString();
            installedHash = inst.value("hashes").toMap().value("root").toString();
            // "embedded" or "user" — QML gates the Uninstall button on
            // installType === "user". Not-installed rows leave this empty.
            installType = inst.value("installType").toString();
        }
        pkg["installedVersion"] = installedVersion;
        pkg["installedHash"] = installedHash;
        pkg["installType"] = installType;

        int status;
        if (!isInstalled) {
            status = static_cast<int>(PackageTypes::NotInstalled);
        } else {
            // If we don't have version info on either side, fall back to a
            // simple "Installed" since we have nothing to compare.
            if (releaseVersion.isEmpty() || installedVersion.isEmpty()) {
                status = static_cast<int>(PackageTypes::Installed);
            } else {
                int cmp = versionCmp(installedVersion, releaseVersion);
                if (cmp < 0) {
                    status = static_cast<int>(PackageTypes::UpgradeAvailable);
                } else if (cmp > 0) {
                    status = static_cast<int>(PackageTypes::DowngradeAvailable);
                } else {
                    // Same version. Compare hashes when both are present.
                    if (!releaseHash.isEmpty() && !installedHash.isEmpty()
                        && releaseHash != installedHash) {
                        status = static_cast<int>(PackageTypes::DifferentHash);
                    } else {
                        status = static_cast<int>(PackageTypes::Installed);
                    }
                }
            }
        }
        pkg["installStatus"] = status;
        pkg["errorMessage"] = QString();

        QVariantList packageVariants = obj.value("variants").toList();
        bool variantAvailable = false;
        for (const QVariant& pv : packageVariants) {
            if (validVariants.contains(pv.toString())) {
                variantAvailable = true;
                break;
            }
        }
        pkg["isVariantAvailable"] = variantAvailable;

        QVariantList depsArray = obj.value("dependencies").toList();
        QStringList deps;
        for (const QVariant& dep : depsArray) {
            deps.append(dep.toString());
        }
        pkg["dependencies"] = deps;

        packages.append(pkg);
    }

    m_packageModel->setPackages(packages);
    refreshHasSelectedPackages();
}

void PackageManagerBackend::processDownloadResults(const QString& releaseTag, const QVariantList& results)
{
    if (!m_logosAPI || !m_logosAPI->getClient("package_manager")->isConnected()) {
        qWarning() << "package_manager not connected, cannot install downloaded packages";
        finishInstallation(0);
        return;
    }

    int totalPackages = m_packageModel ? m_packageModel->getSelectedCount() : results.size();
    installNextPackage(releaseTag, results, 0, 0, totalPackages);
}

void PackageManagerBackend::installOnePackage(const QString& releaseTag,
                                               const QVariantMap& dl,
                                               std::function<void(bool success, const QString& error)> onDone)
{
    Q_UNUSED(releaseTag);

    QString packageName = dl.value("name").toString();
    QString filePath = dl.value("path").toString();
    QString downloadError = dl.value("error").toString();

    if (filePath.isEmpty()) {
        qWarning() << "Download failed for" << packageName << ":" << downloadError;
        if (onDone) onDone(false, downloadError.isEmpty()
                                       ? QStringLiteral("Download failed")
                                       : downloadError);
        return;
    }

    qDebug() << "Download complete for" << packageName << "at" << filePath << "— installing...";
    LogosModules logos(m_logosAPI);
    QPointer<PackageManagerBackend> self(this);
    logos.package_manager.installPluginAsync(filePath, false,
        [self, packageName, onDone](QVariantMap installResult) {
            if (!self) return;
            bool success = !installResult.value("path").toString().isEmpty()
                        && !installResult.contains("error");
            QString err = installResult.value("error").toString();
            if (onDone) onDone(success, success
                                           ? QString()
                                           : (err.isEmpty()
                                                  ? QStringLiteral("Installation failed")
                                                  : err));
        });
}

void PackageManagerBackend::installNextPackage(const QString& releaseTag, const QVariantList& results, int index, int completed, int totalPackages)
{
    while (index < results.size()) {
        QVariantMap dl = results[index].toMap();
        QString packageName = dl.value("name").toString();

        // Bubble the per-package outcome back up through the model + progress
        // signal. installOnePackage handles both the download-failed fast path
        // (empty filePath) and the actual installPlugin IPC call.
        QPointer<PackageManagerBackend> self(this);
        installOnePackage(releaseTag, dl,
            [self, releaseTag, results, packageName, index, completed, totalPackages](bool success, const QString& err) {
                if (!self) return;

                int newCompleted = completed + 1;
                if (success) {
                    self->m_packageModel->updatePackageInstallation(
                        packageName, static_cast<int>(PackageTypes::Installed));
                } else {
                    self->m_packageModel->updatePackageInstallation(
                        packageName, static_cast<int>(PackageTypes::Failed), err);
                }
                emit self->installationProgressUpdated(
                    success ? static_cast<int>(PackageTypes::InProgress)
                            : static_cast<int>(PackageTypes::ProgressFailed),
                    packageName, newCompleted, totalPackages, success,
                    success ? "" : err);

                self->installNextPackage(releaseTag, results, index + 1, newCompleted, totalPackages);
            });
        return;
    }

    finishInstallation(completed);
}

void PackageManagerBackend::finishInstallation(int completed)
{
    m_packageModel->clearAllSelections();
    refreshHasSelectedPackages();
    setIsInstalling(false);
    emit installationProgressUpdated(
        static_cast<int>(PackageTypes::Completed), "", completed, completed, true, "");

    // Full catalog refresh — pulls `installType`, `installedVersion`, and
    // `installedHash` from the on-disk scan. Without this the rows stay in
    // whatever state updatePackageInstallation left them; specifically
    // `installType` is still empty, which means the "Uninstall" button
    // (gated on `installType === "user"`) won't render until the user
    // manually hits Reload. Mirrors the post-upgrade refresh at the end
    // of performPendingUpgrade.
    refreshPackages();
}

void PackageManagerBackend::install()
{
    if (isInstalling()) {
        emit errorOccurred(static_cast<int>(PackageTypes::InstallationAlreadyInProgress));
        return;
    }

    if (m_packageModel->getSelectedCount() == 0) {
        emit errorOccurred(static_cast<int>(PackageTypes::NoPackagesSelected));
        return;
    }

    if (!m_logosAPI
        || !m_logosAPI->getClient("package_downloader")->isConnected()
        || !m_logosAPI->getClient("package_manager")->isConnected()) {
        emit errorOccurred(static_cast<int>(PackageTypes::PackageManagerNotConnected));
        return;
    }

    setIsInstalling(true);

    QStringList selectedNames = m_packageModel->getSelectedPackageNames();
    emit installationProgressUpdated(
        static_cast<int>(PackageTypes::Started), "", 0, selectedNames.size(), true, "");

    for (const QString& packageName : selectedNames) {
        m_packageModel->updatePackageInstallation(packageName, static_cast<int>(PackageTypes::Installing));
    }

    const QString tag = currentReleaseTag();
    LogosModules logos(m_logosAPI);
    QPointer<PackageManagerBackend> self(this);
    logos.package_downloader.downloadPackagesAsync(tag, selectedNames, [self, tag](QVariantList results) {
        if (!self) return;
        self->processDownloadResults(tag, results);
    });
}

void PackageManagerBackend::requestPackageDetails(int index)
{
    QVariantMap pkg = m_packageModel->packageAt(index);
    if (pkg.isEmpty()) {
        return;
    }
    emit packageDetailsLoaded(pkg);
}

void PackageManagerBackend::togglePackage(int index, bool checked)
{
    m_packageModel->updatePackageSelection(index, checked);
    refreshHasSelectedPackages();
}

void PackageManagerBackend::uninstall(int index)
{
    if (!m_logosAPI || !m_logosAPI->getClient("package_manager")->isConnected()) {
        emit errorOccurred(static_cast<int>(PackageTypes::PackageManagerNotConnected));
        return;
    }

    // Resolve moduleName from our own PackageListModel — mirrors what
    // requestVersionChange already does for upgrade/downgrade/sidegrade.
    // Reading moduleName from the source-side model is synchronous and
    // race-free.
    //
    // The previous name-based signature asked QML to pass
    // `model.moduleName` read from the REPLICA side, which could return an
    // empty QVariant briefly after a beginResetModel/endResetModel window
    // (QRO roles are re-synced asynchronously). That forced a defensive
    // "if (!row.moduleName) return" guard in QML — a symptom, not a fix.
    // Taking the index and resolving it locally eliminates the race.
    const QVariantMap pkg = m_packageModel->packageAt(index);
    if (pkg.isEmpty()) {
        qWarning() << "PackageManagerBackend::uninstall invalid index:" << index;
        return;
    }
    const QString name = pkg.value("moduleName").toString();
    if (name.isEmpty()) {
        // Structural invariant: every catalog row carries moduleName
        // (setPackagesFromVariantList sets it from the manifest). If this
        // fires it's a data-population bug, not a replica race — surface
        // it loudly rather than silently dropping the click.
        qWarning() << "PackageManagerBackend::uninstall: row has no moduleName at index" << index;
        return;
    }

    // Stateless wrapper over package_manager.requestUninstall. The module:
    //   1. Sets its pending state + starts the 3s ack timer.
    //   2. Emits "beforeUninstall" with the installed-dependents list so
    //      Basecamp can ack + show the cascade dialog.
    //   3. Waits for Basecamp to call confirmUninstall / cancelUninstall
    //      after the user decides, or auto-cancels if no listener acked
    //      (headless runtime, stalled GUI).
    //
    // Success of the synchronous request just means "request accepted" —
    // the real outcome flows through uninstallFinished (success) or
    // uninstallCancelled (ack timeout / user cancel / error) events which
    // the constructor already subscribes to.
    LogosModules logos(m_logosAPI);
    QPointer<PackageManagerBackend> self(this);
    logos.package_manager.requestUninstallAsync(name,
        [self](QVariantMap result) {
            if (!self) return;
            if (!result.value("success", false).toBool()) {
                const QString err = result.value("error").toString();
                // Embedded-refusal surfaces distinctly so the QML toast is
                // clearer; every other failure funnels through UninstallFailed.
                const int errCode = err.contains("embedded", Qt::CaseInsensitive)
                                        ? static_cast<int>(PackageTypes::PackageNotUninstallable)
                                        : static_cast<int>(PackageTypes::UninstallFailed);
                emit self->errorOccurred(errCode);
            }
        });
}

void PackageManagerBackend::upgradePackage(int index)
{
    requestVersionChange(index, UpgradeMode::Upgrade);
}

void PackageManagerBackend::downgradePackage(int index)
{
    requestVersionChange(index, UpgradeMode::Downgrade);
}

void PackageManagerBackend::sidegradePackage(int index)
{
    requestVersionChange(index, UpgradeMode::Sidegrade);
}

void PackageManagerBackend::requestVersionChange(int index, UpgradeMode mode)
{
    if (!m_logosAPI
        || !m_logosAPI->getClient("package_manager")->isConnected()
        || !m_logosAPI->getClient("package_downloader")->isConnected()) {
        emit errorOccurred(static_cast<int>(PackageTypes::PackageManagerNotConnected));
        return;
    }

    QVariantMap pkg = m_packageModel->packageAt(index);
    if (pkg.isEmpty()) return;

    // The catalog row uses "moduleName" as the stable identifier — that's what
    // uninstallPackage and installPlugin both key on. The "name" field is the
    // display label and may not match.
    const QString moduleName = pkg.value("moduleName").toString();
    if (moduleName.isEmpty()) return;

    // Pin the release tag at request time so a user switching the release
    // combo mid-dialog doesn't retarget the install. The module holds this
    // as part of its pending state and uses it when the user confirms.
    const QString releaseTag = currentReleaseTag();

    LogosModules logos(m_logosAPI);
    QPointer<PackageManagerBackend> self(this);
    logos.package_manager.requestUpgradeAsync(moduleName, releaseTag,
        static_cast<int>(mode),
        [self](QVariantMap result) {
            if (!self) return;
            if (!result.value("success", false).toBool()) {
                emit self->errorOccurred(
                    static_cast<int>(PackageTypes::UninstallFailed));
            }
        });
}

void PackageManagerBackend::subscribePackageManagerCancellationEvents()
{
    if (!m_logosAPI) return;
    LogosAPIClient* client = m_logosAPI->getClient("package_manager");
    if (!client || !client->isConnected()) return;

    LogosModules logos(m_logosAPI);

    // The module emits these with a single string payload: a JSON-encoded
    // { name, reason } (uninstall) or { name, releaseTag, reason } (upgrade).
    // "user cancelled" means the user clicked Cancel in Basecamp's dialog —
    // stay silent (the user already knows). Other reasons (ack timeout,
    // module-side errors) surface as a toast via installationProgressUpdated
    // with the ProgressFailed type so the existing QML handler renders them.
    //
    // Keep the literal string in sync with package_manager.cpp's
    // emitCancellation reason for user-cancel.
    static const QString kReasonUserCancelled = QStringLiteral("user cancelled");

    QPointer<PackageManagerBackend> self(this);
    logos.package_manager.on("uninstallCancelled",
        [self](const QVariantList& data) {
            if (!self || data.isEmpty()) return;
            const QByteArray payload = data.first().toString().toUtf8();
            QJsonParseError parseErr{};
            const QJsonDocument doc = QJsonDocument::fromJson(payload, &parseErr);
            if (parseErr.error != QJsonParseError::NoError || !doc.isObject()) {
                return;
            }
            const QJsonObject obj = doc.object();
            const QString name   = obj.value("name").toString();
            const QString reason = obj.value("reason").toString();
            if (reason == kReasonUserCancelled) return;  // silent
            const QString msg = QStringLiteral("Uninstall of '%1' cancelled: %2")
                                    .arg(name, reason);
            // Dedicated cancellation channel — routing through
            // installationProgressUpdated(ProgressFailed, ...) used to make
            // QML render the message with a hardcoded "Failed to install"
            // prefix, which was misleading for uninstall cancellations.
            emit self->cancellationOccurred(name, msg);
        });

    logos.package_manager.on("upgradeCancelled",
        [self](const QVariantList& data) {
            if (!self || data.isEmpty()) return;
            const QByteArray payload = data.first().toString().toUtf8();
            QJsonParseError parseErr{};
            const QJsonDocument doc = QJsonDocument::fromJson(payload, &parseErr);
            if (parseErr.error != QJsonParseError::NoError || !doc.isObject()) {
                return;
            }
            const QJsonObject obj = doc.object();
            const QString name       = obj.value("name").toString();
            const QString releaseTag = obj.value("releaseTag").toString();
            const QString reason     = obj.value("reason").toString();
            if (reason == kReasonUserCancelled) return;  // silent
            const QString msg = QStringLiteral("Upgrade of '%1' (%2) cancelled: %3")
                                    .arg(name, releaseTag, reason);
            // Same rationale as uninstallCancelled above — use the dedicated
            // cancellation channel so the toast doesn't render as "Failed to
            // install".
            emit self->cancellationOccurred(name, msg);
        });
}

void PackageManagerBackend::subscribePackageManagerRefreshEvents()
{
    if (!m_logosAPI) return;
    LogosAPIClient* client = m_logosAPI->getClient("package_manager");
    if (!client || !client->isConnected()) return;

    LogosModules logos(m_logosAPI);

    // All four events carry the plugin name/path as their payload, but we
    // don't need it — any mutation invalidates the catalog row statuses, so
    // we just arm the debounce and let refreshPackages() rebuild the rows.
    // Releases + the selected-release combo are deliberately left alone
    // (see the m_refreshDebounceTimer wiring in the constructor). Rapid
    // bursts (batch install) collapse to a single refresh because start()
    // resets the pending tick.
    QPointer<PackageManagerBackend> self(this);
    auto arm = [self](const QVariantList&) {
        if (!self || !self->m_refreshDebounceTimer) return;
        self->m_refreshDebounceTimer->start();
    };

    logos.package_manager.on("corePluginFileInstalled", arm);
    logos.package_manager.on("uiPluginFileInstalled",   arm);
    logos.package_manager.on("corePluginUninstalled",   arm);
    logos.package_manager.on("uiPluginUninstalled",     arm);
}

void PackageManagerBackend::subscribePackageManagerUpgradeEvents()
{
    if (!m_logosAPI) return;
    LogosAPIClient* client = m_logosAPI->getClient("package_manager");
    if (!client || !client->isConnected()) return;

    LogosModules logos(m_logosAPI);
    QPointer<PackageManagerBackend> self(this);

    // The module emits upgradeUninstallDone after confirmUpgrade successfully
    // removes the old version. Payload is a JSON-encoded { name, releaseTag,
    // mode }. PMU drives the download+install of the new version from here.
    logos.package_manager.on("upgradeUninstallDone",
        [self](const QVariantList& data) {
            if (!self || data.isEmpty()) return;
            const QByteArray payload = data.first().toString().toUtf8();
            QJsonParseError parseErr{};
            const QJsonDocument doc = QJsonDocument::fromJson(payload, &parseErr);
            if (parseErr.error != QJsonParseError::NoError || !doc.isObject()) return;
            const QJsonObject obj = doc.object();
            const QString name       = obj.value("name").toString();
            const QString releaseTag = obj.value("releaseTag").toString();
            const int mode           = obj.value("mode").toInt();
            self->onUpgradeUninstallDone(name, releaseTag, mode);
        });
}

void PackageManagerBackend::onUpgradeUninstallDone(const QString& moduleName,
                                                    const QString& releaseTag,
                                                    int mode)
{
    // The module's upgradeUninstallDone payload carries the moduleName (stable
    // identifier the gated uninstall/upgrade flow keyed on) as `name`. Map it
    // back to the catalog `name` here — that's what package_downloader keys
    // on (see install() → downloadPackagesAsync(tag, selectedNames) where
    // selectedNames come from getSelectedPackageNames() → catalog names), and
    // it's what user-visible text should show. Fall back to moduleName if
    // no catalog row matches (package dropped from the active category filter
    // between requestUpgrade and upgradeUninstallDone — unlikely but cheap to
    // handle).
    const QString mapped = m_packageModel
        ? m_packageModel->displayNameForModule(moduleName) : QString();
    const QString displayName = mapped.isEmpty() ? moduleName : mapped;

    if (!m_logosAPI
        || !m_logosAPI->getClient("package_downloader")->isConnected()
        || !m_logosAPI->getClient("package_manager")->isConnected()) {
        const QString msg = QStringLiteral("Cannot complete upgrade of '%1': "
                                           "required modules not connected").arg(displayName);
        emit installationProgressUpdated(
            static_cast<int>(PackageTypes::ProgressFailed),
            displayName, 0, 0, false, msg);
        return;
    }

    // Cancel the pending debounced reload — the file-uninstall event
    // subscription (corePluginUninstalled / uiPluginUninstalled) already
    // armed it, but we're about to re-install the new version. Letting
    // the intermediate reload fire would flash the row as "Not Installed"
    // before the download finishes. The explicit refreshPackages() at
    // the end of the install callback brings the catalog fully up to date.
    if (m_refreshDebounceTimer) m_refreshDebounceTimer->stop();

    // Visual feedback — flip the row to Installing immediately so the user
    // sees progress rather than a transient "Not Installed" gap.
    // updatePackageInstallation matches on either name or moduleName.
    m_packageModel->updatePackageInstallation(
        displayName, static_cast<int>(PackageTypes::Installing));

    static const char* modeLabels[] = {"Upgrading", "Downgrading", "Sidegrading"};
    const char* label = (mode >= 0 && mode <= 2) ? modeLabels[mode] : "Upgrading";
    emit installationProgressUpdated(
        static_cast<int>(PackageTypes::InProgress),
        displayName, 0, 1, true,
        QStringLiteral("%1 %2\u2026").arg(label, displayName));

    // Download the new version from the release the user confirmed against.
    // downloadPackageAsync keys on the catalog name (displayName), not the
    // backend moduleName.
    LogosModules logos(m_logosAPI);
    QPointer<PackageManagerBackend> self(this);
    logos.package_downloader.downloadPackageAsync(releaseTag, displayName,
        [self, releaseTag, displayName, mode](QVariantMap dlResult) {
            if (!self) return;
            // installOnePackage expects { name, path, error } — the
            // downloadPackage return already has that shape.
            self->installOnePackage(releaseTag, dlResult,
                [self, displayName, mode](bool success, const QString& err) {
                    if (!self) return;
                    if (success) {
                        self->m_packageModel->updatePackageInstallation(
                            displayName, static_cast<int>(PackageTypes::Installed));
                    } else {
                        self->m_packageModel->updatePackageInstallation(
                            displayName, static_cast<int>(PackageTypes::Failed), err);
                    }

                    static const char* pastLabels[] = {
                        "Upgrade", "Downgrade", "Sidegrade"};
                    const char* opLabel = (mode >= 0 && mode <= 2)
                                              ? pastLabels[mode] : "Upgrade";

                    emit self->installationProgressUpdated(
                        success ? static_cast<int>(PackageTypes::Completed)
                                : static_cast<int>(PackageTypes::ProgressFailed),
                        displayName, 1, 1, success,
                        success ? QStringLiteral("")
                                : QStringLiteral("%1 of '%2' failed: %3")
                                      .arg(opLabel, displayName, err));

                    // Full catalog refresh — pull updated installType,
                    // installedVersion, installedHash from the on-disk scan.
                    self->refreshPackages();
                });
        });
}
