#ifndef _SERVER_INFO_H
#define _SERVER_INFO_H

#ifdef __cplusplus

// #define SRV_IP      "73.239.94.122"
const const char* NETWORK_CONFIG_FILE = "network.cfg";

// Config file format:
// <IP/HOSTNAME>
// <VIDEO/AUDIO PORT>
// <MOUSE       PORT>

const const char*    FALLBACK_SRV_IP         = "192.168.1.49";
const const uint16_t FALLBACK_SRV_AV_PORT    = 50420;
const const uint16_t FALLBACK_SRV_MOUSE_PORT = 50512;

#include <fstream>
#include <string>

inline std::string get_server_ip()
{
    std::ifstream cfg(NETWORK_CONFIG_FILE);
    if(!cfg.good())
        return std::string(FALLBACK_SRV_IP);

    std::string ip;

    if(!std::getline(cfg, ip))
        return std::string(FALLBACK_SRV_IP);
    return ip;
}

inline uint16_t get_server_av_port()
{
    std::ifstream cfg(NETWORK_CONFIG_FILE);
    if(!cfg.good())
        return FALLBACK_SRV_AV_PORT;

    std::string ans;

    if(!std::getline(cfg, ans))
        return FALLBACK_SRV_AV_PORT;
    if(!std::getline(cfg, ans))
        return FALLBACK_SRV_AV_PORT;

    return std::stoul(ans);
}

inline uint16_t get_server_mouse_port()
{
    std::ifstream cfg(NETWORK_CONFIG_FILE);
    if(!cfg.good())
        return FALLBACK_SRV_MOUSE_PORT;

    std::string ans;

    if(!std::getline(cfg, ans))
        return FALLBACK_SRV_MOUSE_PORT;
    if(!std::getline(cfg, ans))
        return FALLBACK_SRV_MOUSE_PORT;
    if(!std::getline(cfg, ans))
        return FALLBACK_SRV_MOUSE_PORT;

    return std::stoul(ans);
}

#endif

#endif//_SERVER_INFO_H
