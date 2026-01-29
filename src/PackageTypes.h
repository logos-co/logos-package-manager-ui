#pragma once

#include <QObject>

class PackageTypes : public QObject {
    Q_OBJECT

public:
    enum InstallStatus {
        NotInstalled = 0,
        Installing = 1,
        Installed = 2,
        Failed = 3
    };
    Q_ENUM(InstallStatus)

    enum ErrorType {
        NoError = 0,
        InstallationAlreadyInProgress = 2,
        NoPackagesSelected = 3,
        PackageManagerNotConnected = 4
    };
    Q_ENUM(ErrorType)

    enum ProgressType {
        IdleState = 0,
        Started = 2,
        InProgress = 3,
        Completed = 4,
        ProgressFailed = 5
    };
    Q_ENUM(ProgressType)

    explicit PackageTypes(QObject* parent = nullptr) : QObject(parent) {}
};
