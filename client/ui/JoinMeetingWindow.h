#pragma once

#include <QWidget>
#include <QStringList>

class QLineEdit;
class QListWidget;
class QCheckBox;
class QLabel;
class QPushButton;

class JoinMeetingWindow : public QWidget
{
    Q_OBJECT
public:
    explicit JoinMeetingWindow(QWidget *parent = nullptr);

    void setDisplayName(const QString &name);
    void setNotice(const QString &message);

signals:
    void joinMeetingRequested(const QString &meetingId,
                              const QString &displayName,
                              bool cameraOn,
                              bool microphoneOn,
                              const QStringList &whitelist,
                              bool arcfaceEnabled);
    void backToLoginRequested();

private:
    QStringList whitelistUsers() const;

    QLineEdit *m_displayNameEdit;
    QLineEdit *m_meetingIdEdit;
    QCheckBox *m_cameraCheck;
    QCheckBox *m_microphoneCheck;
    QCheckBox *m_arcfaceCheck;
    QLineEdit *m_whitelistInput;
    QListWidget *m_whitelistList;
    QLabel *m_noticeLabel;
    QPushButton *m_joinButton;
};
