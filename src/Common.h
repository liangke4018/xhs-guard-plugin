#pragma once
#include <QString>
#include <QStringList>
#include <QList>

extern "C" {
#include <obs-module.h>
}

struct PluginConfig {
	bool scriptEnabled = true;
	QString mediaSourceName = "";
	QString voicePackPath = "";

	// 报时设置
	int timeMin = 120;
	int timeMax = 180;

	// 混淆设置
	int noiseMin = 90;
	int noiseMax = 120;

	// 🎯 新增：智能逻辑配置
	int historySize = 30;        // 去重轮数
	int shortFileThreshold = 6; // 连播阈值(秒)

	// 闪避设置
	QStringList duckSources;
	float duckVolume = 0.0f;
};