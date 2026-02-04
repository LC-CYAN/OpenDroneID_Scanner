#ifndef SCANNER_BLE_H
#define SCANNER_BLE_H

void initBLE();   // 初始化蓝牙硬件
void startBLE();  // 开始扫描 (开启射频)
void stopBLE();   // 停止扫描 (释放射频给 WiFi)

#endif