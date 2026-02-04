/*
 * ESP32-S3 Drone Scanner V401 (Compile Fix)
 * ------------------------------------------------
 * 修复: 将 ODID 枚举宏替换为通用整数值，解决库版本不兼容导致的编译错误。
 * 功能: 全量解析 Basic ID, Location, System, Self ID, Operator ID。
 */

#include "ScannerBLE.h"
#include "DroneStore.h"
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>
#include "esp_gap_ble_api.h"

extern "C" {
    #include "opendroneid.h"
}

// === 辅助工具 ===
static void safeStrCopy(String &target, char* src, int len) {
    char temp[len + 1]; memset(temp, 0, len + 1);
    int idx = 0, valid = 0;
    for (int i = 0; i < len; i++) {
        if (src[i] == 0) break;
        if (isalnum(src[i]) || src[i] == '-' || src[i] == '.' || src[i] == ' ') {
            temp[idx++] = src[i]; valid++;
        }
    }
    if (valid > 0) target = String(temp);
}

// === 状态枚举转字符串 (使用整数，防止宏定义冲突) ===
String getStatusStr(int status) {
    switch(status) {
        case 0: return "Undeclared";
        case 1: return "Ground";
        case 2: return "Airborne";
        case 3: return "Emergency";
        case 4: return "Fail";
        default: return "Unknown";
    }
}

// === 机型枚举转字符串 (使用整数，防止宏定义冲突) ===
String getUATypeStr(int type) {
    switch(type) {
        case 0: return "None";
        case 1: return "Plane";
        case 2: return "Copter";  // 对应 ODID_UATYPE_HELICOPTER_OR_MULTIROTOR
        case 3: return "Gyro";
        case 4: return "Hybrid";  // 对应 ODID_UATYPE_HYBRID_LIFT
        case 5: return "Ornith";
        case 6: return "Glider";
        case 7: return "Kite";
        case 8: return "Balloon";
        case 9: return "Airship";
        case 10: return "Missile";
        case 11: return "UAV";
        case 12: return "Space";
        default: return "Other";
    }
}

// === 解析单块消息 (25 Bytes) ===
// 返回消息类型 ID，失败返回 -1
static int parse_block(DroneInfo &d, uint8_t *block) {
    uint8_t header = block[0];
    if (header == 0x00) return -1; // 零容忍

    uint8_t msgType = (header & 0xF0) >> 4;
    
    switch (msgType) {
        case 0: { // Basic ID
            ODID_BasicID_data data;
            decodeBasicIDMessage(&data, (ODID_BasicID_encoded *)block);
            d.uaType = getUATypeStr(data.UAType);
            if (data.UASID[0] != 0) {
                // 长度优先覆盖
                if (d.sn.length() == 0 || strlen((char*)data.UASID) >= d.sn.length()) 
                    safeStrCopy(d.sn, (char*)data.UASID, sizeof(data.UASID));
                return 0;
            }
            break;
        }
        case 1: { // Location
            ODID_Location_data data;
            decodeLocationMessage(&data, (ODID_Location_encoded *)block);
            if (data.Latitude != 0) {
                d.lat = data.Latitude;
                d.lon = data.Longitude;
                d.alt = (int)data.AltitudeBaro;
                d.height = (int)data.Height;
                d.speed_h = (int)data.SpeedHorizontal;
                d.speed_v = (int)data.SpeedVertical;
                d.dir = (int)data.Direction;
                d.status = getStatusStr(data.Status);
                return 1;
            }
            break;
        }
        case 2: { // Auth
            ODID_Auth_data data;
            decodeAuthMessage(&data, (ODID_Auth_encoded *)block);
            if (data.AuthType != 0) {
                char hex[64]; hex[0] = 0;
                for(int i=0; i<data.Length && i<16; i++) { // 只显示前16字节
                    char tmp[4]; sprintf(tmp, "%02X", data.AuthData[i]); strcat(hex, tmp);
                }
                d.authData = String(hex);
                return 2;
            }
            break;
        }
        case 3: { // Self ID
            ODID_SelfID_data data;
            decodeSelfIDMessage(&data, (ODID_SelfID_encoded *)block);
            if (data.Desc[0] != 0) {
                safeStrCopy(d.selfIdDesc, (char*)data.Desc, sizeof(data.Desc));
                return 3;
            }
            break;
        }
        case 4: { // System
            ODID_System_data data;
            decodeSystemMessage(&data, (ODID_System_encoded *)block);
            if (data.OperatorLatitude != 0) {
                d.op_lat = data.OperatorLatitude;
                d.op_lon = data.OperatorLongitude;
                d.op_alt = (int)data.OperatorAltitudeGeo;
                return 4;
            }
            break;
        }
        case 5: { // Operator ID
            ODID_OperatorID_data data;
            decodeOperatorIDMessage(&data, (ODID_OperatorID_encoded *)block);
            if(data.OperatorId[0]!=0) { 
                safeStrCopy(d.operatorId, (char*)data.OperatorId, sizeof(data.OperatorId)); 
                return 5; 
            }
            break;
        }
    }
    return -1;
}

// === GAP 回调 (Payload Hunter V307 Logic) ===
static void ble_event_handler(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param) {
    if (event == ESP_GAP_BLE_EXT_ADV_REPORT_EVT) {
        auto &report = param->ext_adv_report.params; 
        uint8_t *raw = report.adv_data;
        int len = report.adv_data_len;
        if (len < 15) return;

        // 寻找锚点 0xFFFA
        int anchor = -1;
        for (int i = 0; i < len - 3; i++) {
            if (raw[i] == 0xFA && raw[i+1] == 0xFF) { anchor = i + 2; break; }
        }
        if (anchor == -1) return;

        uint8_t *payload = &raw[anchor];
        int payloadLen = len - anchor;

        char macBuf[18];
        sprintf(macBuf, "%02X:%02X:%02X:%02X:%02X:%02X", 
                report.addr[0], report.addr[1], report.addr[2], report.addr[3], report.addr[4], report.addr[5]);
        String macStr = String(macBuf);
        String proto = (report.primary_phy == ESP_BLE_GAP_PHY_CODED) ? "BLE 5" : "BLE 4";

        if (xSemaphoreTake(listMutex, 0) == pdTRUE) {
            DroneInfo *target = nullptr;
            for (auto &d : droneList) {
                if (d.mac == macStr && d.proto == proto) { target = &d; break; }
            }
            if (!target) {
                DroneInfo newD; newD.mac = macStr; newD.proto = proto;
                newD.lat=0; newD.lon=0; newD.msgCount=0;
                droneList.push_back(newD);
                target = &droneList.back();
            }

            target->rssi = report.rssi;
            target->lastSeen = millis();
            
            String types = "";
            int offset = 0;
            // 循环解析直到末尾
            while (offset + 25 <= payloadLen) {
                int res = parse_block(*target, &payload[offset]);
                if (res != -1) {
                    target->msgCount++;
                    types += String(res) + ",";
                    offset += 25; // 成功解析，跳跃 25
                } else {
                    offset++; // 解析失败，滑窗 1
                }
            }
            if(types.length() > 0) target->debugTypes = types;
            xSemaphoreGive(listMutex);
        }
    }
}

void initBLE() {
    BLEDevice::init("");
    esp_ble_gap_register_callback(ble_event_handler);
}

void startBLE() {
    esp_ble_ext_scan_params_t params = {
        .own_addr_type = BLE_ADDR_TYPE_PUBLIC,
        .filter_policy = BLE_SCAN_FILTER_ALLOW_ALL,
        .scan_duplicate = BLE_SCAN_DUPLICATE_DISABLE,
        .cfg_mask = ESP_BLE_GAP_EXT_SCAN_CFG_UNCODE_MASK | ESP_BLE_GAP_EXT_SCAN_CFG_CODE_MASK,
        .uncoded_cfg = {BLE_SCAN_TYPE_ACTIVE, 40, 30},
        .coded_cfg = {BLE_SCAN_TYPE_ACTIVE, 40, 30},
    };
    esp_ble_gap_set_ext_scan_params(&params);
    esp_ble_gap_start_ext_scan(0, 0);
}

void stopBLE() {
    esp_ble_gap_stop_ext_scan();
}