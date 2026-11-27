#pragma once

// Just enough of a test runner: TEST(name) { CHECK(...); CHECK_EQ(a, b); },
// and one CHECK_MAIN() per test binary.

#include <QCoreApplication>
#include <QEventLoop>
#include <QTimer>
#include <QVariant>

#include <QStringList>

#include <cstdio>
#include <functional>
#include <type_traits>
#include <vector>

namespace check {

struct Case { const char* name; std::function<void()> body; };

inline std::vector<Case>& cases() { static std::vector<Case> all; return all; }
inline int& failures() { static int n = 0; return n; }

struct Register {
    Register(const char* name, std::function<void()> body) { cases().push_back({name, body}); }
};

inline void fail(const char* file, int line, const QString& what)
{
    ++failures();
    std::fprintf(stderr, "%s:%d: %s\n", file, line, qPrintable(what));
}

template <typename T> QString show(const T& v)
{
    if constexpr (std::is_enum_v<T>) return QString::number(static_cast<int>(v));
    else if constexpr (std::is_same_v<T, QStringList>) return QLatin1Char('[') + v.join(QStringLiteral(", ")) + QLatin1Char(']');
    else return QVariant::fromValue(v).toString();
}

inline int run()
{
    for (const Case& c : cases()) {
        const int before = failures();
        c.body();
        std::fprintf(stderr, "%s %s\n", failures() == before ? "PASS" : "FAIL", c.name);
    }
    std::fprintf(stderr, "%zu cases, %d failed checks\n", cases().size(), failures());
    return failures() == 0 ? 0 : 1;
}

}  // namespace check

#define TEST(name) \
    static void name(); \
    static const check::Register name##_registered(#name, name); \
    static void name()

#define CHECK(cond) \
    do { if (!(cond)) { check::fail(__FILE__, __LINE__, QStringLiteral("CHECK(" #cond ")")); return; } } while (0)

#define CHECK_EQ(actual, expected) \
    do { \
        const auto a_ = (actual); const auto e_ = (expected); \
        if (!(a_ == e_)) { \
            check::fail(__FILE__, __LINE__, QStringLiteral(#actual " == " #expected ": got %1, want %2") \
                .arg(check::show(a_), check::show(e_))); \
            return; \
        } \
    } while (0)

// Let queued calls and timers run for `ms`.
#define SPIN(ms) \
    do { QEventLoop loop_; QTimer::singleShot((ms), &loop_, &QEventLoop::quit); loop_.exec(); } while (0)

#define CHECK_MAIN() \
    int main(int argc, char** argv) { QCoreApplication app(argc, argv); return check::run(); }
