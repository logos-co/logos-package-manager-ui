#ifndef PACKAGE_MANAGER_UI_PLUGIN_H
#define PACKAGE_MANAGER_UI_PLUGIN_H

#include <QObject>
#include <QString>
#include "package_manager_ui_interface.h"
#include "LogosViewPluginBase.h"

class LogosAPI;
class PackageManagerBackend;

// Thin plugin entry point. Holds a PackageManagerBackend and lets the
// generated view-plugin base expose it to ui-host.
class PackageManagerUiPlugin : public QObject,
                               public PackageManagerUiInterface,
                               public PackageManagerUiViewPluginBase
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID PackageManagerUiInterface_iid FILE "../metadata.json")
    Q_INTERFACES(PackageManagerUiInterface)

public:
    explicit PackageManagerUiPlugin(QObject* parent = nullptr);
    ~PackageManagerUiPlugin() override;

    QString name()    const override { return "package_manager_ui"; }
    QString version() const override { return "1.0.0"; }

    // Called by ui-host after plugin load. Creates the backend and wires
    // it up with the provided LogosAPI.
    Q_INVOKABLE void initLogos(LogosAPI* api);

private:
    PackageManagerBackend* m_backend = nullptr;
};

#endif
