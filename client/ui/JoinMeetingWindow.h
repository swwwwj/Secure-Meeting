#pragma once

#include <QImage>
#include <QWidget>
#include <QStringList>

#include "services/UserService.h"

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
    void setFaceProfileStatus(bool enrolled, const QString &message);
    void setAvailableFaceUsers(const QList<FaceProfileSummary> &profiles, const QStringList &selectedProfileKeys = {});

signals:
    void joinMeetingRequested(const QString &meetingId,
                              const QString &displayName,
                              bool cameraOn,
                              bool microphoneOn,
                              const QStringList &whitelistProfileKeys,
                              bool arcfaceEnabled);
    void faceProfileEnrollRequested(const QString &label, const QImage &image);
    void refreshFaceProfilesRequested();
    void backToLoginRequested();

private:
    QStringList selectedFaceUsers() const;

    QLineEdit *m_displayNameEdit;
    QLineEdit *m_meetingIdEdit;
    QLineEdit *m_faceNameEdit;
    QCheckBox *m_cameraCheck;
    QCheckBox *m_microphoneCheck;
    QCheckBox *m_arcfaceCheck;
    QLabel *m_faceProfileStatusLabel;
    QPushButton *m_uploadFaceButton;
    QPushButton *m_refreshProfilesButton;
    QListWidget *m_faceProfileList;
    QLabel *m_noticeLabel;
    QPushButton *m_joinButton;
};
