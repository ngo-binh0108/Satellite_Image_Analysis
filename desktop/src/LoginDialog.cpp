#include "LoginDialog.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLineEdit>
#include <QPasswordDigestor>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStackedWidget>
#include <QStandardPaths>

#include "Theme.h"
#include "UiHelpers.h"

using namespace ui;

namespace {
const int Iterations = 120000;
const int MinPasswordLength = 8;

QJsonArray loadUsers()
{
    QFile file(Accounts::filePath());
    return file.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(file.readAll()).object()["users"].toArray() : QJsonArray();
}

QByteArray derive(const QString &password, const QByteArray &salt, int iterations)
{
    return QPasswordDigestor::deriveKeyPbkdf2(QCryptographicHash::Sha256, password.toUtf8(), salt, iterations, 32);
}

// Compares every byte regardless of where the first difference is.
bool sameBytes(const QByteArray &a, const QByteArray &b)
{
    if (a.size() != b.size())
        return false;
    int difference = 0;
    for (int i = 0; i < a.size(); ++i)
        difference |= a[i] ^ b[i];
    return difference == 0;
}

QJsonObject findUser(const QJsonArray &users, const QString &userName)
{
    for (const QJsonValue &user : users)
        if (user.toObject()["name"].toString().compare(userName, Qt::CaseInsensitive) == 0)
            return user.toObject();
    return {};
}
}  // namespace

QString Accounts::filePath()
{
    return QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)).filePath("users.json");
}

bool Accounts::exists(const QString &userName)
{
    return !findUser(loadUsers(), userName).isEmpty();
}

QString Accounts::create(const QString &userName, const QString &password)
{
    static const QRegularExpression allowed("^[A-Za-z0-9._-]{3,32}$");
    if (!allowed.match(userName).hasMatch())
        return QObject::tr("The user name must be 3 to 32 characters: letters, digits, dot, dash or underscore.");
    if (password.size() < MinPasswordLength)
        return QObject::tr("The password must be at least %1 characters long.").arg(MinPasswordLength);
    QJsonArray users = loadUsers();
    if (!findUser(users, userName).isEmpty())
        return QObject::tr("That user name is already taken.");

    QByteArray salt(16, Qt::Uninitialized);
    QRandomGenerator::system()->generate(salt.begin(), salt.end());
    users.append(QJsonObject{
        {"name", userName},
        {"salt", QString::fromLatin1(salt.toBase64())},
        {"hash", QString::fromLatin1(derive(password, salt, Iterations).toBase64())},
        {"iterations", Iterations},
        {"created", QDateTime::currentDateTimeUtc().toString(Qt::ISODate)},
    });
    QDir().mkpath(QFileInfo(filePath()).absolutePath());
    QSaveFile file(filePath());  // written in full or not at all
    if (!file.open(QIODevice::WriteOnly) || file.write(QJsonDocument(QJsonObject{{"users", users}}).toJson()) < 0 || !file.commit())
        return QObject::tr("The account could not be saved to %1.").arg(filePath());
    return {};
}

QString Accounts::check(const QString &userName, const QString &password)
{
    const QJsonObject user = findUser(loadUsers(), userName);
    // The same message for an unknown name and a wrong password, so names cannot be probed.
    const QString wrong = QObject::tr("The user name or password is not correct.");
    if (user.isEmpty())
        return wrong;
    const QByteArray salt = QByteArray::fromBase64(user["salt"].toString().toLatin1());
    const QByteArray expected = QByteArray::fromBase64(user["hash"].toString().toLatin1());
    return sameBytes(derive(password, salt, user["iterations"].toInt(Iterations)), expected) ? QString() : wrong;
}

LoginDialog::LoginDialog(QWidget *parent) : QDialog(parent)
{
    setWindowTitle(tr("EarthView AI - Sign in"));
    setWindowFlag(Qt::WindowContextHelpButtonHint, false);
    setStyleSheet(Theme::styleSheet() + "QDialog { background: #060e1a; }");
    setFixedWidth(420);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(32, 28, 32, 26);
    layout->setSpacing(6);
    auto *logo = new QLabel(QString(Theme::Icon::Globe));
    logo->setObjectName("logo");
    logo->setAlignment(Qt::AlignCenter);
    auto *name = new QLabel("EarthView AI");
    name->setObjectName("appName");
    name->setAlignment(Qt::AlignCenter);
    QLabel *tag = label(tr("Satellite Image Analysis & Processing"), "muted");
    tag->setAlignment(Qt::AlignCenter);
    layout->addWidget(logo);
    layout->addWidget(name);
    layout->addWidget(tag);
    layout->addSpacing(14);

    m_pages = new QStackedWidget;
    m_pages->addWidget(buildSignIn());
    m_pages->addWidget(buildRegister());
    layout->addWidget(m_pages);
    QLabel *note = label(tr("Accounts are kept on this computer only."), "muted");
    note->setAlignment(Qt::AlignCenter);
    layout->addSpacing(8);
    layout->addWidget(note);

    // With no account yet, start on the registration page.
    showPage(loadUsers().isEmpty() ? 1 : 0);
}

QWidget *LoginDialog::buildSignIn()
{
    auto *page = new QWidget;
    auto *v = new QVBoxLayout(page);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(8);
    m_loginName = new QLineEdit;
    m_loginPassword = new QLineEdit;
    m_loginPassword->setEchoMode(QLineEdit::Password);
    m_loginError = label(QString(), "error");
    m_loginError->setWordWrap(true);
    QPushButton *submit = button(tr("Sign in"), "primary");
    submit->setDefault(true);
    QPushButton *toRegister = button(tr("Create an account"), "flat");
    connect(submit, &QPushButton::clicked, this, [this] { signIn(); });
    connect(m_loginPassword, &QLineEdit::returnPressed, this, [this] { signIn(); });
    connect(toRegister, &QPushButton::clicked, this, [this] { showPage(1); });
    v->addWidget(label(tr("User name"), "section"));
    v->addWidget(m_loginName);
    v->addWidget(label(tr("Password"), "section"));
    v->addWidget(m_loginPassword);
    v->addWidget(m_loginError);
    v->addWidget(submit);
    v->addWidget(toRegister);
    return page;
}

QWidget *LoginDialog::buildRegister()
{
    auto *page = new QWidget;
    auto *v = new QVBoxLayout(page);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(8);
    m_newName = new QLineEdit;
    m_newPassword = new QLineEdit;
    m_newPasswordAgain = new QLineEdit;
    m_newPassword->setEchoMode(QLineEdit::Password);
    m_newPasswordAgain->setEchoMode(QLineEdit::Password);
    m_newPassword->setPlaceholderText(tr("At least %1 characters").arg(MinPasswordLength));
    m_registerError = label(QString(), "error");
    m_registerError->setWordWrap(true);
    QPushButton *submit = button(tr("Create account"), "primary");
    QPushButton *toSignIn = button(tr("I already have an account"), "flat");
    connect(submit, &QPushButton::clicked, this, [this] { registerUser(); });
    connect(m_newPasswordAgain, &QLineEdit::returnPressed, this, [this] { registerUser(); });
    connect(toSignIn, &QPushButton::clicked, this, [this] { showPage(0); });
    v->addWidget(label(tr("Choose a user name"), "section"));
    v->addWidget(m_newName);
    v->addWidget(label(tr("Password"), "section"));
    v->addWidget(m_newPassword);
    v->addWidget(label(tr("Repeat the password"), "section"));
    v->addWidget(m_newPasswordAgain);
    v->addWidget(m_registerError);
    v->addWidget(submit);
    v->addWidget(toSignIn);
    return page;
}

void LoginDialog::showPage(int index)
{
    m_pages->setCurrentIndex(index);
    m_loginError->clear();
    m_registerError->clear();
    (index == 0 ? m_loginName : m_newName)->setFocus();
    adjustSize();
}

void LoginDialog::signIn()
{
    const QString name = m_loginName->text().trimmed();
    const QString problem = Accounts::check(name, m_loginPassword->text());
    if (!problem.isEmpty()) {
        m_loginError->setText(problem);
        m_loginPassword->selectAll();
        m_loginPassword->setFocus();
        return;
    }
    m_user = name;
    accept();
}

void LoginDialog::registerUser()
{
    const QString name = m_newName->text().trimmed();
    QString problem;
    if (m_newPassword->text() != m_newPasswordAgain->text())
        problem = tr("The two passwords are not the same.");
    else
        problem = Accounts::create(name, m_newPassword->text());
    if (!problem.isEmpty()) {
        m_registerError->setText(problem);
        return;
    }
    m_user = name;  // a new account is signed in straight away
    accept();
}
