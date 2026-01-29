#pragma once

#include <QObject>
#include <QVariantList>
#include <QVariantMap>
#include <QStringList>
#include "logos_api.h"
#include "logos_api_client.h"
#include "PackageListModel.h"
#include "PackageTypes.h"

class PackageManagerBackend : public QObject {
    Q_OBJECT
    Q_PROPERTY(PackageListModel* packages READ packages CONSTANT)
    Q_PROPERTY(QStringList categories READ categories NOTIFY categoriesChanged)
    Q_PROPERTY(int selectedCategoryIndex READ selectedCategoryIndex WRITE setSelectedCategoryIndex NOTIFY selectedCategoryIndexChanged)
    Q_PROPERTY(bool hasSelectedPackages READ hasSelectedPackages NOTIFY hasSelectedPackagesChanged)
    Q_PROPERTY(bool isInstalling READ isInstalling NOTIFY isInstallingChanged)

public:
    explicit PackageManagerBackend(LogosAPI* logosAPI = nullptr, QObject* parent = nullptr);
    ~PackageManagerBackend() = default;

    PackageListModel* packages() const;
    QStringList categories() const;
    int selectedCategoryIndex() const;
    void setSelectedCategoryIndex(int index);
    bool hasSelectedPackages() const;
    bool isInstalling() const;

public slots:
    void reload();
    void install();
    void testPluginCall();
    void requestPackageDetails(int index);
    void togglePackage(int index, bool checked);

signals:
    void categoriesChanged();
    void selectedCategoryIndexChanged();
    void hasSelectedPackagesChanged();
    void isInstallingChanged();
        
    void errorOccurred(int errorType);
    void installationProgressUpdated(int progressType, const QString& packageName, int completed, int total, bool success, const QString& error);
    void packageDetailsLoaded(const QVariantMap& details);
    void testPluginResult(const QString& msg, bool error);

private:
    void subscribeToInstallationEvents();
    void onPackageInstallationFinished(const QString& packageName, bool success, const QString& error);
    QString determineInstallDirectory(const QString& packageType);
    void ensureDirectoriesSet();
    void setIsInstalling(bool installing);

    PackageListModel* m_packageModel;
    QStringList m_categories;
    int m_selectedCategoryIndex;
    LogosAPI* m_logosAPI;
    bool m_isInstalling;
};
