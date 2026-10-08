#pragma once

class QWidget;
class QString;

void Windows_QWidget_SetForegroundWindow(QWidget* w);

bool Windows_IsInAdmin();

// 摘掉文件的「Internet 下载标记」（NTFS 备用数据流 :Zone.Identifier），
// 也就是资源管理器里「右键 → 属性 → 解除锁定」做的事；返回是否真的删掉了标记。
//
// 为什么必须做：Windows 会拦截「带此标记的可执行文件被其它进程拉起」（Windows 11
// 尤其严格，而且不弹任何提示），拦截时 CreateProcess 返回 ERROR_FILE_NOT_FOUND ——
// Qt 报出来就是「进程启动失败：系统找不到指定的文件」，用户完全看不出是被拦了，
// 只会以为文件丢了。核心 / sing-box CLI / updater 全都是从别的进程拉起的，最容易中招。
bool Windows_RemoveMarkOfTheWeb(const QString &path);
