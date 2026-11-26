#pragma once

#include "logos_module_context.h"
#include "rep_package_manager_ui_source.h"

#include <QAbstractItemModel>
#include <QSet>

#include <functional>
#include <memory>

namespace pmui {
class ModuleCalls;
class Packages;
}  // namespace pmui

namespace logos::installer {
class IntentConsent;
}  // namespace logos::installer

// Runs as the core module `package_manager_ui_backend`; the generated glue
// publishes it to the view, and `packages` (the page of rows) beside it.
// Everything the page shows and does is pmui::Packages': this class hands it
// the package modules and Basecamp's consent once modules() is live, forwards
// the modules' events, and mirrors its state into the .rep's properties.
class PackageManagerBackend : public PackageManagerUiSimpleSource,
                              public LogosModuleContext {
    Q_OBJECT
    Q_PROPERTY(QAbstractItemModel* packages READ packages CONSTANT)

public:
    explicit PackageManagerBackend(QObject* parent = nullptr);
    ~PackageManagerBackend() override;

    QAbstractItemModel* packages() const;

    void onContextReady() override;

    // intents_module's delivery of packages.show; the generated glue lets no
    // one else call it. The view finds the package, then answers with
    // answerReveal().
    QString deliver(const QString& dispatch, const QString& intent, const QString& paramsJson,
                    const QString& requester);

    // See package_manager_ui.rep.
    void refreshCatalog() override;
    void installLocalPackage(QUrl fileUrl) override;
    void runSelectedActions() override;
    void uninstallSelected() override;
    void togglePackage(int index, bool checked) override;
    void reloadPackage(int index) override;
    void setRowVersion(int index, int versionIndex) override;
    void requestPackageDetailsFor(QString name, QString repositoryUrl) override;
    QString displayNameForModule(QString moduleName) override;
    void confirmInstall(QString name, QString version, QString repositoryUrl) override;
    void confirmUpgrade(QString moduleName, QString version, int mode, QString repositoryUrl) override;
    void confirmUninstall(QString moduleName) override;
    void manageRepositories() override;
    void answerReveal(QString dispatch, bool ok, QString error) override;

private:
    void mirror();
    void mirrorSelection();

    pmui::Packages* m_packages;
    std::unique_ptr<pmui::ModuleCalls> m_calls;
    logos::installer::IntentConsent* m_consent = nullptr;
    // intents_module's request(intent, "{}") with no one waiting on the
    // answer, and respond(dispatch, ok, "{}", error).
    std::function<void(const QString& intent)> m_request;
    std::function<void(const QString& dispatch, bool ok, const QString& error)> m_respond;
    // Delivered, not answered yet.
    QSet<QString> m_revealDispatches;
};
