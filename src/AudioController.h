#pragma once
#include <QObject>
#include <QTimer>
#include <QDateTime>
#include <QDir>
#include <QMutex>
#include <QList>
#include <QMap>
#include "Common.h"

// 任务结构体
struct AudioTask {
	QString filePath;
	QString type;   // "time", "noise", "reply"
	qint64 addTime; // 🎯 新增：记录入队时间戳，用于超时判断
};

class AudioController : public QObject {
	Q_OBJECT
public:
	static AudioController &instance();

	void init();
	void setConfig(const PluginConfig &config);
	PluginConfig getConfig() const { return m_config; }

	void enqueueTask(const QString &path, const QString &type);
	QString enqueueTaskAndReturn(const QString &path, const QString &type);

	void triggerManualTime();
	void triggerManualNoise();

	void recordHeartbeat() { m_lastHeartbeatTime = QDateTime::currentSecsSinceEpoch(); }

signals:
	void logMessage(const QString &msg);
	void statusUpdated(const QString &type, const QString &msg, qint64 tNext, qint64 nNext, bool isConnected,
			   int noiseCount, const QString &voiceName);

private slots:
	void onTimerTick();
	void checkMediaStatus();

private:
	AudioController(QObject *parent = nullptr);
	~AudioController();

	void loadConfigFromDisk();
	void playFile(const AudioTask &task);
	void processNextTask();
	void applyDucking(bool active);

	QString pickRandomFile(const QString &path, bool useHistory = false);
	double getAudioDuration(const QString &filePath);
	void cleanUpOldTempFiles(qint64 currentPlayingTs = 0);

	// 声明合并函数
	QString mergeWavFiles(const QStringList &files);

	int getNoiseFileCount();
	void resetTimeTrigger();
	void resetNoiseTrigger();

	PluginConfig m_config;
	QList<AudioTask> m_queue;

	bool m_isPlaying = false;
	QString m_currentJobType = "";
	qint64 m_playStartTime = 0;

	qint64 m_nextTimeTrigger = 0;
	qint64 m_nextNoiseTrigger = 0;
	qint64 m_lastHeartbeatTime = 0;

	QList<QString> m_history;
	QMap<QString, float> m_originalVolumes;

	QTimer *m_mainTimer;
	QTimer *m_playbackMonitorTimer;
	QMutex m_mutex;
};