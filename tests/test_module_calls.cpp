// Every call to another module goes through the typed wrappers: modules().<dep>
// for a dependency, modules().bind_intents() for intents_module, an interface
// dependency. A raw modules().api->getClient(...)->invokeRemoteMethod is a
// second client to that module with its own token handshake; the module keeps
// one token per caller, so at startup one of the two handshakes loses and its
// call is refused.

#include <logos_test.h>

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

} // namespace

LOGOS_TEST(sources_make_no_raw_module_calls) {
    const QDir src(QStringLiteral(PMUI_SOURCE_DIR));
    const QStringList files = src.entryList({QStringLiteral("*.cpp"), QStringLiteral("*.h")},
                                            QDir::Files);
    LOGOS_ASSERT_TRUE(files.contains(QStringLiteral("PackageManagerBackend.cpp")));

    const QRegularExpression raw(QStringLiteral(R"(invokeRemoteMethod\w*\(|onEventWhenAvailable\()"));
    QStringList offenders;
    for (const QString& name : files)
        if (raw.match(codeOf(src.filePath(name))).hasMatch()) offenders << name;
    LOGOS_ASSERT_EQ(offenders, QStringList());
}

// intents_module by its contract, which metadata.json declares.
LOGOS_TEST(intents_module_is_bound_by_its_contract) {
    const QString code = codeOf(QStringLiteral(PMUI_SOURCE_DIR "/PackageManagerBackend.cpp"));
    LOGOS_ASSERT_TRUE(code.contains(QStringLiteral("bind_intents(kIntentsModule)")));
    QFile metadata(QStringLiteral(PMUI_SOURCE_DIR "/../metadata.json"));
    LOGOS_ASSERT_TRUE(metadata.open(QIODevice::ReadOnly));
    LOGOS_ASSERT_TRUE(metadata.readAll().contains(R"("input": "logos-intents-module")"));
}
