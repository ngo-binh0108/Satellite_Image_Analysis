#include <QApplication>
#include <QDir>
#include <QFile>
#include <QStandardPaths>

#include "LoginDialog.h"
#include "MainWindow.h"

int main(int argc, char *argv[])
{
    QApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);
    QApplication app(argc, argv);
    QApplication::setOrganizationName("EarthView");
    QApplication::setApplicationName("EarthView AI");

    const QStringList args = app.arguments();
    const int selfTest = args.indexOf("--selftest");
    if (selfTest >= 0 && selfTest + 2 < args.size()) {  // --selftest <output folder> <image files...>, no sign-in
        MainWindow window;
        window.runSelfTest(args.at(selfTest + 1), args.mid(selfTest + 2));
        window.show();
        return app.exec();
    }
    const int loginTest = args.indexOf("--logintest");
    if (loginTest >= 0 && loginTest + 1 < args.size()) {  // --logintest <output folder>: accounts check on a scratch store
        QStandardPaths::setTestModeEnabled(true);
        QFile::remove(Accounts::filePath());
        const auto report = [](const char *what, const QString &problem) {
            fprintf(stdout, "%-34s %s\n", what, problem.isEmpty() ? "OK" : qPrintable("refused: " + problem));
        };
        report("create with short password", Accounts::create("tester", "short"));
        report("create with bad name", Accounts::create("a b", "long-enough-password"));
        report("create tester", Accounts::create("tester", "correct horse 1"));
        report("create tester again", Accounts::create("Tester", "another password"));
        report("sign in, right password", Accounts::check("tester", "correct horse 1"));
        report("sign in, other letter case name", Accounts::check("TESTER", "correct horse 1"));
        report("sign in, wrong password", Accounts::check("tester", "correct horse 2"));
        report("sign in, unknown user", Accounts::check("nobody", "correct horse 1"));
        QFile stored(Accounts::filePath());
        stored.open(QIODevice::ReadOnly);
        const QByteArray content = stored.readAll();
        fprintf(stdout, "password stored in plain text:     %s\n", content.contains("correct horse") ? "YES (BUG)" : "no");
        LoginDialog login;
        login.show();
        login.grab().save(QDir(args.at(loginTest + 1)).filePath("login.png"));
        QFile::remove(Accounts::filePath());
        fflush(stdout);
        return 0;
    }
    // Sign in, work, and after "Sign out" come back to the sign-in window.
    while (true) {
        LoginDialog login;
        if (login.exec() != QDialog::Accepted)
            return 0;
        MainWindow window(login.userName());
        if (args.contains("--maximized"))
            window.showMaximized();
        else
            window.show();
        const int code = app.exec();
        if (!window.signedOut())
            return code;
    }
}