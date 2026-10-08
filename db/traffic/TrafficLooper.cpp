#include "TrafficLooper.hpp"

#include "rpc/gRPC.h"
#include "ui/mainwindow_interface.h"

#include <QThread>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>
#include <QElapsedTimer>

namespace NekoGui_traffic {

    TrafficLooper *trafficLooper = new TrafficLooper;
    QElapsedTimer elapsedTimer;

    TrafficData *TrafficLooper::update_stats(TrafficData *item) {
#ifndef NKR_NO_GRPC
        // last update
        auto now = elapsedTimer.elapsed();
        auto interval = now - item->last_update;
        if (interval <= 0) return nullptr;

        // query
        // ⚠️ QueryStats 返回的是 core 里的**累计值**（只增不减的计数器），不是增量。
        // 原来直接 `item->downlink += downlink`，等于把累计值每轮都累加一遍，
        // 流量会按运行时间的平方虚高。改成先算增量（本次累计 - 上次累计）。
        bool okUp = false, okDown = false;
        auto uplink = NekoGui_rpc::defaultClient->QueryStats(item->tag, "uplink", &okUp);
        auto downlink = NekoGui_rpc::defaultClient->QueryStats(item->tag, "downlink", &okDown);

        // ⚠️ 查询失败（超时/出错）时 QueryStats 返回 0，那不是真实计数。此时**绝不能**
        // 把基准 last_* 写成 0 —— 否则下一次成功轮询会算出 `累计值 - 0`，把整段会话流量
        // 当成一轮增量重复计入（就是"平方级虚高"换了个触发路径）。失败时连 last_update
        // 也不动，下一轮重来。
        if (!okUp || !okDown) return nullptr;
        item->last_update = now;

        // 增量；core 重启后计数清零，此时"当前 < 上次"，判为计数器被重置过，
        // diff 取 0（不倒退、不虚增），并把基准同步到当前值。
        auto diff_down = downlink >= item->last_downlink ? downlink - item->last_downlink : 0;
        auto diff_up = uplink >= item->last_uplink ? uplink - item->last_uplink : 0;
        item->last_downlink = downlink;
        item->last_uplink = uplink;

        // add diff
        item->downlink += diff_down;
        item->uplink += diff_up;
        item->downlink_rate = diff_down * 1000 / interval;
        item->uplink_rate = diff_up * 1000 / interval;

        // return diff
        auto ret = new TrafficData(item->tag);
        ret->downlink = diff_down;
        ret->uplink = diff_up;
        ret->downlink_rate = item->downlink_rate;
        ret->uplink_rate = item->uplink_rate;
        return ret;
#endif
        return nullptr;
    }

    QJsonArray TrafficLooper::get_connection_list() {
#ifndef NKR_NO_GRPC
        auto str = NekoGui_rpc::defaultClient->ListConnections();
        QJsonDocument jsonDocument = QJsonDocument::fromJson(str.c_str());
        return jsonDocument.array();
#else
        return QJsonArray{};
#endif
    }

    void TrafficLooper::UpdateAll() {
        std::map<std::string, TrafficData *> updated; // tag to diff
        for (const auto &item: this->items) {
            auto data = item.get();
            auto diff = updated[data->tag];
            // 避免重复查询一个 outbound tag
            if (diff == nullptr) {
                diff = update_stats(data);
                updated[data->tag] = diff;
            } else {
                data->uplink += diff->uplink;
                data->downlink += diff->downlink;
                data->uplink_rate = diff->uplink_rate;
                data->downlink_rate = diff->downlink_rate;
            }
        }
        updated[bypass->tag] = update_stats(bypass);
        //
        for (const auto &pair: updated) {
            delete pair.second;
        }
    }

    void TrafficLooper::Loop() {
        elapsedTimer.start();
        while (true) {
            auto sleep_ms = NekoGui::dataStore->traffic_loop_interval;
            if (sleep_ms < 500 || sleep_ms > 5000) sleep_ms = 1000;
            QThread::msleep(sleep_ms);
            if (NekoGui::dataStore->traffic_loop_interval == 0) continue; // user disabled

            // profile start and stop
            if (!loop_enabled) {
                // 停止
                if (looping) {
                    looping = false;
                    runOnUiThread([=] {
                        auto m = GetMainWindow();
                        m->refresh_status("STOP");
                    });
                }
                continue;
            } else {
                // 开始
                if (!looping) {
                    looping = true;
                }
            }

            // do update
            loop_mutex.lock();

            UpdateAll();

            // do conn list update
            QJsonArray conn_list;
            if (NekoGui::dataStore->connection_statistics) {
                conn_list = get_connection_list();
            }

            loop_mutex.unlock();

            // post to UI
            runOnUiThread([=] {
                auto m = GetMainWindow();
                if (proxy != nullptr) {
                    m->refresh_status(QObject::tr("Proxy: %1\nDirect: %2").arg(proxy->DisplaySpeed(), bypass->DisplaySpeed()));
                }
                for (const auto &item: items) {
                    if (item->id < 0) continue;
                    m->refresh_proxy_list(item->id);
                }
                if (NekoGui::dataStore->connection_statistics) {
                    m->refresh_connection_list(conn_list);
                }
            });
        }
    }

} // namespace NekoGui_traffic
