#pragma once
#include <QDockWidget>
#include <QString>
#include <memory>
#include <QTimer>
#include <QPixmap>

namespace Ui {
class Dashboard;
}

class Dashboard : public QDockWidget {
	Q_OBJECT

public:
	explicit Dashboard(QWidget *parent = nullptr);
	virtual ~Dashboard();

private slots:
	// 🎯 修改：增加 voiceName
	void onStatusUpdated(const QString &type, const QString &msg, qint64 tNext, qint64 nNext, bool isConnected,
			     int noiseCount, const QString &voiceName);
	void onLogMessage(const QString &msg);
	void showConfigDialog();
	void updateBreathingEffect();

private:
	std::unique_ptr<Ui::Dashboard> ui;

	void updateStyles(const QString &type, bool enabled);
	void updateConnectionState(bool isConnected);

	enum IconType { Icon_Settings, Icon_Link, Icon_Plane, Icon_Play, Icon_Stop, Icon_Circle };
	QPixmap drawIcon(IconType type, const QColor &color, int size);

	QTimer *m_breathTimer;
	float m_breathStep;
	float m_currentOpacity;
	bool m_isConnected;
};