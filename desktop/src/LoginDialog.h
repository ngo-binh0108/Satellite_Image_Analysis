#pragma once
#include <QDialog>

class QLabel;
class QLineEdit;
class QStackedWidget;

// Local user accounts for this computer (the app is offline, so there is no server to sign in to).
// Passwords are never stored: only a salted PBKDF2-SHA256 hash, in users.json in the app-data folder.
namespace Accounts {
QString filePath();
bool exists(const QString &userName);
// Both return an empty string on success, otherwise the reason to show the user.
QString create(const QString &userName, const QString &password);
QString check(const QString &userName, const QString &password);
}  // namespace Accounts

// Sign-in window with a second page for creating an account.
class LoginDialog : public QDialog {
public:
    explicit LoginDialog(QWidget *parent = nullptr);
    QString userName() const { return m_user; }

private:
    QWidget *buildSignIn();
    QWidget *buildRegister();
    void signIn();
    void registerUser();
    void showPage(int index);

    QStackedWidget *m_pages;
    QLineEdit *m_loginName, *m_loginPassword;
    QLineEdit *m_newName, *m_newPassword, *m_newPasswordAgain;
    QLabel *m_loginError, *m_registerError;
    QString m_user;
};
