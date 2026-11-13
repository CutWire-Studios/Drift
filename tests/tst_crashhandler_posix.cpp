#include "app/platform/CrashHandler.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QtTest>

#include <fcntl.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>

#include <csignal>

using namespace drift::crash;

namespace {

volatile int *g_null = nullptr;

int overflow(int depth)
{
    volatile char pad[4096];
    pad[0] = char(depth);
    return overflow(depth + 1) + pad[0];
}

// Runs `crash` in a forked child with the handler installed; returns the wait status.
template <typename F> int runChild(const QString &root, const QString &log, F crash)
{
    const pid_t pid = fork();
    if (pid == 0) {
        const rlimit noCore{0, 0};
        setrlimit(RLIMIT_CORE, &noCore);
        setCrashLogFd(open(QFile::encodeName(log).constData(), O_WRONLY | O_CREAT | O_APPEND, 0644));
        installCrashHandler(root + "/crashes");
        crash();
        _exit(0);
    }
    int status = 0;
    waitpid(pid, &status, 0);
    return status;
}

QJsonObject onlyCrashJson(const QString &root, QString *stack)
{
    const QStringList dirs = QDir(root + "/crashes").entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    if (dirs.size() != 1)
        return {};
    QFile json(root + "/crashes/" + dirs.first() + "/crash.json");
    QFile stackFile(root + "/crashes/" + dirs.first() + "/stack.txt");
    if (stack)
        *stack = stackFile.open(QIODevice::ReadOnly) ? QString::fromUtf8(stackFile.readAll()) : QString();
    return json.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(json.readAll()).object() : QJsonObject();
}

} // namespace

class TestCrashHandlerPosix : public QObject
{
    Q_OBJECT

private slots:
    void segfault()
    {
        QTemporaryDir dir;
        const QString log = dir.path() + "/drift.log";
        const int status = runChild(dir.path(), log, [] { *g_null = 1; });
        QVERIFY(WIFSIGNALED(status));
        QCOMPARE(WTERMSIG(status), SIGSEGV);

        QString stack;
        const QJsonObject json = onlyCrashJson(dir.path(), &stack);
        QCOMPARE(json.value("code").toString(), QString("0xB"));
        QVERIFY(json.value("offset").toString().startsWith("0x"));
        QVERIFY(!json.value("module").toString().isEmpty());
        QVERIFY(QDateTime::fromString(json.value("crashedAt").toString(), Qt::ISODate).isValid());
        QVERIFY(!stack.isEmpty());

        QFile logFile(log);
        QVERIFY(logFile.open(QIODevice::ReadOnly));
        QVERIFY(QString::fromUtf8(logFile.readAll()).contains("crash: signal 11 (SIGSEGV)"));
    }

    void stackOverflow()
    {
        QTemporaryDir dir;
        // Without the alternate stack the handler could not run at all.
        const int status = runChild(dir.path(), dir.path() + "/drift.log", [] {
            overflow(0);
        });
        QVERIFY(WIFSIGNALED(status));
        QCOMPARE(WTERMSIG(status), SIGSEGV);
        QCOMPARE(onlyCrashJson(dir.path(), nullptr).value("code").toString(), QString("0xB"));
    }

    void abortSkipsSystemRuntime()
    {
        QTemporaryDir dir;
        const int status = runChild(dir.path(), dir.path() + "/drift.log", [] { abort(); });
        QVERIFY(WIFSIGNALED(status));
        QCOMPARE(WTERMSIG(status), SIGABRT);
        const QJsonObject json = onlyCrashJson(dir.path(), nullptr);
        QCOMPARE(json.value("code").toString(), QString("0x6"));
        QVERIFY(!json.value("module").toString().startsWith("libc."));
    }

    void processAliveTracksPids()
    {
        QVERIFY(processAlive(getpid()));
        QVERIFY(!processAlive(0x7ffffff0));
    }
};

QTEST_GUILESS_MAIN(TestCrashHandlerPosix)
#include "tst_crashhandler_posix.moc"
