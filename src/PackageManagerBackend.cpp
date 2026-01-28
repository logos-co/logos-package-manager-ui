#include "PackageManagerBackend.h"
#include <QDebug>
#include <QJsonObject>
#include <QJsonArray>
#include <QCoreApplication>
#include <QDir>
#include <QPointer>
#include <QTimer>
#include <QStandardPaths>
#include "logos_sdk.h"

PackageManagerBackend::PackageManagerBackend(LogosAPI* logosAPI, QObject* parent)
    : QObject(parent)
    , m_packageModel(new PackageListModel(this))
    , m_selectedCategoryIndex(0)
    , m_logosAPI(logosAPI)
    , m_isInstalling(false)
{
    // Create our own LogosAPI instance if not provided (matches ChatWidget pattern)
    if (!m_logosAPI) {
        m_logosAPI = new LogosAPI("core", this);
    }
    
    subscribeToInstallationEvents();
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
        reload();  // Reload with new category filter
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
    if (!m_logosAPI || !m_logosAPI->getClient("package_manager")->isConnected()) {
        qDebug() << "LogosAPI not connected, cannot reload packages";
        return;
    }
    
    LogosModules logos(m_logosAPI);
    
    // Ensure directories are set before checking installed state
    // Without this the correct installed state is not known for the packages
    ensureDirectoriesSet();
    
    // Get categories first
    m_categories = logos.package_manager.getCategories();
    emit categoriesChanged();
    
    // Get filtered packages based on current category
    QString selectedCategory = m_categories.value(m_selectedCategoryIndex, "All");
    QJsonArray packagesArray = logos.package_manager.getPackages(selectedCategory);
    
    QList<QVariantMap> packages;
    for (const QJsonValue& value : packagesArray) {
        QJsonObject obj = value.toObject();
        QVariantMap pkg;
        pkg["name"] = obj.value("name").toString();
        pkg["moduleName"] = obj.value("moduleName").toString();
        pkg["installedVersion"] = "";  // Not used in new flow
        pkg["latestVersion"] = "";  // Not used in new flow
        pkg["description"] = obj.value("description").toString();
        pkg["type"] = obj.value("type").toString();
        pkg["category"] = obj.value("category").toString();
        pkg["installStatus"] = obj.value("installed").toBool()
            ? static_cast<int>(PackageTypes::Installed)
            : static_cast<int>(PackageTypes::NotInstalled);
        
        // Store dependencies and files
        QJsonArray depsArray = obj.value("dependencies").toArray();
        QStringList deps;
        for (const QJsonValue& dep : depsArray) {
            deps.append(dep.toString());
        }
        pkg["dependencies"] = deps;
        
        packages.append(pkg);
    }
    
    m_packageModel->setPackages(packages);
}

void PackageManagerBackend::subscribeToInstallationEvents()
{
    if (!m_logosAPI) {
        return;
    }
    
    LogosAPIClient* client = m_logosAPI->getClient("package_manager");
    if (!client || !client->isConnected()) {
        return;
    }
    
    LogosModules logos(m_logosAPI);
    // The event subscription outlives this QObject; guard against use-after-free.
    QPointer<PackageManagerBackend> self(this);
    logos.package_manager.on("packageInstallationFinished", [self](const QVariantList& data) {
        if (!self) {
            return;
        }
        if (data.size() < 3) {
            return;
        }
        QString packageName = data[0].toString();
        bool success = data[1].toBool();
        QString error = data[2].toString();
        
        // Run in the Qt event loop, but never use a deleted QObject as the timer context.
        QTimer::singleShot(0, QCoreApplication::instance(), [self, packageName, success, error]() {
            if (!self) {
                return;
            }
            self->onPackageInstallationFinished(packageName, success, error);
        });
    });
}

void PackageManagerBackend::onPackageInstallationFinished(const QString& packageName, bool success, const QString& error)
{
    qDebug() << "Package installation finished:" << packageName << success << error;

    m_packageModel->updatePackageInstallation(
        packageName, success ? static_cast<int>(PackageTypes::Installed) : static_cast<int>(PackageTypes::Failed));

    int completed = m_packageModel->getCompletedInstallCount();
    int totalPackages = m_packageModel->getSelectedCount();
    
    if (success) {
        emit installationProgressUpdated(
            static_cast<int>(PackageTypes::InProgress),
            packageName,
            completed,
            totalPackages,
            true,
            ""
        );
    } else {
        QString failureReason = error.isEmpty() ? "installation failed" : error;
        emit installationProgressUpdated(
            static_cast<int>(PackageTypes::ProgressFailed),
            packageName,
            completed,
            totalPackages,
            false,
            failureReason
        );
    }
    
    if (completed >= totalPackages) {
        m_packageModel->clearAllSelections();
        emit hasSelectedPackagesChanged();
        
        setIsInstalling(false);
        
        emit installationProgressUpdated(
            static_cast<int>(PackageTypes::Completed),
            "",
            completed,
            completed,
            true,
            ""
        );
    }
}

QString PackageManagerBackend::determineInstallDirectory(const QString& packageType)
{
    QDir appDir(QCoreApplication::applicationDirPath());
    appDir.cdUp();
    
    bool isUiPlugin = (packageType.compare("ui", Qt::CaseInsensitive) == 0);
    
    QString installDir;
    if (isUiPlugin) {
        // UI plugins go to /plugins directory
#ifdef LOGOS_DISTRIBUTED_BUILD
        // For distributed builds (DMG/AppImage), use Application Support
        installDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/plugins";
#else
        // For development builds (nix), use bundled plugins directory
        installDir = QDir::cleanPath(appDir.absolutePath() + "/plugins");
#endif
    } else {
        // Core modules go to /modules directory
#ifdef LOGOS_DISTRIBUTED_BUILD
        // For distributed builds (DMG/AppImage), use Application Support
        installDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/modules";
#else
        // For development builds (nix), use bundled modules directory
        installDir = QDir::cleanPath(appDir.absolutePath() + "/modules");
#endif
    }
    
    return installDir;
}

void PackageManagerBackend::ensureDirectoriesSet()
{
    if (!m_logosAPI || !m_logosAPI->getClient("package_manager")->isConnected()) {
        return;
    }
    
    LogosModules logos(m_logosAPI);
    
    QString modulesDir = determineInstallDirectory("");  // Core modules
    QString pluginsDir = determineInstallDirectory("ui");  // UI plugins
    
    qDebug() << "Setting package manager directories - modules:" << modulesDir << ", plugins:" << pluginsDir;
    logos.package_manager.setPluginsDirectory(modulesDir);
    logos.package_manager.setUiPluginsDirectory(pluginsDir);
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
    
    if (!m_logosAPI || !m_logosAPI->getClient("package_manager")->isConnected()) {
        emit errorOccurred(static_cast<int>(PackageTypes::PackageManagerNotConnected));
        return;
    }

    setIsInstalling(true);
    
    QStringList selectedNames = m_packageModel->getSelectedPackageNames();
    emit installationProgressUpdated(
        static_cast<int>(PackageTypes::Started),
        "",
        0,
        selectedNames.size(),
        true,
        ""
    );
    
    LogosModules logos(m_logosAPI);
    
    // Determine install directory (first package's type determines directory)
    QString installDir = determineInstallDirectory("");
    
    for (const QString& packageName : selectedNames) {
        m_packageModel->updatePackageInstallation(packageName, static_cast<int>(PackageTypes::Installing));
        logos.package_manager.installPackageAsync(packageName, installDir);
    }
}

void PackageManagerBackend::testPluginCall()
{
    if (m_logosAPI && m_logosAPI->getClient("package_manager")->isConnected()) {
        LogosModules logos(m_logosAPI);
        const QString result = logos.package_manager.testPluginCall("my test string");
        emit testPluginResult(result, false);
    } else {
        emit testPluginResult("package_manager not connected", true);
    }
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
