/**
 * @file ESP32_Smart_Frame.ino
 * @brief ESP32 Smart Photo Frame - Master Firmware V9.3
 * @author Your Name / GitHub Handle
 * @version 9.3
 * @date 2023-10-27
 * * FEATURES:
 * - 3.2" TFT LCD (ILI9341) + Touch (XPT2046)
 * - Animated Faces (Cat, Emo, Toxic, Iron, Heart)
 * - WiFi Weather & Time Sync (NTP)
 * - Auto-scaling Photo Gallery (SD Card)
 * - Web Interface for Configuration
 * * LIBRARIES REQUIRED:
 * - TFT_eSPI
 * - LVGL (v8.x)
 * - TJpg_Decoder
 * - ArduinoJson (v6)
 */

#include <lvgl.h>
#include <TFT_eSPI.h>
#include <WiFi.h>
#include <WebServer.h>
#include <FS.h>
#include <SD.h>
#include <SPI.h>
#include <TJpg_Decoder.h>
#include <HTTPClient.h> 
#include <WiFiClientSecure.h> 
#include <WiFiClient.h> 
#include <Preferences.h> 
#include <ArduinoJson.h> 
#include "time.h"

// =================================================================================
// 1. CONFIGURATION
// =================================================================================

#define DEFAULT_CITY    "London"
#define NTP_SERVER      "pool.ntp.org"

// Pin Definitions (ESP32 Dev Module)
#define TFT_BL      21 
#define PWM_CHANNEL 1   
#define SD_CS       5
#define SD_SCK      14
#define SD_MISO     4   
#define SD_MOSI     13
#define T_CLK       25
#define T_DIN       26 
#define T_DO        27 
#define T_CS        33 

// UI Colors (RGB565)
#define C_CAT_ORANGE  0xFA60 
#define C_EMO_CYAN    0x07FF 
#define C_MYSTIC_PURP 0x901F
#define C_TOXIC_GREEN 0x07E0 
#define C_IRON_RED    0xF800 
#define C_HEART_PINK  0xF81F 
#define C_GOLD        0xFDA0
#define C_GREY        0x8410
#define C_SKY         0x87FF

#define C_BG_INDEX    0
#define C_FG_INDEX    1 
#define C_TG_INDEX    3 

// =================================================================================
// 2. GLOBALS
// =================================================================================

TFT_eSPI tft = TFT_eSPI(); 
TFT_eSprite eyeSprite = TFT_eSprite(&tft); 
WebServer server(80);
SPIClass sdSPI(HSPI); 

// --- FILESYSTEM GLOBALS ---
File root;        
File photoRoot;   
File uploadFile;  

Preferences preferences; 
SemaphoreHandle_t dataMutex; 

bool sdAvailable = false;
bool apMode = false;
bool newDataAvailable = false;
String ssidListHTML = ""; 

char wifi_ssid[33] = "";      
char wifi_pass[64] = "";      
char geo_city[64] = DEFAULT_CITY; 
int screen_brightness = 255; 
int current_face_id = 0; 
uint32_t clock_color = 0xFFFFFF; 

// Weather Data
char city_lat[16] = "0"; char city_lon[16] = "0";
char city_name[64] = "Loading..."; 
char currentTemp[16] = "--"; char currentWind[16] = "--";
int currentWeatherCode = 0;

enum ScreenState { STATE_CLOCK, STATE_CALENDAR, STATE_WEATHER, STATE_SLIDESHOW, STATE_EYES, STATE_IP };
ScreenState currentState = STATE_CLOCK;

unsigned long lastSlideTime = 0; const int slideInterval = 4000; 
unsigned long lastEyeTime = 0; int eyeInterval = 1000; 
float currentEyeX = 0, currentEyeY = 0;
float targetEyeX = 0, targetEyeY = 0;
bool isMoving = false;
int prevMinute = -1, prevDay = -1;

// Font Declaration (Requires cifre125.c in sketch folder)
LV_FONT_DECLARE(cifre125); 

static lv_disp_draw_buf_t draw_buf;
static lv_color_t buf[320 * 20]; 
lv_obj_t * cont_clock;    
lv_obj_t * label_ceas_big;
lv_obj_t * label_data_big;
lv_obj_t * cont_calendar; 
lv_obj_t * calendar;

// =================================================================================
// 3. FORWARD DECLARATIONS
// =================================================================================
void update_timer_cb(lv_timer_t * timer);
void enter_eyes(); void exit_eyes();
void enter_slideshow(); void show_next_image();
void drawWeatherInfo(); void switch_to(ScreenState newState);
void drawSetupScreen(); void drawOnlineScreen();
void scanNetworks(); void apply_clock_color();
void update_eyes_palette();

// =================================================================================
// 4. HARDWARE DRIVERS
// =================================================================================

class SoftTouch {
  public:
    void begin() { pinMode(T_CLK, OUTPUT); pinMode(T_DIN, OUTPUT); pinMode(T_DO, INPUT); pinMode(T_CS, OUTPUT); digitalWrite(T_CS, HIGH); }
    bool getPoint(uint16_t *x, uint16_t *y) {
      if (readAxis(0xB0) < 50) return false;
      int avgX = 0, avgY = 0; for(int i=0; i<4; i++) { avgX += readAxis(0xD0); avgY += readAxis(0x90); }
      *x = map(avgX/4, 200, 3900, 0, 320); *y = 240 - map(avgY/4, 200, 3900, 0, 240); 
      return (*x > 0 && *x < 320 && *y > 0 && *y < 240);
    }
  private:
    int readAxis(uint8_t ctrl) { digitalWrite(T_CS, LOW); spiTransfer(ctrl); int val = (spiTransfer(0) << 8) | spiTransfer(0); digitalWrite(T_CS, HIGH); return (val >> 3) & 0xFFF; }
    uint8_t spiTransfer(uint8_t data) { uint8_t r = 0; for (int i=0; i<8; i++) { digitalWrite(T_DIN, (data & 0x80)); data <<= 1; digitalWrite(T_CLK, HIGH); digitalWrite(T_CLK, LOW); r <<= 1; if (digitalRead(T_DO)) r |= 1; } return r; }
} touchDriver;

void my_touchpad_read(lv_indev_drv_t * indev_driver, lv_indev_data_t * data) {
    uint16_t x, y; if (touchDriver.getPoint(&x, &y)) { data->state = LV_INDEV_STATE_PR; data->point.x = x; data->point.y = y; } else { data->state = LV_INDEV_STATE_REL; }
}
void my_disp_flush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p) {
    tft.startWrite(); tft.setAddrWindow(area->x1, area->y1, (area->x2 - area->x1 + 1), (area->y2 - area->y1 + 1)); tft.pushColors((uint16_t *)&color_p->full, (area->x2 - area->x1 + 1) * (area->y2 - area->y1 + 1), true); tft.endWrite(); lv_disp_flush_ready(disp);
}
void setBrightness(uint8_t level) { ledcWrite(PWM_CHANNEL, level); }
bool tft_output(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t* bitmap) {
   if ( y >= tft.height() ) return 0;
   tft.pushImage(x, y, w, h, bitmap);
   return 1;
}
void fadeOut() { for (int i = screen_brightness; i >= 0; i -= 25) { setBrightness(i); delay(5); } setBrightness(0); }
void fadeIn() { for (int i = 0; i <= screen_brightness; i += 25) { setBrightness(i); delay(5); } setBrightness(screen_brightness); }

// =================================================================================
// 5. HELPER LOGIC
// =================================================================================

const char* getWeatherDescription(int code) {
    if (code == 0) return "Clear Sky";
    if (code <= 3) return "Cloudy";
    if (code <= 48) return "Fog";
    if (code <= 79) return "Rain/Snow";
    return "Storm";
}

void scanNetworks() {
    int n = WiFi.scanNetworks();
    ssidListHTML = "";
    if (n == 0) ssidListHTML = "<option value='No Networks'>";
    else for (int i = 0; i < (n > 10 ? 10 : n); ++i) ssidListHTML += "<option value='" + WiFi.SSID(i) + "'>";
}

void resolveCityLocation() {
    if (WiFi.status() != WL_CONNECTED) return;
    WiFiClientSecure client; client.setInsecure(); HTTPClient http;
    http.setTimeout(5000); 
    String cityEncoded = String(geo_city); cityEncoded.replace(" ", "%20");
    String url = "https://geocoding-api.open-meteo.com/v1/search?name=" + cityEncoded + "&count=1&language=en&format=json";
    if (http.begin(client, url)) {
        if (http.GET() == 200) {
            String payload = http.getString();
            JsonDocument doc;
            if (!deserializeJson(doc, payload)) {
                if (doc["results"].size() > 0) {
                    if (xSemaphoreTake(dataMutex, portMAX_DELAY)) {
                        strlcpy(city_name, doc["results"][0]["name"] | "Unknown", sizeof(city_name));
                        snprintf(city_lat, sizeof(city_lat), "%.4f", doc["results"][0]["latitude"].as<double>());
                        snprintf(city_lon, sizeof(city_lon), "%.4f", doc["results"][0]["longitude"].as<double>());
                        xSemaphoreGive(dataMutex);
                    }
                }
            }
        }
        http.end();
    }
}

void fetchWeather() {
    if (WiFi.status() != WL_CONNECTED) return;
    char lat[16], lon[16];
    if (xSemaphoreTake(dataMutex, portMAX_DELAY)) {
        strncpy(lat, city_lat, sizeof(lat)); strncpy(lon, city_lon, sizeof(lon));
        xSemaphoreGive(dataMutex);
    }
    if(lat[0] == '0' && lat[1] == '\0') { resolveCityLocation(); return; }
    WiFiClientSecure client; client.setInsecure(); HTTPClient http;
    http.setTimeout(5000); 
    String url = "https://api.open-meteo.com/v1/forecast?latitude=" + String(lat) + "&longitude=" + String(lon) + "&current_weather=true";
    if (http.begin(client, url)) {
        if (http.GET() == 200) {
            String payload = http.getString();
            JsonDocument doc;
            if (!deserializeJson(doc, payload)) {
                JsonObject current = doc["current_weather"];
                if (current) {
                    if (xSemaphoreTake(dataMutex, portMAX_DELAY)) {
                        snprintf(currentTemp, sizeof(currentTemp), "%.1f", current["temperature"].as<double>());
                        snprintf(currentWind, sizeof(currentWind), "%.1f", current["windspeed"].as<double>());
                        currentWeatherCode = current["weathercode"];
                        newDataAvailable = true; 
                        xSemaphoreGive(dataMutex);
                    }
                }
            }
        }
        http.end();
    }
}

void networkTask(void * parameter) {
    vTaskDelay(5000 / portTICK_PERIOD_MS); 
    while(true) {
        if (WiFi.status() != WL_CONNECTED && !apMode) { WiFi.reconnect(); vTaskDelay(10000 / portTICK_PERIOD_MS); }
        if (WiFi.status() == WL_CONNECTED) {
             if (city_lat[0] == '0') resolveCityLocation();
             fetchWeather();
             vTaskDelay(1800000 / portTICK_PERIOD_MS); 
        } else { vTaskDelay(10000 / portTICK_PERIOD_MS); }
    }
}

// =================================================================================
// 6. DRAWING & ANIMATION
// =================================================================================

// --- STANDARD DRAWING FUNCTIONS ---
void drawSun(int x, int y) { tft.fillCircle(x, y, 20, C_GOLD); for (int i = 0; i < 8; i++) { float a = i * 45 * 0.017453; tft.drawLine(x + cos(a) * 26, y + sin(a) * 26, x + cos(a) * 34, y + sin(a) * 34, C_GOLD); } }
void drawCloud(int x, int y, uint16_t color) { tft.fillCircle(x, y, 15, color); tft.fillCircle(x + 15, y - 5, 20, color); tft.fillCircle(x + 30, y, 15, color); tft.fillRect(x, y, 30, 16, color); }
void drawRain(int x, int y) { drawCloud(x, y, C_GREY); tft.drawLine(x + 10, y + 20, x + 5, y + 30, C_SKY); tft.drawLine(x + 25, y + 20, x + 20, y + 30, C_SKY); }
void drawSnow(int x, int y) { drawCloud(x, y, C_GREY); tft.fillCircle(x + 10, y + 25, 2, TFT_WHITE); tft.fillCircle(x + 25, y + 25, 2, TFT_WHITE); }
void drawStorm(int x, int y) { drawCloud(x, y, 0x3186); tft.fillTriangle(x + 15, y + 20, x + 10, y + 35, x + 20, y + 30, C_GOLD); tft.fillTriangle(x + 20, y + 30, x + 12, y + 45, x + 25, y + 35, C_GOLD); }
void drawWeatherIcon(int code, int x, int y) { if (code <= 1) drawSun(x + 20, y + 10); else if (code <= 3) drawCloud(x, y, TFT_WHITE); else if (code <= 48) drawCloud(x, y, C_GREY); else if (code <= 69) drawRain(x, y); else if (code <= 79) drawSnow(x, y); else if (code >= 95) drawStorm(x, y); else drawCloud(x, y, TFT_WHITE); }

void drawWeatherInfo() {
    char temp[16], wind[16]; int code; const char* desc;
    if (xSemaphoreTake(dataMutex, portMAX_DELAY)) { strncpy(temp, currentTemp, sizeof(temp)); strncpy(wind, currentWind, sizeof(wind)); code = currentWeatherCode; desc = getWeatherDescription(code); newDataAvailable = false; xSemaphoreGive(dataMutex); } else return;
    tft.fillScreen(TFT_BLACK); drawWeatherIcon(code, 135, 30);
    tft.setTextColor(TFT_WHITE, TFT_BLACK); tft.setTextDatum(MC_DATUM); tft.setTextSize(3); 
    char bufT[20]; snprintf(bufT, sizeof(bufT), "%s C", temp); tft.drawString(bufT, 160, 120, 4); 
    tft.drawFastHLine(60, 160, 200, 0x52AA); tft.setTextColor(C_GOLD, TFT_BLACK); tft.setTextSize(1); tft.drawString(desc, 160, 185, 4);
    tft.setTextColor(C_SKY, TFT_BLACK); char bufW[32]; snprintf(bufW, sizeof(bufW), "Wind: %s km/h", wind); tft.drawString(bufW, 160, 215, 4);
}

// --- FACES (FULL MASTER LOGIC) ---
void update_eyes_palette() {
    if (!eyeSprite.created()) return;
    uint16_t primaryColor = C_CAT_ORANGE;
    if (current_face_id == 1) primaryColor = C_EMO_CYAN;
    if (current_face_id == 2) primaryColor = C_MYSTIC_PURP;
    if (current_face_id == 3) primaryColor = C_TOXIC_GREEN;
    if (current_face_id == 4) primaryColor = C_IRON_RED;
    if (current_face_id == 5) primaryColor = C_HEART_PINK;
    
    uint16_t p[16]; 
    p[0]=TFT_BLACK; 
    p[1]=primaryColor; 
    p[2]=TFT_WHITE; 
    p[3]=primaryColor; 
    eyeSprite.createPalette(p, 16);
}

void draw_single_eye_content(int w, int h) {
    int cX = w/2;
    // Face 1: Emo (Rounded Square)
    if (current_face_id == 1) {
        int r = (w < 30) ? w / 2 : 15; 
        eyeSprite.fillRoundRect(0, 0, w, 100, r, C_FG_INDEX); 
        return;
    }
    // Face 3: Toxic (Plus Sign)
    if (current_face_id == 3) {
        int r = (w < 16) ? w / 2 : 8;
        eyeSprite.fillRoundRect(0, 0, w, 100, r, C_FG_INDEX);
        if (w > 35) { 
            int pSize = 12, pLen = 30, cY = 50; 
            eyeSprite.fillRect(cX - pSize/2, cY - pLen/2, pSize, pLen, C_BG_INDEX);
            eyeSprite.fillRect(cX - pLen/2, cY - pSize/2, pLen, pSize, C_BG_INDEX);
        }
        return;
    }
    // Face 5: Heart Eyes
    if (current_face_id == 5) {
        int r = 12; int hTopY = 30;
        if (w > 25) {
            eyeSprite.fillCircle(cX - r + 1, hTopY, r, C_FG_INDEX);
            eyeSprite.fillCircle(cX + r - 1, hTopY, r, C_FG_INDEX);
            eyeSprite.fillTriangle(cX - (r*2) + 3, hTopY + 3, cX + (r*2) - 3, hTopY + 3, cX, hTopY + r + 20, C_FG_INDEX);
        } else {
            eyeSprite.fillRect(cX - 2, 20, 4, 60, C_FG_INDEX);
        }
        return;
    }
    // Face 0, 2, 4: Standard Slits
    if (current_face_id == 2) eyeSprite.fillRoundRect(cX - 9, 10, 18, 80, 6, C_FG_INDEX); 
    else eyeSprite.fillRect(cX - 6, 10, 12, 80, C_FG_INDEX); 
}

void draw_eyes(int leftH, int rightH, float x_offset, float y_offset, int tongueH) {
    if (!eyeSprite.created()) return;
    eyeSprite.fillSprite(C_BG_INDEX); 
    
    // INCREASED HEIGHT to 170px to fit whiskers
    int cY = 85 + (int)y_offset; // Centered vertically in 170px sprite
    int cX = 160 + (int)x_offset; 
    
    // Tongue (If applicable)
    if (tongueH > 0) {
        int tW = 30; int tX = cX - (tW/2); 
        eyeSprite.fillRoundRect(tX, cY + 55, tW, tongueH, 10, C_TG_INDEX);
        eyeSprite.drawFastVLine(cX, cY + 60, tongueH - 10, C_BG_INDEX);
    }

    int eyeWidth = 60;
    int leftXbase = cX - 60 - (eyeWidth/2);
    int rightXbase = cX + 60 - (eyeWidth/2);

    // Left Eye
    eyeSprite.setViewport(leftXbase, cY - 50, eyeWidth, 100); 
    draw_single_eye_content(eyeWidth, 100); 
    eyeSprite.resetViewport();

    // Right Eye
    eyeSprite.setViewport(rightXbase, cY - 50, eyeWidth, 100); 
    draw_single_eye_content(eyeWidth, 100); 
    eyeSprite.resetViewport();

    // Lids (Blinking)
    int lLidH = (90 - leftH) / 2;
    eyeSprite.fillRect(leftXbase, cY - 45, eyeWidth, lLidH, C_BG_INDEX); 
    eyeSprite.fillRect(leftXbase, cY + 45 - lLidH, eyeWidth, lLidH, C_BG_INDEX); 
    int rLidH = (90 - rightH) / 2;
    eyeSprite.fillRect(rightXbase, cY - 45, eyeWidth, rLidH, C_BG_INDEX); 
    eyeSprite.fillRect(rightXbase, cY + 45 - rLidH, eyeWidth, rLidH, C_BG_INDEX); 

    // Face 4: Iron Cat Brows (FIXED: Separate thick angled lines above eyes)
    if (current_face_id == 4) {
        // Left Brow (thick angled line down towards nose)
        eyeSprite.fillTriangle(leftXbase - 10, cY - 65, leftXbase + eyeWidth - 15, cY - 48, leftXbase + eyeWidth - 5, cY - 65, C_FG_INDEX);
        // Right Brow (mirrored)
        eyeSprite.fillTriangle(rightXbase + eyeWidth + 10, cY - 65, rightXbase + 15, cY - 48, rightXbase + 5, cY - 65, C_FG_INDEX);
    }

    // Nose & Whiskers (Face 0, 2, 5 ONLY - EXCLUDING 4)
    if (current_face_id == 0 || current_face_id == 2 || current_face_id == 5) {
        int noseY = cY + 30; 
        eyeSprite.fillTriangle(cX - 18, noseY, cX + 18, noseY, cX, noseY + 20, C_FG_INDEX);
        eyeSprite.fillTriangle(cX - 12, noseY + 3, cX + 12, noseY + 3, cX, noseY + 15, C_BG_INDEX); 

        // Whiskers (Animated Sine Wave)
        int wY = noseY + 10 + (int)(2 * sin(millis() / 400.0));
        eyeSprite.fillRect(cX - 120, wY, 35, 4, C_FG_INDEX); 
        eyeSprite.fillRect(cX - 120, wY + 20, 35, 4, C_FG_INDEX);
        eyeSprite.fillRect(cX + 85, wY, 35, 4, C_FG_INDEX); 
        eyeSprite.fillRect(cX + 85, wY + 20, 35, 4, C_FG_INDEX); 
    }
    
    eyeSprite.pushSprite(0, 35);
}

void update_eyes() {
    unsigned long now = millis();
    if (now - lastEyeTime > eyeInterval) {
        int choice = random(0, 100); 
        int openSize = 80; int closeSize = 4; 
        
        if (choice < 15) { // Blink
            isMoving = true;
            for (int h = openSize; h >= closeSize; h -= 30) draw_eyes(h, h, currentEyeX, currentEyeY, 0);
            draw_eyes(closeSize, closeSize, currentEyeX, currentEyeY, 0); delay(100); 
            for (int h = closeSize; h <= openSize; h += 20) draw_eyes(h, h, currentEyeX, currentEyeY, 0);
            eyeInterval = random(800, 2500); isMoving = false;
        } 
        else if (choice < 30) { // Wink
            isMoving = true;
            bool winkLeft = random(0,2);
            for (int h = openSize; h >= closeSize; h -= 25) draw_eyes(winkLeft?h:openSize, winkLeft?openSize:h, currentEyeX, currentEyeY, 0);
            delay(300); 
            for (int h = closeSize; h <= openSize; h += 20) draw_eyes(winkLeft?h:openSize, winkLeft?openSize:h, currentEyeX, currentEyeY, 0);
            eyeInterval = random(1000, 3000); isMoving = false;
        }
        else if (choice < 40) { // Tongue/Squint
            isMoving = true;
            for (int t = 0; t <= 35; t+=5) { draw_eyes(map(t, 0, 35, 80, 30), map(t, 0, 35, 80, 30), currentEyeX, currentEyeY, t); delay(20); }
            delay(800); 
            for (int t = 35; t >= 0; t-=5) { draw_eyes(map(t, 0, 35, 80, 30), map(t, 0, 35, 80, 30), currentEyeX, currentEyeY, t); delay(20); }
            eyeInterval = random(2000, 5000); isMoving = false;
        }
        else if (choice < 80) { // Look Random
            targetEyeX = random(-30, 31); targetEyeY = random(-20, 21);
            isMoving = true; eyeInterval = random(1500, 4000); 
        } else {
             isMoving = false; eyeInterval = random(1000, 2000);
        }
        lastEyeTime = now;
    }

    if (isMoving) {
        currentEyeX += (targetEyeX - currentEyeX) * 0.3; 
        currentEyeY += (targetEyeY - currentEyeY) * 0.3; 
        if (abs(targetEyeX - currentEyeX) < 0.5 && abs(targetEyeY - currentEyeY) < 0.5) isMoving = false;
    } else {
        currentEyeX += random(-5, 6) * 0.05; 
        currentEyeY += random(-5, 6) * 0.05; 
        currentEyeX *= 0.95; 
        currentEyeY *= 0.95;
    }
    draw_eyes(80, 80, currentEyeX, currentEyeY, 0);
}

void show_next_image() { 
    if(!sdAvailable) {
        tft.fillScreen(TFT_BLACK);
        tft.setTextColor(TFT_RED, TFT_BLACK); tft.drawCentreString("SD Card Failed!", 160, 110, 2);
        return; 
    }
    
    if(!SD.exists("/photos")) { 
        SD.mkdir("/photos"); 
        tft.fillScreen(TFT_BLACK);
        tft.setTextColor(TFT_WHITE, TFT_BLACK); tft.drawCentreString("Created /photos", 160, 100, 2);
        tft.setTextColor(TFT_GREEN, TFT_BLACK); tft.drawCentreString("Upload pics via WiFi!", 160, 140, 2);
        return; 
    }
    
    if(!photoRoot) photoRoot = SD.open("/photos");
    if(!photoRoot) return;

    int attempts = 0; bool found = false; 
    while (attempts < 30 && !found) { 
        File file = photoRoot.openNextFile(); 
        if (!file) { 
            photoRoot.rewindDirectory(); 
            file = photoRoot.openNextFile(); 
        } 
        
        if (file) { 
            String fname = file.name(); 
            if (!file.isDirectory() && !fname.startsWith("._") && (fname.endsWith(".jpg") || fname.endsWith(".JPG"))) { 
                String fullPath = "/photos/" + fname;
                
                // --- AUTO ZOOM LOGIC ---
                uint16_t w = 0, h = 0;
                TJpgDec.getJpgSize(&w, &h, fullPath.c_str());
                uint8_t scale = 1;
                if(w > 320 || h > 240) {
                    if(w > 640 || h > 480) scale = 4; 
                    else scale = 2; 
                }
                TJpgDec.setJpgScale(scale);
                // -----------------------

                TJpgDec.drawSdJpg(0, 0, fullPath.c_str()); 
                found = true; 
            } 
            file.close(); 
            if(found) return; 
        } else {
             tft.fillScreen(TFT_BLACK);
             tft.setTextColor(TFT_ORANGE, TFT_BLACK); tft.drawCentreString("No Photos Found!", 160, 100, 2);
             tft.setTextColor(TFT_CYAN, TFT_BLACK); tft.drawCentreString("Upload via WiFi", 160, 140, 2);
             return;
        }
        attempts++; 
    } 
}

void enter_eyes() { 
    if (!eyeSprite.created()) { 
        eyeSprite.setColorDepth(4); 
        // 320x170 fits all faces safely in memory
        if(!eyeSprite.createSprite(320, 170)) {
            tft.setTextColor(TFT_RED);
            tft.drawString("Mem Err", 0, 0); 
            return;
        }
    } 
    update_eyes_palette(); 
    currentEyeX = 0; currentEyeY = 0; 
    targetEyeX = 0; targetEyeY = 0; 
    tft.fillScreen(TFT_BLACK); 
    draw_eyes(80, 80, 0, 0, 0); 
    lastEyeTime = millis(); 
}

void exit_eyes() { if (eyeSprite.created()) eyeSprite.deleteSprite(); }
void enter_slideshow() { 
    if(sdAvailable) { 
        if(!photoRoot) photoRoot = SD.open("/photos"); 
        show_next_image(); 
        lastSlideTime = millis(); 
    } 
}

void drawSetupScreen() { tft.fillScreen(TFT_RED); tft.setTextColor(TFT_WHITE, TFT_RED); tft.setTextSize(2); tft.drawCentreString("1. Connect WiFi:", 160, 40, 1); tft.setTextColor(TFT_YELLOW, TFT_RED); tft.drawCentreString("ESP32-Frame", 160, 70, 1); tft.setTextColor(TFT_WHITE, TFT_RED); tft.drawCentreString("2. Go to:", 160, 130, 1); tft.setTextColor(TFT_CYAN, TFT_RED); tft.setTextSize(3); tft.drawCentreString("192.168.4.1", 160, 160, 1); }
void drawOnlineScreen() { tft.fillScreen(TFT_BLACK); tft.setTextColor(TFT_WHITE, TFT_BLACK); tft.setTextSize(2); tft.drawCentreString("Device Online", 160, 60, 1); tft.setTextColor(TFT_CYAN, TFT_BLACK); tft.setTextSize(3); tft.drawCentreString(WiFi.localIP().toString(), 160, 110, 1); tft.setTextSize(1); tft.setTextColor(C_GREY, TFT_BLACK); tft.drawCentreString("Type this IP in your browser", 160, 160, 4); tft.drawCentreString("Tap screen to exit", 160, 200, 2); }

void switch_to(ScreenState newState) {
    if (currentState == newState && newState != STATE_IP) return;
    if (newState != STATE_IP) fadeOut();
    if (currentState == STATE_EYES) exit_eyes();
    lv_obj_add_flag(cont_clock, LV_OBJ_FLAG_HIDDEN); lv_obj_add_flag(cont_calendar, LV_OBJ_FLAG_HIDDEN);
    currentState = newState; tft.fillScreen(TFT_BLACK);
    if (currentState == STATE_CLOCK) { lv_obj_clear_flag(cont_clock, LV_OBJ_FLAG_HIDDEN); prevMinute = -1; lv_timer_handler(); }
    else if (currentState == STATE_CALENDAR) { lv_obj_clear_flag(cont_calendar, LV_OBJ_FLAG_HIDDEN); prevDay = -1; lv_timer_handler(); }
    else if (currentState == STATE_WEATHER) { drawWeatherInfo(); }
    else if (currentState == STATE_EYES) enter_eyes();
    else if (currentState == STATE_SLIDESHOW) enter_slideshow();
    else if (currentState == STATE_IP) { if(apMode) drawSetupScreen(); else drawOnlineScreen(); }
    if (!apMode) fadeIn(); else setBrightness(screen_brightness); 
}

// =================================================================================
// 7. WEB SERVER & UI CONTROL
// =================================================================================

const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html><html><head><meta name="viewport" content="width=device-width, initial-scale=1">
<title>Frame Studio</title>
<style>
body{font-family:'Segoe UI',sans-serif;background:#1a1a1a;color:#fff;text-align:center;padding:5px;margin:0}
.card{background:#2d2d2d;padding:15px;margin:10px auto;border-radius:12px;max-width:420px}
button{background:#00e5ff;color:#000;padding:10px;border:none;border-radius:6px;width:30%;margin:2px;cursor:pointer}
input,select{padding:10px;width:90%;margin:5px;background:#444;border:none;color:#fff;border-radius:6px}
</style>
</head><body>
<div class="card">
  <h3>Control</h3>
  <button onclick="s(0)">Clock</button><button onclick="s(1)">Calendar</button>
  <button onclick="s(2)">Weather</button><button onclick="s(3)">Slideshow</button>
  <button onclick="s(4)">Faces</button>
</div>
<div class="card">
  <h3>Choose Face</h3>
  <select onchange="sf(this.value)">
    <option value="0">Cat (Orange)</option>
    <option value="1">Emo (Blue)</option>
    <option value="2">Mystic (Purple)</option>
    <option value="3">Toxic (Green)</option>
    <option value="4">Iron (Red)</option>
    <option value="5">Heart (Pink)</option>
  </select>
</div>
<div class="card">
  <h3>WiFi & Location</h3>
  <form action="/save" method="POST">
    <input list="ssidList" name="ssid" placeholder="Select or Type SSID">
    <datalist id="ssidList">%SSID_LIST%</datalist>
    <input type="text" name="pass" placeholder="WiFi Password">
    <input type="text" name="city" placeholder="City Name (e.g. London)">
    <button style="width:90%;background:#4f4">Save & Restart</button>
  </form>
</div>
<div class="card">
  <h3>Photo Gallery (in /photos)</h3>
  <form method="POST" action="/upload" enctype="multipart/form-data">
    <input type="file" name="update"><button style="width:90%;background:#4f4">Upload</button>
  </form>
  <div style="margin-top:10px">%GALLERY_CONTENT%</div>
</div>
<div class="card">
  <h3>Settings</h3>
  <label>Clock Color:</label><input type="color" value="#%CLR%" onchange="fetch('/set_color?val='+this.value.substring(1))">
  <br><br><label>Brightness:</label>
  <form action="/brightness" method="POST"><input type="range" name="val" min="10" max="255" value="%BRIGHTNESS%" onchange="this.form.submit()"></form>
</div>
<script>
function s(id){fetch('/set_state?id='+id)}
function sf(id){fetch('/set_face?id='+id)}
</script></body></html>)rawliteral";

void handleRoot() { 
    String page = index_html; 
    page.replace("%BRIGHTNESS%", String(screen_brightness));
    char hexCol[8]; sprintf(hexCol, "%06X", clock_color); page.replace("%CLR%", String(hexCol));
    page.replace("%SSID_LIST%", ssidListHTML);

    String gallery = "";
    if (sdAvailable) {
        File folder = SD.open("/photos");
        if(folder) {
          while (true) {
              File entry = folder.openNextFile();
              if (!entry) break;
              String fname = entry.name();
              if (!entry.isDirectory() && !fname.startsWith("._") && (fname.endsWith(".jpg") || fname.endsWith(".JPG"))) {
                  String path = "/photos/" + fname; 
                  gallery += "<div style='display:inline-block;margin:5px'><img src='" + path + "' style='width:60px;height:60px;object-fit:cover'><br><a href='/delete?path=" + path + "'><button style='background:#f44;font-size:10px;padding:5px'>Del</button></a></div>";
              }
              entry.close();
          }
          folder.close();
        }
    } else { gallery = "<p>SD Not Found</p>"; }
    page.replace("%GALLERY_CONTENT%", gallery);
    server.send(200, "text/html", page); 
}

void handleSaveSettings() { 
    if (server.hasArg("ssid")) { 
        preferences.begin("config", false); 
        preferences.putString("ssid", server.arg("ssid")); 
        preferences.putString("pass", server.arg("pass")); 
        preferences.putString("city", server.arg("city")); 
        preferences.end();
        server.send(200, "text/plain", "Saved. Restarting...");
        delay(500); 
        ESP.restart(); 
    } else {
        server.send(400, "text/plain", "Missing args");
    }
}

void handleSetColor() {
    if (server.hasArg("val")) {
        clock_color = strtoul(server.arg("val").c_str(), NULL, 16);
        preferences.begin("config", false); preferences.putUInt("color", clock_color); preferences.end();
        apply_clock_color();
    }
    server.send(200, "text/plain", "OK");
}
void handleSetState() { if (server.hasArg("id")) switch_to((ScreenState)server.arg("id").toInt()); server.send(200, "text/plain", "OK"); }
void handleSetFace() { 
    if (server.hasArg("id")) {
        current_face_id = server.arg("id").toInt();
        update_eyes_palette();
        // Force redraw if current state is EYES
        if (currentState == STATE_EYES) { 
            tft.fillScreen(TFT_BLACK); 
            draw_eyes(80, 80, currentEyeX, currentEyeY, 0); 
        }
    }
    server.send(200, "text/plain", "OK"); 
}
void handleBrightness() { if (server.hasArg("val")) { screen_brightness = server.arg("val").toInt(); setBrightness(screen_brightness); preferences.begin("config", false); preferences.putInt("bright", screen_brightness); preferences.end(); } server.sendHeader("Location", "/"); server.send(303); }
void handleFileUpload() { HTTPUpload& upload = server.upload(); if (upload.status == UPLOAD_FILE_START) { if(sdAvailable && !SD.exists("/photos")) SD.mkdir("/photos"); String f = "/photos/" + upload.filename; if(SD.exists(f)) SD.remove(f); uploadFile = SD.open(f, FILE_WRITE); } else if (upload.status == UPLOAD_FILE_WRITE && uploadFile) uploadFile.write(upload.buf, upload.currentSize); else if (upload.status == UPLOAD_FILE_END && uploadFile) { uploadFile.close(); server.sendHeader("Location", "/"); server.send(303); } }
void handleDelete() { if (server.hasArg("path")) { String path = server.arg("path"); if (SD.exists(path)) SD.remove(path); } server.sendHeader("Location", "/"); server.send(303); }
bool handleFileRead(String path) { 
    if (path.endsWith("/")) path += "index.htm"; 
    if (sdAvailable && SD.exists(path)) { 
        File file = SD.open(path, "r"); 
        String contentType = "text/plain"; 
        if (path.endsWith(".jpg") || path.endsWith(".JPG")) contentType = "image/jpeg"; 
        server.streamFile(file, contentType); 
        file.close(); 
        return true; 
    } 
    return false; 
}

void initWebServer() {
    server.on("/", HTTP_GET, handleRoot); 
    server.on("/brightness", HTTP_POST, handleBrightness);
    server.on("/set_state", HTTP_GET, handleSetState);
    server.on("/set_face", HTTP_GET, handleSetFace);
    server.on("/set_color", HTTP_GET, handleSetColor);
    server.on("/save", HTTP_POST, handleSaveSettings); 
    server.on("/upload", HTTP_POST, [](){ server.send(200); }, handleFileUpload); 
    server.on("/delete", HTTP_GET, handleDelete); 
    server.onNotFound([]() { if (!handleFileRead(server.uri())) server.send(404, "text/plain", "Not Found"); });
    server.begin();
}

// =================================================================================
// 8. SETUP & LOOP
// =================================================================================

void update_timer_cb(lv_timer_t * timer) {
    if (currentState != STATE_CLOCK && currentState != STATE_CALENDAR) return; 
    struct tm timeinfo; if(!getLocalTime(&timeinfo)) return; 
    
    if (timeinfo.tm_min != prevMinute) {
        prevMinute = timeinfo.tm_min; 
        char t[10], d[20]; 
        sprintf(t, "%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min); 
        sprintf(d, "%02d/%02d/%d", timeinfo.tm_mday, timeinfo.tm_mon+1, timeinfo.tm_year+1900);
        lv_label_set_text(label_ceas_big, t); 
        lv_label_set_text(label_data_big, d);
    }
    
    if (timeinfo.tm_mday != prevDay) {
        prevDay = timeinfo.tm_mday; 
        lv_calendar_set_today_date(calendar, timeinfo.tm_year+1900, timeinfo.tm_mon+1, timeinfo.tm_mday); 
        lv_calendar_set_showed_date(calendar, timeinfo.tm_year+1900, timeinfo.tm_mon+1);
    }
}

void apply_clock_color() {
    lv_obj_set_style_text_color(label_ceas_big, lv_color_hex(clock_color), 0);
    lv_obj_set_style_text_color(label_data_big, lv_color_hex(clock_color), 0);
}

void build_ui() {
    lv_obj_t * scr = lv_scr_act(); lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    static lv_style_t style_big; lv_style_init(&style_big); lv_style_set_text_font(&style_big, &cifre125); lv_style_set_text_color(&style_big, lv_color_white());
    
    cont_clock = lv_obj_create(scr); lv_obj_set_size(cont_clock, 320, 240); lv_obj_set_style_bg_color(cont_clock, lv_color_black(), 0); lv_obj_set_style_border_width(cont_clock, 0, 0);
    
    label_ceas_big = lv_label_create(cont_clock); lv_obj_add_style(label_ceas_big, &style_big, 0); lv_label_set_text(label_ceas_big, "00:00"); lv_obj_align(label_ceas_big, LV_ALIGN_CENTER, 0, -20);
    label_data_big = lv_label_create(cont_clock); lv_label_set_text(label_data_big, "Wait..."); lv_obj_align(label_data_big, LV_ALIGN_CENTER, 0, 55); lv_obj_set_style_text_color(label_data_big, lv_palette_main(LV_PALETTE_YELLOW), 0);
    
    cont_calendar = lv_obj_create(scr); lv_obj_set_size(cont_calendar, 320, 240); lv_obj_set_style_bg_color(cont_calendar, lv_color_black(), 0); lv_obj_add_flag(cont_calendar, LV_OBJ_FLAG_HIDDEN);
    calendar = lv_calendar_create(cont_calendar); lv_obj_set_size(calendar, 320, 240); lv_obj_align(calendar, LV_ALIGN_CENTER, 0, 0); 
    lv_calendar_header_arrow_create(calendar); 
    
    apply_clock_color();
}

void initDisplay() {
    tft.init(); tft.setRotation(3); tft.fillScreen(TFT_BLACK); tft.setSwapBytes(false); tft.invertDisplay(false);
    pinMode(TFT_BL, OUTPUT); ledcSetup(PWM_CHANNEL, 5000, 8); ledcAttachPin(TFT_BL, PWM_CHANNEL);
    preferences.begin("config", true); 
    screen_brightness = preferences.getInt("bright", 255); 
    clock_color = preferences.getUInt("color", 0xFFFFFF);
    preferences.end(); 

    setBrightness(screen_brightness); 
    tft.setTextColor(TFT_WHITE, TFT_BLACK); tft.drawCentreString("Booting...", 160, 110, 2);
    TJpgDec.setJpgScale(1); TJpgDec.setSwapBytes(true); TJpgDec.setCallback(tft_output);
}

void initFilesystem() {
    sdSPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
    if (!SD.begin(SD_CS, sdSPI)) { sdAvailable = false; Serial.println("SD Fail"); } 
    else { 
        sdAvailable = true; 
        root = SD.open("/"); 
        // Auto-create photos folder
        if(!SD.exists("/photos")) SD.mkdir("/photos");
        // Open the global handle for the slideshow loop
        photoRoot = SD.open("/photos");
        Serial.println("SD OK"); 
    }
}

void initNetwork() {
    preferences.begin("config", true); 
    String s = preferences.getString("ssid", ""); strlcpy(wifi_ssid, s.c_str(), sizeof(wifi_ssid));
    String p = preferences.getString("pass", ""); strlcpy(wifi_pass, p.c_str(), sizeof(wifi_pass));
    preferences.end();

    if (strlen(wifi_ssid) == 0) { apMode = true; } 
    else {
        tft.drawCentreString("Connecting WiFi...", 160, 110, 2);
        WiFi.begin(wifi_ssid, wifi_pass);
        int i=0; while(WiFi.status()!=WL_CONNECTED && i++<20) delay(500);
        if(WiFi.status()==WL_CONNECTED) { apMode=false; configTime(7200, 3600, NTP_SERVER); } else apMode=true;
    }
    if (apMode) { scanNetworks(); WiFi.softAP("ESP32-Frame"); switch_to(STATE_IP); }
}

void setup() {
    Serial.begin(115200); touchDriver.begin(); dataMutex = xSemaphoreCreateMutex(); 
    initFilesystem(); 
    initDisplay(); 
    
    lv_init(); lv_disp_draw_buf_init(&draw_buf, buf, NULL, 320 * 20);
    static lv_disp_drv_t disp_drv; lv_disp_drv_init(&disp_drv); disp_drv.hor_res = 320; disp_drv.ver_res = 240; disp_drv.flush_cb = my_disp_flush; disp_drv.draw_buf = &draw_buf; lv_disp_drv_register(&disp_drv);
    static lv_indev_drv_t indev_drv; lv_indev_drv_init(&indev_drv); indev_drv.type = LV_INDEV_TYPE_POINTER; indev_drv.read_cb = my_touchpad_read; lv_indev_drv_register(&indev_drv);
    
    build_ui(); initNetwork(); initWebServer(); lv_timer_create(update_timer_cb, 1000, NULL);
    if (!apMode) xTaskCreatePinnedToCore(networkTask, "NetTask", 8192, NULL, 1, NULL, 0);
}

void loop() {
    server.handleClient(); if (apMode) return;
    uint16_t x, y;
    if (touchDriver.getPoint(&x, &y)) {
        if (currentState == STATE_CLOCK) switch_to(STATE_CALENDAR);
        else if (currentState == STATE_CALENDAR) switch_to(STATE_WEATHER);
        else if (currentState == STATE_WEATHER) switch_to(STATE_SLIDESHOW);
        else if (currentState == STATE_SLIDESHOW) switch_to(STATE_EYES);
        else if (currentState == STATE_EYES) switch_to(STATE_IP);
        else if (currentState == STATE_IP) switch_to(STATE_CLOCK);       
        delay(500); 
    }
    
    if (currentState == STATE_CLOCK || currentState == STATE_CALENDAR) lv_timer_handler(); 
    if (currentState == STATE_WEATHER && newDataAvailable) drawWeatherInfo();
    if (currentState == STATE_SLIDESHOW) { if (millis() - lastSlideTime > slideInterval) { lastSlideTime = millis(); show_next_image(); } } 
    else if (currentState == STATE_EYES) { update_eyes(); }
    delay(5);
}