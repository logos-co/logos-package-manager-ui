#pragma once

// package_downloader, package_manager and Basecamp's consent, faked. The
// fake's package files hold what they are ("name|version|rootHash|signer"),
// so what inspect and verify read follows the file, as package_manager's do.
// Everything answers at once.

#include <logos/installer/Consent.h>
#include <logos/installer/InstallModules.h>
#include <logos/installer/PackageModules.h>

#include <QDir>
#include <QFile>
#include <QHash>
#include <QTemporaryDir>

namespace fakes {

using logos::installer::Consent;

struct Modules : logos::installer::PackageModules, logos::installer::InstallModules {
    QTemporaryDir files;
    QStringList calls;

    QVariantList repositories;
    QVariantList catalog;
    QStringList variants{QStringLiteral("linux-x86_64")};
    QString downloadSource = QStringLiteral("any");
    QVariantMap storage{{"state", "running"}};
    // Reads whose call fails ("getCatalog", ...).
    QStringList failing;

    // Installed packages by name, as getInstalledPackages reports them.
    QHash<QString, QVariantMap> installed;
    // What the downloader serves, keyed "name@version": "name|version|rootHash|signer".
    QHash<QString, QString> served;

    void addInstalled(const QString& name, const QString& version, const QString& rootHash,
                      const QString& type = QStringLiteral("user"))
    {
        installed.insert(name, {{"name", name}, {"version", version}, {"installType", type},
                                {"hashes", QVariantMap{{"root", rootHash}}}});
    }
    void serve(const QString& name, const QString& version, const QString& rootHash,
               const QString& signer = {})
    {
        served.insert(name + "@" + version, QStringList{name, version, rootHash, signer}.join('|'));
    }
    QVariantList installedList() const
    {
        QVariantList list;
        for (const QVariantMap& record : installed) list.append(record);
        return list;
    }
    static QStringList read(const QString& path)
    {
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly)) return {};
        return QString::fromUtf8(f.readAll()).split('|');
    }

    // PackageModules
    void startDownloader(std::function<void(bool)> done) override
    {
        calls << "start";
        done(true);
    }
    void listRepositories(std::function<void(const QVariantList&, bool)> done) override
    {
        calls << "listRepositories";
        const bool read = !failing.contains("listRepositories");
        done(read ? repositories : QVariantList{}, read);
    }
    void getCatalog(std::function<void(const QVariantList&, bool)> done) override
    {
        calls << "getCatalog";
        const bool read = !failing.contains("getCatalog");
        done(read ? catalog : QVariantList{}, read);
    }
    void refreshCatalog(std::function<void(const QString&)> done) override
    {
        calls << "refreshCatalog";
        done({});
    }
    void getDownloadSource(std::function<void(const QString&)> done) override { done(downloadSource); }
    void getStorageState(std::function<void(const QVariantMap&)> done) override { done(storage); }
    void getInstalledPackages(std::function<void(const QVariantList&, bool)> done) override
    {
        const bool read = !failing.contains("getInstalledPackages");
        done(read ? installedList() : QVariantList{}, read);
    }
    void getValidVariants(std::function<void(const QStringList&, bool)> done) override
    {
        const bool read = !failing.contains("getValidVariants");
        done(read ? variants : QStringList{}, read);
    }

    // InstallModules
    void downloadPinned(const QString& repositoryUrl, const QString& name, const QString& version,
                        const QString& rootHash, std::function<void(const QVariantMap&)> done) override
    {
        calls << "download " + name;
        const QString content = served.value(name + "@" + version);
        if (content.isEmpty()) {
            done({{"name", name}, {"error", "no such release"}});
            return;
        }
        const QString path = QDir(files.path()).filePath(name + "-" + version + ".lgx");
        QFile f(path);
        f.open(QIODevice::WriteOnly);
        f.write(content.toUtf8());
        f.close();
        done({{"name", name}, {"version", version}, {"rootHash", rootHash},
              {"repositoryUrl", repositoryUrl}, {"source", "https://repo.example/" + name + ".lgx"},
              {"path", path}});
    }
    void verifyPackage(const QString& path, std::function<void(const QVariantMap&)> done) override
    {
        const QStringList p = read(path);
        done({{"packageValid", p.size() == 4}, {"isSigned", !p.value(3).isEmpty()},
              {"signatureValid", !p.value(3).isEmpty()}, {"signerDid", p.value(3)}});
    }
    void inspectPackage(const QString& path, std::function<void(const QVariantMap&)> done) override
    {
        const QStringList p = read(path);
        if (p.size() != 4) {
            done({{"error", "Failed to load LGX package"}});
            return;
        }
        done({{"name", p.value(0)}, {"version", p.value(1)}, {"rootHash", p.value(2)},
              {"signatureStatus", p.value(3).isEmpty() ? "unsigned" : "signed"},
              {"signerDid", p.value(3)}, {"isAlreadyInstalled", installed.contains(p.value(0))},
              {"installedVersion", installed.value(p.value(0)).value("version")}});
    }
    void installPlugin(const QString& path, const QString&,
                       std::function<void(const QVariantMap&, const QString&)> done) override
    {
        const QStringList p = read(path);
        calls << "install " + p.value(0);
        addInstalled(p.value(0), p.value(1), p.value(2));
        done({{"name", p.value(0)}, {"path", "/modules/" + p.value(0)}}, {});
    }
    void uninstallPackage(const QString& name, std::function<void(const QVariantMap&)> done) override
    {
        calls << "uninstall " + name;
        installed.remove(name);
        done({{"success", true}});
    }
    void readInstalledPackages(std::function<void(const QVariantList&, bool)> done) override
    {
        done(installedList(), true);
    }
};

struct Basecamp : Consent {
    struct Asked { QString intent; QVariantMap params; };
    QList<Asked> asked;
    // What Basecamp answers; with none, it waits.
    std::function<Answer(const QString&, const QVariantMap&)> decide;
    QList<std::function<void(const Answer&)>> waiting;

    void ask(const QString& intent, const QVariantMap& params,
             std::function<void(const Answer&)> answered) override
    {
        asked.append({intent, params});
        if (!decide) {
            waiting.append(std::move(answered));
            return;
        }
        answered(decide(intent, params));
    }
};

inline Consent::Answer approve(const QVariantMap& data)
{
    Consent::Answer a;
    a.approved = true;
    a.data = data;
    return a;
}

inline Consent::Answer refuse(const QString& error)
{
    Consent::Answer a;
    a.error = error;
    return a;
}

inline QVariantMap release(const QString& repo, const QString& name, const QString& version,
                           const QString& rootHash)
{
    return {{"name", name}, {"version", version}, {"rootHash", rootHash}, {"repositoryUrl", repo}};
}

}  // namespace fakes
