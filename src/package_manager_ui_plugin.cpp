#include "package_manager_ui_plugin.h"
#include "PackageManagerBackend.h"

#include <QDebug>

PackageManagerUiPlugin::PackageManagerUiPlugin(QObject* parent)
    : QObject(parent)
{
}

PackageManagerUiPlugin::~PackageManagerUiPlugin() = default;

void PackageManagerUiPlugin::initLogos(LogosAPI* api)
{
    if (m_backend) return;
    m_backend = new PackageManagerBackend(api, this);
    setBackend(m_backend);
    qDebug() << "PackageManagerUiPlugin: backend initialized";
}
