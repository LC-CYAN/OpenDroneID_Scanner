#ifndef BLE4_LEGACY_H
#define BLE4_LEGACY_H

#include <stdint.h>

// 初始化 BLE 4 广播参数
void ble4_init();

// 更新要广播的 25 字节 OpenDroneID 数据
// 这个函数会自动处理 ASTM 头和 raw packet 的拼装
void ble4_update_data(uint8_t *odid_msg_25bytes);

// 开始 BLE 4 广播
void ble4_start();

// 停止 BLE 4 广播 (如果你需要在 BLE 4 和 BLE 5 之间切换，这个很有用)
void ble4_stop();

#endif