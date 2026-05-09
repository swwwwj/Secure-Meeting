#pragma once

#include <QMainWindow>

class QStackedWidget;
class LoginWindow;
class RegisterWindow;
class JoinMeetingWindow;
class MeetingWindow;

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);

    LoginWindow *loginWindow() const;
    RegisterWindow *registerWindow() const;
    JoinMeetingWindow *joinWindow() const;
    MeetingWindow *meetingWindow() const;

    void showLoginPage();
    void showRegisterPage();
    void showJoinPage();
    void showMeetingPage();

private:
    QStackedWidget *m_stack;
    LoginWindow *m_loginWindow;
    RegisterWindow *m_registerWindow;
    JoinMeetingWindow *m_joinWindow;
    MeetingWindow *m_meetingWindow;
};
