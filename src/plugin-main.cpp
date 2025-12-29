#include <obs-module.h>
#include <obs-frontend-api.h>
#include <QMainWindow>
#include <QAction>
#include "AudioController.h"
#include "HttpServer.h"
#include "Dashboard.h"

// 必须导出的模块信息
OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("xhs-guard", "en-US")

// 全局静态变量，用于在菜单回调中访问仪表盘
static Dashboard *g_dashboard = nullptr;
static HttpServer *g_httpServer = nullptr;

/**
 * 🎯 新增：菜单点击后的回调函数
 * 作用：切换仪表盘的显示和隐藏状态
 */
static void on_menu_toggle_dashboard(void *data)
{
	// data 就是我们在下面传进来的 g_dashboard 指针
	Dashboard *dock = static_cast<Dashboard *>(data);
	if (dock) {
		if (dock->isHidden()) {
			dock->show();
			dock->raise(); // 将窗口提到最前面
		} else {
			dock->hide();
		}
	}
}

/**
 * 插件加载时的入口函数
 */
bool obs_module_load(void)
{
	// 1. 初始化音频控制大脑
	AudioController::instance().init();

	// 2. 启动 HTTP 服务器 (监听 18888 端口)
	g_httpServer = new HttpServer();
	if (!g_httpServer->start(18888)) {
		blog(LOG_ERROR, "[智播精灵] HTTP服务器启动失败，端口18888可能被占用");
	} else {
		blog(LOG_INFO, "[智播精灵] 原生中控已就绪，监听端口: 18888");
	}

	// 3. 获取 OBS 主窗口指针
	QMainWindow *mainWin = static_cast<QMainWindow *>(obs_frontend_get_main_window());

	if (mainWin) {
		// 4. 创建并挂载仪表盘 (Dock)
		g_dashboard = new Dashboard(mainWin);

		// 将仪表盘添加到 OBS 的右侧停靠区
		mainWin->addDockWidget(Qt::RightDockWidgetArea, g_dashboard);

		/**
         * 🎯 核心修改：在 OBS 的“工具 (Tools)”菜单中添加条目
         * 参数 1: 菜单显示的文字
         * 参数 2: 点击后执行的函数 (上面定义的 on_menu_toggle_dashboard)
         * 参数 3: 传递给函数的数据 (这里把仪表盘指针传过去)
         */
		obs_frontend_add_tools_menu_item("智播精灵", on_menu_toggle_dashboard, g_dashboard);

		blog(LOG_INFO, "[智播精灵] 仪表盘已挂载，并已添加到工具菜单");
	}

	return true;
}

/**
 * 插件卸载时的清理函数
 */
void obs_module_unload(void)
{
	if (g_httpServer) {
		g_httpServer->close();
		delete g_httpServer;
		g_httpServer = nullptr;
	}

	// 注意：g_dashboard 是 mainWin 的子元素，OBS 会自动清理它，不需要手动 delete
}