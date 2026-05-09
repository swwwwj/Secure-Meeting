#pragma once

#include <QWidget>

class QLineEdit;
class QLabel;
class QPushButton;

class RegisterWindow : public QWidget
{
    Q_OBJECT
public:
    explicit RegisterWindow(QWidget *parent = nullptr);

signals:
    void registerSubmitRequested(const QString &username, const QString &password, const QString &confirmPassword);
    void backToLoginRequested();

public slots:
    void setMessage(const QString &message);
    void setRuntimeMode(const QString &modeName);

private:
    QLineEdit *m_usernameEdit;
    QLineEdit *m_passwordEdit;
    QLineEdit *m_confirmPasswordEdit;
    QLabel *m_modeLabel;
    QLabel *m_messageLabel;
    QPushButton *m_registerButton;
    QPushButton *m_backButton;
};
