# deployment 发布物说明

本目录存放随发布包分发的运行时数据文件。**geo 数据文件已从仓库移除，改为 CI 构建时从官方发布物下载固定版本并强制 SHA256 校验**（见 `.github/workflows/build.yml` 的 `Download geo data` 步骤）。

## geo 数据文件

| 文件 | 格式 | 固定来源版本（CI 下载） | 校验 |
|---|---|---|---|
| geoip.db | sing-box 规则数据库 | [SagerNet/sing-geoip](https://github.com/SagerNet/sing-geoip) `20260912` | 官方 sha256sum 强制校验 |
| geosite.db | sing-box 规则数据库 | [SagerNet/sing-geosite](https://github.com/SagerNet/sing-geosite) `20260925013933` | 官方 sha256sum 强制校验 |
| geoip.dat | 旧版 v2ray 格式（兼容导入） | [Loyalsoldier/v2ray-rules-dat](https://github.com/Loyalsoldier/v2ray-rules-dat) `202609270006` | 官方 sha256sum 强制校验 |
| geosite.dat | 旧版 v2ray 格式（兼容导入） | [v2fly/domain-list-community](https://github.com/v2fly/domain-list-community) `20260925234224`（dlc.dat） | 官方 sha256sum 强制校验 |

## 许可证

- 生成工具（代码）：[SagerNet/sing-geoip](https://github.com/SagerNet/sing-geoip)、[SagerNet/sing-geosite](https://github.com/SagerNet/sing-geosite)，GPLv3（Copyright (C) 2022 by nekohasekai）
- 数据源（IP 归属数据）：[v2fly/geoip](https://github.com/v2fly/geoip) 与 [Loyalsoldier/v2ray-rules-dat](https://github.com/Loyalsoldier/v2ray-rules-dat)，CC-BY-SA-4.0（已核实）
- 数据源（域名列表）：[v2fly/domain-list-community](https://github.com/v2fly/domain-list-community)（仓库 LICENSE 为 MIT，具体数据许可以其官方发布物标注为准）

## 版本更新方式

需要更新数据时：修改 `.github/workflows/build.yml` 中 `GEOIP_DB_VER` / `GEOSITE_DB_VER` / `GEOIP_DAT_VER` / `GEOSITE_DAT_VER` 四个环境变量，随下次发版自动下载新版本。
