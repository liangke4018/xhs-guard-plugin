#pragma once
#include <QDialog>
#include <memory>
#include "Common.h"

namespace Ui {
class ConfigDialog;
}

class ConfigDialog : public QDialog {
	Q_OBJECT

public:
	explicit ConfigDialog(QWidget *parent = nullptr);
	virtual ~ConfigDialog();

private slots:
	// 业务处理槽函数
	void handleBrowseClicked();
	void handleSliderChanged(int value);
	void handleAccepted();

private:
	std::unique_ptr<Ui::ConfigDialog> ui;

	void refreshObsSources(); // 刷新并过滤 OBS 来源
	void loadConfig();        // 将内存配置加载到 UI
	void saveConfig();        // 将 UI 配置保存到内存和文件
	QString getConfigPath();  // 获取 JSON 路径

	// 🎯 新增：添加标签辅助函数
	void addDuckTag(const QString &sourceName);
};