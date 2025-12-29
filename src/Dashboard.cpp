#include "Dashboard.h"
#include "ui_Dashboard.h"
#include "AudioController.h"
#include "ConfigDialog.h"

#include <QDateTime>
#include <QPushButton>
#include <QCheckBox>
#include <QToolButton>
#include <QLabel>
#include <QTextEdit>
#include <QTimer>
#include <QMainWindow>
#include <QPainter>
#include <QStyle>
#include <QGridLayout>
#include <QScrollBar>

extern "C" {
#include <obs-frontend-api.h>
}

// === 视觉规范颜色定义 ===
const QColor COL_BG_DARK = QColor("#18181b");
const QColor COL_GREEN = QColor("#10b981");
const QColor COL_CYAN = QColor("#22d3ee");
const QColor COL_AMBER = QColor("#fbbf24");
const QColor COL_PINK = QColor("#f472b6");
const QColor COL_GRAY = QColor("#71717a");
const QColor COL_TEXT_WHITE = QColor("#ffffff");

Dashboard::Dashboard(QWidget *parent) : QDockWidget(parent), ui(new Ui::Dashboard), m_isConnected(false)
{
	ui->setupUi(this);

	// ==========================================================
	// 1. 布局重构 (UI 细节最终版)
	// ==========================================================

	// A. 全局容器间距
	if (ui->contentLayout) {
		ui->contentLayout->setSpacing(0);
		// 四周间距 12px
		ui->contentLayout->setContentsMargins(12, 12, 12, 12);

		// 垂直伸缩因子 (日志框占满剩余)
		ui->contentLayout->setStretch(0, 0);
		ui->contentLayout->setStretch(1, 0);
		ui->contentLayout->setStretch(2, 0);
		ui->contentLayout->setStretch(3, 0);
		ui->contentLayout->setStretch(4, 1);
	}

	// B. 顶部栏 (TopBar)
	if (ui->topBar->layout()) {
		ui->topBar->layout()->setAlignment(Qt::AlignVCenter);
		ui->topBar->layout()->setContentsMargins(12, 0, 12, 0);
		ui->topBar->layout()->setSpacing(8);
	}

	// C. 监控状态卡片
	if (ui->statusCard->layout()) {
		ui->statusCard->layout()->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
		ui->statusCard->layout()->setContentsMargins(12, 0, 12, 0);
		// 图标与文字间距 4px
		ui->statusCard->layout()->setSpacing(4);
	}
	if (ui->vlStatusText) {
		ui->vlStatusText->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
		ui->vlStatusText->setContentsMargins(0, 0, 0, 0);
		ui->vlStatusText->setSpacing(2);
	}

	// D. 倒计时卡片布局
	auto fixCardLayout = [](QFrame *card, QLabel *valLabel, QLabel *titleLabel, QToolButton *btn) {
		QGridLayout *gl = qobject_cast<QGridLayout *>(card->layout());
		if (gl) {
			gl->setContentsMargins(0, 0, 0, 0);
			gl->setSpacing(0);

			gl->removeWidget(valLabel);
			gl->addWidget(valLabel, 0, 0, 2, 2, Qt::AlignTop | Qt::AlignHCenter);

			gl->removeWidget(titleLabel);
			gl->addWidget(titleLabel, 0, 0, 3, 2, Qt::AlignBottom | Qt::AlignHCenter);

			btn->raise();
		}
	};
	fixCardLayout(ui->cardTime, ui->tVal, ui->lblTitleTime, ui->btnTriggerTime);
	fixCardLayout(ui->cardNoise, ui->nVal, ui->lblTitleNoise, ui->btnTriggerNoise);

	// E. 文字行 (InfoLayout) 物理修复
	if (ui->infoLayout) {
		// 间距 (6, 6, 8, 12)
		ui->infoLayout->setContentsMargins(6, 6, 8, 12);

		// 锁死标签高度，防止拉伸
		ui->lblInfo1->setFixedHeight(20);
		ui->lblInfo2->setFixedHeight(20);

		ui->lblInfo1->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
		ui->lblInfo2->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
	}

	// F. 其他对齐
	ui->stText->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
	ui->stSub->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);

	// 【修改点 1】隐藏原来的连接文字标签，不再显示
	ui->lblConnText->setVisible(false);
	// 原代码: ui->lblConnText->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);

	ui->iconLink->setAlignment(Qt::AlignCenter);

	// 【修改点 2】给图标开启鼠标追踪（可选，但这能确保悬停立即生效）并设置手型光标
	ui->iconLink->setCursor(Qt::PointingHandCursor);

	// ==========================================================
	// 2. 全局样式表 (Global CSS)
	// ==========================================================
	QString baseStyle = R"(
		QWidget {
			background-color: #18181b;
			color: #e4e4e7;
			font-family: 'Microsoft YaHei', 'Segoe UI', sans-serif;
		}
		
		QLabel { background: transparent; border: none; }

		/* --- 顶部栏 --- */
		QFrame#topBar {
			background-color: #232326;
			border-radius: 8px;
			min-height: 44px; max-height: 44px;
		}

		/* 开关样式 */
		QCheckBox#masterSwitch {
			background: transparent; border: none; outline: none; padding: 0px;
			min-width: 46px; max-width: 46px; 
			min-height: 40px; max-height: 40px; 
		}
		QCheckBox#masterSwitch::indicator { width: 0px; height: 0px; image: none; }

		/* 设置按钮 */
		QToolButton#btnSettings {
			background: transparent; border: none; margin-left: 4px;
			min-width: 22px; max-width: 22px; min-height: 22px; max-height: 22px;
		}
		QToolButton#btnSettings:hover { background-color: #3f3f46; border-radius: 4px; }

		/* --- 监控卡片 --- */
		QFrame#statusCard {
			background-color: #232326;
			border-radius: 12px;
			min-height: 68px; max-height: 68px; 
			margin-top: 12px;
		}

		/* --- 倒计时卡片 --- */
		QFrame#cardTime, QFrame#cardNoise {
			background-color: #232326;
			border-radius: 10px;
			min-height: 68px; max-height: 68px;
			margin-top: 12px;
		}

		/* 倒计时数字 */
		QLabel#tVal, QLabel#nVal {
			font-size: 24px; 
			font-weight: bold;
			font-family: 'Consolas', 'DIN Alternate', sans-serif;
			padding-top: 12px; 
			padding-bottom: 0px;
			margin: 0px;
		}
		
		/* 标题 margin-bottom 12px */
		QLabel#lblTitleTime, QLabel#lblTitleNoise {
			font-size: 11px;
			color: #a1a1aa;
			margin-bottom: 12px;
		}

		/* 纸飞机按钮 */
		QToolButton#btnTriggerTime, QToolButton#btnTriggerNoise {
			background: transparent; border: none; 
			padding: 2px;
			margin-top: 4px; margin-right: 4px;
			min-width: 24px; max-width: 24px; min-height: 24px; max-height: 24px;
		}
		QToolButton#btnTriggerTime:hover, QToolButton#btnTriggerNoise:hover {
			background-color: #3f3f46; border-radius: 4px;
		}

		/* --- 底部信息 --- */
		QLabel#lblInfo1, QLabel#lblInfo2 {
			font-size: 10px; color: #a1a1aa;
			padding: 0px; margin: 0px;
		}

		/* --- 日志框 --- */
		QTextEdit#logBox {
			background-color: #09090b;
			border: 1px solid #27272a;
			border-radius: 6px;
			color: #a1a1aa;
			font-size: 11px;
			font-family: 'Consolas', monospace;
			padding: 4px;
			margin: 0px; 
			min-height: 55px; 
		}
	)";
	this->setStyleSheet(baseStyle);

	// === 3. 图标初始化 ===
	ui->btnSettings->setIcon(QIcon(":/assets/icon_settings_1.svg"));
	ui->btnSettings->setIconSize(QSize(20, 20));
	ui->btnTriggerTime->setToolTip(QString::fromUtf8("插入报时")); // <--- 新增这行

	ui->iconLink->setFixedSize(22, 21);
	ui->iconLink->setScaledContents(true);
	ui->btnTriggerNoise->setToolTip(QString::fromUtf8("插入混淆")); // <--- 新增这行

	QIcon iconOff(":/assets/switch_off_1.svg");
	QIcon iconOn(":/assets/switch_on_1.svg");

	ui->masterSwitch->setFixedSize(46, 40);
	ui->masterSwitch->setIconSize(QSize(46, 20));
	ui->masterSwitch->setIcon(iconOff);

	ui->btnTriggerTime->setIcon(QIcon(drawIcon(Icon_Plane, COL_GRAY, 16)));
	ui->btnTriggerTime->setIconSize(QSize(16, 16));
	ui->btnTriggerNoise->setIcon(QIcon(drawIcon(Icon_Plane, COL_GRAY, 16)));
	ui->btnTriggerNoise->setIconSize(QSize(16, 16));

	// === 4. 信号绑定 ===
	connect(&AudioController::instance(), &AudioController::statusUpdated, this, &Dashboard::onStatusUpdated);
	connect(&AudioController::instance(), &AudioController::logMessage, this, &Dashboard::onLogMessage);

	connect(ui->masterSwitch, &QCheckBox::toggled, this, [this, iconOn, iconOff](bool checked) {
		PluginConfig cfg = AudioController::instance().getConfig();
		cfg.scriptEnabled = checked;
		AudioController::instance().setConfig(cfg);
		ui->masterSwitch->setIcon(checked ? iconOn : iconOff);
	});

	connect(ui->btnTriggerTime, &QToolButton::clicked, this,
		[]() { AudioController::instance().triggerManualTime(); });
	connect(ui->btnTriggerNoise, &QToolButton::clicked, this,
		[]() { AudioController::instance().triggerManualNoise(); });

	connect(ui->btnSettings, &QToolButton::clicked, this, &Dashboard::showConfigDialog);

	// 初始刷新
	updateConnectionState(false);
	updateStyles("disabled", false);
}

Dashboard::~Dashboard() {}

QPixmap Dashboard::drawIcon(IconType type, const QColor &color, int size)
{
	QString resourcePath;
	switch (type) {
	case Icon_Settings:
		resourcePath = ":/assets/icon_settings_1.svg";
		break;
	case Icon_Link:
		resourcePath = ":/assets/icon_link_1.svg";
		break;
	case Icon_Plane:
		resourcePath = ":/assets/icon_plane_1.svg";
		break;
	case Icon_Play:
		resourcePath = ":/assets/icon_play_1.svg";
		break;
	case Icon_Stop:
		resourcePath = ":/assets/icon_stop_1.svg";
		break;
	case Icon_Circle:
		resourcePath = ":/assets/icon_monitor_1.svg";
		break;
	default:
		return QPixmap();
	}

	QPixmap src(resourcePath);
	if (src.isNull())
		return QPixmap(size, size);

	QPixmap dest(size * 2, size * 2);
	dest.fill(Qt::transparent);

	QPainter p(&dest);
	p.setRenderHint(QPainter::Antialiasing);
	p.setRenderHint(QPainter::SmoothPixmapTransform);

	if (type == Icon_Plane) {
		p.translate(size, size);
		p.translate(-size, -size);
	}

	QRect targetRect(0, 0, size * 2, size * 2);
	p.drawPixmap(targetRect, src);
	p.setCompositionMode(QPainter::CompositionMode_SourceIn);
	p.fillRect(targetRect, color);
	p.end();

	return dest;
}

void Dashboard::updateConnectionState(bool isConnected)
{
	int size = 22;
	if (isConnected) {
		ui->iconLink->setPixmap(drawIcon(Icon_Link, COL_GREEN, size));

		// 【修改点】不再设置 lblConnText，而是设置图标的悬停提示
		ui->iconLink->setToolTip(QString::fromUtf8("直播中控台已连接"));
	} else {
		ui->iconLink->setPixmap(drawIcon(Icon_Link, COL_GRAY, size));

		// 【修改点】同上，设置为未连接的提示
		ui->iconLink->setToolTip(QString::fromUtf8("直播中控台未连接"));
	}
}

void Dashboard::onStatusUpdated(const QString &type, const QString &msg, qint64 tNext, qint64 nNext, bool isConnected,
				int noiseCount, const QString &voiceName)
{
	updateConnectionState(isConnected);

	bool enabled = AudioController::instance().getConfig().scriptEnabled;

	// 更新开关
	ui->masterSwitch->blockSignals(true);
	ui->masterSwitch->setChecked(enabled);
	ui->masterSwitch->setIcon(QIcon(enabled ? ":/assets/switch_on_1.svg" : ":/assets/switch_off_1.svg"));
	ui->masterSwitch->blockSignals(false);

	// 1. 更新倒计时数字
	if (enabled) {
		ui->tVal->setText(QString("%1s").arg(tNext > 0 ? tNext : 0));
		ui->nVal->setText(QString("%1s").arg(nNext > 0 ? nNext : 0));

		ui->btnTriggerTime->setIcon(QIcon(drawIcon(Icon_Plane, COL_CYAN, 16)));
		ui->btnTriggerNoise->setIcon(QIcon(drawIcon(Icon_Plane, COL_AMBER, 16)));

		ui->tVal->setStyleSheet(
			QString("font-size: 24px; font-weight: bold; font-family: 'Consolas', sans-serif; padding-top: 12px; color: %1; border: none;")
				.arg(COL_CYAN.name()));
		ui->nVal->setStyleSheet(
			QString("font-size: 24px; font-weight: bold; font-family: 'Consolas', sans-serif; padding-top: 12px; color: %1; border: none;")
				.arg(COL_AMBER.name()));
	} else {
		ui->tVal->setText("--");
		ui->nVal->setText("--");

		ui->btnTriggerTime->setIcon(QIcon(drawIcon(Icon_Plane, COL_GRAY, 16)));
		ui->btnTriggerNoise->setIcon(QIcon(drawIcon(Icon_Plane, COL_GRAY, 16)));

		ui->tVal->setStyleSheet(
			"font-size: 24px; font-weight: bold; font-family: 'Consolas', sans-serif; padding-top: 12px; color: #71717a; border: none;");
		ui->nVal->setStyleSheet(
			"font-size: 24px; font-weight: bold; font-family: 'Consolas', sans-serif; padding-top: 12px; color: #71717a; border: none;");
	}

	QString subLabelStyle =
		"font-size: 11px; color: #a1a1aa; border: none; qproperty-alignment: AlignBottom | AlignHCenter; margin-bottom: 12px;";
	ui->lblTitleTime->setStyleSheet(subLabelStyle);
	ui->lblTitleNoise->setStyleSheet(subLabelStyle);

	// Info样式
	QString infoStyle = "font-size: 10px; color: #a1a1aa; border: none;";
	ui->lblInfo1->setText(QString::fromUtf8("插播音色: %1").arg(voiceName));
	ui->lblInfo1->setStyleSheet(infoStyle);

	ui->lblInfo2->setText(QString::fromUtf8("混淆素材库: %1个文件").arg(noiseCount));
	ui->lblInfo2->setStyleSheet(infoStyle);

	// 2. 更新状态卡片内容
	QString displayTitle;
	QString displaySub;
	QString queueSuffix = " (0)";
	if (msg.contains(" (+")) {
		int idx = msg.lastIndexOf(" (+");
		if (idx != -1)
			queueSuffix = msg.mid(idx).trimmed();
		if (!queueSuffix.startsWith(" "))
			queueSuffix = " " + queueSuffix;
	}

	if (!enabled) {
		displayTitle = QString::fromUtf8("已关闭");
		displaySub = QString::fromUtf8("智播已停止工作");
	} else {
		if (type == "playing_time") {
			displayTitle = QString::fromUtf8("正在报时");
			displaySub = QString::fromUtf8("当前播放队列") + queueSuffix;
		} else if (type == "playing_noise") {
			displayTitle = QString::fromUtf8("正在播放混淆");
			displaySub = QString::fromUtf8("当前播放队列") + queueSuffix;
		} else if (type == "playing_reply") {
			displayTitle = QString::fromUtf8("正在智能回复");
			displaySub = QString::fromUtf8("当前播放队列") + queueSuffix;
		} else {
			displayTitle = QString::fromUtf8("监控中");
			displaySub = QString::fromUtf8("正在监控直播间...");
		}
	}

	ui->stText->setText(displayTitle);
	ui->stSub->setText(displaySub);

	updateStyles(enabled ? type : "disabled", enabled);
}

// 🎯 补回 updateStyles 函数实现 (解决 LNK2001 错误)
void Dashboard::updateStyles(const QString &type, bool enabled)
{
	auto drawStatusIcon = [this](IconType iconType, const QColor &color) -> QPixmap {
		return drawIcon(iconType, color, 17);
	};

	QString cardStyleBase =
		"QFrame#statusCard { background: #232326; border-radius: 12px; border: %1; margin-top: 12px; }";
	QString timeCardStyle =
		"QFrame#cardTime { background: #232326; border-radius: 10px; border: %1; margin-top: 12px; }";
	QString noiseCardStyle =
		"QFrame#cardNoise { background: #232326; border-radius: 10px; border: %1; margin-top: 12px; }";

	QString borderStyle;
	QColor titleColor;
	IconType iconToDraw;
	QColor iconColor;

	QString timeBorder = "1px solid #3f3f46";
	QString noiseBorder = "1px solid #3f3f46";

	if (type == "disabled") {
		borderStyle = "1px solid #3f3f46";
		titleColor = COL_TEXT_WHITE;
		iconToDraw = Icon_Stop;
		iconColor = COL_GRAY;
	} else if (type == "idle") {
		borderStyle = "1px solid #27272a";
		titleColor = COL_TEXT_WHITE;
		iconToDraw = Icon_Circle;
		iconColor = COL_GREEN;
		timeBorder = "1px solid #164e63";
		noiseBorder = "1px solid #451a03";
	} else if (type == "playing_time") {
		borderStyle = QString("2px solid %1").arg(COL_CYAN.name());
		titleColor = COL_CYAN;
		iconToDraw = Icon_Play;
		iconColor = COL_CYAN;
		timeBorder = QString("1px solid %1").arg(COL_CYAN.name());
		noiseBorder = "1px solid #451a03";
	} else if (type == "playing_noise") {
		borderStyle = QString("2px solid %1").arg(COL_AMBER.name());
		titleColor = COL_AMBER;
		iconToDraw = Icon_Play;
		iconColor = COL_AMBER;
		timeBorder = "1px solid #164e63";
		noiseBorder = QString("1px solid %1").arg(COL_AMBER.name());
	} else if (type == "playing_reply") {
		borderStyle = QString("2px solid %1").arg(COL_PINK.name());
		titleColor = COL_PINK;
		iconToDraw = Icon_Play;
		iconColor = COL_PINK;
	} else {
		borderStyle = "1px solid #3f3f46";
		titleColor = COL_TEXT_WHITE;
		iconToDraw = Icon_Circle;
		iconColor = COL_GRAY;
	}

	ui->statusCard->setStyleSheet(cardStyleBase.arg(borderStyle));

	ui->stText->setStyleSheet(
		QString("font-size: 18px; font-weight: 900; color: %1; border: none;").arg(titleColor.name()));
	ui->stSub->setStyleSheet("font-size: 11px; color: #a1a1aa; border: none;");

	ui->iconStatus->setPixmap(drawStatusIcon(iconToDraw, iconColor));

	if (enabled) {
		ui->cardTime->setStyleSheet(timeCardStyle.arg(timeBorder));
		ui->cardNoise->setStyleSheet(noiseCardStyle.arg(noiseBorder));
	} else {
		ui->cardTime->setStyleSheet(timeCardStyle.arg("1px solid #3f3f46"));
		ui->cardNoise->setStyleSheet(noiseCardStyle.arg("1px solid #3f3f46"));
	}
}

void Dashboard::onLogMessage(const QString &msg)
{
	ui->logBox->append(QString("[%1] %2").arg(QDateTime::currentDateTime().toString("HH:mm:ss"), msg));
	// 强制滚动条到底部
	QScrollBar *sb = ui->logBox->verticalScrollBar();
	sb->setValue(sb->maximum());
}

void Dashboard::showConfigDialog()
{
	QMainWindow *mainWin = static_cast<QMainWindow *>(obs_frontend_get_main_window());
	ConfigDialog dlg(mainWin);
	dlg.exec();
}

void Dashboard::updateBreathingEffect() {}