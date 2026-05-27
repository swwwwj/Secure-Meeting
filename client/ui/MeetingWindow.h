#pragma once

#include <QWidget>
#include <QStringList>
#include <QImage>
#include <QList>

class QLabel;
class QGridLayout;
class QToolButton;
class QPushButton;
class VideoWidget;
class QButtonGroup;
class QComboBox;

class MeetingWindow : public QWidget
{
    Q_OBJECT
public:
    explicit MeetingWindow(QWidget *parent = nullptr);

    void setMeetingInfo(const QString &meetingId, const QString &userName);
    void setStatusMessage(const QString &message);
    void setParticipants(const QStringList &participants);
    void setPrimaryFrame(const QImage &frame);
    void setLocalMediaState(bool cameraOn, bool microphoneOn);
    void clearPrimaryFrame();
    void setAIEnabled(bool enabled);
    void setArcFaceEnabled(bool enabled);
    void setEnrollableUsers(const QStringList &users);

signals:
    void leaveClicked();
    void cameraToggled(bool enabled);
    void microphoneToggled(bool enabled);
    void aiToggled(bool enabled);
    void protectionLevelChanged(const QString &level);
    void enrollFaceRequested(const QString &userId);

private:
    void rebuildGrid(const QStringList &participants);
    static int columnsForCount(int count);

    QWidget *m_gridHost;
    QGridLayout *m_grid;
    QList<VideoWidget *> m_tiles;

    QLabel *m_titleLabel;
    QLabel *m_statusLabel;
    QToolButton *m_cameraButton;
    QToolButton *m_microphoneButton;
    QToolButton *m_aiButton;
    QPushButton *m_leaveButton;
    QButtonGroup *m_protectionGroup;
    QWidget *m_arcfacePanel;
    QLabel *m_arcfaceStatusLabel;
    QComboBox *m_enrollUserCombo;
    QPushButton *m_enrollFaceButton;
};
