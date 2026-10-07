#pragma once

#include "main/NekoGui.hpp"

namespace NekoGui_traffic {
    class TrafficData : public JsonStore {
    public:
        int id = -1; // ent id
        std::string tag;

        long long downlink = 0;
        long long uplink = 0;
        long long downlink_rate = 0;
        long long uplink_rate = 0;

        // 上次从 sing-box core 读到的**累计**计数器值。QueryStats 返回的是累计值
        // （不是增量），必须记下上一次的值、算差，否则 downlink += 累计值 会把流量
        // 放大成平方级（跑越久虚高越离谱）。持久化，重启后靠"当前 < 上次"判定
        // 计数器已被 core 重置过、diff 归零。
        long long last_downlink = 0;
        long long last_uplink = 0;

        long long last_update;

        explicit TrafficData(std::string tag) {
            this->tag = std::move(tag);
            _add(new configItem("dl", &downlink, itemType::integer64));
            _add(new configItem("ul", &uplink, itemType::integer64));
            _add(new configItem("last_dl", &last_downlink, itemType::integer64));
            _add(new configItem("last_ul", &last_uplink, itemType::integer64));
        };

        void Reset() {
            downlink = 0;
            uplink = 0;
            downlink_rate = 0;
            uplink_rate = 0;
            // 注意：last_downlink / last_uplink 故意**不**清零 —— 它们跟踪的是 core
            // 里那个只会涨的计数器，跟"面板要显示从哪个点开始算"无关。清零反而会
            // 让下一次 diff 变成整个累计值，又虚高一次。
        }

        [[nodiscard]] QString DisplaySpeed() const {
            return UNICODE_LRO + QStringLiteral("%1↑ %2↓").arg(ReadableSize(uplink_rate), ReadableSize(downlink_rate));
        }

        [[nodiscard]] QString DisplayTraffic() const {
            if (downlink + uplink == 0) return "";
            return UNICODE_LRO + QStringLiteral("%1↑ %2↓").arg(ReadableSize(uplink), ReadableSize(downlink));
        }
    };
} // namespace NekoGui_traffic
