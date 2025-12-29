#include "AudioController.h"
#include <QRandomGenerator>
#include <QDebug>
#include <QFileInfo>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFile>
#include <QStandardPaths>

extern "C" {
#include <obs.h>
#include <obs-frontend-api.h>
#include <obs-module.h>
}

AudioController &AudioController::instance()
{
	static AudioController inst;
	return inst;
}

AudioController::AudioController(QObject *parent) : QObject(parent)
{
	m_mainTimer = new QTimer(this);
	connect(m_mainTimer, &QTimer::timeout, this, &AudioController::onTimerTick);

	m_playbackMonitorTimer = new QTimer(this);
	connect(m_playbackMonitorTimer, &QTimer::timeout, this, &AudioController::checkMediaStatus);
}

AudioController::~AudioController()
{
	m_mainTimer->stop();
	m_playbackMonitorTimer->stop();
}

void AudioController::init()
{
	loadConfigFromDisk();
	cleanUpOldTempFiles(0);
	m_mainTimer->start(1000);
	resetTimeTrigger();
	resetNoiseTrigger();
}

void AudioController::loadConfigFromDisk()
{
	char *path_c = obs_module_config_path("xhs-guard-config.json");
	if (!path_c)
		return;
	QString path = QString::fromUtf8(path_c);
	bfree(path_c);

	QFile file(path);
	if (!file.open(QIODevice::ReadOnly))
		return;

	QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
	if (doc.isNull())
		return;

	QJsonObject root = doc.object();
	QMutexLocker locker(&m_mutex);

	m_config.mediaSourceName = root["mediaSourceName"].toString();
	m_config.voicePackPath = root["voicePackPath"].toString();
	m_config.timeMin = root["timeMin"].toInt(120);
	m_config.timeMax = root["timeMax"].toInt(180);
	m_config.noiseMin = root["noiseMin"].toInt(90);
	m_config.noiseMax = root["noiseMax"].toInt(120);
	m_config.duckVolume = root["duckVolume"].toDouble(0.2);

	if (root.contains("historySize"))
		m_config.historySize = root["historySize"].toInt(30);
	if (root.contains("shortFileThreshold"))
		m_config.shortFileThreshold = root["shortFileThreshold"].toInt(6);

	m_config.duckSources.clear();
	QJsonArray arr = root["duckSources"].toArray();
	for (const auto &val : arr) {
		m_config.duckSources.append(val.toString());
	}
}

void AudioController::setConfig(const PluginConfig &config)
{
	QMutexLocker locker(&m_mutex);
	m_config = config;
}

void AudioController::enqueueTask(const QString &path, const QString &type)
{
	enqueueTaskAndReturn(path, type);
}

QString AudioController::enqueueTaskAndReturn(const QString &path, const QString &type)
{
	QMutexLocker locker(&m_mutex);
	QString fileToPlay = "";
	QFileInfo info(path);

	if (info.isFile()) {
		fileToPlay = path;
	} else if (info.isDir()) {
		fileToPlay = pickRandomFile(path, type == "noise");
	}

	if (fileToPlay.isEmpty())
		return "";

	AudioTask task;
	task.filePath = fileToPlay;
	task.type = type;
	// 🎯 新增：记录入队时间
	task.addTime = QDateTime::currentSecsSinceEpoch();

	m_queue.append(task);
	emit logMessage(QString::fromUtf8(">>> [入队] ") + QFileInfo(fileToPlay).fileName());

	if (!m_isPlaying) {
		m_isPlaying = true;
		QTimer::singleShot(0, this, &AudioController::processNextTask);
	}

	return QFileInfo(fileToPlay).fileName();
}

void AudioController::processNextTask()
{
	m_playbackMonitorTimer->stop();

	QMutexLocker locker(&m_mutex);

	// 🎯 修改：循环检查队列，直到找到有效任务或队列为空
	while (!m_queue.isEmpty()) {
		// 预取第一个任务（暂不移除）
		AudioTask task = m_queue.first();

		// 🎯 核心逻辑：检查报时任务是否过期 (>30秒)
		if (task.type == "time") {
			qint64 now = QDateTime::currentSecsSinceEpoch();
			if (now - task.addTime > 30) {
				// 任务已过期，移除并记录日志
				m_queue.removeFirst();
				emit logMessage(QString::fromUtf8("⚠️ 报时任务过期(>30s)，已丢弃: ") +
						QFileInfo(task.filePath).fileName());

				// 继续下一次循环，检查下一个任务
				continue;
			}
		}

		// 任务有效，移除并开始播放
		m_queue.removeFirst();
		playFile(task);
		return; // 退出函数，开始播放
	}

	// === 如果代码走到这里，说明队列为空（或任务全被丢弃） ===

	// 以下保持原有逻辑：清理状态、恢复音量、重置 OBS 源
	m_isPlaying = false;
	m_currentJobType = "";
	applyDucking(false);

	obs_source_t *source = obs_get_source_by_name(m_config.mediaSourceName.toUtf8().constData());
	if (source) {
		obs_data_t *settings = obs_data_create();
		obs_data_set_string(settings, "local_file", "");
		obs_source_update(source, settings);
		obs_data_release(settings);

		obs_source_set_enabled(source, false);
		obs_source_set_muted(source, true);
		obs_source_release(source);
	}
}

void AudioController::playFile(const AudioTask &task)
{
	m_currentJobType = task.type;

	if (task.type == "time") {
		QString baseName = QFileInfo(task.filePath).baseName();
		QStringList parts = baseName.split('_');
		if (parts.size() >= 3) {
			bool ok;
			qint64 ts = parts.last().toLongLong(&ok);
			if (ok)
				cleanUpOldTempFiles(ts);
		}
	}

	obs_source_t *source = obs_get_source_by_name(m_config.mediaSourceName.toUtf8().constData());
	if (source) {
		applyDucking(true);

		obs_data_t *settings = obs_data_create();
		obs_data_set_string(settings, "local_file",
				    QDir::toNativeSeparators(task.filePath).toUtf8().constData());
		obs_data_set_bool(settings, "looping", false);
		obs_data_set_bool(settings, "close_when_inactive", true);
		obs_data_set_bool(settings, "restart_on_activate", true);
		obs_source_update(source, settings);
		obs_data_release(settings);

		obs_source_set_muted(source, false);
		obs_source_set_enabled(source, false);
		obs_source_set_enabled(source, true);

		obs_source_release(source);

		emit logMessage("[" + task.type + "] " + QString::fromUtf8("播放: ") +
				QFileInfo(task.filePath).fileName());

		m_playStartTime = QDateTime::currentMSecsSinceEpoch();
		m_playbackMonitorTimer->start(200);
	} else {
		emit logMessage(QString::fromUtf8(">>> [错误] 找不到媒体源，跳过"));
		processNextTask();
	}
}

void AudioController::checkMediaStatus()
{
	if (!m_isPlaying) {
		m_playbackMonitorTimer->stop();
		return;
	}

	obs_source_t *source = obs_get_source_by_name(m_config.mediaSourceName.toUtf8().constData());
	if (source) {
		obs_media_state state = obs_source_media_get_state(source);
		qint64 now = QDateTime::currentMSecsSinceEpoch();
		qint64 elapsed = now - m_playStartTime;

		if (elapsed > 60000) {
			emit logMessage(QString::fromUtf8(">>> [异常] 播放超时，强制跳过"));
			obs_source_release(source);
			processNextTask();
			return;
		}

		if (state == OBS_MEDIA_STATE_ENDED) {
			obs_source_release(source);
			processNextTask();
		} else if (state == OBS_MEDIA_STATE_PLAYING || state == OBS_MEDIA_STATE_OPENING ||
			   state == OBS_MEDIA_STATE_BUFFERING) {
			obs_source_release(source);
		} else {
			if (elapsed < 2000) {
				obs_source_release(source);
			} else {
				// 超过2秒且不是播放状态，判定为结束
				obs_source_release(source);
				processNextTask();
			}
		}
	} else {
		processNextTask();
	}
}

void AudioController::applyDucking(bool active)
{
	if (active) {
		for (const QString &name : m_config.duckSources) {
			obs_source_t *s = obs_get_source_by_name(name.toUtf8().constData());
			if (s) {
				if (!m_originalVolumes.contains(name)) {
					float currentVol = obs_source_get_volume(s);
					m_originalVolumes.insert(name, currentVol);
				}
				obs_source_set_volume(s, m_config.duckVolume);
				obs_source_release(s);
			}
		}
	} else {
		QMapIterator<QString, float> i(m_originalVolumes);
		while (i.hasNext()) {
			i.next();
			QString name = i.key();
			float originalVol = i.value();
			obs_source_t *s = obs_get_source_by_name(name.toUtf8().constData());
			if (s) {
				obs_source_set_volume(s, originalVol);
				obs_source_release(s);
			}
		}
		m_originalVolumes.clear();
	}
}

QString AudioController::pickRandomFile(const QString &path, bool useHistory)
{
	QDir dir(path);
	QStringList filters;
	filters << "*.wav" << "*.mp3";
	QStringList files = dir.entryList(filters, QDir::Files);
	if (files.isEmpty())
		return "";

	if (!useHistory || files.size() <= m_config.historySize) {
		return dir.absoluteFilePath(files[QRandomGenerator::global()->bounded(files.size())]);
	}

	QString pickedFile;
	int maxRetries = 20;
	do {
		pickedFile = dir.absoluteFilePath(files[QRandomGenerator::global()->bounded(files.size())]);
		maxRetries--;
	} while (m_history.contains(pickedFile) && maxRetries > 0);

	m_history.append(pickedFile);
	if (m_history.size() > m_config.historySize)
		m_history.removeFirst();
	return pickedFile;
}

double AudioController::getAudioDuration(const QString &filePath)
{
	QFile file(filePath);
	if (!file.open(QIODevice::ReadOnly))
		return 0.0;
	qint64 fileSize = file.size();
	QString ext = QFileInfo(filePath).suffix().toLower();
	if (ext == "wav") {
		file.seek(28);
		QByteArray rateData = file.read(4);
		if (rateData.size() < 4)
			return 0.0;
		quint32 byteRate = *reinterpret_cast<const quint32 *>(rateData.constData());
		return byteRate > 0 ? (double)(fileSize - 44) / byteRate : 0.0;
	}
	return 0.0;
}

void AudioController::cleanUpOldTempFiles(qint64 currentPlayingTs)
{
	QString tempPath = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
	QDir dir(tempPath);
	dir.setNameFilters(QStringList() << "xhs_time_*.wav");
	for (const QFileInfo &fi : dir.entryInfoList(QDir::Files)) {
		if (currentPlayingTs == 0) {
			QFile::remove(fi.absoluteFilePath());
		} else {
			QStringList parts = fi.baseName().split('_');
			if (parts.size() >= 3) {
				bool ok;
				qint64 fileTs = parts.last().toLongLong(&ok);
				if (ok && fileTs < currentPlayingTs)
					QFile::remove(fi.absoluteFilePath());
			}
		}
	}
}

QString AudioController::mergeWavFiles(const QStringList &files)
{
	if (files.isEmpty())
		return "";
	if (files.size() == 1)
		return files[0];

	QString tempName = QString("xhs_time_%1.wav").arg(QDateTime::currentMSecsSinceEpoch());
	QString tempPath =
		QStandardPaths::writableLocation(QStandardPaths::TempLocation).replace("\\", "/") + "/" + tempName;

	QFile outFile(tempPath);
	if (!outFile.open(QIODevice::WriteOnly))
		return "";

	QByteArray allPcmData;
	quint16 audioFormat = 1;
	quint16 numChannels = 2;
	quint32 sampleRate = 44100;
	quint32 byteRate = 176400;
	quint16 blockAlign = 4;
	quint16 bitsPerSample = 16;
	bool formatCaptured = false;

	for (const QString &filePath : files) {
		QFile f(filePath);
		if (!f.open(QIODevice::ReadOnly))
			continue;
		QByteArray content = f.readAll();
		f.close();
		if (content.size() < 44 || memcmp(content.constData(), "RIFF", 4) != 0)
			continue;

		int pos = 12;
		while (pos + 8 <= content.size()) {
			QByteArray chunkId = content.mid(pos, 4);
			quint32 chunkSize = *reinterpret_cast<const quint32 *>(content.constData() + pos + 4);
			if (!formatCaptured && chunkId == "fmt ") {
				if (chunkSize >= 16 && pos + 8 + 16 <= content.size()) {
					const char *ptr = content.constData() + pos + 8;
					audioFormat = *reinterpret_cast<const quint16 *>(ptr);
					numChannels = *reinterpret_cast<const quint16 *>(ptr + 2);
					sampleRate = *reinterpret_cast<const quint32 *>(ptr + 4);
					byteRate = *reinterpret_cast<const quint32 *>(ptr + 8);
					blockAlign = *reinterpret_cast<const quint16 *>(ptr + 12);
					bitsPerSample = *reinterpret_cast<const quint16 *>(ptr + 14);
					formatCaptured = true;
				}
			} else if (chunkId == "data") {
				int actualSize = qMin((int)chunkSize, content.size() - (pos + 8));
				if (actualSize > 0)
					allPcmData.append(content.mid(pos + 8, actualSize));
			}
			pos += 8 + chunkSize;
		}
	}

	if (allPcmData.isEmpty()) {
		outFile.close();
		return "";
	}

	QByteArray header;
	header.resize(44);
	char *h = header.data();
	memcpy(h, "RIFF", 4);
	quint32 fileSize = 36 + allPcmData.size();
	memcpy(h + 4, &fileSize, 4);
	memcpy(h + 8, "WAVE", 4);
	memcpy(h + 12, "fmt ", 4);
	quint32 fmtSize = 16;
	memcpy(h + 16, &fmtSize, 4);
	memcpy(h + 20, &audioFormat, 2);
	memcpy(h + 22, &numChannels, 2);
	memcpy(h + 24, &sampleRate, 4);
	memcpy(h + 28, &byteRate, 4);
	memcpy(h + 32, &blockAlign, 2);
	memcpy(h + 34, &bitsPerSample, 2);
	memcpy(h + 36, "data", 4);
	quint32 dataSize = allPcmData.size();
	memcpy(h + 40, &dataSize, 4);

	outFile.write(header);
	outFile.write(allPcmData);
	outFile.close();
	return tempPath;
}

int AudioController::getNoiseFileCount()
{
	if (m_config.voicePackPath.isEmpty())
		return 0;
	QDir dir(m_config.voicePackPath + "/noise");
	return dir.entryList(QStringList() << "*.wav" << "*.mp3", QDir::Files).size();
}

void AudioController::onTimerTick()
{
	qint64 now = QDateTime::currentSecsSinceEpoch();
	bool isConnected = (now - m_lastHeartbeatTime) < 10;

	// 🎯 核心修复：防卡死自愈逻辑
	// 如果插件认为正在播放，但 OBS 媒体源实际上已经停止/无状态超过 3 秒
	// 且距离开始播放已经超过了 5 秒（避开加载期），则强制重置
	if (m_isPlaying && (QDateTime::currentMSecsSinceEpoch() - m_playStartTime > 5000)) {
		obs_source_t *source = obs_get_source_by_name(m_config.mediaSourceName.toUtf8().constData());
		if (source) {
			obs_media_state state = obs_source_media_get_state(source);
			if (state != OBS_MEDIA_STATE_PLAYING && state != OBS_MEDIA_STATE_BUFFERING &&
			    state != OBS_MEDIA_STATE_OPENING) {
				// 发现逻辑状态与物理状态不符，强制自愈
				m_isPlaying = false;
				applyDucking(false);
				m_playbackMonitorTimer->stop();
			}
			obs_source_release(source);
		}
	}

	QString statusType = m_isPlaying ? "playing_" + m_currentJobType : "idle";
	QString statusMsg = m_isPlaying ? QString::fromUtf8("正在执行音频任务")
					: QString::fromUtf8("正在监控直播间...");
	if (!m_queue.isEmpty()) {
		statusMsg += QString(" (+%1)").arg(m_queue.size());
	}

	QString voiceName = "默认";
	if (!m_config.voicePackPath.isEmpty())
		voiceName = QDir(m_config.voicePackPath).dirName();

	emit statusUpdated(statusType, statusMsg, m_nextTimeTrigger - now, m_nextNoiseTrigger - now, isConnected,
			   getNoiseFileCount(), voiceName);

	if (m_config.scriptEnabled) {
		if (now >= m_nextTimeTrigger) {
			triggerManualTime();
			resetTimeTrigger();
		}
		if (now >= m_nextNoiseTrigger) {
			triggerManualNoise();
			resetNoiseTrigger();
		}
	}
}

void AudioController::resetTimeTrigger()
{
	m_nextTimeTrigger = QDateTime::currentSecsSinceEpoch() +
			    QRandomGenerator::global()->bounded(m_config.timeMin, m_config.timeMax + 1);
}
void AudioController::resetNoiseTrigger()
{
	m_nextNoiseTrigger = QDateTime::currentSecsSinceEpoch() +
			     QRandomGenerator::global()->bounded(m_config.noiseMin, m_config.noiseMax + 1);
}

void AudioController::triggerManualTime()
{
	QString root = m_config.voicePackPath;
	if (root.isEmpty())
		return;
	QStringList files;
	QString fPrefix = pickRandomFile(root + "/prefix", false);
	if (!fPrefix.isEmpty())
		files << fPrefix;
	QString fDate = root + "/date/" + QDateTime::currentDateTime().toString("MMdd") + ".wav";
	if (QFile::exists(fDate))
		files << fDate;
	QString fTime = root + "/time/" + QDateTime::currentDateTime().toString("HHmm") + ".wav";
	if (QFile::exists(fTime))
		files << fTime;

	if (files.isEmpty())
		return;
	QString mergedFile = mergeWavFiles(files);
	if (!mergedFile.isEmpty())
		enqueueTask(mergedFile, "time");
}

void AudioController::triggerManualNoise()
{
	QString noiseDir = m_config.voicePackPath + "/noise";
	QString f1 = pickRandomFile(noiseDir, true);
	if (!f1.isEmpty()) {
		enqueueTask(f1, "noise");
		if (getAudioDuration(f1) < m_config.shortFileThreshold) {
			QString f2 = pickRandomFile(noiseDir, true);
			if (!f2.isEmpty())
				enqueueTask(f2, "noise");
		}
	}
}