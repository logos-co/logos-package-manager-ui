#include "ModuleCalls.h"

#include "CallFailure.h"
#include "logos_sdk.h"

namespace pmui {

namespace {

const QString kDownloader = QStringLiteral("package_downloader");
const QString kManager = QStringLiteral("package_manager");

}  // namespace

ModuleCalls::ModuleCalls(LogosModules& modules)
    : m_modules(modules)
{
}

void ModuleCalls::startDownloader(std::function<void(bool)> done)
{
    m_modules.package_downloader.startAsyncResult(
        [done](logos::AsyncResult<QVariantMap> r) {
            done(r.ok() && r.value.value(QStringLiteral("success")).toBool());
        },
        Timeout(kReadMs));
}

void ModuleCalls::listRepositories(std::function<void(const QVariantList&, bool)> done)
{
    m_modules.package_downloader.listRepositoriesAsyncResult(
        [done](logos::AsyncResult<QVariantList> r) { done(r.value, r.ok()); }, Timeout(kReadMs));
}

void ModuleCalls::getCatalog(std::function<void(const QVariantList&, bool)> done)
{
    m_modules.package_downloader.getCatalogAsyncResult(
        [done](logos::AsyncResult<QVariantList> r) { done(r.value, r.ok()); }, Timeout(kReadMs));
}

void ModuleCalls::refreshCatalog(std::function<void(const QString&)> done)
{
    m_modules.package_downloader.refreshCatalogAsyncResult(
        [done](logos::AsyncResult<QVariantMap> r) {
            if (!r.ok()) return done(callFailure(kDownloader, r.error.code, r.error.message, kDownloadMs));
            done(r.value.value(QStringLiteral("error")).toString());
        },
        Timeout(kDownloadMs));
}

void ModuleCalls::getDownloadSource(std::function<void(const QString&)> done)
{
    m_modules.package_downloader.getDownloadSourceAsync(
        [done](const QString& source) { done(source); }, Timeout(kReadMs));
}

void ModuleCalls::getStorageState(std::function<void(const QVariantMap&)> done)
{
    m_modules.package_downloader.getStorageStateAsync(
        [done](const QVariantMap& state) { done(state); }, Timeout(kReadMs));
}

void ModuleCalls::getInstalledPackages(std::function<void(const QVariantList&, bool)> done)
{
    m_modules.package_manager.getInstalledPackagesAsyncResult(
        [done](logos::AsyncResult<QVariantList> r) { done(r.value, r.ok()); }, Timeout(kReadMs));
}

void ModuleCalls::getValidVariants(std::function<void(const QStringList&, bool)> done)
{
    m_modules.package_manager.getValidVariantsAsyncResult(
        [done](logos::AsyncResult<QStringList> r) { done(r.value, r.ok()); }, Timeout(kReadMs));
}

void ModuleCalls::downloadPinned(const QString& repositoryUrl, const QString& name,
                                 const QString& version, const QString& rootHash,
                                 std::function<void(const QVariantMap&)> done)
{
    m_modules.package_downloader.downloadPinnedAsyncResult(
        repositoryUrl, name, version, rootHash,
        [done, name](logos::AsyncResult<QVariantMap> r) {
            if (!r.ok())
                return done({{QStringLiteral("name"), name},
                             {QStringLiteral("error"),
                              callFailure(kDownloader, r.error.code, r.error.message, kDownloadMs)}});
            done(r.value);
        },
        Timeout(kDownloadMs));
}

void ModuleCalls::verifyPackage(const QString& path, std::function<void(const QVariantMap&)> done)
{
    m_modules.package_manager.verifyPackageAsyncResult(
        path,
        [done](logos::AsyncResult<QVariantMap> r) {
            if (!r.ok())
                return done({{QStringLiteral("error"),
                              callFailure(kManager, r.error.code, r.error.message, kVerifyMs)}});
            done(r.value);
        },
        Timeout(kVerifyMs));
}

void ModuleCalls::inspectPackage(const QString& path, std::function<void(const QVariantMap&)> done)
{
    m_modules.package_manager.inspectPackageAsyncResult(
        path,
        [done](logos::AsyncResult<QVariantMap> r) {
            if (!r.ok())
                return done({{QStringLiteral("error"),
                              callFailure(kManager, r.error.code, r.error.message, kVerifyMs)}});
            done(r.value);
        },
        Timeout(kVerifyMs));
}

void ModuleCalls::installPlugin(const QString& path, const QString& source,
                                std::function<void(const QVariantMap&, const QString&)> done)
{
    m_modules.package_manager.installPluginAsyncResult(
        path, false, source,
        [done](logos::AsyncResult<QVariantMap> r) {
            if (!r.ok()) return done({}, callFailure(kManager, r.error.code, r.error.message, kInstallMs));
            done(r.value, {});
        },
        Timeout(kInstallMs));
}

void ModuleCalls::uninstallPackage(const QString& name, std::function<void(const QVariantMap&)> done)
{
    m_modules.package_manager.uninstallPackageAsyncResult(
        name,
        [done](logos::AsyncResult<QVariantMap> r) {
            if (!r.ok())
                return done({{QStringLiteral("success"), false},
                             {QStringLiteral("error"),
                              callFailure(kManager, r.error.code, r.error.message, kInstallMs)}});
            done(r.value);
        },
        Timeout(kInstallMs));
}

void ModuleCalls::readInstalledPackages(std::function<void(const QVariantList&, bool)> done)
{
    m_modules.package_manager.getInstalledPackagesAsyncResult(
        [done](logos::AsyncResult<QVariantList> r) { done(r.value, r.ok()); }, Timeout(kReadMs));
}

}  // namespace pmui
