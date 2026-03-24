#include "PackageManagerBackend.h"
#include <QDebug>
#include <QVariant>
#include <QPointer>
#include <QSet>
#include "logos_sdk.h"

PackageManagerBackend::PackageManagerBackend(LogosAPI* logosAPI, QObject* parent)
    : QObject(parent)
    , m_packageModel(new PackageListModel(this))
    , m_selectedCategoryIndex(0)
    , m_logosAPI(logosAPI)
    , m_isInstalling(false)
{
    if (!m_logosAPI) {
        m_logosAPI = new LogosAPI("core", this);
    }

    reload();
}

PackageListModel* PackageManagerBackend::packages() const
{
    return m_packageModel;
}

QStringList PackageManagerBackend::categories() const
{
    return m_categories;
}

int PackageManagerBackend::selectedCategoryIndex() const
{
    return m_selectedCategoryIndex;
}

void PackageManagerBackend::setSelectedCategoryIndex(int index)
{
    if (m_selectedCategoryIndex != index && index >= 0 && index < m_categories.size()) {
        m_selectedCategoryIndex = index;
        emit selectedCategoryIndexChanged();
        reload();
    }
}

bool PackageManagerBackend::hasSelectedPackages() const
{
    return m_packageModel->getSelectedCount() > 0;
}

bool PackageManagerBackend::isInstalling() const
{
    return m_isInstalling;
}

void PackageManagerBackend::setIsInstalling(bool installing)
{
    if (m_isInstalling != installing) {
        m_isInstalling = installing;
        emit isInstallingChanged();
    }
}

void PackageManagerBackend::reload()
{
    if (!m_logosAPI || !m_logosAPI->getClient("package_downloader")->isConnected()) {
        qDebug() << "package_downloader not connected, cannot reload packages";
        return;
    }

    LogosModules logos(m_logosAPI);
    QPointer<PackageManagerBackend> self(this);

    // Fetch categories async, then packages, then installed + variants from package_manager
    logos.package_downloader.getCategoriesAsync([self](QVariant result) {
        if (!self) return;
        QStringList categories = result.toStringList();
        self->m_categories = categories;
        emit self->categoriesChanged();

        QString selectedCategory = self->m_categories.value(self->m_selectedCategoryIndex, "All");

        LogosModules logos2(self->m_logosAPI);
        logos2.package_downloader.getPackagesAsync(selectedCategory, [self](QVariantList packagesArray) {
            if (!self) return;

            // Now fetch installed packages and valid variants from package_manager
            LogosModules logos3(self->m_logosAPI);
            logos3.package_manager.getInstalledPackagesAsync([self, packagesArray](QVariantList installedPackages) {
                if (!self) return;

                LogosModules logos4(self->m_logosAPI);
                logos4.package_manager.getValidVariantsAsync([self, packagesArray, installedPackages](QVariant result) {
                    if (!self) return;
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
    // Build a set of installed package names for quick lookup
    QSet<QString> installedNames;
    for (const QVariant& val : installedPackages) {
        QVariantMap obj = val.toMap();
        installedNames.insert(obj.value("name").toString());
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

        // Determine install status from package-manager's installed list
        // Compare using moduleName which matches the name in manifest.json
        QString moduleName = obj.value("moduleName").toString();
        pkg["installStatus"] = installedNames.contains(moduleName)
            ? static_cast<int>(PackageTypes::Installed)
            : static_cast<int>(PackageTypes::NotInstalled);

        // Determine variant availability by checking if any of the package's
        // variants match the platform's valid variants
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
}

void PackageManagerBackend::processDownloadResults(const QVariantList& results)
{
    if (!m_logosAPI || !m_logosAPI->getClient("package_manager")->isConnected()) {
        qWarning() << "package_manager not connected, cannot install downloaded packages";
        setIsInstalling(false);
        return;
    }

    installNextPackage(results, 0, 0, results.size());
}

void PackageManagerBackend::installNextPackage(const QVariantList& results, int index, int completed, int totalPackages)
{
    // Skip failed downloads
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

        // Install this package async
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
                QString error = installResult.value("error").toString();
                emit self->installationProgressUpdated(
                    success ? static_cast<int>(PackageTypes::InProgress)
                            : static_cast<int>(PackageTypes::ProgressFailed),
                    packageName, newCompleted, totalPackages, success,
                    success ? "" : error);

                self->installNextPackage(results, index + 1, newCompleted, totalPackages);
            });
        return;
    }

    // All packages processed
    finishInstallation(completed);
}

void PackageManagerBackend::finishInstallation(int completed)
{
    m_packageModel->clearAllSelections();
    emit hasSelectedPackagesChanged();
    setIsInstalling(false);
    emit installationProgressUpdated(
        static_cast<int>(PackageTypes::Completed), "", completed, completed, true, "");
}


void PackageManagerBackend::install()
{
    if (m_isInstalling) {
        emit errorOccurred(static_cast<int>(PackageTypes::InstallationAlreadyInProgress));
        return;
    }

    if (m_packageModel->getSelectedCount() == 0) {
        emit errorOccurred(static_cast<int>(PackageTypes::NoPackagesSelected));
        return;
    }

    if (!m_logosAPI || !m_logosAPI->getClient("package_downloader")->isConnected()) {
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

    // Download all packages async; when done, process results (install each)
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
    emit hasSelectedPackagesChanged();
}
