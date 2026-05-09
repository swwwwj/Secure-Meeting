#pragma once

#include <QWidget>
#include <QString>

class QLineEdit;
class QLabel;
class QPushButton;

class LoginWindow : public QWidget
{
    Q_OBJECT
public:
    explicit LoginWindow(QWidget *parent = nullptr);

signals:
    void loginRequested(const QString &username, const QString &password);
    void goToRegisterRequested();

public slots:
    void setErrorMessage(const QString &message);
    void setRuntimeMode(const QString &modeName);

private:
    QLineEdit *m_usernameEdit;
    QLineEdit *m_passwordEdit;
    QLabel *m_errorLabel;
    QLabel *m_modeLabel;
    QPushButton *m_loginButton;
    QPushButton *m_registerButton;
};
