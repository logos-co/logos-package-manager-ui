#include "PackageManagerBackend.h"
#include <QDebug>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QPluginLoader>
#include <QTimer>
#include <QStandardPaths>
#include <algorithm>
#include "logos_sdk.h"

PackageManagerBackend::PackageManagerBackend(LogosAPI* logosAPI, QObject* parent)
    : QObject(parent)
    , m_selectedCategoryIndex(0)
    , m_detailsHtml("Select a package to view its details.")
    , m_hasSelectedPackages(false)
    , m_logosAPI(logosAPI)
    , m_isInstalling(false)
{
    subscribeToInstallationEvents();
    reload();
}

QVariantList PackageManagerBackend::packages() const
{
    return m_packages;
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

QString PackageManagerBackend::detailsHtml() const
{
    return m_detailsHtml;
}

bool PackageManagerBackend::hasSelectedPackages() const
{
    return m_hasSelectedPackages;
}

bool PackageManagerBackend::isInstalling() const
{
    return m_isInstalling;
}

void PackageManagerBackend::reload()
{
    if (!m_logosAPI || !m_logosAPI->getClient("package_manager")->isConnected()) {
        qDebug() << "LogosAPI not connected, cannot reload packages";
        return;
    }
    
    LogosModules logos(m_logosAPI);
    
    // Get categories first
    m_categories = logos.package_manager.getCategories();
    emit categoriesChanged();
    
    // Get filtered packages based on current category
    QString selectedCategory = m_categories.value(m_selectedCategoryIndex, "All");
    QJsonArray packagesArray = logos.package_manager.getPackages(selectedCategory);
    
    // Convert to QVariantList for QML
    m_packages.clear();
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
        pkg["isSelected"] = m_selectedPackages.contains(obj.value("name").toString());
        pkg["installed"] = obj.value("installed").toBool();
        
        // Store dependencies and files
        QJsonArray depsArray = obj.value("dependencies").toArray();
        QStringList deps;
        for (const QJsonValue& dep : depsArray) {
            deps.append(dep.toString());
        }
        pkg["dependencies"] = deps;
        
        m_packages.append(pkg);
    }
    
    emit packagesChanged();
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
    logos.package_manager.on("packageInstallationFinished", [this](const QVariantList& data) {
        if (data.size() < 3) {
            return;
        }
        QString packageName = data[0].toString();
        bool success = data[1].toBool();
        QString error = data[2].toString();
        
        QTimer::singleShot(0, this, [this, packageName, success, error]() {
            onPackageInstallationFinished(packageName, success, error);
        });
    });
}

void PackageManagerBackend::onPackageInstallationFinished(const QString& packageName, bool success, const QString& error)
{
    qDebug() << "Package installation finished:" << packageName << success << error;
    
    if (success) {
        emit packageInstalled(packageName);
        m_detailsHtml = QString("<h3>Installation Complete</h3><p>Successfully installed: <b>%1</b></p>")
            .arg(packageName);
    } else {
        QString failureReason = error.isEmpty() ? "installation failed" : error;
        m_detailsHtml = QString("<h3>Installation Failed</h3><p>Failed to install: <b>%1</b></p><p>Error: %2</p>")
            .arg(packageName)
            .arg(failureReason);
    }
    
    emit detailsHtmlChanged();
    
    // Check if this was the last package - the event is emitted for each package
    // When all done, m_isInstalling will be set to false in the final event
    QTimer::singleShot(500, this, [this]() {
        if (!m_isInstalling) {
            emit packagesInstalled();
            reload();  // Refresh package list
        }
    });
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

void PackageManagerBackend::install()
{
    if (m_isInstalling) {
        m_detailsHtml = "<p><b>Installation already in progress.</b> Please wait for it to complete.</p>";
        emit detailsHtmlChanged();
        return;
    }
    
    if (m_selectedPackages.isEmpty()) {
        m_detailsHtml = "No packages selected. Select at least one package to install.";
        emit detailsHtmlChanged();
        return;
    }
    
    if (!m_logosAPI || !m_logosAPI->getClient("package_manager")->isConnected()) {
        m_detailsHtml = "<p><b>Error:</b> package_manager not connected</p>";
        emit detailsHtmlChanged();
        return;
    }

    m_isInstalling = true;
    emit isInstallingChanged();
    
    m_detailsHtml = QString("<h3>Starting Installation...</h3><p>%1 package(s) to install.</p>")
        .arg(m_selectedPackages.size());
    emit detailsHtmlChanged();
    
    LogosModules logos(m_logosAPI);
    
    // Determine install directory (first package's type determines directory)
    QString installDir = determineInstallDirectory("");
    
    // Convert QSet to QStringList
    QStringList packageList = m_selectedPackages.values();
    
    logos.package_manager.installPackagesAsync(packageList, installDir);
}

void PackageManagerBackend::testPluginCall()
{
    if (m_logosAPI && m_logosAPI->getClient("package_manager")->isConnected()) {
        LogosModules logos(m_logosAPI);
        const QString result = logos.package_manager.testPluginCall("my test string");
        m_detailsHtml = QString("<h3>Test Call Result</h3><p>%1</p>")
                           .arg(result.toHtmlEscaped());
    } else {
        m_detailsHtml = "<p><b>Error:</b> package_manager not connected</p>";
    }
    emit detailsHtmlChanged();
}

void PackageManagerBackend::selectPackage(int index)
{
    if (index < 0 || index >= m_packages.size()) {
        return;
    }

    QVariantMap pkg = m_packages[index].toMap();
    QString packageName = pkg["name"].toString();
    QString description = pkg["description"].toString();
    QString type = pkg["type"].toString();
    QString moduleName = pkg["moduleName"].toString();
    QString category = pkg["category"].toString();
    bool installed = pkg["installed"].toBool();

        QString detailText = QString("<h2>%1</h2>").arg(packageName);
        
        if (!moduleName.isEmpty() && moduleName != packageName) {
            detailText += QString("<p><b>Module Name:</b> %1</p>").arg(moduleName);
        }
        
        detailText += QString("<p><b>Description:</b> %1</p>").arg(description.isEmpty() ? "No description available" : description);
        
        if (!type.isEmpty()) {
            detailText += QString("<p><b>Type:</b> %1</p>").arg(type);
        }
        
        if (!category.isEmpty()) {
            detailText += QString("<p><b>Category:</b> %1</p>").arg(category);
        }
        
    if (installed) {
        detailText += QString("<p><b>Status:</b> Installed</p>");
    }

    QVariantList dependencies = pkg["dependencies"].toList();
    if (!dependencies.isEmpty()) {
            detailText += "<p><b>Dependencies:</b></p><ul>";
        for (const QVariant& dependency : dependencies) {
            detailText += QString("<li>%1</li>").arg(dependency.toString());
            }
            detailText += "</ul>";
        } else {
            detailText += "<p><b>Dependencies:</b> None</p>";
        }

        m_detailsHtml = detailText;
    emit detailsHtmlChanged();
}

void PackageManagerBackend::togglePackage(int index, bool checked)
{
    if (index < 0 || index >= m_packages.size()) {
        return;
    }

    QVariantMap pkg = m_packages[index].toMap();
    QString packageName = pkg["name"].toString();

        if (checked) {
        m_selectedPackages.insert(packageName);
    } else {
        m_selectedPackages.remove(packageName);
    }

    // Update the package's selected state
    pkg["isSelected"] = checked;
    m_packages[index] = pkg;
    emit packagesChanged();
    
    bool hasSelected = !m_selectedPackages.isEmpty();
    if (m_hasSelectedPackages != hasSelected) {
        m_hasSelectedPackages = hasSelected;
        emit hasSelectedPackagesChanged();
    }
}
