#include "ui/RegisterWindow.h"

#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

RegisterWindow::RegisterWindow(QWidget *parent)
    : QWidget(parent)
    , m_usernameEdit(new QLineEdit(this))
    , m_passwordEdit(new QLineEdit(this))
    , m_confirmPasswordEdit(new QLineEdit(this))
    , m_modeLabel(new QLabel(this))
    , m_messageLabel(new QLabel(this))
    , m_registerButton(new QPushButton("注册", this))
    , m_backButton(new QPushButton("返回登录", this))
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

    auto *title = new QLabel("注册账号", card);
    title->setObjectName("authTitle");
    title->setAlignment(Qt::AlignCenter);
    auto *subtitle = new QLabel("创建账号后返回登录", card);
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

    m_confirmPasswordEdit->setPlaceholderText("确认密码");
    m_confirmPasswordEdit->setEchoMode(QLineEdit::Password);
    m_confirmPasswordEdit->setObjectName("inputField");
    m_confirmPasswordEdit->setFixedHeight(42);

    m_messageLabel->setObjectName("errorLabel");
    m_messageLabel->setAlignment(Qt::AlignCenter);
    m_messageLabel->setVisible(false);

    m_registerButton->setObjectName("primaryButton");
    m_registerButton->setFixedHeight(42);
    m_backButton->setObjectName("textButton");

    layout->addWidget(title);
    layout->addWidget(subtitle);
    layout->addWidget(m_modeLabel);
    layout->addSpacing(8);
    layout->addWidget(m_usernameEdit);
    layout->addWidget(m_passwordEdit);
    layout->addWidget(m_confirmPasswordEdit);
    layout->addWidget(m_messageLabel);
    layout->addWidget(m_registerButton);
    layout->addWidget(m_backButton, 0, Qt::AlignHCenter);

    root->addWidget(card, 0, Qt::AlignHCenter);
    root->addStretch(1);

    connect(m_registerButton, &QPushButton::clicked, this, [this]() {
        emit registerSubmitRequested(m_usernameEdit->text().trimmed(),
                                     m_passwordEdit->text(),
                                     m_confirmPasswordEdit->text());
    });
    connect(m_backButton, &QPushButton::clicked, this, &RegisterWindow::backToLoginRequested);

    setRuntimeMode("Unknown");
}

void RegisterWindow::setMessage(const QString &message)
{
    m_messageLabel->setVisible(!message.isEmpty());
    m_messageLabel->setText(message);
}

void RegisterWindow::setRuntimeMode(const QString &modeName)
{
    m_modeLabel->setText(QString("当前模式：%1").arg(modeName));
}
