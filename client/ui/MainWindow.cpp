#include "ui/MainWindow.h"

#include "ui/JoinMeetingWindow.h"
#include "ui/LoginWindow.h"
#include "ui/MeetingWindow.h"
#include "ui/RegisterWindow.h"

#include <QStackedWidget>

static const char *kStyle = R"(
QMainWindow, QStackedWidget, QWidget {
    background: #ffffff;
    color: #252933;
    font-family: "Segoe UI", "Microsoft YaHei UI", "Inter", "HarmonyOS Sans SC", "Source Han Sans SC";
    font-size: 14px;
}
QWidget#authCard, QWidget#panelCard, QWidget#topBar {
    background: #ffffff;
    border: 1px solid #ebeef3;
    border-radius: 14px;
}
QWidget#floatingBar {
    background: #ffffff;
    border: 1px solid #e9edf3;
    border-radius: 18px;
}
QLabel#authTitle {
    font-size: 28px;
    font-weight: 700;
    color: #20242c;
}
QLabel#authSubtitle, QLabel#mutedText, QLabel#pageSubtitle {
    color: #7d8592;
    font-size: 13px;
    font-weight: 500;
}
QLabel#pageTitle {
    font-size: 26px;
    font-weight: 700;
    color: #20242c;
}
QLabel#sectionTitle {
    font-size: 14px;
    font-weight: 600;
    color: #353b46;
}
QLabel#meetingTitle {
    font-size: 15px;
    font-weight: 600;
    color: #2b313b;
}
QLineEdit#inputField {
    background: #ffffff;
    border: 1px solid #d9dee7;
    border-radius: 10px;
    padding: 0 12px;
    color: #20242c;
    font-size: 14px;
    font-weight: 500;
}
QLineEdit#inputField:hover { border-color: #c4cbd8; }
QLineEdit#inputField:focus { border: 1px solid #d33a31; }
QListWidget {
    background: #ffffff;
    border: 1px solid #d9dee7;
    border-radius: 10px;
    padding: 6px;
}
QListWidget::item {
    border-radius: 8px;
    padding: 6px 8px;
}
QListWidget::item:selected {
    background: #fce8e6;
    color: #b8281f;
}
QCheckBox {
    spacing: 8px;
    color: #4b5362;
    font-size: 13px;
}
QPushButton#primaryButton {
    background: #d33a31;
    border: none;
    border-radius: 10px;
    color: #ffffff;
    font-size: 14px;
    font-weight: 600;
    padding: 0 18px;
}
QPushButton#primaryButton:hover { background: #df4a40; }
QPushButton#primaryButton:pressed { background: #b82f27; }
QPushButton#secondaryButton, QToolButton#floatingButton, QToolButton#segButton {
    background: #ffffff;
    border: 1px solid #d9dee7;
    border-radius: 10px;
    color: #39404d;
    font-size: 13px;
    font-weight: 600;
    padding: 0 14px;
    min-height: 34px;
}
QPushButton#secondaryButton:hover, QToolButton#floatingButton:hover, QToolButton#segButton:hover {
    border-color: #c7cfdb;
    background: #fafbfc;
}
QToolButton#segButton:checked, QToolButton#floatingButton:checked {
    border-color: #d33a31;
    background: #fce8e6;
    color: #b72a22;
}
QPushButton#textButton {
    background: transparent;
    border: none;
    color: #7d8592;
    font-size: 13px;
    font-weight: 600;
    padding: 0;
}
QPushButton#textButton:hover { color: #d33a31; }
QPushButton#dangerButton {
    background: #d33a31;
    border: none;
    border-radius: 10px;
    min-height: 34px;
    padding: 0 16px;
    color: #fff;
    font-size: 13px;
    font-weight: 600;
}
QPushButton#dangerButton:hover { background: #df4a40; }
QLabel#errorLabel {
    color: #d33a31;
    font-size: 12px;
    font-weight: 600;
}
QWidget#videoCard {
    background: #ffffff;
    border: 1px solid #ebeef3;
    border-radius: 14px;
}
QLabel#videoCanvas {
    background: #f4f6f9;
    border-radius: 10px;
    color: #9aa2b0;
    font-size: 14px;
    font-weight: 600;
}
QLabel#nameChip {
    background: #ffffff;
    border-radius: 8px;
    padding: 2px 8px;
    font-size: 12px;
    font-weight: 600;
    color: #313844;
}
QLabel#statusChip {
    background: #ffffff;
    border-radius: 8px;
    padding: 2px 8px;
    color: #7d8592;
    font-size: 11px;
    font-weight: 600;
}
)";

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_stack(new QStackedWidget(this))
    , m_loginWindow(new LoginWindow(this))
    , m_registerWindow(new RegisterWindow(this))
    , m_joinWindow(new JoinMeetingWindow(this))
    , m_meetingWindow(new MeetingWindow(this))
{
    setWindowTitle("Secure Meeting");
    setMinimumSize(1080, 700);
    resize(1360, 860);

    m_stack->addWidget(m_loginWindow);
    m_stack->addWidget(m_registerWindow);
    m_stack->addWidget(m_joinWindow);
    m_stack->addWidget(m_meetingWindow);
    setCentralWidget(m_stack);
    setStyleSheet(kStyle);
    showLoginPage();
}

LoginWindow *MainWindow::loginWindow() const { return m_loginWindow; }
RegisterWindow *MainWindow::registerWindow() const { return m_registerWindow; }
JoinMeetingWindow *MainWindow::joinWindow() const { return m_joinWindow; }
MeetingWindow *MainWindow::meetingWindow() const { return m_meetingWindow; }

void MainWindow::showLoginPage() { m_stack->setCurrentIndex(0); }
void MainWindow::showRegisterPage() { m_stack->setCurrentIndex(1); }
void MainWindow::showJoinPage() { m_stack->setCurrentIndex(2); }
void MainWindow::showMeetingPage() { m_stack->setCurrentIndex(3); }
