#include "db/ConfigBuilder.hpp"
#include "db/Database.hpp"
#include "fmt/includes.h"
#include "fmt/Preset.hpp"

#include <QApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>

#define BOX_UNDERLYING_DNS dataStore->core_box_underlying_dns.isEmpty() ? "local" : dataStore->core_box_underlying_dns

namespace NekoGui {

    QStringList getAutoBypassExternalProcessPaths(const std::shared_ptr<BuildConfigResult> &result) {
        QStringList paths;
        for (const auto &extR: result->extRs) {
            auto path = extR->program;
            if (path.trimmed().isEmpty()) continue;
            paths << path.replace("\\", "/");
        }
        return paths;
    }

    QString genTunName() {
        auto tun_name = "neko-tun";
#ifdef Q_OS_MACOS
        tun_name = "utun9";
#endif
        return tun_name;
    }

    void MergeJson(QJsonObject &dst, const QJsonObject &src) {
        // 合并
        if (src.isEmpty()) return;
        for (const auto &key: src.keys()) {
            auto v_src = src[key];
            if (dst.contains(key)) {
                auto v_dst = dst[key];
                if (v_src.isObject() && v_dst.isObject()) { // isObject 则合并？
                    auto v_src_obj = v_src.toObject();
                    auto v_dst_obj = v_dst.toObject();
                    MergeJson(v_dst_obj, v_src_obj);
                    dst[key] = v_dst_obj;
                } else {
                    dst[key] = v_src;
                }
            } else if (v_src.isArray()) {
                if (key.startsWith("+")) {
                    auto key2 = SubStrAfter(key, "+");
                    auto v_dst = dst[key2];
                    auto v_src_arr = v_src.toArray();
                    auto v_dst_arr = v_dst.toArray();
                    QJSONARRAY_ADD(v_src_arr, v_dst_arr)
                    dst[key2] = v_src_arr;
                } else if (key.endsWith("+")) {
                    auto key2 = SubStrBefore(key, "+");
                    auto v_dst = dst[key2];
                    auto v_src_arr = v_src.toArray();
                    auto v_dst_arr = v_dst.toArray();
                    QJSONARRAY_ADD(v_dst_arr, v_src_arr)
                    dst[key2] = v_dst_arr;
                } else {
                    dst[key] = v_src;
                }
            } else {
                dst[key] = v_src;
            }
        }
    }

    // Common

    std::shared_ptr<BuildConfigResult> BuildConfig(const std::shared_ptr<ProxyEntity> &ent, bool forTest, bool forExport) {
        auto result = std::make_shared<BuildConfigResult>();
        auto status = std::make_shared<BuildConfigStatus>();
        status->ent = ent;
        status->result = result;
        status->forTest = forTest;
        status->forExport = forExport;

        auto customBean = dynamic_cast<NekoGui_fmt::CustomBean *>(ent->bean.get());
        if (customBean != nullptr && customBean->core == "internal-full") {
            result->coreConfig = QString2QJsonObject(customBean->config_simple);
        } else {
            BuildConfigSingBox(status);
        }

        // apply custom config
        MergeJson(result->coreConfig, QString2QJsonObject(ent->bean->custom_config));

        return result;
    }

    QString BuildChain(int chainId, const std::shared_ptr<BuildConfigStatus> &status) {
        auto group = profileManager->GetGroup(status->ent->gid);
        if (group == nullptr) {
            status->result->error = QStringLiteral("This profile is not in any group, your data may be corrupted.");
            return {};
        }

        auto resolveChain = [=](const std::shared_ptr<ProxyEntity> &ent) {
            QList<std::shared_ptr<ProxyEntity>> resolved;
            if (ent->type == "chain") {
                auto list = ent->ChainBean()->list;
                std::reverse(std::begin(list), std::end(list));
                for (auto id: list) {
                    resolved += profileManager->GetProfile(id);
                    if (resolved.last() == nullptr) {
                        status->result->error = QStringLiteral("chain missing ent: %1").arg(id);
                        break;
                    }
                    if (resolved.last()->type == "chain") {
                        status->result->error = QStringLiteral("chain in chain is not allowed: %1").arg(id);
                        break;
                    }
                }
            } else {
                resolved += ent;
            };
            return resolved;
        };

        // Make list
        auto ents = resolveChain(status->ent);
        if (!status->result->error.isEmpty()) return {};

        if (group->front_proxy_id >= 0) {
            auto fEnt = profileManager->GetProfile(group->front_proxy_id);
            if (fEnt == nullptr) {
                status->result->error = QStringLiteral("front proxy ent not found.");
                return {};
            }
            ents += resolveChain(fEnt);
            if (!status->result->error.isEmpty()) return {};
        }

        // BuildChain
        QString chainTagOut = BuildChainInternal(0, ents, status);

        // Chain ent traffic stat
        if (ents.length() > 1) {
            status->ent->traffic_data->id = status->ent->id;
            status->ent->traffic_data->tag = chainTagOut.toStdString();
            status->result->outboundStats += status->ent->traffic_data;
        }

        return chainTagOut;
    }

#define DOMAIN_USER_RULE                                                             \
    for (const auto &line: SplitLinesSkipSharp(dataStore->routing->proxy_domain)) {  \
        if (dataStore->routing->dns_routing) status->domainListDNSRemote += line;    \
        status->domainListRemote += line;                                            \
    }                                                                                \
    for (const auto &line: SplitLinesSkipSharp(dataStore->routing->direct_domain)) { \
        if (dataStore->routing->dns_routing) status->domainListDNSDirect += line;    \
        status->domainListDirect += line;                                            \
    }                                                                                \
    for (const auto &line: SplitLinesSkipSharp(dataStore->routing->block_domain)) {  \
        status->domainListBlock += line;                                             \
    }

#define IP_USER_RULE                                                             \
    for (const auto &line: SplitLinesSkipSharp(dataStore->routing->block_ip)) {  \
        status->ipListBlock += line;                                             \
    }                                                                            \
    for (const auto &line: SplitLinesSkipSharp(dataStore->routing->proxy_ip)) {  \
        status->ipListRemote += line;                                            \
    }                                                                            \
    for (const auto &line: SplitLinesSkipSharp(dataStore->routing->direct_ip)) { \
        status->ipListDirect += line;                                            \
    }

    QString BuildChainInternal(int chainId, const QList<std::shared_ptr<ProxyEntity>> &ents,
                               const std::shared_ptr<BuildConfigStatus> &status) {
        QString chainTag = "c-" + Int2String(chainId);
        QString chainTagOut;
        bool muxApplied = false;

        QString pastTag;
        int pastExternalStat = 0;
        int index = 0;

        for (const auto &ent: ents) {
            // tagOut: v2ray outbound tag for a profile
            // profile2 (in) (global)   tag g-(id)
            // profile1                 tag (chainTag)-(id)
            // profile0 (out)           tag (chainTag)-(id) / single: chainTag=g-(id)
            auto tagOut = chainTag + "-" + Int2String(ent->id);

            // needGlobal: can only contain one?
            bool needGlobal = false;

            // first profile set as global
            auto isFirstProfile = index == ents.length() - 1;
            if (isFirstProfile) {
                needGlobal = true;
                tagOut = "g-" + Int2String(ent->id);
            }

            // last profile set as "proxy"
            if (chainId == 0 && index == 0) {
                needGlobal = false;
                tagOut = "proxy";
            }

            // ignoreConnTag
            if (index != 0) {
                status->result->ignoreConnTag << tagOut;
            }

            if (needGlobal) {
                if (status->globalProfiles.contains(ent->id)) {
                    continue;
                }
                status->globalProfiles += ent->id;
            }

            if (index > 0) {
                // chain rules: past
                if (pastExternalStat == 0) {
                    auto replaced = status->outbounds.last().toObject();
                    replaced["detour"] = tagOut;
                    status->outbounds.removeLast();
                    status->outbounds += replaced;
                } else {
                    status->routingRules += QJsonObject{
                        {"inbound", QJsonArray{pastTag + "-mapping"}},
                        {"outbound", tagOut},
                    };
                }
            } else {
                // index == 0 means last profile in chain / not chain
                chainTagOut = tagOut;
                status->result->outboundStat = ent->traffic_data;
            }

            // chain rules: this
            auto ext_mapping_port = 0;
            auto ext_socks_port = 0;
            auto thisExternalStat = ent->bean->NeedExternal(isFirstProfile);
            if (thisExternalStat < 0) {
                status->result->error = "This configuration cannot be set automatically, please try another.";
                return {};
            }

            // determine port
            if (thisExternalStat > 0) {
                if (ent->type == "custom") {
                    auto bean = ent->CustomBean();
                    if (IsValidPort(bean->mapping_port)) {
                        ext_mapping_port = bean->mapping_port;
                    } else {
                        ext_mapping_port = MkPort();
                    }
                    if (IsValidPort(bean->socks_port)) {
                        ext_socks_port = bean->socks_port;
                    } else {
                        ext_socks_port = MkPort();
                    }
                } else {
                    ext_mapping_port = MkPort();
                    ext_socks_port = MkPort();
                }
            }
            if (thisExternalStat == 2) dataStore->need_keep_vpn_off = true;
            if (thisExternalStat == 1) {
                // mapping
                status->inbounds += QJsonObject{
                    {"type", "direct"},
                    {"tag", tagOut + "-mapping"},
                    {"listen", "127.0.0.1"},
                    {"listen_port", ext_mapping_port},
                    {"override_address", ent->bean->serverAddress},
                    {"override_port", ent->bean->serverPort},
                };
                // no chain rule and not outbound, so need to set to direct
                if (isFirstProfile) {
                    status->routingRules += QJsonObject{
                        {"inbound", QJsonArray{tagOut + "-mapping"}},
                        {"outbound", "direct"},
                    };
                }
            }

            // Outbound

            QJsonObject outbound;
            auto stream = GetStreamSettings(ent->bean.get());

            if (thisExternalStat > 0) {
                auto extR = ent->bean->BuildExternal(ext_mapping_port, ext_socks_port, thisExternalStat);
                if (extR.program.isEmpty()) {
                    status->result->error = QObject::tr("Core not found: %1").arg(ent->bean->DisplayCoreType());
                    return {};
                }
                if (!extR.error.isEmpty()) { // rejected
                    status->result->error = extR.error;
                    return {};
                }
                extR.tag = ent->bean->DisplayType();
                status->result->extRs.emplace_back(std::make_shared<NekoGui_fmt::ExternalBuildResult>(extR));

                // SOCKS OUTBOUND
                outbound["type"] = "socks";
                outbound["server"] = "127.0.0.1";
                outbound["server_port"] = ext_socks_port;
            } else {
                const auto coreR = ent->bean->BuildCoreObjSingBox();
                if (coreR.outbound.isEmpty()) {
                    status->result->error = "unsupported outbound";
                    return {};
                }
                if (!coreR.error.isEmpty()) { // rejected
                    status->result->error = coreR.error;
                    return {};
                }
                outbound = coreR.outbound;
            }

            // outbound misc
            outbound["tag"] = tagOut;
            ent->traffic_data->id = ent->id;
            ent->traffic_data->tag = tagOut.toStdString();
            status->result->outboundStats += ent->traffic_data;

            // mux common
            auto needMux = ent->type == "vmess" || ent->type == "trojan" || ent->type == "vless";
            needMux &= dataStore->mux_concurrency > 0;

            if (stream != nullptr) {
                if (stream->network == "grpc" || stream->network == "quic" || (stream->network == "http" && stream->security == "tls")) {
                    needMux = false;
                }
                if (stream->multiplex_status == 0) {
                    if (!dataStore->mux_default_on) needMux = false;
                } else if (stream->multiplex_status == 1) {
                    needMux = true;
                } else if (stream->multiplex_status == 2) {
                    needMux = false;
                }
            }
            if (ent->type == "vless" && outbound["flow"] != "") {
                needMux = false;
            }

            // common
            // apply domain_strategy
            outbound["domain_strategy"] = dataStore->routing->outbound_domain_strategy;
            // apply mux
            if (!muxApplied && needMux) {
                auto muxObj = QJsonObject{
                    {"enabled", true},
                    {"protocol", dataStore->mux_protocol},
                    {"padding", dataStore->mux_padding},
                    {"max_streams", dataStore->mux_concurrency},
                };
                outbound["multiplex"] = muxObj;
                muxApplied = true;
            }

            // apply custom outbound settings
            MergeJson(outbound, QString2QJsonObject(ent->bean->custom_outbound));

            // Bypass Lookup for the first profile
            auto serverAddress = ent->bean->serverAddress;

            auto customBean = dynamic_cast<NekoGui_fmt::CustomBean *>(ent->bean.get());
            if (customBean != nullptr && customBean->core == "internal") {
                auto server = QString2QJsonObject(customBean->config_simple)["server"].toString();
                if (!server.isEmpty()) serverAddress = server;
            }

            if (!IsIpAddress(serverAddress)) {
                status->domainListDNSDirect += "full:" + serverAddress;
            }

            status->outbounds += outbound;
            pastTag = tagOut;
            pastExternalStat = thisExternalStat;
            index++;
        }

        return chainTagOut;
    }

    // SingBox

    void BuildConfigSingBox(const std::shared_ptr<BuildConfigStatus> &status) {
        // Log
        status->result->coreConfig["log"] = QJsonObject{{"level", dataStore->log_level}};

        // Inbounds

        // mixed-in
        if (IsValidPort(dataStore->inbound_socks_port) && !status->forTest) {
            QJsonObject inboundObj;
            inboundObj["tag"] = "mixed-in";
            inboundObj["type"] = "mixed";
            inboundObj["listen"] = dataStore->inbound_address;
            inboundObj["listen_port"] = dataStore->inbound_socks_port;
            if (dataStore->inbound_auth->NeedAuth()) {
                inboundObj["users"] = QJsonArray{
                    QJsonObject{
                        {"username", dataStore->inbound_auth->username},
                        {"password", dataStore->inbound_auth->password},
                    },
                };
            }
            status->inbounds += inboundObj;
        }

        // tun-in
        if (dataStore->vpn_internal_tun && dataStore->spmode_vpn && !status->forTest) {
            QJsonObject inboundObj;
            inboundObj["tag"] = "tun-in";
            inboundObj["type"] = "tun";
            inboundObj["interface_name"] = genTunName();
            inboundObj["auto_route"] = true;
            inboundObj["endpoint_independent_nat"] = true;
            inboundObj["mtu"] = dataStore->vpn_mtu;
            inboundObj["stack"] = Preset::SingBox::VpnImplementation.value(dataStore->vpn_implementation);
            inboundObj["strict_route"] = dataStore->vpn_strict_route;
            QJsonArray tunAddress{"172.19.0.1/28"};
            if (dataStore->vpn_ipv6) tunAddress += "fdfe:dcba:9876::1/126";
            inboundObj["address"] = tunAddress;
            status->inbounds += inboundObj;
        }

        // Outbounds
        auto tagProxy = BuildChain(0, status);
        if (!status->result->error.isEmpty()) return;

        // direct & bypass（sing-box 1.14: block/dns 特殊出站已移除，改用规则动作）
        status->outbounds += QJsonObject{
            {"type", "direct"},
            {"tag", "direct"},
        };
        status->outbounds += QJsonObject{
            {"type", "direct"},
            {"tag", "bypass"},
        };

        // custom inbound
        if (!status->forTest) QJSONARRAY_ADD(status->inbounds, QString2QJsonObject(dataStore->custom_inbound)["inbounds"].toArray())

        status->result->coreConfig.insert("inbounds", status->inbounds);
        status->result->coreConfig.insert("outbounds", status->outbounds);

        // user rule
        if (!status->forTest) {
            DOMAIN_USER_RULE
            IP_USER_RULE
        }

        // sing-box common rule object（sing-box 1.14: geoip/geosite 规则项已移除，改用 rule_set 引用）
        QStringList geoRuleSetRefs;
        auto make_rule = [&](const QStringList &list, bool isIP = false) {
            QJsonObject rule;
            //
            QJsonArray ip_cidr;
            QJsonArray ruleSets;
            //
            QJsonArray domain_keyword;
            QJsonArray domain_subdomain;
            QJsonArray domain_regexp;
            QJsonArray domain_full;
            for (auto item: list) {
                if (isIP) {
                    if (item.startsWith("geoip:")) {
                        auto cat = item.mid(6);
                        if (cat == "private") {
                            // sing-box 1.14: geoip.db 已移除 private 类别，改用内置私有网段
                            for (const auto &seg : QJsonArray{"10.0.0.0/8", "100.64.0.0/10", "127.0.0.0/8", "169.254.0.0/16", "172.16.0.0/12", "192.0.0.0/24", "192.0.2.0/24", "192.88.99.0/24", "192.168.0.0/16", "198.18.0.0/15", "198.51.100.0/24", "203.0.113.0/24", "224.0.0.0/3", "240.0.0.0/4", "::1/128", "fc00::/7", "fe80::/10", "ff00::/8"}) ip_cidr += seg;
                        } else {
                            auto tag = "geoip-" + item.replace("geoip:", "");
                            if (!geoRuleSetRefs.contains(tag)) geoRuleSetRefs += tag;
                            ruleSets += tag;
                        }
                    } else {
                        ip_cidr += item;
                    }
                } else {
                    // https://www.v2fly.org/config/dns.html#dnsobject
                    if (item.startsWith("geosite:")) {
                        auto tag = "geosite-" + item.replace("geosite:", "");
                        if (!geoRuleSetRefs.contains(tag)) geoRuleSetRefs += tag;
                        ruleSets += tag;
                    } else if (item.startsWith("full:")) {
                        domain_full += item.replace("full:", "").toLower();
                    } else if (item.startsWith("domain:")) {
                        domain_subdomain += item.replace("domain:", "").toLower();
                    } else if (item.startsWith("regexp:")) {
                        domain_regexp += item.replace("regexp:", "").toLower();
                    } else if (item.startsWith("keyword:")) {
                        domain_keyword += item.replace("keyword:", "").toLower();
                    } else {
                        domain_subdomain += item.toLower();
                    }
                }
            }
            if (isIP) {
                if (ip_cidr.isEmpty() && ruleSets.isEmpty()) return rule;
                rule["ip_cidr"] = ip_cidr;
            } else {
                if (domain_keyword.isEmpty() && domain_subdomain.isEmpty() && domain_regexp.isEmpty() && domain_full.isEmpty() && ruleSets.isEmpty()) {
                    return rule;
                }
                rule["domain"] = domain_full;
                rule["domain_suffix"] = domain_subdomain; // v2ray Subdomain => sing-box suffix
                rule["domain_keyword"] = domain_keyword;
                rule["domain_regex"] = domain_regexp;
            }
            if (!ruleSets.isEmpty()) rule["rule_set"] = ruleSets;
            return rule;
        };

        // final add DNS（sing-box 1.14: 服务器必须带 type，address 改为 server）
        QJsonObject dns;
        QJsonArray dnsServers;
        QJsonArray dnsRules;

        auto buildDnsServer = [](const QString &tag, const QString &address, const QString &detour, const QString &domainResolver) {
            QJsonObject obj{{"tag", tag}};
            if (!domainResolver.isEmpty()) obj["domain_resolver"] = domainResolver;
            // sing-box 1.13+: DNS 服务器默认 dialer 即直连，显式 detour 到空的 direct 出站会报错，故省略
            if (!detour.isEmpty() && detour != "direct") obj["detour"] = detour;
            if (address == "local") {
                obj["type"] = "local";
                return obj;
            }
            QString type = "udp";
            QString server = address;
            QString path;
            if (server.startsWith("tcp://")) {
                type = "tcp";
                server = server.mid(6);
            } else if (server.startsWith("udp://")) {
                type = "udp";
                server = server.mid(6);
            } else if (server.startsWith("tls://")) {
                type = "tls";
                server = server.mid(6);
            } else if (server.startsWith("quic://")) {
                type = "quic";
                server = server.mid(7);
            } else if (server.startsWith("h3://")) {
                type = "h3";
                server = server.mid(5);
            } else if (server.startsWith("https://")) {
                type = "https";
                server = server.mid(8);
                auto slash = server.indexOf('/');
                if (slash >= 0) {
                    path = server.mid(slash);
                    server = server.left(slash);
                }
            }
            obj["type"] = type;
            obj["server"] = server;
            if (!path.isEmpty()) obj["path"] = path;
            return obj;
        };

        // Remote
        if (!status->forTest)
            dnsServers += buildDnsServer("dns-remote", dataStore->routing->remote_dns, tagProxy, "dns-local");

        // Direct（forTest 用系统 DNS 最稳：DoH 服务器不可达时 URL 测试会整体失败）
        if (!status->forTest) {
        QJsonObject directObj = buildDnsServer("dns-direct", dataStore->routing->direct_dns, "direct", "dns-local");
        if (dataStore->routing->dns_final_out == "bypass") {
            dnsServers.prepend(directObj);
        } else {
            dnsServers.append(directObj);
        }
        }
        // ⚠️ 这里不能再放「catch-all → dns-direct」的 DNS 规则（曾是 {"outbound":"any"}）。
        //
        // sing-box 的 DNS 规则是**首个匹配生效**，而 `{"outbound":"any"}` 会匹配**每一条**
        // DNS 查询 —— 于是全部 DNS 都被按「直连」解析，绕开代理，等于 DNS 全泄露；真机
        // 复现的 core 日志就是 `dns: match[0] outbound=any => route(dns-direct)`。
        // 它排在数组首位，还会把下面 domainListDNSRemote（走代理）那几条规则整个盖掉，
        // dns_routing 形同失效。（`outbound` 这个 DNS 规则项 sing-box 1.12 起已废弃、
        // 1.14 移除，本就不该再用。）
        //
        // 默认 DNS 由服务器顺序（dns_final_out）决定：proxy → dns-remote，bypass →
        // dns-direct；某类域名要固定走哪个 server，由下面的 domainListDNS* 规则指定。

        // Fakedns
        if (dataStore->fake_dns && dataStore->vpn_internal_tun && dataStore->spmode_vpn && !status->forTest) {
            dnsServers += QJsonObject{
                {"type", "fakeip"},
                {"tag", "dns-fake"},
                {"inet4_range", "198.18.0.0/15"},
                {"inet6_range", "fc00::/18"},
            };
        }

        // Underlying 100% Working DNS ?
        dnsServers += buildDnsServer("dns-local", BOX_UNDERLYING_DNS, "direct", "");

        // sing-box dns rule object
        auto add_rule_dns = [&](const QStringList &list, const QString &server) {
            auto rule = make_rule(list, false);
            if (rule.isEmpty()) return;
            rule["server"] = server;
            dnsRules += rule;
        };
        add_rule_dns(status->domainListDNSRemote, "dns-remote");
        add_rule_dns(status->domainListDNSDirect, "dns-direct");

        // built-in rules（sing-box 1.14: rcode 服务器已移除，改用 predefined 规则动作）
        if (!status->forTest) {
            dnsRules += QJsonObject{
                {"query_type", QJsonArray{32, 33}},
                {"action", "predefined"},
                {"rcode", "NOERROR"},
            };
            dnsRules += QJsonObject{
                {"domain_suffix", ".lan"},
                {"action", "predefined"},
                {"rcode", "NOERROR"},
            };
        }

        // fakedns rule
        if (dataStore->fake_dns && dataStore->vpn_internal_tun && dataStore->spmode_vpn && !status->forTest) {
            dnsRules += QJsonObject{
                {"inbound", "tun-in"},
                {"server", "dns-fake"},
            };
        }

        dns["servers"] = dnsServers;
        dns["rules"] = dnsRules;

        if (dataStore->routing->use_dns_object) {
            dns = QString2QJsonObject(dataStore->routing->dns_object);
        }
        status->result->coreConfig.insert("dns", dns);

        // Routing

        // dns hijack（sing-box 1.14: dns 出站已移除，改用 hijack-dns 动作）
        if (!status->forTest) {
            status->routingRules += QJsonObject{
                {"protocol", "dns"},
                {"action", "hijack-dns"},
            };
        }

        // sniffing & domain_strategy（sing-box 1.14: 入站字段已移除，改用规则动作）
        if (!status->forTest) {
            auto addInboundActions = [&](const QString &inboundTag) {
                if (dataStore->routing->sniffing_mode != SniffingMode::DISABLE) {
                    status->routingRules += QJsonObject{
                        {"inbound", inboundTag},
                        {"action", "sniff"},
                    };
                }
                if (!dataStore->routing->domain_strategy.isEmpty()) {
                    status->routingRules += QJsonObject{
                        {"inbound", inboundTag},
                        {"action", "resolve"},
                        {"strategy", dataStore->routing->domain_strategy},
                    };
                }
            };
            addInboundActions("mixed-in");
            if (dataStore->vpn_internal_tun && dataStore->spmode_vpn) addInboundActions("tun-in");
        }

        // sing-box routing rule object
        auto add_rule_route = [&](const QStringList &list, bool isIP, const QString &out) {
            auto rule = make_rule(list, isIP);
            if (rule.isEmpty()) return;
            if (out == "block") {
                rule["action"] = "reject"; // sing-box 1.14: block 出站已移除，改用 reject 动作
            } else {
                rule["outbound"] = out;
            }
            status->routingRules += rule;
        };

        // final add user rule
        add_rule_route(status->domainListBlock, false, "block");
        add_rule_route(status->domainListRemote, false, tagProxy);
        add_rule_route(status->domainListDirect, false, "bypass");
        add_rule_route(status->ipListBlock, true, "block");
        add_rule_route(status->ipListRemote, true, tagProxy);
        add_rule_route(status->ipListDirect, true, "bypass");

        // built-in rules（block 出站已移除，改用 reject 动作）
        status->routingRules += QJsonObject{
            {"network", "udp"},
            {"port", QJsonArray{135, 137, 138, 139, 5353}},
            {"action", "reject"},
        };
        status->routingRules += QJsonObject{
            {"ip_cidr", QJsonArray{"224.0.0.0/3", "ff00::/8"}},
            {"action", "reject"},
        };
        status->routingRules += QJsonObject{
            {"source_ip_cidr", QJsonArray{"224.0.0.0/3", "ff00::/8"}},
            {"action", "reject"},
        };

        // tun user rule
        if (dataStore->vpn_internal_tun && dataStore->spmode_vpn && !status->forTest) {
            auto match_out = dataStore->vpn_rule_white ? "proxy" : "bypass";

            QString process_name_rule = dataStore->vpn_rule_process.trimmed();
            if (!process_name_rule.isEmpty()) {
                auto arr = SplitLinesSkipSharp(process_name_rule);
                QJsonObject rule{{"outbound", match_out},
                                 {"process_name", QList2QJsonArray(arr)}};
                status->routingRules += rule;
            }

            QString cidr_rule = dataStore->vpn_rule_cidr.trimmed();
            if (!cidr_rule.isEmpty()) {
                auto arr = SplitLinesSkipSharp(cidr_rule);
                QJsonObject rule{{"outbound", match_out},
                                 {"ip_cidr", QList2QJsonArray(arr)}};
                status->routingRules += rule;
            }

            auto autoBypassExternalProcessPaths = getAutoBypassExternalProcessPaths(status->result);
            if (!autoBypassExternalProcessPaths.isEmpty()) {
                QJsonObject rule{{"outbound", "bypass"},
                                 {"process_name", QList2QJsonArray(autoBypassExternalProcessPaths)}};
                status->routingRules += rule;
            }
        }

        // geopath（sing-box 1.14: geoip/geosite 数据库已移除，geoip:/geosite: 规则改用 rule_set）
        auto geoip = FindCoreAsset("geoip.db");
        auto geosite = FindCoreAsset("geosite.db");
        if (geoip.isEmpty()) status->result->error = +"geoip.db not found";
        if (geosite.isEmpty()) status->result->error = +"geosite.db not found";

        // 按需生成 geoip/geosite rule_set（调用内置 sing-box CLI 导出）
        QJsonArray ruleSets;
        for (const auto &tag : geoRuleSetRefs) {
            auto ruleSetDir = QFileInfo(geoip).absolutePath() + "/rule_set";
            QDir().mkpath(ruleSetDir);
            QString category = tag.mid(tag.indexOf('-') + 1);
            QString srsName, format;
            if (tag.startsWith("geoip-")) {
                srsName = "geoip-" + category + ".json";
                format = "source";
            } else {
                srsName = "geosite-" + category + ".json";
                format = "source";
            }
            auto srsPath = ruleSetDir + "/" + srsName;
            if (!QFile::exists(srsPath)) {
                // 内置 sing-box CLI 的文件名随平台不同：原实现硬编码 "sing-box.exe"，
                // Linux/macOS 上永远找不到该文件 → 静默跳过 rule_set 导出，
                // 而路由里仍引用 geoip-*/geosite-* 标签，sing-box 启动即报 rule_set not found。
#ifdef Q_OS_WIN
                auto cli = QApplication::applicationDirPath() + "/sing-box.exe";
#else
                auto cli = QApplication::applicationDirPath() + "/sing-box";
#endif
                if (!QFile::exists(cli)) {
                    qWarning() << "geo rule_set export skipped: sing-box CLI not found at" << cli;
                } else {
                    QProcess p;
                    p.setWorkingDirectory(ruleSetDir);
                    if (tag.startsWith("geoip-")) {
                        p.start(cli, {"geoip", "export", category, "-f", geoip});
                    } else {
                        p.start(cli, {"geosite", "export", category, "-f", geosite});
                    }
                    p.waitForFinished(30000);
                }
            }
            if (QFile::exists(srsPath)) {
                ruleSets += QJsonObject{
                    {"tag", tag},
                    {"type", "local"},
                    {"format", format},
                    {"path", srsPath},
                };
            }
        }

        // final add routing rule
        auto routingRules = QString2QJsonObject(dataStore->routing->custom)["rules"].toArray();
        if (status->forTest) routingRules = {};
        if (!status->forTest) QJSONARRAY_ADD(routingRules, QString2QJsonObject(dataStore->custom_route_global)["rules"].toArray())
        QJSONARRAY_ADD(routingRules, status->routingRules)
        auto routeObj = QJsonObject{
            {"rules", routingRules},
            {"auto_detect_interface", dataStore->spmode_vpn}, // TODO force enable?
        };
        if (!ruleSets.isEmpty()) routeObj["rule_set"] = ruleSets;
        if (!status->forTest) routeObj["final"] = dataStore->routing->def_outbound;
        if (status->forExport) {
            routeObj.remove("auto_detect_interface");
        }
        status->result->coreConfig.insert("route", routeObj);

        // experimental
        QJsonObject experimentalObj;

        if (!status->forTest && dataStore->core_box_clash_api > 0) {
            QJsonObject clash_api = {
                {"external_controller", "127.0.0.1:" + Int2String(dataStore->core_box_clash_api)},
                {"secret", dataStore->core_box_clash_api_secret},
                {"external_ui", "dashboard"},
            };
            experimentalObj["clash_api"] = clash_api;
        }

        if (!experimentalObj.isEmpty()) status->result->coreConfig.insert("experimental", experimentalObj);
    }

    QString WriteVPNSingBoxConfig() {
        // tun user rule
        auto match_out = dataStore->vpn_rule_white ? "neko-socks" : "direct";
        auto no_match_out = dataStore->vpn_rule_white ? "direct" : "neko-socks";

        QString process_name_rule = dataStore->vpn_rule_process.trimmed();
        if (!process_name_rule.isEmpty()) {
            auto arr = SplitLinesSkipSharp(process_name_rule);
            QJsonObject rule{{"outbound", match_out},
                             {"process_name", QList2QJsonArray(arr)}};
            process_name_rule = "," + QJsonObject2QString(rule, false);
        }

        QString cidr_rule = dataStore->vpn_rule_cidr.trimmed();
        if (!cidr_rule.isEmpty()) {
            auto arr = SplitLinesSkipSharp(cidr_rule);
            QJsonObject rule{{"outbound", match_out},
                             {"ip_cidr", QList2QJsonArray(arr)}};
            cidr_rule = "," + QJsonObject2QString(rule, false);
        }

        // TODO bypass ext core process path?

        // auth
        QString socks_user_pass;
        if (dataStore->inbound_auth->NeedAuth()) {
            socks_user_pass = R"( "username": "%1", "password": "%2", )";
            socks_user_pass = socks_user_pass.arg(dataStore->inbound_auth->username, dataStore->inbound_auth->password);
        }
        // gen config
        auto configFn = ":/neko/vpn/sing-box-vpn.json";
        if (QFile::exists("vpn/sing-box-vpn.json")) configFn = "vpn/sing-box-vpn.json";
        auto underlyingDns = BOX_UNDERLYING_DNS;
        QString dnsDirectOpts = underlyingDns == "local" ? "\"type\": \"local\"," : QString(R"("type": "udp", "server": "%1",)").arg(underlyingDns);
        QString tunAddress = dataStore->vpn_ipv6 ? R"("172.19.0.1/28", "fdfe:dcba:9876::1/126")" : R"("172.19.0.1/28")";
        auto config = ReadFileText(configFn)
                          .replace("%DNS_DIRECT_OPTS%", dnsDirectOpts)
                          .replace("%TUN_ADDRESS%", tunAddress)
                          .replace("//%SOCKS_USER_PASS%", socks_user_pass)
                          .replace("//%PROCESS_NAME_RULE%", process_name_rule)
                          .replace("//%CIDR_RULE%", cidr_rule)
                          .replace("%MTU%", Int2String(dataStore->vpn_mtu))
                          .replace("%STACK%", Preset::SingBox::VpnImplementation.value(dataStore->vpn_implementation))
                          .replace("%TUN_NAME%", genTunName())
                          .replace("%STRICT_ROUTE%", dataStore->vpn_strict_route ? "true" : "false")
                          .replace("%FINAL_OUT%", no_match_out)
                          .replace("%FAKE_DNS_INBOUND%", dataStore->fake_dns ? "tun-in" : "empty")
                          .replace("%PORT%", Int2String(dataStore->inbound_socks_port));
        // write config
        QFile file;
        file.setFileName(QFileInfo(configFn).fileName());
        file.open(QIODevice::ReadWrite | QIODevice::Truncate);
        file.write(config.toUtf8());
        file.close();
        return QFileInfo(file).absoluteFilePath();
    }

    QString WriteVPNLinuxScript(const QString &configPath) {
#ifdef Q_OS_WIN
        return {};
#endif
        // gen script
        auto scriptFn = ":/neko/vpn/vpn-run-root.sh";
        if (QFile::exists("vpn/vpn-run-root.sh")) scriptFn = "vpn/vpn-run-root.sh";
        auto script = ReadFileText(scriptFn)
                          .replace("./nekobox_core", QApplication::applicationDirPath() + "/nekobox_core")
                          .replace("$CONFIG_PATH", configPath);
        // write script
        QFile file2;
        file2.setFileName(QFileInfo(scriptFn).fileName());
        file2.open(QIODevice::ReadWrite | QIODevice::Truncate);
        file2.write(script.toUtf8());
        file2.close();
        return QFileInfo(file2).absoluteFilePath();
    }

} // namespace NekoGui
