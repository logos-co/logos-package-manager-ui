#pragma once

#include <functional>
#include <QObject>
#include <QVariantList>
#include <QVariantMap>
#include <QStringList>
#include "logos_api.h"
#include "logos_api_client.h"
#include "PackageListModel.h"
#include "PackageTypes.h"
#include "rep_package_manager_ui_source.h"

// PackageManagerBackend is the source-side implementation of the
// PackageManagerUi .rep interface. Inheriting from the generated
// PackageManagerUiSimpleSource gives us a single source of truth for the
// properties / signals / slots and a real static metaobject that can be
// remoted via QRemoteObjectNode::enableRemoting() when this module runs as
// a view module.
//
// The PackageListModel* is kept as a subclass-only Q_PROPERTY because
// QAbstractItemModel* can't flow through a .rep — it's remoted separately
// via QRemoteObjectNode::enableRemoting(model, "packages") on the host
// side, and exposed to QML through logos.model(...).
class PackageManagerBackend : public PackageManagerUiSimpleSource {
    Q_OBJECT
    Q_PROPERTY(PackageListModel* packages READ packages CONSTANT)

public:
    explicit PackageManagerBackend(LogosAPI* logosAPI = nullptr, QObject* parent = nullptr);
    ~PackageManagerBackend() = default;

    PackageListModel* packages() const;

public slots:
    // Overrides of the pure-virtual slots generated from the .rep.
    void reload() override;
    void install() override;
    void requestPackageDetails(int index) override;
    void togglePackage(int index, bool checked) override;

private slots:
    void onSelectedReleaseIndexChanged();

private:
    void refreshPackages();
    void refreshReleases(std::function<void()> onDone);
    void setPackagesFromVariantList(const QVariantList& packagesArray,
                                    const QVariantList& installedPackages,
                                    const QStringList& validVariants);
    void processDownloadResults(const QString& releaseTag, const QVariantList& results);
    void installNextPackage(const QString& releaseTag, const QVariantList& results, int index, int completed, int totalPackages);
    void finishInstallation(int completed);
    void refreshHasSelectedPackages();

    // Returns the release tag string for the currently selected release index.
    // Empty string means "latest".
    QString currentReleaseTag() const;

    // Three-way version comparator (dotted-numeric semantics).
    // Returns -1 if a < b, 0 if a == b, +1 if a > b. Missing components treated as 0.
    static int versionCmp(const QString& a, const QString& b);

    PackageListModel* m_packageModel;
    LogosAPI* m_logosAPI;
    int m_reloadGeneration = 0;
    bool m_suppressReleaseChange = false;
};
