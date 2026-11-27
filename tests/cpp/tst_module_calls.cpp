// Every call to another module goes through the typed wrappers: modules().<dep>
// for a dependency, modules().bind_intents() for intents_module, an interface
// dependency. A raw modules().api->getClient(...)->invokeRemoteMethod is a
// second client to that module with its own token handshake; the module keeps
// one token per caller, so at startup one of the two handshakes loses and its
// call is refused.

#include "CallFailure.h"
#include "check.h"

#include <QDir>
#include <QFile>
#include <QRegularExpression>
#include <QStringList>

namespace {

// The source without comments, so prose may still name the call.
QString codeOf(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return QString();
    QString text = QString::fromUtf8(file.readAll());
    text.remove(QRegularExpression(QStringLiteral(R"(/\*.*?\*/)"),
                                   QRegularExpression::DotMatchesEverythingOption));
    text.remove(QRegularExpression(QStringLiteral(R"(//[^\n]*)")));
    return text;
}

}  // namespace

TEST(sources_make_no_raw_module_calls) {
    const QDir src(QStringLiteral(PMUI_SOURCE_DIR));
    const QStringList files = src.entryList({QStringLiteral("*.cpp"), QStringLiteral("*.h")},
                                            QDir::Files);
    CHECK(files.contains(QStringLiteral("PackageManagerBackend.cpp")));
    CHECK(files.contains(QStringLiteral("ModuleCalls.cpp")));

    const QRegularExpression raw(QStringLiteral(R"(invokeRemoteMethod\w*\(|onEventWhenAvailable\()"));
    QStringList offenders;
    for (const QString& name : files)
        if (raw.match(codeOf(src.filePath(name))).hasMatch()) offenders << name;
    CHECK_EQ(offenders, QStringList());
}

// A plain fooAsync() hands a timeout over as an empty reply, which reads as
// "fetched nothing", "not signed as you approved it" or "could not uninstall". These take the result-carrying call.
TEST(calls_that_report_back_carry_the_call_error) {
    const QString code = codeOf(QStringLiteral(PMUI_SOURCE_DIR "/ModuleCalls.cpp"));
    CHECK(code.contains(QStringLiteral("downloadPinnedAsyncResult(")));
    const QRegularExpression plain(QStringLiteral(
        R"(\b(downloadPinned|refreshCatalog|verifyPackage|inspectPackage|installPlugin|uninstallPackage)Async\()"));
    QStringList offenders;
    for (auto it = plain.globalMatch(code); it.hasNext();) offenders << it.next().captured(1);
    CHECK_EQ(offenders, QStringList());
}

// One package may take every step package_downloader bounds: Logos Storage
// starting (90 s), its manifest (60 s), a stall (60 s), then HTTP (600 s).
TEST(a_download_outlasts_the_downloaders_own_limits) {
    constexpr int bounded = (90 + 60 + 60 + 600) * 1000;
    CHECK(pmui::kDownloadMs > bounded);
}

TEST(a_timeout_is_reported_as_one) {
    const QString module = QStringLiteral("package_downloader");
    CHECK_EQ(pmui::callFailure(module, "timeout", "call to 'x' timed out after 1800000ms", pmui::kDownloadMs),
             QStringLiteral("package_downloader did not answer within 30 min."));
    CHECK_EQ(pmui::callFailure(module, "timeout", "", 30 * 1000),
             QStringLiteral("package_downloader did not answer within 30 s."));
    CHECK_EQ(pmui::callFailure(module, "timeout", "", 90 * 1000),
             QStringLiteral("package_downloader did not answer within 90 s."));
    CHECK_EQ(pmui::callFailure(module, "transport_error", "connection lost", pmui::kDownloadMs),
             QStringLiteral("connection lost"));
    CHECK_EQ(pmui::callFailure(module, "call_failed", "", pmui::kDownloadMs),
             QStringLiteral("package_downloader did not reply."));
}

// intents_module by its contract, which metadata.json declares.
TEST(intents_module_is_bound_by_its_contract) {
    const QString code = codeOf(QStringLiteral(PMUI_SOURCE_DIR "/PackageManagerBackend.cpp"));
    CHECK(code.contains(QStringLiteral("bind_intents(kIntentsModule)")));
    QFile metadata(QStringLiteral(PMUI_SOURCE_DIR "/../metadata.json"));
    CHECK(metadata.open(QIODevice::ReadOnly));
    CHECK(metadata.readAll().contains(R"("input": "logos-intents-module")"));
}

CHECK_MAIN()
