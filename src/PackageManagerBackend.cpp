#include "PackageManagerBackend.h"
#include <QDebug>
#include <QTimer>
#include <QVariant>
#include <QPointer>
#include <QSet>
#include "logos_sdk.h"

PackageManagerBackend::PackageManagerBackend(LogosAPI* logosAPI, QObject* parent)
    : PackageManagerUiSimpleSource(parent)
    , m_packageModel(new PackageListModel(this))
    , m_logosAPI(logosAPI)
{
    // Initialise base-class properties to sane defaults.
    setSelectedCategoryIndex(0);
    setHasSelectedPackages(false);
    setIsInstalling(false);

    // Changing the selected category triggers a reload.
    connect(this, &PackageManagerUiSimpleSource::selectedCategoryIndexChanged,
            this, &PackageManagerBackend::reload);

    if (!m_logosAPI) {
        m_logosAPI = new LogosAPI("core", this);
    }

    // Defer the first reload until the event loop starts so ui-host can signal
    // ready and the Package Manager tab appears immediately.
    QTimer::singleShot(0, this, &PackageManagerBackend::reload);
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

    ++m_reloadGeneration;
    int currentGeneration = m_reloadGeneration;

    LogosModules logos(m_logosAPI);
    QPointer<PackageManagerBackend> self(this);
    logos.package_downloader.getCategoriesAsync([self, currentGeneration](QVariant result) {
        if (!self || self->m_reloadGeneration != currentGeneration) return;
        QStringList categoryList = result.toStringList();
        self->setCategories(categoryList);

        QString selectedCategory = categoryList.value(self->selectedCategoryIndex(), "All");

        LogosModules logos2(self->m_logosAPI);
        logos2.package_downloader.getPackagesAsync(selectedCategory, [self, currentGeneration](QVariantList packagesArray) {
            if (!self || self->m_reloadGeneration != currentGeneration) return;

            LogosModules logos3(self->m_logosAPI);
            logos3.package_manager.getInstalledPackagesAsync([self, currentGeneration, packagesArray](QVariantList installedPackages) {
                if (!self || self->m_reloadGeneration != currentGeneration) return;

                LogosModules logos4(self->m_logosAPI);
                logos4.package_manager.getValidVariantsAsync([self, currentGeneration, packagesArray, installedPackages](QVariant result) {
                    if (!self || self->m_reloadGeneration != currentGeneration) return;
                    QStringList validVariants = result.toStringList();
                    self->setPackagesFromVariantList(packagesArray, installedPackages, validVariants);
                });
            });
        });
    });
}

void PackageManagerBackend::setPackagesFromVariantList(const QVariantList& packagesArray,
                                                        const QVariantList& installedPackages,
                                                        const QStringList& validVariants)
{
    QSet<QString> installedNames;
    for (const QVariant& val : installedPackages) {
        QVariantMap obj = val.toMap();
        QString installedName = obj.value("name").toString();
        if (!installedName.isEmpty()) {
            installedNames.insert(installedName);
        }
    }

    QList<QVariantMap> packages;
    for (const QVariant& value : packagesArray) {
        QVariantMap obj = value.toMap();
        QVariantMap pkg;
        QString name = obj.value("name").toString();
        pkg["name"] = name;
        pkg["moduleName"] = obj.value("moduleName").toString();
        pkg["installedVersion"] = "";
        pkg["latestVersion"] = "";
        pkg["description"] = obj.value("description").toString();
        pkg["type"] = obj.value("type").toString();
        pkg["category"] = obj.value("category").toString();

        QString moduleName = obj.value("moduleName").toString();
        pkg["installStatus"] = installedNames.contains(moduleName)
            ? static_cast<int>(PackageTypes::Installed)
            : static_cast<int>(PackageTypes::NotInstalled);

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

void PackageManagerBackend::processDownloadResults(const QVariantList& results)
{
    if (!m_logosAPI || !m_logosAPI->getClient("package_manager")->isConnected()) {
        qWarning() << "package_manager not connected, cannot install downloaded packages";
        finishInstallation(0);
        return;
    }

    int totalPackages = m_packageModel ? m_packageModel->getSelectedCount() : results.size();
    installNextPackage(results, 0, 0, totalPackages);
}

void PackageManagerBackend::installNextPackage(const QVariantList& results, int index, int completed, int totalPackages)
{
    while (index < results.size()) {
        QVariantMap dl = results[index].toMap();
        QString packageName = dl.value("name").toString();
        QString filePath = dl.value("path").toString();
        QString error = dl.value("error").toString();

        if (filePath.isEmpty()) {
            qWarning() << "Download failed for" << packageName << ":" << error;
            m_packageModel->updatePackageInstallation(
                packageName, static_cast<int>(PackageTypes::Failed));
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
            [self, results, packageName, index, completed, totalPackages](QVariantMap installResult) {
                if (!self) return;
                bool success = !installResult.value("path").toString().isEmpty()
                            && !installResult.contains("error");

                if (success) {
                    self->m_packageModel->updatePackageInstallation(
                        packageName, static_cast<int>(PackageTypes::Installed));
                } else {
                    self->m_packageModel->updatePackageInstallation(
                        packageName, static_cast<int>(PackageTypes::Failed));
                }

                int newCompleted = completed + 1;
                QString err = installResult.value("error").toString();
                emit self->installationProgressUpdated(
                    success ? static_cast<int>(PackageTypes::InProgress)
                            : static_cast<int>(PackageTypes::ProgressFailed),
                    packageName, newCompleted, totalPackages, success,
                    success ? "" : err);

                self->installNextPackage(results, index + 1, newCompleted, totalPackages);
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

    LogosModules logos(m_logosAPI);
    QPointer<PackageManagerBackend> self(this);
    logos.package_downloader.downloadPackagesAsync(selectedNames, [self](QVariantList results) {
        if (!self) return;
        self->processDownloadResults(results);
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
