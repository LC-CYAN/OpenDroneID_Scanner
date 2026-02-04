/*
 * ESP32-S3 Drone Scanner V405 (Extended Timeout)
 * ------------------------------------------------
 * 调整: 将设备超时移除时间延长至 20 秒。
 *11123231
 *  功能: 紧凑型排版，全量数据解析，静态详情页。
 */


#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include <Wire.h>
#include "DroneStore.h"
#include "ScannerBLE.h"

// ================= 1. 全局变量 =================
std::vector<DroneInfo> droneList;
SemaphoreHandle_t listMutex;

// ================= 2. 硬件配置 =================
#define PIN_POWER_ON 15
#define LCD_CS 6
#define LCD_SCK 47
#define LCD_D0 18
#define LCD_D1 7
#define LCD_D2 48
#define LCD_D3 5
#define LCD_RES 17
#define TOUCH_SDA 40
#define TOUCH_SCL 39
#define TOUCH_ADDR 0x38

// 颜色
#define BLACK   0x0000
#define BLUE    0x001F
#define GREEN   0x07E0
#define RED     0xF800
#define WHITE   0xFFFF
#define CYAN    0x07FF
#define YELLOW  0xFFE0
#define GRAY    0x8410
#define DARK    0x0020
#define DARK_HL 0x2187 
#define DRAWER_BG 0x10A2

// UI 状态
enum AppState { STATE_LIST, STATE_DETAIL };
AppState currentState = STATE_LIST;

// 触摸与滚动
int lastValidX = 0; int lastValidY = 0;
int startTouchX = -1; int startTouchY = -1;
bool isTouching = false; bool isDragging = false;

// 列表页依然保留滚动，详情页移除滚动
float listScrollY = 0; float listVelocityY = 0;      

int dragBackDist = 0; bool isSwipingBack = false;
int pressedIndex = -1; 

int selectedIndex = -1;
int detailPage = 0;

// GFX
Arduino_DataBus *bus = new Arduino_ESP32QSPI(LCD_CS, LCD_SCK, LCD_D0, LCD_D1, LCD_D2, LCD_D3);
Arduino_GFX *gfx = new Arduino_RM67162(bus, LCD_RES, 0);
Arduino_Canvas *canvas = new Arduino_Canvas(240, 536, gfx);

// ================= 3. 触摸驱动 =================
void writeRegister(uint8_t devAddr, uint8_t reg, uint8_t data) { Wire.beginTransmission(devAddr); Wire.write(reg); Wire.write(data); Wire.endTransmission(); }
void readBytes(uint8_t devAddr, uint8_t reg, uint8_t *buf, uint8_t len) { Wire.beginTransmission(devAddr); Wire.write(reg); Wire.endTransmission(); Wire.requestFrom(devAddr, len); for(int i=0; i<len; i++) if(Wire.available()) buf[i] = Wire.read(); }

bool getTouch(int *x, int *y) { 
    uint8_t data[6]; 
    readBytes(TOUCH_ADDR, 0x02, data, 5); 
    if ((data[0] & 0x0F) > 0) { 
        *x = ((data[1] & 0x0F) << 8) | data[2]; 
        *y = ((data[3] & 0x0F) << 8) | data[4]; 
        return true; 
    } 
    return false; 
}

// ================= 4. 交互逻辑 =================
void updatePhysics() {
    // 仅列表页保留惯性滚动
    if (currentState == STATE_LIST) {
        if (!isTouching && abs(listVelocityY) > 0.1) { 
            listScrollY += listVelocityY; listVelocityY *= 0.92; 
            if (abs(listVelocityY) < 0.1) listVelocityY = 0; 
        }
        int totalH = 0;
        if (xSemaphoreTake(listMutex, 10) == pdTRUE) {
            totalH = droneList.size() * 60; 
            xSemaphoreGive(listMutex);
        }
        int maxScroll = max(0, totalH - 180); 
        if (listScrollY < 0) { listScrollY = 0; listVelocityY = 0; } 
        if (listScrollY > maxScroll) { listScrollY = maxScroll; listVelocityY = 0; }
    }
}

void handleTouch() {
    int rawX, rawY; bool nowTouched = getTouch(&rawX, &rawY); 
    int sx = rawY; int sy = 240 - rawX; 

    if (nowTouched) {
        lastValidX = sx; lastValidY = sy;
        if (!isTouching) {
            // === 按下 ===
            isTouching = true; startTouchX = sx; startTouchY = sy; isDragging = false; 
            listVelocityY = 0;
            
            if (sx < 50) isSwipingBack = true; else isSwipingBack = false;
            
            if (currentState == STATE_LIST && !isSwipingBack && sy > 40) { 
                pressedIndex = (sy - 40 + (int)listScrollY) / 60; 
            }
        } else {
            // === 拖动 ===
            int dy = sy - startTouchY; int dx = sx - startTouchX;
            if (abs(dx) > 5 || abs(dy) > 5) { isDragging = true; pressedIndex = -1; }
            
            if (isSwipingBack) { 
                if (dx > 0) dragBackDist = dx; 
            } else if (currentState == STATE_LIST && isDragging) { 
                listScrollY -= (sy - startTouchY); startTouchY = sy; startTouchX = sx; 
            }
        }
    } else { 
        if (isTouching) {
            // === 抬起 ===
            isTouching = false; pressedIndex = -1; 
            
            if (isSwipingBack) {
                if (dragBackDist > 80) { 
                    if (currentState == STATE_DETAIL) currentState = STATE_LIST; 
                }
                dragBackDist = 0; isSwipingBack = false;
            } else if (!isDragging) {
                // === 点击 ===
                int clickY = lastValidY;
                if (currentState == STATE_LIST && clickY > 40) {
                    int clickedIdx = (clickY - 40 + (int)listScrollY) / 60;
                    if (xSemaphoreTake(listMutex, 50) == pdTRUE) {
                        if (clickedIdx >= 0 && clickedIdx < droneList.size()) {
                            selectedIndex = clickedIdx;
                            currentState = STATE_DETAIL;
                            detailPage = 0;
                        }
                        xSemaphoreGive(listMutex);
                    }
                } else if (currentState == STATE_DETAIL) {
                    // 点击任意区域翻页 (除了顶部 Header)
                    if (clickY > 60) detailPage = (detailPage + 1) % 3;
                }
            }
        }
    }
}

// ================= 5. 绘图函数 =================

void drawDrawerAnimation() {
    if (dragBackDist > 5) { 
        int w = min(dragBackDist, 180); 
        canvas->fillRect(0, 0, w, 240, DRAWER_BG); 
        canvas->drawFastVLine(w, 0, 240, CYAN); 
        if (w > 50) { 
            canvas->setTextSize(2); canvas->setTextColor(WHITE); 
            canvas->setCursor(10, 110); 
            if (w >= 80) canvas->print("RELEASE"); else canvas->print("BACK"); 
        } 
    }
}

void drawListScreen() {
    canvas->fillScreen(BLACK);
    canvas->fillRect(0, 0, 536, 40, 0x2124);
    canvas->setTextSize(2); canvas->setTextColor(WHITE);
    canvas->setCursor(10, 10); canvas->print("SCANNER V405");
    
    if (xSemaphoreTake(listMutex, 50) == pdTRUE) {
        canvas->setTextColor(GRAY); canvas->setCursor(400, 10); 
        canvas->printf("CNT:%d", droneList.size());
        
        int itemH = 60; int listTop = 40;
        
        for (int i = 0; i < droneList.size(); i++) {
            int drawY = listTop + (i * itemH) - (int)listScrollY;
            if (drawY + itemH < 40 || drawY > 240) continue;
            
            if (i == pressedIndex) canvas->fillRect(0, drawY, 536, itemH, DARK_HL);
            DroneInfo &d = droneList[i];
            
            if (d.proto == "BLE 5") canvas->setTextColor(CYAN); else canvas->setTextColor(GREEN);
            canvas->setTextSize(2); canvas->setCursor(10, drawY + 8); 
            canvas->printf("[%s] ", d.proto.c_str());
            
            canvas->setTextColor(WHITE);
            if (d.sn.length() > 0) canvas->print(d.sn); else canvas->print("Unknown Device");
            
            canvas->setTextSize(1); canvas->setTextColor(GRAY);
            canvas->setCursor(10, drawY + 35); canvas->printf("MAC:%s  ", d.mac.c_str());
            
            uint16_t rssiColor = (d.rssi > -70) ? GREEN : RED;
            canvas->setTextColor(rssiColor); canvas->printf("%d dBm", d.rssi);
            
            canvas->setTextColor(YELLOW); canvas->setCursor(300, drawY + 35);
            if(d.uaType.length()>0) canvas->print(d.uaType);
            
            canvas->drawFastHLine(10, drawY + itemH - 1, 516, DARK);
        }
        xSemaphoreGive(listMutex);
    }
}

void drawItemCompact(int x, int y, const char* label, String val, uint16_t color=WHITE) {
    canvas->setTextSize(1); canvas->setTextColor(GRAY); canvas->setCursor(x, y + 4); canvas->print(label);
    canvas->setTextSize(2); canvas->setTextColor(color); canvas->setCursor(x, y + 14); canvas->print(val);
}

void drawDetailScreen() {
    canvas->fillScreen(BLACK);
    
    DroneInfo t;
    bool hasData = false;
    if (xSemaphoreTake(listMutex, 50) == pdTRUE) {
        if (selectedIndex >= 0 && selectedIndex < droneList.size()) { t = droneList[selectedIndex]; hasData = true; } 
        else { currentState = STATE_LIST; }
        xSemaphoreGive(listMutex);
    }
    if (!hasData) return;

    // --- Fixed Header (60px) ---
    canvas->fillRect(0, 0, 536, 60, 0x2124);
    canvas->drawFastHLine(0, 59, 536, CYAN);
    
    canvas->setTextSize(2); canvas->setTextColor(WHITE); 
    canvas->setCursor(20, 10); 
    if(t.sn.length()>0) canvas->print(t.sn); else canvas->print(t.mac);
    
    canvas->setTextColor(GREEN); canvas->setCursor(420, 10); canvas->printf("%d dBm", t.rssi);
    canvas->setTextSize(1); canvas->setTextColor(CYAN); canvas->setCursor(20, 38); canvas->printf("MAC: %s (%s)", t.mac.c_str(), t.proto.c_str());

    // --- Static Content Area ---
    int baseY = 70;
    int gap = 38; // 紧凑行距
    int col1 = 20; int col2 = 200; int col3 = 380;

    if (detailPage == 0) { // === Page 1: Flight ===
        drawItemCompact(col1, baseY, "MODEL / TYPE", (t.uaType.length()>0 ? t.uaType : "N/A"), GREEN);
        drawItemCompact(col2, baseY, "STATUS", (t.status.length()>0 ? t.status : "N/A"), YELLOW);
        drawItemCompact(col3, baseY, "HEADING", String(t.dir) + " deg");
        
        baseY += gap;
        drawItemCompact(col1, baseY, "LATITUDE", (t.lat!=0 ? String(t.lat, 6) : "N/A"));
        drawItemCompact(col2, baseY, "LONGITUDE", (t.lon!=0 ? String(t.lon, 6) : "N/A"));
        
        baseY += gap;
        drawItemCompact(col1, baseY, "ALTITUDE (Baro)", (t.lat!=0 ? String(t.alt)+" m" : "N/A"), CYAN);
        drawItemCompact(col2, baseY, "HEIGHT (Rel)", (t.lat!=0 ? String(t.height)+" m" : "N/A"), CYAN);
        
        baseY += gap;
        drawItemCompact(col1, baseY, "SPEED H", String(t.speed_h)+" m/s");
        drawItemCompact(col2, baseY, "SPEED V", String(t.speed_v)+" m/s");

    } else if (detailPage == 1) { // === Page 2: System ===
        drawItemCompact(col1, baseY, "OPERATOR ID", (t.operatorId.length()>0 ? t.operatorId : "None"));
        baseY += gap;
        
        drawItemCompact(col1, baseY, "OP LATITUDE", (t.op_lat!=0 ? String(t.op_lat, 5) : "N/A"), GRAY);
        drawItemCompact(col2, baseY, "OP LONGITUDE", (t.op_lon!=0 ? String(t.op_lon, 5) : "N/A"), GRAY);
        baseY += gap;
        
        drawItemCompact(col1, baseY, "SELF ID (TEXT)", (t.selfIdDesc.length()>0 ? t.selfIdDesc : "None"), YELLOW);
        baseY += gap;
        
        drawItemCompact(col1, baseY, "CLASSIFICATION", (t.classification.length()>0 ? t.classification : "N/A"));

    } else { // === Page 3: Raw ===
        drawItemCompact(col1, baseY, "MSG COUNT", String(t.msgCount), CYAN);
        drawItemCompact(col2, baseY, "LAST SEEN", String((millis()-t.lastSeen)/1000)+"s ago", RED);
        baseY += gap;
        
        drawItemCompact(col1, baseY, "DEBUG BLOCKS", t.debugTypes, GRAY);
        baseY += gap;
        
        canvas->setTextSize(1); canvas->setTextColor(GRAY); canvas->setCursor(col1, baseY); canvas->print("AUTH DATA (HEX)");
        baseY += 12;
        canvas->setTextSize(1); canvas->setTextColor(YELLOW); 
        if(t.authData.length() > 0) {
            for(int i=0; i<t.authData.length() && i<120; i+=45) {
                canvas->setCursor(col1, baseY); 
                canvas->print(t.authData.substring(i, min((int)t.authData.length(), i+45)));
                baseY += 10;
            }
        } else {
            canvas->setCursor(col1, baseY); canvas->print("No Auth Data");
        }
    }

    canvas->setTextSize(1); canvas->setTextColor(GRAY); 
    canvas->setCursor(220, 225); canvas->printf("PAGE %d/3 (Tap to flip)", detailPage + 1);
}

// ================= 6. Setup & Loop =================
void setup() {
    Serial.begin(115200);
    pinMode(PIN_POWER_ON, OUTPUT); digitalWrite(PIN_POWER_ON, HIGH); delay(100);
    if (!canvas->begin()) { Serial.println("GFX Fail"); while(1); }
    canvas->setRotation(1);
    Wire.begin(TOUCH_SDA, TOUCH_SCL);
    listMutex = xSemaphoreCreateMutex();
    initBLE();
    startBLE();
}

void loop() {
    updatePhysics();
    handleTouch();
    if (currentState == STATE_LIST) drawListScreen(); else drawDetailScreen();
    drawDrawerAnimation();
    canvas->flush();
    
    // [Fix] 20秒超时移除
    static unsigned long lastClean = 0;
    if (millis() - lastClean > 1000) {
        lastClean = millis();
        if (xSemaphoreTake(listMutex, 10) == pdTRUE) {
            for (auto it = droneList.begin(); it != droneList.end(); ) {
                // 修改此处为 20000 (20秒)
                if (millis() - it->lastSeen > 20000) it = droneList.erase(it);
                else ++it;
            }
            xSemaphoreGive(listMutex);
        }
    }
    delay(20);
}