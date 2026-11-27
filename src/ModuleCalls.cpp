#include "ModuleCalls.h"

#include "logos_sdk.h"

namespace pmui {

namespace {

// A download or an install can take minutes; the rest answers at once.
constexpr int kReadMs = 30 * 1000;
constexpr int kDownloadMs = 5 * 60 * 1000;
constexpr int kVerifyMs = 2 * 60 * 1000;
constexpr int kInstallMs = 5 * 60 * 1000;

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
    m_modules.package_downloader.refreshCatalogAsync(
        [done](const QVariantMap& reply) { done(reply.value(QStringLiteral("error")).toString()); },
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
    m_modules.package_downloader.downloadPinnedAsync(
        repositoryUrl, name, version, rootHash,
        [done, name](const QVariantMap& reply) {
            if (reply.isEmpty())
                done({{QStringLiteral("name"), name},
                      {QStringLiteral("error"), QStringLiteral("package_downloader did not reply")}});
            else
                done(reply);
        },
        Timeout(kDownloadMs));
}

void ModuleCalls::verifyPackage(const QString& path, std::function<void(const QVariantMap&)> done)
{
    m_modules.package_manager.verifyPackageAsync(
        path, [done](const QVariantMap& verified) { done(verified); }, Timeout(kVerifyMs));
}

void ModuleCalls::inspectPackage(const QString& path, std::function<void(const QVariantMap&)> done)
{
    m_modules.package_manager.inspectPackageAsync(
        path, [done](const QVariantMap& inspected) { done(inspected); }, Timeout(kVerifyMs));
}

void ModuleCalls::installPlugin(const QString& path, const QString& source,
                                std::function<void(const QVariantMap&, const QString&)> done)
{
    // The result-carrying call: a reply that never came is not an empty one.
    m_modules.package_manager.installPluginAsyncResult(
        path, false, source,
        [done](logos::AsyncResult<QVariantMap> r) {
            if (!r.ok()) {
                const QString message = QString::fromStdString(r.error.message);
                done({}, message.isEmpty() ? QStringLiteral("package_manager did not reply") : message);
                return;
            }
            done(r.value, {});
        },
        Timeout(kInstallMs));
}

void ModuleCalls::uninstallPackage(const QString& name, std::function<void(const QVariantMap&)> done)
{
    m_modules.package_manager.uninstallPackageAsync(
        name, [done](const QVariantMap& reply) { done(reply); }, Timeout(kInstallMs));
}

void ModuleCalls::readInstalledPackages(std::function<void(const QVariantList&, bool)> done)
{
    m_modules.package_manager.getInstalledPackagesAsyncResult(
        [done](logos::AsyncResult<QVariantList> r) { done(r.value, r.ok()); }, Timeout(kReadMs));
}

}  // namespace pmui
