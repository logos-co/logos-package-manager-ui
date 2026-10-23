// Where an install's bytes come from: the transport package_downloader names
// in each progress sample, kept on the row for the action pill ("Finding
// peers…") and the Source column (the source in use, highlighted).

#include <logos_test.h>

#include <QVariantMap>

#include "PackageListModel.h"
#include "PackageTypes.h"

namespace {

const QString kRepo = QStringLiteral("https://example.test/logos-repo.json");

PackageListModel& installingWallet(PackageListModel& model) {
    QVariantMap row;
    row["name"] = QStringLiteral("wallet_module");
    row["repositoryUrl"] = kRepo;
    row["installStatus"] = static_cast<int>(PackageTypes::NotInstalled);
    model.setPackages({row});
    model.updateRowInstallation(QStringLiteral("wallet_module"), kRepo,
                                static_cast<int>(PackageTypes::Installing));
    return model;
}

QString transportOf(const PackageListModel& model) {
    return model.data(model.index(0), PackageListModel::DownloadTransportRole).toString();
}

} // namespace

LOGOS_TEST(progress_records_the_transport_on_the_installing_row) {
    PackageListModel model;
    installingWallet(model);

    model.updateDownloadProgress(QStringLiteral("wallet_module"), 0, 4096, QStringLiteral("logos"));

    LOGOS_ASSERT_EQ(transportOf(model).toStdString(), std::string("logos"));
}

// A fallback shows as the transport changing.
LOGOS_TEST(a_fallback_replaces_the_transport) {
    PackageListModel model;
    installingWallet(model);

    model.updateDownloadProgress(QStringLiteral("wallet_module"), 0, 4096, QStringLiteral("logos"));
    model.updateDownloadProgress(QStringLiteral("wallet_module"), 0, 4096, QStringLiteral("http"));

    LOGOS_ASSERT_EQ(transportOf(model).toStdString(), std::string("http"));
}

// A downloader that predates the transport sends none: keep the last one.
LOGOS_TEST(a_sample_without_a_transport_keeps_the_last_one) {
    PackageListModel model;
    installingWallet(model);

    model.updateDownloadProgress(QStringLiteral("wallet_module"), 0, 4096, QStringLiteral("logos"));
    model.updateDownloadProgress(QStringLiteral("wallet_module"), 1024, 4096);

    LOGOS_ASSERT_EQ(transportOf(model).toStdString(), std::string("logos"));
}

// The Source column goes back to the installed copy once it is done.
LOGOS_TEST(the_transport_is_cleared_when_the_install_ends) {
    PackageListModel model;
    installingWallet(model);
    model.updateDownloadProgress(QStringLiteral("wallet_module"), 0, 4096, QStringLiteral("logos"));

    model.updatePackageInstallation(QStringLiteral("wallet_module"),
                                    static_cast<int>(PackageTypes::Installed));

    LOGOS_ASSERT_EQ(transportOf(model).toStdString(), std::string());
}
