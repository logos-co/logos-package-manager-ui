#pragma once

#include <logos/installer/InstallModules.h>
#include <logos/installer/PackageModules.h>

struct LogosModules;

namespace pmui {

// package_downloader and package_manager, as the library asks for them,
// through the backend's generated wrappers (modules()). Each call has a
// deadline, and a call that fails answers as the library expects: empty.
class ModuleCalls : public logos::installer::PackageModules,
                    public logos::installer::InstallModules {
public:
    explicit ModuleCalls(LogosModules& modules);

    void startDownloader(std::function<void(bool)> done) override;
    void listRepositories(std::function<void(const QVariantList&, bool)> done) override;
    void getCatalog(std::function<void(const QVariantList&, bool)> done) override;
    void refreshCatalog(std::function<void(const QString& error)> done) override;
    void getDownloadSource(std::function<void(const QString&)> done) override;
    void getStorageState(std::function<void(const QVariantMap&)> done) override;
    void getInstalledPackages(std::function<void(const QVariantList&, bool)> done) override;
    void getValidVariants(std::function<void(const QStringList&, bool)> done) override;

    void downloadPinned(const QString& repositoryUrl, const QString& name, const QString& version,
                        const QString& rootHash,
                        std::function<void(const QVariantMap&)> done) override;
    void verifyPackage(const QString& path, std::function<void(const QVariantMap&)> done) override;
    void inspectPackage(const QString& path, std::function<void(const QVariantMap&)> done) override;
    void installPlugin(const QString& path, const QString& source,
                       std::function<void(const QVariantMap& reply, const QString& callError)> done) override;
    void uninstallPackage(const QString& name, std::function<void(const QVariantMap&)> done) override;
    void readInstalledPackages(std::function<void(const QVariantList& installed, bool read)> done) override;

private:
    LogosModules& m_modules;
};

}  // namespace pmui
