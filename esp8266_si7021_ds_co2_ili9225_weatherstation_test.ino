#include <TFT_eSPI.h>
#include <SPI.h>
#include <Wire.h>
#include <SI7021.h>
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClientSecureBearSSL.h>
#include <ArduinoJson.h>
#include <ArduinoUZlib.h>
#include <WiFiUdp.h>
#include <TimeLib.h>
#include "chinesefont.h"

// ==================== 引脚定义 ====================
#define SDA_PIN D4
#define SCL_PIN D3
#define BUTTON_PIN D0

// ==================== WiFi配置 ====================
const char* ssid = "onepiece";
const char* password = "89023510";

// ==================== 和风天气配置 ====================
String weather_city_code = "101120101";
String weather_api_key = "834df835928347f1b227c21387ad079e";
String current_weather_url = "https://devapi.qweather.com/v7/weather/now?key=" + weather_api_key + "&location=" + weather_city_code;
String forecast_weather_url = "https://devapi.qweather.com/v7/weather/3d?key=" + weather_api_key + "&location=" + weather_city_code;

// ==================== GZIP解压缓冲区 ====================
#define IN_BUF_SIZE 3072
uint8_t in_buf[IN_BUF_SIZE] = { 0 };

// ==================== 屏幕对象 ====================
TFT_eSPI tft = TFT_eSPI();

// ==================== 温湿度传感器 ====================
SI7021 si7021;

// ==================== NTP时间同步 ====================
WiFiUDP ntpUDP;
unsigned long lastNtpSync = 0;
unsigned long currentEpoch = 0;

// ==================== 传感器数据 ====================
float temperature = 0.0;
float humidity = 0.0;

// ==================== 天气数据 ====================
String weather_text = "晴";
int weather_temp = 25;
int weather_humidity = 60;
String wind_dir = "南风";
String wind_scale = "2";
String weather_code = "100";

// ==================== 三天天气预报 ====================
String forecast_date[3] = { "N/A", "N/A", "N/A" };
String forecast_text_day[3] = { "N/A", "N/A", "N/A" };
String forecast_text_night[3] = { "N/A", "N/A", "N/A" };
int forecast_temp_max[3] = { 0, 0, 0 };
int forecast_temp_min[3] = { 0, 0, 0 };
String forecast_code_day[3] = { "999", "999", "999" };
String forecast_code_night[3] = { "999", "999", "999" };

// ==================== 时间变量 ====================
unsigned long lastSensorRead = 0;
unsigned long lastWeatherUpdate = 0;
unsigned long lastTimeUpdate = 0;
const long sensorInterval = 10000;
const long weatherInterval = 300000;
const long timeInterval = 1000;

// ==================== 屏幕相关 ====================
bool showForecast = false;
unsigned long lastButtonPress = 0;
const long buttonDebounce = 300;

// ==================== 屏幕尺寸 ====================
const int SCREEN_WIDTH = 176;
const int SCREEN_HEIGHT = 220;
const int TOP_AREA_HEIGHT = 50;
const int DIVIDER_Y = TOP_AREA_HEIGHT;

// ==================== 缓存当前显示的时间字符串 ====================
String cachedDate = "";
String cachedTime = "";

// ==========================================
// 汉字显示函数
// ==========================================
void drawHz(int x, int y, const unsigned char* data, uint16_t color) {
  for (int i = 0; i < FONT_SIZE; i++) {
    for (int j = 0; j < 8; j++) {
      if (pgm_read_byte(&data[i * 2]) & (0x80 >> j)) {
        tft.drawPixel(x + j, y + i, color);
      }
    }
    for (int j = 0; j < 4; j++) {
      if (pgm_read_byte(&data[i * 2 + 1]) & (0x80 >> j)) {
        tft.drawPixel(x + 8 + j, y + i, color);
      }
    }
  }
}

int drawChineseChar(int x, int y, const char* str, uint16_t color) {
  for (int i = 0; i < FONT_COUNT; i++) {
    if (strcmp(str, chineseFont[i].ch) == 0) {
      drawHz(x, y, chineseFont[i].data, color);
      return FONT_SIZE;
    }
  }
  return FONT_SIZE;
}

void showChineseString(int x, int y, const char* str, uint16_t color) {
  int cursorX = x;
  while (*str) {
    if (*str < 0x80) {
      tft.setCursor(cursorX, y);
      tft.setTextColor(color);
      tft.print(*str);
      cursorX += 6;
      str++;
    } else {
      char temp[4] = { 0 };
      strncpy(temp, str, 3);
      cursorX += drawChineseChar(cursorX, y, temp, color);
      str += 3;
    }
  }
}

// ==================== 进度条显示函数 ====================
void showProgressBar(String message, int progress) {
  tft.fillScreen(TFT_BLACK);

  showChineseString(20, 60, message.c_str(), TFT_CYAN);

  int barWidth = SCREEN_WIDTH - 40;
  int barHeight = 10;
  int barX = 20;
  int barY = 90;

  tft.drawRect(barX, barY, barWidth, barHeight, TFT_WHITE);

  int fillWidth = (barWidth * progress) / 100;
  if (fillWidth > 0) {
    tft.fillRect(barX, barY, fillWidth, barHeight, TFT_GREEN);
  }

  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(1);
  tft.setCursor(barX + barWidth / 2 - 10, barY + barHeight + 10);
  tft.print(progress);
  tft.print("%");
}

// ==================== NTP时间同步 ====================
void syncTime() {
  Serial.println("\n========== NTP时间同步开始 ==========");

  const char* ntpServer = "pool.ntp.org";
  const int ntpPort = 123;
  const int NTP_PACKET_SIZE = 48;
  byte packetBuffer[NTP_PACKET_SIZE];

  WiFiUDP udp;
  udp.begin(ntpPort);

  memset(packetBuffer, 0, NTP_PACKET_SIZE);
  packetBuffer[0] = 0b11100011;

  udp.beginPacket(ntpServer, ntpPort);
  udp.write(packetBuffer, NTP_PACKET_SIZE);
  udp.endPacket();

  delay(1000);

  if (udp.parsePacket() == NTP_PACKET_SIZE) {
    udp.read(packetBuffer, NTP_PACKET_SIZE);
    unsigned long highWord = word(packetBuffer[40], packetBuffer[41]);
    unsigned long lowWord = word(packetBuffer[42], packetBuffer[43]);
    unsigned long secsSince1900 = highWord << 16 | lowWord;
    const unsigned long seventyYears = 2208988800UL;
    currentEpoch = secsSince1900 - seventyYears + 28800;

    time_t t = currentEpoch;
    struct tm* tm_info = localtime(&t);
    char timeStr[30];
    strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", tm_info);
    Serial.print("✅ NTP同步成功！当前时间: ");
    Serial.println(timeStr);
  } else {
    Serial.println("❌ NTP同步失败！");
  }

  udp.stop();
  lastNtpSync = millis();
  Serial.println("========== NTP时间同步结束 ==========\n");
}

// 获取当前时间戳（秒）
unsigned long getCurrentTimestamp() {
  if (millis() - lastNtpSync > 3600000) {
    syncTime();
  }
  return currentEpoch + (millis() - lastNtpSync) / 1000;
}

String getCurrentDate() {
  unsigned long timestamp = getCurrentTimestamp();
  time_t t = timestamp;
  struct tm* tm_info = localtime(&t);
  char buffer[10];
  strftime(buffer, sizeof(buffer), "%m-%d", tm_info);
  return String(buffer);
}

String getCurrentTime() {
  unsigned long timestamp = getCurrentTimestamp();
  time_t t = timestamp;
  struct tm* tm_info = localtime(&t);
  char buffer[10];
  strftime(buffer, sizeof(buffer), "%H:%M", tm_info);
  return String(buffer);
}

// ==================== WiFi连接 ====================
void setup_wifi() {
  delay(10);
  Serial.println();
  Serial.println("=== WiFi 连接 ===");
  Serial.print("连接到: ");
  Serial.println(ssid);

  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.setTextSize(1);
  tft.setCursor(20, 80);
  showChineseString(20, 80, "=== WiFi 连接 ===", TFT_CYAN);

  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setCursor(20, 110);
  tft.print("SSID: ");
  tft.print(ssid);

  WiFi.begin(ssid, password);

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    tft.setCursor(20 + (attempts * 8), 130);
    tft.print(".");
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n✅ WiFi 连接成功！");
    Serial.print("   IP地址: ");
    Serial.println(WiFi.localIP());
    Serial.print("   信号强度: ");
    Serial.print(WiFi.RSSI());
    Serial.println(" dBm");

    tft.setCursor(20, 150);
    tft.setTextColor(TFT_GREEN, TFT_BLACK);
    showChineseString(20, 150, "已连接", TFT_GREEN);
    tft.setCursor(20, 165);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.print(WiFi.localIP());
    delay(1500);
  } else {
    Serial.println("\n❌ WiFi 连接失败！");
    tft.setCursor(20, 150);
    tft.setTextColor(TFT_RED, TFT_BLACK);
    showChineseString(20, 150, "连接失败", TFT_RED);
    delay(2000);
  }
  tft.fillScreen(TFT_BLACK);
}

// ==================== 读取传感器 ====================
void readSI7021() {
  temperature = si7021.getCelsiusHundredths() / 100.0;
  humidity = si7021.getHumidityBasisPoints() / 100.0;
  if (isnan(temperature) || isnan(humidity)) {
    temperature = 0.0;
    humidity = 0.0;
  }
}

// ==================== GZIP解压和API调用 ====================
bool parseCurrentWeatherJSON(String jsonString) {
  Serial.println("   开始解析当前天气JSON...");

  DynamicJsonDocument doc(3072);
  DeserializationError error = deserializeJson(doc, jsonString);
  if (error) {
    Serial.print("   ❌ JSON解析失败: ");
    Serial.println(error.c_str());
    return false;
  }

  String code = doc["code"].as<String>();
  if (code != "200") {
    Serial.print("   ❌ API返回错误代码: ");
    Serial.println(code);
    return false;
  }

  JsonObject now = doc["now"];
  weather_text = now["text"].as<String>();
  weather_temp = now["temp"].as<String>().toInt();
  weather_humidity = now["humidity"].as<String>().toInt();
  wind_dir = now["windDir"].as<String>();
  wind_scale = now["windScale"].as<String>();
  weather_code = now["icon"].as<String>();

  Serial.println("   ✅ 当前天气解析成功!");
  Serial.println("   ┌─────────────────────────────────┐");
  Serial.print("   │ 天气状况: ");
  Serial.println(weather_text);
  Serial.print("   │ 温度: ");
  Serial.print(weather_temp);
  Serial.println("°C");
  Serial.print("   │ 湿度: ");
  Serial.print(weather_humidity);
  Serial.println("%");
  Serial.print("   │ 风向: ");
  Serial.print(wind_dir);
  Serial.print("  风力: ");
  Serial.print(wind_scale);
  Serial.println("级");
  Serial.print("   │ 天气代码: ");
  Serial.println(weather_code);
  Serial.println("   └─────────────────────────────────┘");

  return true;
}

bool parseForecastWeatherJSON(String jsonString) {
  Serial.println("   开始解析天气预报JSON...");

  DynamicJsonDocument doc(3072);
  DeserializationError error = deserializeJson(doc, jsonString);
  if (error) {
    Serial.print("   ❌ JSON解析失败: ");
    Serial.println(error.c_str());
    return false;
  }

  String code = doc["code"].as<String>();
  if (code != "200") {
    Serial.print("   ❌ API返回错误代码: ");
    Serial.println(code);
    return false;
  }

  JsonArray daily = doc["daily"];
  Serial.print("   ✅ 天气预报解析成功! 获取到 ");
  Serial.print(daily.size());
  Serial.println(" 天数据");
  Serial.println("   ┌─────────────────────────────────────────┐");

  for (int i = 0; i < daily.size() && i < 3; i++) {
    forecast_date[i] = daily[i]["fxDate"].as<String>();
    forecast_text_day[i] = daily[i]["textDay"].as<String>();
    forecast_code_day[i] = daily[i]["iconDay"].as<String>();
    forecast_text_night[i] = daily[i]["textNight"].as<String>();
    forecast_code_night[i] = daily[i]["iconNight"].as<String>();
    forecast_temp_max[i] = daily[i]["tempMax"].as<String>().toInt();
    forecast_temp_min[i] = daily[i]["tempMin"].as<String>().toInt();

    Serial.print("   │ 第");
    Serial.print(i + 1);
    Serial.print("天 ");
    Serial.print(forecast_date[i]);
    Serial.print(": ");
    Serial.print(forecast_text_day[i]);
    Serial.print(" ");
    Serial.print(forecast_temp_max[i]);
    Serial.print("°C / ");
    Serial.print(forecast_text_night[i]);
    Serial.print(" ");
    Serial.print(forecast_temp_min[i]);
    Serial.println("°C");
  }
  Serial.println("   └─────────────────────────────────────────┘");

  return true;
}

void getCurrentWeather() {
  Serial.println("\n========== 获取当前天气 ==========");

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("❌ WiFi未连接，无法获取天气");
    return;
  }

  std::unique_ptr<BearSSL::WiFiClientSecure> client(new BearSSL::WiFiClientSecure);
  client->setInsecure();

  HTTPClient http;
  if (!http.begin(*client, current_weather_url)) {
    Serial.println("❌ HTTP连接失败");
    return;
  }

  http.addHeader("Accept-Encoding", "gzip");
  http.setReuse(false);
  http.setTimeout(5000);

  Serial.print("请求URL: ");
  Serial.println(current_weather_url);

  unsigned long startTime = millis();
  int httpCode = http.GET();
  unsigned long responseTime = millis() - startTime;

  Serial.print("HTTP响应码: ");
  Serial.println(httpCode);
  Serial.print("响应时间: ");
  Serial.print(responseTime);
  Serial.println(" ms");

  if (httpCode == HTTP_CODE_OK) {
    size_t in_len = http.getStreamPtr()->readBytes(in_buf, IN_BUF_SIZE);
    Serial.print("接收GZIP数据长度: ");
    Serial.print(in_len);
    Serial.println(" 字节");

    if (in_len >= 2 && in_buf[0] == 0x1F && in_buf[1] == 0x8B) {
      Serial.println("检测到GZIP压缩数据，开始解压...");

      uint8_t* decompressed_buf = nullptr;
      uint32_t decompressed_len = 0;
      int ret = ArduinoUZlib::decompress(in_buf, in_len, decompressed_buf, decompressed_len);

      if (ret > 0 && decompressed_buf != nullptr) {
        Serial.print("✅ 解压成功！解压后大小: ");
        Serial.print(ret);
        Serial.println(" 字节");

        String payload = String((char*)decompressed_buf);
        parseCurrentWeatherJSON(payload);
        free(decompressed_buf);
      } else {
        Serial.print("❌ 解压失败，错误码: ");
        Serial.println(ret);
      }
    } else {
      Serial.println("响应不是GZIP格式，直接解析...");
      String payload = http.getString();
      parseCurrentWeatherJSON(payload);
    }
  } else {
    Serial.print("❌ HTTP请求失败: ");
    Serial.println(http.errorToString(httpCode));
  }

  http.end();
  Serial.println("========== 当前天气获取结束 ==========\n");
}

void getForecastWeather() {
  Serial.println("\n========== 获取天气预报 ==========");

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("❌ WiFi未连接，无法获取天气预报");
    return;
  }

  std::unique_ptr<BearSSL::WiFiClientSecure> client(new BearSSL::WiFiClientSecure);
  client->setInsecure();

  HTTPClient http;
  if (!http.begin(*client, forecast_weather_url)) {
    Serial.println("❌ HTTP连接失败");
    return;
  }

  http.addHeader("Accept-Encoding", "gzip");
  http.setReuse(false);
  http.setTimeout(5000);

  Serial.print("请求URL: ");
  Serial.println(forecast_weather_url);

  unsigned long startTime = millis();
  int httpCode = http.GET();
  unsigned long responseTime = millis() - startTime;

  Serial.print("HTTP响应码: ");
  Serial.println(httpCode);
  Serial.print("响应时间: ");
  Serial.print(responseTime);
  Serial.println(" ms");

  if (httpCode == HTTP_CODE_OK) {
    size_t in_len = http.getStreamPtr()->readBytes(in_buf, IN_BUF_SIZE);
    Serial.print("接收GZIP数据长度: ");
    Serial.print(in_len);
    Serial.println(" 字节");

    if (in_len >= 2 && in_buf[0] == 0x1F && in_buf[1] == 0x8B) {
      Serial.println("检测到GZIP压缩数据，开始解压...");

      uint8_t* decompressed_buf = nullptr;
      uint32_t decompressed_len = 0;
      int ret = ArduinoUZlib::decompress(in_buf, in_len, decompressed_buf, decompressed_len);

      if (ret > 0 && decompressed_buf != nullptr) {
        Serial.print("✅ 解压成功！解压后大小: ");
        Serial.print(ret);
        Serial.println(" 字节");

        String payload = String((char*)decompressed_buf);
        parseForecastWeatherJSON(payload);
        free(decompressed_buf);
      } else {
        Serial.print("❌ 解压失败，错误码: ");
        Serial.println(ret);
      }
    } else {
      Serial.println("响应不是GZIP格式，直接解析...");
      String payload = http.getString();
      parseForecastWeatherJSON(payload);
    }
  } else {
    Serial.print("❌ HTTP请求失败: ");
    Serial.println(http.errorToString(httpCode));
  }

  http.end();
  Serial.println("========== 天气预报获取结束 ==========\n");
}

// ==================== 绘制天气图标（和风天气官方样式） ====================
void drawWeatherIcon(int x, int y, String iconCode, bool isDay = true) {
  // 晴（白天100/夜晚150）- 太阳/月亮
  if (iconCode == "100" || iconCode == "150") {
    if (isDay || iconCode == "100") {
      // 太阳：大圆 + 光线
      tft.fillCircle(x + 15, y + 15, 9, TFT_YELLOW);
      // 绘制太阳光线
      for (int i = 0; i < 8; i++) {
        int angle = i * 45;
        int x1 = 15 + cos(angle * 3.14159 / 180) * 14;
        int y1 = 15 + sin(angle * 3.14159 / 180) * 14;
        int x2 = 15 + cos(angle * 3.14159 / 180) * 20;
        int y2 = 15 + sin(angle * 3.14159 / 180) * 20;
        tft.drawLine(x + x1, y + y1, x + x2, y + y2, TFT_YELLOW);
      }
    } else {
      // 月亮：圆形 + 缺口
      tft.fillCircle(x + 15, y + 15, 9, TFT_WHITE);
      tft.fillCircle(x + 10, y + 11, 8, TFT_BLACK);
    }
  }
  // 多云/少云/晴间多云（101/102/103/151/152/153）
  else if (iconCode == "101" || iconCode == "102" || iconCode == "103" || iconCode == "151" || iconCode == "152" || iconCode == "153") {
    // 太阳/月亮 + 云
    if (iconCode.startsWith("15") || !isDay) {
      // 月亮
      tft.fillCircle(x + 10, y + 12, 6, TFT_WHITE);
      tft.fillCircle(x + 7, y + 9, 5, TFT_BLACK);
    } else {
      // 太阳
      tft.fillCircle(x + 10, y + 12, 6, TFT_YELLOW);
    }
    // 云
    tft.fillCircle(x + 18, y + 16, 7, TFT_LIGHTGREY);
    tft.fillCircle(x + 25, y + 15, 6, TFT_LIGHTGREY);
    tft.fillRect(x + 18, y + 16, 13, 5, TFT_LIGHTGREY);
  }
  // 阴（104）
  else if (iconCode == "104") {
    tft.fillCircle(x + 15, y + 12, 8, TFT_DARKGREY);
    tft.fillCircle(x + 23, y + 14, 7, TFT_DARKGREY);
    tft.fillCircle(x + 8, y + 14, 6, TFT_DARKGREY);
    tft.fillRect(x + 8, y + 14, 21, 6, TFT_DARKGREY);
  }
  // 雨（300-399）- 云 + 雨滴
  else if (iconCode.startsWith("3") || iconCode == "305" || iconCode == "306" || iconCode == "307" || iconCode == "309" || iconCode == "399") {
    // 云
    tft.fillCircle(x + 12, y + 12, 7, TFT_DARKGREY);
    tft.fillCircle(x + 20, y + 11, 6, TFT_DARKGREY);
    tft.fillCircle(x + 6, y + 13, 5, TFT_DARKGREY);
    tft.fillRect(x + 6, y + 13, 20, 5, TFT_DARKGREY);
    // 雨滴
    for (int i = 0; i < 3; i++) {
      tft.fillRect(x + 10 + i * 8, y + 20, 2, 6, TFT_BLUE);
    }
  }
  // 雷阵雨（302/303/304）- 云 + 雨滴 + 闪电
  else if (iconCode == "302" || iconCode == "303" || iconCode == "304") {
    tft.fillCircle(x + 12, y + 10, 7, TFT_DARKGREY);
    tft.fillCircle(x + 20, y + 9, 6, TFT_DARKGREY);
    tft.fillRect(x + 6, y + 11, 20, 5, TFT_DARKGREY);
    // 雨滴
    tft.fillRect(x + 10, y + 18, 2, 5, TFT_BLUE);
    tft.fillRect(x + 18, y + 19, 2, 4, TFT_BLUE);
    // 闪电
    tft.fillTriangle(x + 25, y + 12, x + 21, y + 17, x + 26, y + 17, TFT_YELLOW);
    tft.fillTriangle(x + 22, y + 17, x + 27, y + 12, x + 27, y + 17, TFT_YELLOW);
  }
  // 雪（400-499）- 云 + 雪花
  else if (iconCode.startsWith("4")) {
    tft.fillCircle(x + 12, y + 12, 7, TFT_LIGHTGREY);
    tft.fillCircle(x + 20, y + 11, 6, TFT_LIGHTGREY);
    tft.fillRect(x + 6, y + 13, 20, 5, TFT_LIGHTGREY);
    // 雪花
    for (int i = 0; i < 3; i++) {
      tft.drawLine(x + 10 + i * 8, y + 20, x + 10 + i * 8, y + 24, TFT_WHITE);
      tft.drawLine(x + 8 + i * 8, y + 22, x + 12 + i * 8, y + 22, TFT_WHITE);
    }
  }
  // 雾/霾（500-515）
  else if (iconCode.startsWith("5")) {
    tft.fillCircle(x + 12, y + 12, 7, TFT_DARKGREY);
    tft.fillCircle(x + 20, y + 11, 6, TFT_DARKGREY);
    // 雾线
    for (int i = 0; i < 3; i++) {
      tft.drawLine(x + 5 + i * 10, y + 20, x + 15 + i * 10, y + 20, TFT_LIGHTGREY);
    }
  }
  // 沙尘/浮尘/扬沙（503/504/507/508）
  else if (iconCode == "503" || iconCode == "504" || iconCode == "507" || iconCode == "508") {
    for (int i = 0; i < 3; i++) {
      tft.drawLine(x + 5 + i * 8, y + 15, x + 25, y + 15 + i * 3, TFT_ORANGE);
    }
  }
  // 默认图标
  else {
    tft.fillCircle(x + 15, y + 15, 10, TFT_LIGHTGREY);
    tft.setTextColor(TFT_BLACK, TFT_LIGHTGREY);
    tft.setTextSize(1);
    tft.setCursor(x + 12, y + 12);
    tft.print("?");
  }
}

// ==================== 绘制顶部区域（使用中文单位 ℃ 和 ％） ====================
void drawTopArea() {
  tft.fillRect(0, 0, SCREEN_WIDTH, TOP_AREA_HEIGHT, TFT_BLACK);
  tft.drawFastHLine(5, 0, SCREEN_WIDTH - 10, TFT_CYAN);
  tft.drawFastHLine(5, TOP_AREA_HEIGHT - 1, SCREEN_WIDTH - 10, TFT_CYAN);

  // 温度
  showChineseString(10, 5, "温度", TFT_WHITE);
  tft.setTextColor(TFT_RED, TFT_BLACK);
  tft.setTextSize(2);
  tft.setCursor(10, 25);
  tft.print(temperature, 1);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  // 使用中文单位 ℃
  showChineseString(12 + 30 + (temperature < 10 ? 6 : 12), 27, " ℃", TFT_WHITE);

  // 湿度
  showChineseString(75, 5, "湿度", TFT_WHITE);
  tft.setTextColor(TFT_BLUE, TFT_BLACK);
  tft.setTextSize(2);
  tft.setCursor(75, 25);
  tft.print(humidity, 1);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  // 使用中文单位 ％
  showChineseString(70 + 30 + (humidity < 10 ? 6 : 12), 27, "  ％", TFT_WHITE);

  tft.setTextColor(TFT_YELLOW, TFT_BLACK);
  tft.setCursor(SCREEN_WIDTH - 30, SCREEN_HEIGHT - 190);
  showChineseString(SCREEN_WIDTH - 30, SCREEN_HEIGHT - 190, "本地", TFT_YELLOW);

  // WiFi信号
  int rssi = WiFi.RSSI();
  int x = SCREEN_WIDTH - 20;
  int y = 12;
  tft.fillRect(x, y - 8, 3, 8, TFT_BLACK);
  tft.fillRect(x + 4, y - 6, 3, 6, TFT_BLACK);
  tft.fillRect(x + 8, y - 4, 3, 4, TFT_BLACK);
  tft.fillRect(x + 12, y - 2, 3, 2, TFT_BLACK);
  if (rssi > -50) {
    tft.fillRect(x, y - 8, 3, 8, TFT_GREEN);
    tft.fillRect(x + 4, y - 6, 3, 6, TFT_GREEN);
    tft.fillRect(x + 8, y - 4, 3, 4, TFT_GREEN);
    tft.fillRect(x + 12, y - 2, 3, 2, TFT_GREEN);
  } else if (rssi > -60) {
    tft.fillRect(x, y - 8, 3, 8, TFT_GREEN);
    tft.fillRect(x + 4, y - 6, 3, 6, TFT_GREEN);
    tft.fillRect(x + 8, y - 4, 3, 4, TFT_GREEN);
  } else if (rssi > -70) {
    tft.fillRect(x, y - 8, 3, 8, TFT_GREEN);
    tft.fillRect(x + 4, y - 6, 3, 6, TFT_GREEN);
  } else if (rssi > -80) {
    tft.fillRect(x, y - 8, 3, 8, TFT_GREEN);
  }

  tft.drawFastHLine(5, DIVIDER_Y, SCREEN_WIDTH - 10, TFT_WHITE);
}

// ==================== 更新时间显示 ====================
void updateTimeDisplay() {
  String newDate = getCurrentDate();
  String newTime = getCurrentTime();

  if (newTime != cachedTime) {
    cachedDate = newDate;
    cachedTime = newTime;

    int rightX = SCREEN_WIDTH - 70;
    tft.fillRect(rightX, DIVIDER_Y + 5, 60, 30, TFT_BLACK);

    tft.setTextColor(TFT_YELLOW, TFT_BLACK);
    tft.setTextSize(2);
    tft.setCursor(rightX, DIVIDER_Y + 5);
    tft.print(cachedDate);
    tft.setCursor(rightX, DIVIDER_Y + 25);
    tft.print(cachedTime);
  }
}

// ==================== 绘制当前天气屏幕（使用中文单位 ℃ 和 ％） ====================
void drawCurrentWeatherScreen() {
  tft.fillRect(0, DIVIDER_Y + 1, SCREEN_WIDTH, SCREEN_HEIGHT - DIVIDER_Y - 1, TFT_BLACK);

  //showChineseString(20, DIVIDER_Y + 5, "济南", TFT_CYAN);
  String cityWeather = "济南  " + weather_text;
  showChineseString(20, DIVIDER_Y + 5, cityWeather.c_str(), TFT_CYAN);

  cachedDate = getCurrentDate();
  cachedTime = getCurrentTime();
  int rightX = SCREEN_WIDTH - 70;
  tft.setTextColor(TFT_YELLOW, TFT_BLACK);
  tft.setTextSize(2);
  tft.setCursor(rightX, DIVIDER_Y + 5);
  tft.print(cachedDate);
  tft.setCursor(rightX, DIVIDER_Y + 25);
  tft.print(cachedTime);

  drawWeatherIcon(30, DIVIDER_Y + 30, weather_code, true);

  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(2);
  tft.setCursor(20, DIVIDER_Y + 60);
  tft.print(weather_text);

  // 温度（使用中文单位 ℃）
  tft.setTextColor(TFT_RED, TFT_BLACK);
  tft.setTextSize(3);
  tft.setCursor(20, DIVIDER_Y + 85);
  tft.print(weather_temp);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  showChineseString(15 + (weather_temp >= 10 ? 42 : 36), DIVIDER_Y + 87, "℃", TFT_WHITE);

  // 湿度（使用中文单位 ％）
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(2);
  showChineseString(20, DIVIDER_Y + 120, "湿度", TFT_WHITE);
  tft.setTextColor(TFT_BLUE, TFT_BLACK);
  tft.setCursor(50, DIVIDER_Y + 120);
  tft.print(weather_humidity);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  showChineseString(45 + (weather_humidity >= 10 ? 30 : 24), DIVIDER_Y + 122, "％", TFT_WHITE);

  // 风力
  showChineseString(20, DIVIDER_Y + 140, "风力", TFT_WHITE);
  tft.setTextColor(TFT_GREEN, TFT_BLACK);
  String windDisplay = wind_dir + " " + wind_scale + " " + "级";
  showChineseString(50, DIVIDER_Y + 140, windDisplay.c_str(), TFT_GREEN);

  tft.setTextColor(TFT_YELLOW, TFT_BLACK);
  tft.setCursor(SCREEN_WIDTH - 30, SCREEN_HEIGHT - 15);
  showChineseString(SCREEN_WIDTH - 30, SCREEN_HEIGHT - 15, "今天", TFT_YELLOW);
}

// ==================== 绘制三天预报屏幕（使用中文单位 ℃） ====================
void drawForecastScreen() {
  tft.fillRect(0, DIVIDER_Y + 1, SCREEN_WIDTH, SCREEN_HEIGHT - DIVIDER_Y - 1, TFT_BLACK);

  showChineseString(20, DIVIDER_Y + 5, "三天预报", TFT_CYAN);

  for (int i = 0; i < 3; i++) {
    int y_start = DIVIDER_Y + 20 + i * 50;

    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setTextSize(1);
    tft.setCursor(10, y_start);
    if (forecast_date[i] != "N/A" && forecast_date[i].length() >= 10) {
      tft.print(forecast_date[i].substring(5, 10));
    } else {
      tft.print(forecast_date[i]);
    }

    drawWeatherIcon(10, y_start + 10, forecast_code_day[i], true);
    drawWeatherIcon(40, y_start + 10, forecast_code_night[i], false);

    // 最高温（使用中文单位 ℃）
    tft.setTextColor(TFT_RED, TFT_BLACK);
    tft.setCursor(70, y_start);
    tft.print(forecast_temp_max[i]);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    showChineseString(70 + (forecast_temp_max[i] >= 10 ? 30 : 24), y_start + 2, "℃", TFT_WHITE);

    // 最低温（使用中文单位 ℃）
    tft.setTextColor(TFT_BLUE, TFT_BLACK);
    tft.setCursor(100, y_start);
    tft.print(forecast_temp_min[i]);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    showChineseString(100 + (forecast_temp_min[i] >= 10 ? 30 : 24), y_start + 2, "℃", TFT_WHITE);

    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setCursor(70, y_start + 15);
    tft.print(forecast_text_day[i]);
    tft.setCursor(70, y_start + 30);
    tft.print(forecast_text_night[i]);

    if (i < 2) {
      tft.drawFastHLine(5, y_start + 45, SCREEN_WIDTH - 10, TFT_DARKGREY);
    }
  }

  tft.setTextColor(TFT_YELLOW, TFT_BLACK);
  tft.setCursor(SCREEN_WIDTH - 30, SCREEN_HEIGHT - 15);
  showChineseString(SCREEN_WIDTH - 30, SCREEN_HEIGHT - 15, "三天", TFT_YELLOW);
}

// ==================== 更新显示 ====================
void updateDisplay() {
  drawTopArea();
  if (showForecast) {
    drawForecastScreen();
  } else {
    drawCurrentWeatherScreen();
  }
}

// ==================== 按钮检测 ====================
void checkButton() {
  static bool lastButtonState = HIGH;
  bool currentButtonState = digitalRead(BUTTON_PIN);
  if (currentButtonState == LOW && lastButtonState == HIGH) {
    if (millis() - lastButtonPress > buttonDebounce) {
      showForecast = !showForecast;
      updateDisplay();
      lastButtonPress = millis();
    }
  }
  lastButtonState = currentButtonState;
}

// ==================== 初始化 ====================
void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println("\n\n==========================================");
  Serial.println("      室内空气质量监测仪 v2.0");
  Serial.println("==========================================\n");

  pinMode(BUTTON_PIN, INPUT_PULLUP);
  Wire.begin(SDA_PIN, SCL_PIN);
  tft.init();
  tft.setRotation(2);
  tft.fillScreen(TFT_BLACK);
  tft.setTextSize(1);

  setup_wifi();

  showProgressBar("正在解析天气信息", 0);

  if (WiFi.status() == WL_CONNECTED) {
    showProgressBar("正在解析天气信息", 30);
    getCurrentWeather();
    showProgressBar("正在解析天气信息", 70);
    getForecastWeather();
    showProgressBar("正在解析天气信息", 100);
    delay(500);
  }

  syncTime();
  readSI7021();
  updateDisplay();

  Serial.println("\n==========================================");
  Serial.println("         系统初始化完成！");
  Serial.println("==========================================\n");
}

// ==================== 主循环 ====================
void loop() {
  unsigned long currentMillis = millis();

  if (currentMillis - lastTimeUpdate >= timeInterval) {
    lastTimeUpdate = currentMillis;
    if (!showForecast) {
      updateTimeDisplay();
    }
  }

  if (currentMillis - lastSensorRead >= sensorInterval) {
    lastSensorRead = currentMillis;
    readSI7021();
    drawTopArea();
  }

  if (currentMillis - lastWeatherUpdate >= weatherInterval) {
    lastWeatherUpdate = currentMillis;
    if (WiFi.status() == WL_CONNECTED) {
      showProgressBar("正在更新天气", 0);
      getCurrentWeather();
      showProgressBar("正在更新天气", 50);
      getForecastWeather();
      showProgressBar("正在更新天气", 100);
      delay(300);
      updateDisplay();
    }
  }

  checkButton();
  delay(100);
}