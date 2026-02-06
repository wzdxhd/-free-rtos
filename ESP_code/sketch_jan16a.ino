#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <Preferences.h>

// ================= 配置区 =================

// 1. Wi-Fi 信息
const char* ssid = "ZTE_T3000_18.1";
const char* password = "111111100";

// 2. OneNET MQTT 服务器 (仅用于传感器数据上传)
const char* mqtt_server = "218.201.45.7";
const int mqtt_port = 1883;

// 3. 设备信息
const char* product_id = "39Wepy3ER3";
const char* device_name = "esp8266";

// 4. 当前固件版本
String  current_version;

// 5. 鉴权 Token
String auth_token = "version=2018-10-31&res=products%2F39Wepy3ER3%2Fdevices%2Fesp8266&et=2085716656&method=md5&sign=Q03gJw%2BnfjqnUmF7wkQoAA%3D%3D";

// 6. API 基础路径
String api_base = "http://iot-api.heclouds.com/fuse-ota/" + String(product_id) + "/" + String(device_name);

// ================= 全局对象 =================

WiFiClient espClient;
PubSubClient client(espClient);
Preferences preferences;

// 定时器变量
unsigned long lastCheckTime = 0;
const long checkInterval = 60000; // 每60秒检测一次 OTA

// ================= 函数声明 =================

void setupWiFi();
void reconnect();
void parseAndUpload(String dataStr);
bool waitForAck(unsigned long timeoutMs);

// OTA 相关
void checkAndPerformOTA();
bool reportVersion(String ver);
bool reportStatus(long tid, int step, int progress);
void startDownloadAndFlash(long tid, long totalSize, String targetVer);

// ================= 主程序 =================

void setup() {
  // 调试串口 (连接电脑)
  Serial.begin(115200);
  
  // 【重要修改】与 STM32 通信建议改为 115200，否则 OTA 极慢
  // 请务必同时修改 STM32 端的代码波特率
  Serial2.begin(115200, SERIAL_8N1, 16, 17); 
  
  setupWiFi();
  
  // MQTT 初始化
  client.setServer(mqtt_server, mqtt_port);

  preferences.begin("ota_config", false); // 命名空间为 ota_config
  current_version = preferences.getString("ver", "V1.0");  // 如果读取不到 "ver" 键值，默认返回 "V1.0"
  preferences.end();
  
  Serial.print("当前固件版本: ");
  Serial.println(current_version);

  // 开机上报版本
  Serial.println(">>> 系统启动，上报当前版本...");
  reportVersion(current_version);
}

void loop() {
  // MQTT 连接维护 (仅用于上传数据)
  if (!client.connected()) {
    reconnect();
  }
  client.loop();

  // --- 任务 1: 处理 STM32 发来的传感器数据 ---
  if (Serial2.available()) {
    String serialData = Serial2.readStringUntil('\n');
    serialData.trim(); 

    // 过滤掉 ACK 信号，只处理数据
    if (serialData.length() > 0 && serialData != "ACK_OK") {
        Serial.print("收到 STM32 数据: ");
        Serial.println(serialData);
        parseAndUpload(serialData);
    }
  }

  // --- 任务 2: 定时检测 OTA 升级任务 ---
  unsigned long currentMillis = millis();
  if (currentMillis - lastCheckTime >= checkInterval) {
    lastCheckTime = currentMillis;
    checkAndPerformOTA(); 
  }
}

// ================= 核心：OTA 升级逻辑 =================

void checkAndPerformOTA() {
  if(WiFi.status() != WL_CONNECTED) return;

  HTTPClient http;
  String url = api_base + "/check?type=1&version=" + current_version;
  
  Serial.println("[OTA] 正在检测升级任务...");
  http.begin(url);
  http.addHeader("Authorization", auth_token);
  http.setTimeout(5000); //5s超时
  
  int httpCode = http.GET();
  if (httpCode == 200) {
    String payload = http.getString();
    StaticJsonDocument<1024> doc;
    deserializeJson(doc, payload);

    int code = doc["code"];
    if (code == 0 && doc.containsKey("data")) {
      long tid = doc["data"]["tid"];
      long size = doc["data"]["size"];
      String targetVer = doc["data"]["target"].as<String>();
      
      Serial.printf("[OTA] 发现新版本: %s (TID: %ld, Size: %ld)\n", targetVer.c_str(), tid, size);
      startDownloadAndFlash(tid, size, targetVer);
      
    } else {
      Serial.println("[OTA] 当前无新任务");
    }
  } else {
    Serial.printf("[OTA] 检测失败 HTTP: %d\n", httpCode);
  }
  http.end();
}

bool reportVersion(String ver) {
  HTTPClient http;
  String url = api_base + "/version";
  http.begin(url);
  http.addHeader("Authorization", auth_token);
  http.addHeader("Content-Type", "application/json");

  StaticJsonDocument<200> doc;
  doc["s_version"] = ver;
  doc["f_version"] = ver;
  String json;
  serializeJson(doc, json);

  int httpCode = http.POST(json);
  http.end();
  return (httpCode == 200);
}

bool reportStatus(long tid, int step, int progress) {
  HTTPClient http;
  String url = api_base + "/" + String(tid) + "/status";
  http.begin(url);
  http.addHeader("Authorization", auth_token);
  http.addHeader("Content-Type", "application/json");

  StaticJsonDocument<200> doc;
  doc["step"] = step;
  if(step == 10) doc["desc"] = "downloading";
  
  String json;
  serializeJson(doc, json);
  int httpCode = http.POST(json);
  http.end();
  return (httpCode == 200);
}

void startDownloadAndFlash(long tid, long totalSize, String targetVer) {
  HTTPClient http;
  String downloadUrl = api_base + "/" + String(tid) + "/download";
  
  Serial.println("[OTA] 开始下载固件...");
  http.begin(downloadUrl);
  http.addHeader("Authorization", auth_token);
  http.setTimeout(10000); 

  int httpCode = http.GET();
  
  // 1. HTTP 请求成功
  if (httpCode == 200) {
    long len = http.getSize();
    WiFiClient *stream = http.getStreamPtr();

    // 握手
    Serial2.printf("CMD_OTA_START:%ld\n", len);
    delay(50);
    if (!waitForAck(10000)) {
       Serial.println("[OTA] 错误: STM32 握手失败");
       http.end(); return;
    }

    uint8_t buff[256]; 
    long totalRead = 0;
    int lastProgress = 0;
    bool isSuccess = false; // 初始为失败
    
    // 2. 循环下载
    while (http.connected() && (len > 0 || len == -1)) {
      size_t size = stream->available();
      if (size) {
        int c = stream->readBytes(buff, ((size > sizeof(buff)) ? sizeof(buff) : size));
        Serial2.write(buff, c);

        // 等待 STM32 写入确认
        if (!waitForAck(5000)) { 
           Serial.println("[OTA] 错误: STM32 写入超时/无响应");
           // isSuccess 保持为 false
           break; 
        }

        if (len > 0) len -= c;
        totalRead += c;

        // 进度条
        int currentProgress = (totalRead * 100) / totalSize;
        if (currentProgress - lastProgress >= 10) {
           Serial.printf("[OTA] 进度: %d%%\n", currentProgress);
           reportStatus(tid, 10, currentProgress);
           lastProgress = currentProgress;
        }
      }
      delay(0);
    }

    // 3. 校验最终结果
    if (len == 0) {
        isSuccess = true;
    }

    // 4. 根据结果处理
    if (isSuccess) {
        Serial.println("[OTA] 传输完成，发送结束指令...");
        Serial2.println("CMD_OTA_END");
        
        // 稍微延时等待 STM32 完成最后操作
        delay(500); 
        
        reportStatus(tid, 101, 100); 
        delay(500);
        reportStatus(tid, 201, 100);
        
        Serial.println("[OTA] 流程结束，保存新版本号...");
        preferences.begin("ota_config", false);
        preferences.putString("ver", targetVer);
        preferences.end();
        
        // 更新内存变量
        current_version = targetVer;
        
        Serial.printf(">>> 升级成功! 当前版本已更新为: %s <<<\n", current_version.c_str());
    } else {
        // 如果 httpCode是200但 isSuccess是false，说明是传输中断
        Serial.printf("[OTA] 传输中断! 剩余未传字节: %ld\n", len);
    }

  } else {
    // 5. HTTP 请求本身失败 (如 404, 500, 401)
    Serial.printf("[OTA] 下载链接请求失败, HTTP Code: %d\n", httpCode);
  }
  
  http.end();
}

// ================= 辅助函数 =================

void setupWiFi() {
  delay(10);
  Serial.print("连接 WiFi: ");
  Serial.println(ssid);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi 已连接");
}

void reconnect() {
  while (!client.connected()) {
    Serial.print("连接 MQTT...");
    if (client.connect(device_name, product_id, auth_token.c_str())) {
      Serial.println("成功!");
    } else {
      Serial.print("失败, =");
      Serial.print(client.state());
      delay(5000);
    }
  }
}

void parseAndUpload(String dataStr) {
  float t_val, h_val, l_val;
  int c_val;
  int n = sscanf(dataStr.c_str(), "%f,%f,%d,%f", &t_val, &h_val, &c_val, &l_val);

  if (n == 4) {
    StaticJsonDocument<512> doc;
    doc["id"] = String(millis());
    doc["version"] = "1.0";
    JsonObject params = doc.createNestedObject("params");
    params["temp"]["value"] = t_val;
    params["humi"]["value"] = h_val;
    params["command"]["value"] = c_val;
    params["light"]["value"] = l_val; 

    char jsonBuffer[512];
    serializeJson(doc, jsonBuffer);
    
    // 构造 Topic
    String topic_post = "$sys/" + String(product_id) + "/" + String(device_name) + "/thing/property/post";
    client.publish(topic_post.c_str(), jsonBuffer);
    Serial.println("数据已上传");
  }
}

bool waitForAck(unsigned long timeoutMs) {
  unsigned long start = millis();
  String rxBuffer = "";
  rxBuffer.reserve(64); // 预留内存
  
  while (millis() - start < timeoutMs) {
    if (Serial2.available()) {
      char c = Serial2.read();
      rxBuffer += c;
      
      // 检查 ACK
      if (rxBuffer.indexOf("ACK_OK") != -1) {
        // 清空串口剩余缓存，防止影响下一次读取
        while(Serial2.available()) Serial2.read(); 
        return true;
      }

      // 【关键修改】防止缓冲区溢出，但不要暴力清空，而是保留最近的数据
      // 这样可以避免切断跨越边界的 "ACK_OK"
      if (rxBuffer.length() > 100) {
          // 只保留最后 20 个字符，足够匹配 ACK_OK
          rxBuffer = rxBuffer.substring(rxBuffer.length() - 20);
      }
    }

    delay(5); //让出CPU时间
  }
  return false;
}
