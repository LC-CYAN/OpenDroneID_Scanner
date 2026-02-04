#include "ble4_legacy.h"
#include "esp_gap_ble_api.h"
#include "esp_bt.h"
#include "esp_bt_main.h"
#include <string.h> // for memcpy

// --- 私有变量 (仅在此文件内可见) ---

// OpenDroneID ASTM F3411 Legacy Advertising Packet (31 Bytes Max)
// 结构: Length(1) + Type(1) + UUID(2) + AppCode(1) + Data(25) + Padding(1)
static uint8_t _ble4_raw_packet[31] = {
    0x1E,             // Byte 0: Length = 30 bytes (Type + UUID + Code + Data)
    0x16,             // Byte 1: Type = 0x16 (Service Data - 16-bit UUID)
    0xFA, 0xFF,       // Byte 2-3: UUID = 0xFFFA (ASTM International, Little Endian)
    0x0D,             // Byte 4: Application Code = 0x0D (OpenDroneID)
    
    // Byte 5-29: 预留给 25 字节的 ODID 数据 (初始全0)
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00,
    
    0x00              // Byte 30: Padding/Reserved
};

// BLE 4 广播参数
static esp_ble_adv_params_t _ble4_adv_params = {
    .adv_int_min        = 0x00A0, // 最小间隔: 100ms (单位: 0.625ms, 0xA0=160 -> 100ms)
    .adv_int_max        = 0x00A0, // 最大间隔: 100ms
    .adv_type           = ADV_TYPE_IND,       // 通用广播 (可被发现，不可连接其实更好，但 ADV_IND 兼容性最强)
    .own_addr_type      = BLE_ADDR_TYPE_PUBLIC,
    .peer_addr          = {0},
    .peer_addr_type     = BLE_ADDR_TYPE_PUBLIC,
    .channel_map        = ADV_CHNL_ALL,
    .adv_filter_policy  = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY,
};

// --- 函数实现 ---

void ble4_init() {
    // 这里假设 esp_bt_controller_init 已经在主程序或其他地方调用过了
    // 如果没有，需要在这里初始化 BT controller 和 Bluedroid
    
    // 配置初始的广播数据
    esp_ble_gap_config_adv_data_raw(_ble4_raw_packet, 30); 
}

void ble4_update_data(uint8_t *odid_msg_25bytes) {
    if (odid_msg_25bytes == nullptr) return;

    // 1. 将数据拷贝到 Raw Buffer 的第 5 个字节开始的位置
    // 偏移量 5 是因为: Len(1) + Type(1) + UUID_L(1) + UUID_H(1) + AppCode(1)
    memcpy(&_ble4_raw_packet[5], odid_msg_25bytes, 25);

    // 2. 重新配置广播数据 (ESP32 会在下一次广播间隔生效)
    // 注意长度是 30 (从 Byte 0 到 Byte 29)，Byte 30 是填充位，这里我们只发有效数据
    esp_err_t rc = esp_ble_gap_config_adv_data_raw(_ble4_raw_packet, 30);
    
    if (rc != ESP_OK) {
        // 可以在这里加串口打印调试
        // printf("BLE4 Update Failed: %d\n", rc);
    }
}

void ble4_start() {
    esp_ble_gap_start_advertising(&_ble4_adv_params);
}

void ble4_stop() {
    esp_ble_gap_stop_advertising();
}