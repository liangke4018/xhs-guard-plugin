#include "ConfigDialog.h"
#include "ui_ConfigDialog.h"
#include "AudioController.h"

#include <QFileDialog>
#include <QListWidgetItem>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>
#include <QFile>
#include <QDir>
#include <QDebug>
#include <QAbstractItemView>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QToolButton>
// 🎯 新增：绘图相关头文件
#include <QPainter>
#include <QIcon>
#include <QPixmap>

extern "C" {
#include <obs.h>
#include <obs-module.h>
}

#ifndef OBS_SOURCE_AUDIO
#define OBS_SOURCE_AUDIO (1 << 3)
#endif

ConfigDialog::ConfigDialog(QWidget *parent) : QDialog(parent), ui(new Ui::ConfigDialog)
{
	ui->setupUi(this);

	// 窗口属性
	setWindowFlags(Qt::Dialog | Qt::WindowTitleHint | Qt::WindowCloseButtonHint);
	setWindowModality(Qt::WindowModal);

	// 1. 下拉框样式 (高度 28px)
	QString comboStyle = R"(
		QComboBox {
			padding: 4px;
			background-color: #2a2a2e;
			color: #ffffff;
			border: 1px solid #3a3a3e;
			border-radius: 4px;
			min-height: 28px;
		}
		QComboBox:hover { border-color: #DF6A46; }
		QComboBox::drop-down { border: 0px; width: 20px; }
		QComboBox QAbstractItemView {
			background-color: #2a2a2e;
			color: #ffffff;
			selection-background-color: #DF6A46;
			selection-color: #ffffff;
			outline: 0;
			border: 1px solid #3a3a3e;
		}
	)";
	ui->comboMediaSource->setStyleSheet(comboStyle);
	ui->comboMediaSource->setView(new QListView());

	ui->comboAddDuckSource->setStyleSheet(comboStyle);
	ui->comboAddDuckSource->setView(new QListView());

	// 2. Tooltip 样式
	QString tooltipStyle = R"(
		QToolTip {
			color: #ffffff;
			background-color: #1a1a1d;
			border: 1px solid #DF6A46;
			border-radius: 4px;
			padding: 8px;
			font-family: "Microsoft YaHei";
			font-size: 12px;
			max-width: 300px;
		}
	)";
	this->setStyleSheet(this->styleSheet() + tooltipStyle);

	refreshObsSources();
	loadConfig();

	// 信号绑定
	connect(ui->btnBrowse, &QPushButton::clicked, this, &ConfigDialog::handleBrowseClicked);
	connect(ui->sliderDuckVol, &QSlider::valueChanged, this, &ConfigDialog::handleSliderChanged);

	// 标签添加逻辑
	connect(ui->comboAddDuckSource, &QComboBox::textActivated, this, [this](const QString &text) {
		if (text.isEmpty() || text.contains("--"))
			return;
		addDuckTag(text);

		int idx = ui->comboAddDuckSource->findText(text);
		if (idx != -1)
			ui->comboAddDuckSource->removeItem(idx);
		ui->comboAddDuckSource->setCurrentIndex(0);
	});

	connect(ui->buttonBox, &QDialogButtonBox::accepted, this, &ConfigDialog::handleAccepted);
	connect(ui->buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

ConfigDialog::~ConfigDialog() {}

// 🎯 核心修复：使用 QToolButton + 视觉微调实现完美居中
void ConfigDialog::addDuckTag(const QString &sourceName)
{
	QListWidgetItem *item = new QListWidgetItem(ui->listDuckTags);

	QWidget *tagWidget = new QWidget();
	tagWidget->setObjectName("tagContainer");

	tagWidget->setStyleSheet("#tagContainer { "
				 "  background-color: #444; "
				 "  border-radius: 4px; "
				 "  border: 1px solid #555; "
				 "}"
				 "QLabel { "
				 "  color: #eeeeee; "
				 "  padding-left: 4px; "
				 "  background: transparent; "
				 "  border: none;"
				 "}"
				 "QToolButton#btnDelete { " /* 改为 QToolButton 选择器 */
				 "  border: none; "
				 "  background: transparent; "
				 "  padding: 0px; "
				 "  margin: 0px; "
				 "  margin-top: 1px; " /* 🎯 关键：下移 1px，视觉上与文字基线对齐 */
				 "}");

	QHBoxLayout *layout = new QHBoxLayout(tagWidget);
	layout->setContentsMargins(8, 0, 4, 0); // 上下无边距，完全靠对齐
	layout->setSpacing(8);
	layout->setAlignment(Qt::AlignVCenter);

	QLabel *lbl = new QLabel(sourceName);
	lbl->setAlignment(Qt::AlignVCenter | Qt::AlignLeft);

	// 使用 QToolButton，它对图标的居中支持更好
	QToolButton *btnClose = new QToolButton();
	btnClose->setObjectName("btnDelete");
	btnClose->setFixedSize(20, 20); // 稍微加大点击区域
	btnClose->setCursor(Qt::PointingHandCursor);
	btnClose->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);

	// 🎨 绘制图标 (保持不变)
	auto drawIcon = [](const QColor &color) -> QPixmap {
		QPixmap pix(32, 32);
		pix.fill(Qt::transparent);
		QPainter p(&pix);
		p.setRenderHint(QPainter::Antialiasing);
		p.setPen(QPen(color, 4)); // 线宽
		int pad = 8;
		p.drawLine(pad, pad, 32 - pad, 32 - pad);
		p.drawLine(32 - pad, pad, pad, 32 - pad);
		return pix;
	};

	QIcon icon;
	icon.addPixmap(drawIcon(QColor("#aaaaaa")), QIcon::Normal);
	icon.addPixmap(drawIcon(QColor("#FF5555")), QIcon::Active); // 悬停/按下变红
	// QToolButton 悬停时会自动尝试使用 Active 状态，或者我们需要手动触发
	// 为了保险，设置 Enter/Leave 模式或样式表 hover 也可以，但 QIcon 状态更原生

	btnClose->setIcon(icon);
	btnClose->setIconSize(QSize(10, 10)); // 图标小一点，更精致

	// 强制 QToolButton 在悬停时刷新图标状态
	btnClose->setStyleSheet("QToolButton:hover { border: none; }");

	layout->addWidget(lbl);
	layout->addStretch();
	layout->addWidget(btnClose);

	int textWidth = lbl->fontMetrics().horizontalAdvance(sourceName);
	item->setSizeHint(QSize(textWidth + 65, 32));

	ui->listDuckTags->setItemWidget(item, tagWidget);

	connect(btnClose, &QToolButton::clicked, this, [this, item, sourceName]() {
		ui->comboAddDuckSource->addItem(sourceName);
		delete ui->listDuckTags->takeItem(ui->listDuckTags->row(item));
	});
}

void ConfigDialog::refreshObsSources()
{
	ui->comboMediaSource->clear();
	ui->comboAddDuckSource->clear();

	ui->comboMediaSource->addItem(QString::fromUtf8("-- 请选择媒体源 --"));
	ui->comboAddDuckSource->addItem(QString::fromUtf8("-- 点击添加音频源 --"));

	auto enumProc = [](void *data, obs_source_t *source) {
		ConfigDialog *self = static_cast<ConfigDialog *>(data);
		const char *name = obs_source_get_name(source);
		const char *id = obs_source_get_id(source);
		uint32_t flags = obs_source_get_output_flags(source);

		if (strcmp(id, "ffmpeg_source") == 0 || strcmp(id, "vlc_source") == 0) {
			self->ui->comboMediaSource->addItem(QString::fromUtf8(name));
		}
		if (flags & OBS_SOURCE_AUDIO) {
			self->ui->comboAddDuckSource->addItem(QString::fromUtf8(name));
		}
		return true;
	};
	obs_enum_sources(enumProc, this);
}

void ConfigDialog::loadConfig()
{
	PluginConfig cfg = AudioController::instance().getConfig();

	int mediaIdx = ui->comboMediaSource->findText(cfg.mediaSourceName);
	if (mediaIdx != -1)
		ui->comboMediaSource->setCurrentIndex(mediaIdx);

	ui->editVoicePath->setText(cfg.voicePackPath);
	ui->spinTimeMin->setValue(cfg.timeMin);
	ui->spinTimeMax->setValue(cfg.timeMax);
	ui->spinNoiseMin->setValue(cfg.noiseMin);
	ui->spinNoiseMax->setValue(cfg.noiseMax);

	ui->spinHistorySize->setValue(cfg.historySize);
	ui->spinShortThreshold->setValue(cfg.shortFileThreshold);

	ui->listDuckTags->clear();
	for (const QString &name : cfg.duckSources) {
		int idx = ui->comboAddDuckSource->findText(name);
		if (idx != -1) {
			addDuckTag(name);
			ui->comboAddDuckSource->removeItem(idx);
		}
	}

	ui->sliderDuckVol->setValue(static_cast<int>(cfg.duckVolume * 100));
	ui->lblVolVal->setText(QString::number(ui->sliderDuckVol->value()) + "%");
}

void ConfigDialog::handleBrowseClicked()
{
	QString dir = QFileDialog::getExistingDirectory(this, QString::fromUtf8("选择语音包根目录"),
							ui->editVoicePath->text());
	if (!dir.isEmpty())
		ui->editVoicePath->setText(QDir::toNativeSeparators(dir));
}

void ConfigDialog::handleSliderChanged(int value)
{
	ui->lblVolVal->setText(QString::number(value) + "%");
}

void ConfigDialog::handleAccepted()
{
	saveConfig();
	this->accept();
}

void ConfigDialog::saveConfig()
{
	PluginConfig cfg;

	cfg.mediaSourceName = ui->comboMediaSource->currentText();
	if (cfg.mediaSourceName.contains("--"))
		cfg.mediaSourceName = "";

	cfg.voicePackPath = ui->editVoicePath->text();
	cfg.timeMin = ui->spinTimeMin->value();
	cfg.timeMax = ui->spinTimeMax->value();
	cfg.noiseMin = ui->spinNoiseMin->value();
	cfg.noiseMax = ui->spinNoiseMax->value();

	cfg.historySize = ui->spinHistorySize->value();
	cfg.shortFileThreshold = ui->spinShortThreshold->value();

	cfg.duckVolume = ui->sliderDuckVol->value() / 100.0f;

	cfg.duckSources.clear();
	for (int i = 0; i < ui->listDuckTags->count(); ++i) {
		QWidget *w = ui->listDuckTags->itemWidget(ui->listDuckTags->item(i));
		if (w) {
			QLabel *lbl = w->findChild<QLabel *>();
			if (lbl)
				cfg.duckSources.append(lbl->text());
		}
	}

	AudioController::instance().setConfig(cfg);

	QJsonObject root;
	root["mediaSourceName"] = cfg.mediaSourceName;
	root["voicePackPath"] = cfg.voicePackPath;
	root["timeMin"] = cfg.timeMin;
	root["timeMax"] = cfg.timeMax;
	root["noiseMin"] = cfg.noiseMin;
	root["noiseMax"] = cfg.noiseMax;
	root["historySize"] = cfg.historySize;
	root["shortFileThreshold"] = cfg.shortFileThreshold;
	root["duckVolume"] = static_cast<double>(cfg.duckVolume);

	QJsonArray sourcesArray;
	for (const QString &s : cfg.duckSources)
		sourcesArray.append(s);
	root["duckSources"] = sourcesArray;

	QString path = getConfigPath();
	QDir().mkpath(QFileInfo(path).absolutePath());
	QFile file(path);
	if (file.open(QIODevice::WriteOnly)) {
		file.write(QJsonDocument(root).toJson());
		file.close();
	}
}

QString ConfigDialog::getConfigPath()
{
	char *path_c = obs_module_config_path("xhs-guard-config.json");
	QString path = QString::fromUtf8(path_c);
	if (path_c)
		bfree(path_c);
	return path;
}