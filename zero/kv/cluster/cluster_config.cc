/**
 * @file cluster_config.cc
 * @brief Redis Cluster nodes.conf 解析与写入
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#include "cluster_config.h"
#include "cluster_manager.h"
#include "cluster_slot.h"
#include "zero/core/log/log.h"
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace zero {
namespace kv {

static zero::Logger::ptr g_logger = ZERO_LOG_NAME("system");

ClusterConfig::ClusterConfig(const std::string& path)
    : m_path(path) {
}

static bool parseAddr(const std::string& s, std::string& ip, int& port, int& cport) {
    size_t at = s.find('@');
    std::string client_part = s;
    cport = 0;
    if(at != std::string::npos) {
        client_part = s.substr(0, at);
        cport = std::stoi(s.substr(at + 1));
    }
    size_t colon = client_part.find(':');
    if(colon == std::string::npos) return false;
    ip = client_part.substr(0, colon);
    port = std::stoi(client_part.substr(colon + 1));
    return !ip.empty() && port > 0;
}

static uint16_t parseFlags(const std::string& s) {
    uint16_t flags = 0;
    std::istringstream ss(s);
    std::string token;
    while(std::getline(ss, token, ',')) {
        if(token == "myself") flags |= static_cast<uint16_t>(ClusterNodeFlag::Myself);
        else if(token == "master") flags |= static_cast<uint16_t>(ClusterNodeFlag::Master);
        else if(token == "slave") flags |= static_cast<uint16_t>(ClusterNodeFlag::Slave);
        else if(token == "fail?") flags |= static_cast<uint16_t>(ClusterNodeFlag::PFail);
        else if(token == "fail") flags |= static_cast<uint16_t>(ClusterNodeFlag::Fail);
        else if(token == "noflags") {}
    }
    return flags;
}

static std::string buildFlags(uint16_t flags) {
    std::vector<std::string> out;
    if(flags & static_cast<uint16_t>(ClusterNodeFlag::Myself)) out.push_back("myself");
    if(flags & static_cast<uint16_t>(ClusterNodeFlag::Master)) out.push_back("master");
    if(flags & static_cast<uint16_t>(ClusterNodeFlag::Slave)) out.push_back("slave");
    if(flags & static_cast<uint16_t>(ClusterNodeFlag::Fail)) out.push_back("fail");
    if(flags & static_cast<uint16_t>(ClusterNodeFlag::PFail)) out.push_back("fail?");
    if(out.empty()) out.push_back("noflags");
    std::string s;
    for(size_t i = 0; i < out.size(); ++i) {
        if(i > 0) s += ",";
        s += out[i];
    }
    return s;
}

static bool parseSlots(const std::string& s, std::bitset<kClusterSlotCount>& slots) {
    if(s.empty()) return true;
    // 支持 "0" "0-100" "[0-<-...>]"（迁移标记）
    std::string token = s;
    bool importing = false;
    bool migrating = false;
    if(token.front() == '[' && token.back() == ']') {
        token = token.substr(1, token.size() - 2);
        if(token.find("->-") != std::string::npos) migrating = true;
        else if(token.find("-<-") != std::string::npos) importing = true;
        size_t dash = token.find('-');
        if(dash != std::string::npos) {
            token = token.substr(0, dash);
        }
    }
    size_t dash = token.find('-');
    if(dash == std::string::npos) {
        int slot = std::stoi(token);
        if(slot < 0 || slot >= kClusterSlotCount) return false;
        slots.set(slot);
    } else {
        int start = std::stoi(token.substr(0, dash));
        int end = std::stoi(token.substr(dash + 1));
        if(start < 0 || end >= kClusterSlotCount || start > end) return false;
        for(int i = start; i <= end; ++i) slots.set(i);
    }
    (void)migrating; (void)importing;  // 迁移状态暂不在配置中持久化
    return true;
}

bool ClusterConfig::load(ClusterManager* mgr) const {
    if(!mgr) return false;
    std::ifstream ifs(m_path);
    if(!ifs.is_open()) {
        ZERO_LOG_INFO(g_logger) << "Cluster config not found, starting fresh: " << m_path;
        return true;
    }
    std::string line;
    std::shared_ptr<ClusterNode> myself;
    std::string my_node_id;
    while(std::getline(ifs, line)) {
        if(line.empty() || line[0] == '#') continue;
        std::istringstream ss(line);
        std::string node_id, addr, flags_str, master_id, ping_sent, pong_recv, epoch, link_state;
        ss >> node_id >> addr >> flags_str >> master_id >> ping_sent >> pong_recv >> epoch >> link_state;
        if(node_id.empty()) continue;

        std::string ip;
        int port = 0, cport = 0;
        if(!parseAddr(addr, ip, port, cport)) continue;

        auto node = std::make_shared<ClusterNode>();
        node->node_id = node_id;
        node->ip = ip;
        node->port = port;
        node->cluster_port = cport > 0 ? cport : port + 10000;
        node->flags = parseFlags(flags_str);
        if(master_id != "-") node->master_id = master_id;
        node->config_epoch = std::stoull(epoch);

        std::string slot_token;
        while(ss >> slot_token) {
            parseSlots(slot_token, node->slots);
        }

        if(node->hasFlag(ClusterNodeFlag::Myself)) {
            myself = node;
            my_node_id = node_id;
        }
        mgr->addNode(node);
    }

    // 重新初始化 myself 引用
    if(myself) {
        // ClusterManager::init 已经由 kv_server 调用过，这里同步状态
        mgr->setMyselfFromConfig(myself);
    }

    ZERO_LOG_INFO(g_logger) << "Loaded cluster config from " << m_path;
    return true;
}

bool ClusterConfig::save(const ClusterManager* mgr) const {
    if(!mgr) return false;
    std::string tmp = m_path + ".tmp";
    std::ofstream ofs(tmp);
    if(!ofs.is_open()) return false;

    auto nodes = mgr->getAllNodes();
    for(const auto& node : nodes) {
        ofs << node->node_id << " ";
        ofs << node->ip << ":" << node->port << "@" << node->cluster_port << " ";
        ofs << buildFlags(node->flags) << " ";
        if(node->hasFlag(ClusterNodeFlag::Slave) && !node->master_id.empty()) {
            ofs << node->master_id;
        } else {
            ofs << "-";
        }
        ofs << " ";
        ofs << node->ping_sent << " ";
        ofs << node->pong_received << " ";
        ofs << node->config_epoch << " ";
        ofs << "connected ";

        // slots
        if(node->hasFlag(ClusterNodeFlag::Master)) {
            std::vector<int> slots;
            for(int i = 0; i < kClusterSlotCount; ++i) {
                if(node->slots.test(i)) slots.push_back(i);
            }
            if(!slots.empty()) {
                int start = slots[0];
                int prev = start;
                for(size_t i = 1; i <= slots.size(); ++i) {
                    if(i == slots.size() || slots[i] != prev + 1) {
                        if(start == prev) {
                            ofs << start << " ";
                        } else {
                            ofs << start << "-" << prev << " ";
                        }
                        if(i < slots.size()) {
                            start = slots[i];
                            prev = start;
                        }
                    } else {
                        prev = slots[i];
                    }
                }
            }
        }
        ofs << "\n";
    }
    ofs.close();
    if(std::rename(tmp.c_str(), m_path.c_str()) != 0) {
        ZERO_LOG_ERROR(g_logger) << "Failed to rename cluster config temp file";
        return false;
    }
    return true;
}

} // namespace kv
} // namespace zero
