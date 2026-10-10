# 协议导入支持表

> 基于内核版本：sing-box **v1.14.3**（升级内核后同步更新本表）
>
> 维护约定：本项目仅使用 sing-box 单一内核；协议导入链路为 `链接 → Bean(TryParseLink) → BuildCoreObjSingBox → sing-box JSON`。
> 同一协议存在多种链接格式时（如 vless reality / vless ws / vless grpc），统一在对应 Bean 的 `TryParseLink` 内部分支处理，不拆多个 parser。
> 新协议接入流程：① 查本表 → ② 按既有 Bean 模板补解析与 outbound → ③ 更新本表 → ④ bump 版本发版（CI 自动构建发布）。

---

## SOCKS (4/4a/5)

- 链接格式：
  ```
  socks5://user:pass@host:port#name
  socks4://host:port#name
  socks://host:port#name          （无版本号默认 5）
  ```
- 支持字段：
  - username ✅　password ✅（密码含 `#` 且位于 `@` 之前可正常导入，自动转义 `%23`）
- 对应 sing-box：`{ "type": "socks", "server", "server_port", "version", "username", "password" }`

## HTTP(S)

- 链接格式：
  ```
  http://user:pass@host:port#name
  https://host:port#name           （security=tls）
  http://:@host:port#name          （空认证，已兼容）
  ```
- 支持字段：
  - username ✅　password ✅　TLS(https) ✅
- 对应 sing-box：`{ "type": "http", "server", "server_port", "username", "password", "tls" }`

## Shadowsocks

- 链接格式：
  ```
  ss://base64(method:password)@host:port#name
  ss://2022-blake3-aes-128-gcm:password@host:port?plugin=...#name
  ss://base64(json)#name          （新格式：server/port/method/password/plugin）
  ```
- 支持字段：
  - method ✅　password ✅　plugin(obfs / v2ray-plugin) ✅　2022-blake3 ✅
- 对应 sing-box：`{ "type": "shadowsocks", "server", "server_port", "method", "password", "plugin" }`

## VMess

- 链接格式：
  ```
  vmess://base64(json)             （v2rayN 格式）
  vmess://uuid@host:port?encryption=...&security=tls&type=ws&path=..&host=..#name （ducksoft 格式）
  ```
- 支持字段：
  - uuid ✅　security(aes-128-gcm / 2022-blake3) ✅　tls ✅　reality ✅　sni ✅
  - ws/grpc/tcp/http 传输 ✅　allowInsecure ✅　utlsFingerprint ✅
- 未实现：flow（VMess 无此概念）
- 对应 sing-box：`{ "type": "vmess", "server", "server_port", "uuid", "security", "tls", "transport" }`

## VLESS

- 链接格式：
  ```
  vless://uuid@host:port?type=ws&security=tls&flow=xtls-rprx-vision&sni=..&pbk=..&sid=..&spx=..#name
  ```
- 支持字段：
  - uuid ✅　flow ✅　tls ✅　reality(pbk/sid/spx) ✅　sni ✅　allowInsecure ✅
  - ws/grpc/tcp/http 传输 ✅　utlsFingerprint ✅
- 对应 sing-box：`{ "type": "vless", "server", "server_port", "uuid", "flow", "tls", "transport" }`

## Trojan

- 链接格式：
  ```
  trojan://password@host:port?type=ws&security=tls&sni=..#name
  ```
- 支持字段：
  - password ✅　tls ✅　sni ✅　ws/grpc/tcp/http 传输 ✅　allowInsecure ✅
- 对应 sing-box：`{ "type": "trojan", "server", "server_port", "password", "tls", "transport" }`

## NaïveProxy

- 链接格式：
  ```
  naive+http://user:pass@host:port#name
  naive+tls://user:pass@host:port#name
  ```
- 支持字段：
  - username ✅　password ✅　http/tls ✅
- 对应 sing-box：`{ "type": "naive", "server", "server_port", "username", "password" }`

## TUIC

- 链接格式：
  ```
  tuic://uuid:password@host:port?congestion_control=bbr&alpn=h3&udp_relay_mode=native&sni=..&allow_insecure=1&disable_sni=1#name
  ```
- 支持字段：
  - uuid ✅　password ✅　congestion_control ✅　alpn ✅　udp_relay_mode ✅
  - sni ✅　allow_insecure ✅　disable_sni ✅
- 对应 sing-box：`{ "type": "tuic", "server", "server_port", "uuid", "password", "congestion_control", "alpn", "udp_relay_mode", "tls" }`

## Hysteria (hy)

- 链接格式：
  ```
  hy://host:port?auth=pass&up=10&down=50&obfs=xplus&peer=sni&insecure=1#name
  hysteria://host:port?auth=...&up=...&down=...&obfs=...&peer=...#name
  ```
- 支持字段：
  - auth(password) ✅　up ✅　down ✅　obfs ✅　peer/sni ✅　insecure ✅
- 对应 sing-box：`{ "type": "hysteria", "server", "server_port", "auth_str", "obfs", "up_mbps", "down_mbps", "tls" }`

## Hysteria2 (hy2)

- 链接格式：
  ```
  hy2://password@host:port?obfs=salamander&obfs-password=..&mport=..&insecure=1&sni=..#name
  hysteria2://...（同上）
  ```
- 支持字段：
  - password ✅　obfs(salamander) ✅　obfs-password ✅　mport(端口跳跃) ✅　sni ✅　insecure ✅
- 对应 sing-box：`{ "type": "hysteria2", "server", "server_port", "password", "obfs", "obfs-password", "hop-port", "tls" }`

## WireGuard

- 链接格式：
  ```
  wireguard://base64(json)          （v2rayN/sing-box 标准：server/port/private_key/public_key/preshared_key/mtu/reserved）
  wireguard://host:port?private_key=..&public_key=..&preshared_key=..&mtu=1420&reserved=1,2,3#name
  wg://host:port?...（同上，简单格式）
  ```
- 支持字段：
  - private_key ✅　peer_public_key ✅　preshared_key ✅　mtu ✅（默认 1420）　reserved ✅（逗号分隔，转 sing-box 数组）
- 对应 sing-box：`{ "type": "wireguard", "server", "server_port", "local_private_key", "peer_public_key", "preshared_key", "mtu", "reserved" }`

## SSH

- 链接格式：
  ```
  ssh://user:pass@host:port#name
  ```
- 支持字段：
  - username ✅　password ✅
- 未实现：private_key（sing-box 支持私钥认证，当前 Bean 未加字段）
- 对应 sing-box：`{ "type": "ssh", "server", "server_port", "user", "password" }`

## AnyTLS

- 链接格式：
  ```
  anytls://password@host:port?sni=..&allowInsecure=1#name
  ```
- 支持字段：
  - password ✅　sni ✅　allowInsecure ✅
- 未实现：uuid、alpn（sing-box anytls 支持，当前 Bean 未加字段）
- 对应 sing-box：`{ "type": "anytls", "server", "server_port", "password", "tls" }`

## Brook

- 链接格式：
  ```
  brook://password@host:port?protocol=ws#name
  ```
- 支持字段：
  - password ✅　protocol ✅（ws 默认；ws/wss/quic 透传）
- 说明：sing-box 内置 outbound，兼容支持（使用较少，勿与 Brook 官方客户端混淆）
- 对应 sing-box：`{ "type": "brook", "server", "server_port", "password", "protocol" }`

---

## 订阅分发前缀

订阅更新（GroupUpdater）按以下前缀分发到对应 Bean：

| 前缀 | Bean |
| --- | --- |
| `socks5://` `socks4://` `socks://` | SocksHttpBean |
| `http://` `https://` | SocksHttpBean |
| `ss://` | ShadowSocksBean |
| `vmess://` | VMessBean |
| `vless://` `trojan://` | TrojanVLESSBean |
| `naive+` | NaiveBean |
| `tuic://` | QUICBean(TUIC) |
| `hy://` `hysteria://` | QUICBean(Hysteria) |
| `hy2://` `hysteria2://` | QUICBean(Hysteria2) |
| `wireguard://` `wg://` | WireGuardBean |
| `ssh://` | SSHBean |
| `anytls://` | AnyTLSBean |
| `brook://` | BrookBean |
