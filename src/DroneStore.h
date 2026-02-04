#ifndef DRONE_STORE_H
#define DRONE_STORE_H

#include <Arduino.h>
#include <vector>

struct DroneInfo {
    String mac;
    int rssi;
    String proto; // "BLE 4", "BLE 5"
    
    // === Basic ID (Type 0) ===
    String sn;        // 序列号
    String uaType;    // 机型分类 (Helicopter, Glider...)
    
    // === Location (Type 1) ===
    double lat;       // 纬度
    double lon;       // 经度
    int alt;          // 气压高度 (m)
    int height;       // 相对起飞点高度 (m)
    int speed_h;      // 水平速度 (m/s)
    int speed_v;      // 垂直速度 (m/s)
    int dir;          // 航向 (0-360)
    String status;    // 飞行状态 (Ground, Airborne...)
    
    // === System (Type 4) ===
    double op_lat;    // 飞手纬度
    double op_lon;    // 飞手经度
    int op_alt;       // 飞手高度
    String classification; // 分类 (EU/UAS)
    
    // === Operator ID (Type 5) ===
    String operatorId; 
    
    // === Self ID (Type 3) ===
    String selfIdDesc; // 自述文本 (如 "Fire", "Police")
    
    // === Auth (Type 2) ===
    String authData;   // 认证数据 (Hex)

    // === Meta ===
    unsigned long lastSeen;
    unsigned long msgCount;
    String debugTypes; // 记录收到了哪些包类型 (0,1,3,4...)
};

extern std::vector<DroneInfo> droneList;
extern SemaphoreHandle_t listMutex;

#endif