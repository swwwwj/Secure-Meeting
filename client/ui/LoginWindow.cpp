#include "ui/LoginWindow.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>

LoginWindow::LoginWindow(QWidget *parent)
    : QWidget(parent)
    , m_usernameEdit(new QLineEdit(this))
    , m_passwordEdit(new QLineEdit(this))
    , m_errorLabel(new QLabel(this))
    , m_modeLabel(new QLabel(this))
    , m_loginButton(new QPushButton("登录", this))
    , m_registerButton(new QPushButton("注册账号", this))
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    root->addStretch(1);

    auto *card = new QWidget(this);
    card->setObjectName("authCard");
    card->setFixedWidth(460);
    auto *layout = new QVBoxLayout(card);
    layout->setContentsMargins(44, 48, 44, 40);
    layout->setSpacing(14);

    auto *title = new QLabel("Secure Meeting", card);
    title->setObjectName("authTitle");
    title->setAlignment(Qt::AlignCenter);
    auto *subtitle = new QLabel("登录后继续加入会议", card);
    subtitle->setObjectName("authSubtitle");
    subtitle->setAlignment(Qt::AlignCenter);
    m_modeLabel->setObjectName("authSubtitle");
    m_modeLabel->setAlignment(Qt::AlignCenter);

    m_usernameEdit->setPlaceholderText("用户名");
    m_usernameEdit->setObjectName("inputField");
    m_usernameEdit->setFixedHeight(42);

    m_passwordEdit->setPlaceholderText("密码");
    m_passwordEdit->setEchoMode(QLineEdit::Password);
    m_passwordEdit->setObjectName("inputField");
    m_passwordEdit->setFixedHeight(42);

    m_errorLabel->setObjectName("errorLabel");
    m_errorLabel->setAlignment(Qt::AlignCenter);
    m_errorLabel->setVisible(false);

    m_loginButton->setObjectName("primaryButton");
    m_loginButton->setFixedHeight(42);
    m_registerButton->setObjectName("textButton");

    layout->addWidget(title);
    layout->addWidget(subtitle);
    layout->addWidget(m_modeLabel);
    layout->addSpacing(8);
    layout->addWidget(m_usernameEdit);
    layout->addWidget(m_passwordEdit);
    layout->addWidget(m_errorLabel);
    layout->addWidget(m_loginButton);
    layout->addWidget(m_registerButton, 0, Qt::AlignHCenter);

    root->addWidget(card, 0, Qt::AlignHCenter);
    root->addStretch(1);

    connect(m_loginButton, &QPushButton::clicked, this, [this]() {
        emit loginRequested(m_usernameEdit->text().trimmed(), m_passwordEdit->text());
    });
    connect(m_registerButton, &QPushButton::clicked, this, &LoginWindow::goToRegisterRequested);

    setRuntimeMode("Unknown");
}

void LoginWindow::setErrorMessage(const QString &message)
{
    m_errorLabel->setVisible(!message.isEmpty());
    m_errorLabel->setText(message);
}

void LoginWindow::setRuntimeMode(const QString &modeName)
{
    m_modeLabel->setText(QString("当前模式：%1").arg(modeName));
}
