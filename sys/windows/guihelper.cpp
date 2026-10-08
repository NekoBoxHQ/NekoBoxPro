
#include "guihelper.h"

#include <QDir>
#include <QWidget>

#include <windows.h>
#include <shlobj.h>

void Windows_QWidget_SetForegroundWindow(QWidget *w) {
    HWND hForgroundWnd = GetForegroundWindow();
    DWORD dwForeID = ::GetWindowThreadProcessId(hForgroundWnd, NULL);
    DWORD dwCurID = ::GetCurrentThreadId();
    ::AttachThreadInput(dwCurID, dwForeID, TRUE);
    ::SetForegroundWindow((HWND) w->winId());
    ::AttachThreadInput(dwCurID, dwForeID, FALSE);
}

int isThisAdmin = -1; // cached

bool Windows_IsInAdmin() {
    if (isThisAdmin >= 0) return isThisAdmin;
    isThisAdmin = IsUserAnAdmin();
    return isThisAdmin;
}

bool Windows_RemoveMarkOfTheWeb(const QString &path) {
    if (path.isEmpty()) return false;
    // 必须把「原路径」和「补 .exe 后的路径」都试一遍：
    // CreateProcess 会自动给没有扩展名的可执行文件补上 .exe（核心就是 D:\...\nekobox_core
    // 这种写法、updater 也是 "./updater"），但 DeleteFile 不会 —— 只试原路径的话，
    // 恰恰是最容易中招的那两个（核心 / updater）摘不掉标记。
    // 没有这个流（文件不是从网上下来的）或文件不存在时返回 0，属正常情况，静默忽略。
    for (const auto &candidate: {path, path + QStringLiteral(".exe")}) {
        const auto stream = QDir::toNativeSeparators(candidate) + QStringLiteral(":Zone.Identifier");
        const auto native = stream.toStdWString();
        if (DeleteFileW(native.c_str()) != 0) return true;
    }
    return false;
}
