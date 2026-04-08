#ifndef PACKAGE_MANAGER_UI_INTERFACE_H
#define PACKAGE_MANAGER_UI_INTERFACE_H

#include <QObject>
#include <QString>
#include "interface.h"

class PackageManagerUiInterface : public PluginInterface
{
public:
    virtual ~PackageManagerUiInterface() = default;
};

#define PackageManagerUiInterface_iid "org.logos.PackageManagerUiInterface"
Q_DECLARE_INTERFACE(PackageManagerUiInterface, PackageManagerUiInterface_iid)

#endif
