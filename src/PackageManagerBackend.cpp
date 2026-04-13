#include "PackageManagerBackend.h"
#include <QDebug>
#include <QTimer>
#include <QVariant>
#include <QPointer>
#include <QSet>
#include <QHash>
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
        bool isInstalled = installedByName.contains(moduleName);
        if (isInstalled) {
            const QVariantMap& inst = installedByName[moduleName];
            installedVersion = inst.value("version").toString();
            installedHash = inst.value("hashes").toMap().value("root").toString();
        }
        pkg["installedVersion"] = installedVersion;
        pkg["installedHash"] = installedHash;

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

void PackageManagerBackend::installNextPackage(const QString& releaseTag, const QVariantList& results, int index, int completed, int totalPackages)
{
    Q_UNUSED(releaseTag);

    while (index < results.size()) {
        QVariantMap dl = results[index].toMap();
        QString packageName = dl.value("name").toString();
        QString filePath = dl.value("path").toString();
        QString error = dl.value("error").toString();

        if (filePath.isEmpty()) {
            qWarning() << "Download failed for" << packageName << ":" << error;
            m_packageModel->updatePackageInstallation(
                packageName, static_cast<int>(PackageTypes::Failed),
                error.isEmpty() ? QStringLiteral("Download failed") : error);
            completed++;
            emit installationProgressUpdated(
                static_cast<int>(PackageTypes::ProgressFailed),
                packageName, completed, totalPackages, false, error);
            index++;
            continue;
        }

        qDebug() << "Download complete for" << packageName << "at" << filePath << "— installing...";
        LogosModules logos(m_logosAPI);
        QPointer<PackageManagerBackend> self(this);
        logos.package_manager.installPluginAsync(filePath, false,
            [self, releaseTag, results, packageName, index, completed, totalPackages](QVariantMap installResult) {
                if (!self) return;
                bool success = !installResult.value("path").toString().isEmpty()
                            && !installResult.contains("error");

                int newCompleted = completed + 1;
                QString err = installResult.value("error").toString();

                if (success) {
                    self->m_packageModel->updatePackageInstallation(
                        packageName, static_cast<int>(PackageTypes::Installed));
                } else {
                    self->m_packageModel->updatePackageInstallation(
                        packageName, static_cast<int>(PackageTypes::Failed),
                        err.isEmpty() ? QStringLiteral("Installation failed") : err);
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
