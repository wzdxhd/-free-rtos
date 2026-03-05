#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <Preferences.h>

// ================= 配置区 =================
const char* ssid = "Alan Router";
const char* password = "i'mfaded";
const char* mqtt_server = "218.201.45.7";
const int mqtt_port = 1883;

const char* product_id = "39Wepy3ER3";
const char* device_name = "esp8266";
String current_version;
String auth_token = "version=2018-10-31&res=products%2F39Wepy3ER3%2Fdevices%2Fesp8266&et=2085716656&method=md5&sign=Q03gJw%2BnfjqnUmF7wkQoAA%3D%3D";
String api_base = "http://iot-api.heclouds.com/fuse-ota/" + String(product_id) + "/" + String(device_name);

// ================= 全局对象 =================
WiFiClient espClient;
PubSubClient client(espClient);
Preferences preferences;

unsigned long lastCheckTime = 0;
const long checkInterval = 60000; 

// ================= 函数声明 =================
void setupWiFi();
void reconnect();
void parseAndUpload(String dataStr);
int waitForAck(unsigned long timeoutMs); // 修改返回值为 int
uint16_t calculate_crc16(uint8_t *data, uint16_t length); // 新增 CRC 函数

void checkAndPerformOTA();
bool reportVersion(String ver);
bool reportStatus(long tid, int step, int progress);
void startDownloadAndFlash(long tid, long totalSize, String targetVer);

// ================= 主程序 =================
void setup() {
  Serial.begin(115200);
  Serial2.begin(115200, SERIAL_8N1, 16, 17); 
  
  setupWiFi();
  client.setServer(mqtt_server, mqtt_port);

  preferences.begin("ota_config", false); 
  current_version = preferences.getString("ver", "V1.0");  
  preferences.end();
  
  Serial.print("当前固件版本: ");
  Serial.println(current_version);
  reportVersion(current_version);
}

void loop() {
  if (!client.connected()) reconnect();
  client.loop();

  // 处理传感器数据
  if (Serial2.available()) {
    String serialData = Serial2.readStringUntil('\n');
    serialData.trim(); 
    if (serialData.length() > 0 && serialData != "ACK_OK" && serialData != "NACK_R") {
        Serial.print("收到 STM32 数据: ");
        Serial.println(serialData);
        parseAndUpload(serialData);
    }
  }

  // 定时检测 OTA
  if (millis() - lastCheckTime >= checkInterval) {
    lastCheckTime = millis();
    checkAndPerformOTA(); 
  }
}

// ================= 新增：CRC16 查表法计算 =================
uint16_t calculate_crc16(uint8_t *data, uint16_t length) {
    uint16_t crc = 0xFFFF;
    for (uint16_t i = 0; i < length; i++) {
        crc ^= data[i];
        for (uint8_t j = 0; j < 8; j++) {
            if (crc & 0x0001) crc = (crc >> 1) ^ 0xA001;
            else crc >>= 1;
        }
    }
    return crc;
}

// ================= 核心：OTA 下载与分包发送 =================
void startDownloadAndFlash(long tid, long totalSize, String targetVer) {
  HTTPClient http;
  String downloadUrl = api_base + "/" + String(tid) + "/download";
  
  Serial.println("[OTA] 开始下载固件...");
  http.begin(downloadUrl);
  http.addHeader("Authorization", auth_token);
  http.setTimeout(10000); 

  int httpCode = http.GET();
  if (httpCode == 200) {
    WiFiClient *stream = http.getStreamPtr();

    // 1. 发送启动指令并等待 STM32 擦除 Flash
    Serial.printf("[OTA] 正在要求 STM32 擦除 %ld 字节...\n", totalSize);
    Serial2.printf("CMD_OTA_START:%ld\n", totalSize);
    
    if (waitForAck(15000) != 1) { // 擦除可能耗时，给15秒超时
       Serial.println("[OTA] 错误: STM32 擦除超时/握手失败");
       http.end(); return;
    }

    uint8_t buff[256]; 
    long remainSize = totalSize;
    long totalRead = 0;
    int lastProgress = 0;
    bool fetchNewChunk = true; // 标志位：是否需要从网络读取新数据
    int chunk_len = 0;

    // 2. 严格对齐的分包发送循环
    while (http.connected() && remainSize > 0) {
      
      // 如果上一包发送成功，才从网络取新数据；如果是重传，直接复用 buff 里的老数据
      if (fetchNewChunk) {
          // 精确计算这一包该读多少 (不超过 256)
          chunk_len = (remainSize > 256) ? 256 : remainSize;
          
          // 阻塞等待网络流到来
          while (stream->available() == 0 && http.connected()) { delay(10); }
          if (!http.connected()) break;

          // 从 HTTP 流中拉取指定长度
          int c = stream->readBytes(buff, chunk_len);
          if (c != chunk_len) {
              Serial.println("[OTA] 网络流意外断开");
              break;
          }
      }

      // 计算本包 256 (或余数) 字节的 CRC16
      uint16_t crc = calculate_crc16(buff, chunk_len);

      // 发送：数据区 + 2字节CRC
      Serial2.write(buff, chunk_len);
      Serial2.write((uint8_t*)&crc, 2); // ESP32 是小端，刚好低位在前发给 STM32

      // 停等协议：等待 STM32 校验和写入
      int ackStatus = waitForAck(5000);
      
      if (ackStatus == 1) { // 收到 ACK_OK -> 成功
          fetchNewChunk = true;  // 允许取下一包
          remainSize -= chunk_len;
          totalRead += chunk_len;

          // 打印进度
          int currentProgress = (totalRead * 100) / totalSize;
          if (currentProgress - lastProgress >= 5) {
             Serial.printf("[OTA] 进度: %d%%\n", currentProgress);
             reportStatus(tid, 10, currentProgress);
             lastProgress = currentProgress;
          }
      } 
      else if (ackStatus == 2) { // 收到 NACK_R -> 失败要求重传
          fetchNewChunk = false; // 下个循环不读网络流，原样重发 buff
          Serial.println("[OTA] CRC校验失败, 正在重发本包数据...");
      } 
      else { // 收到 0 -> 超时挂死
          Serial.println("[OTA] 错误: STM32 接收超时挂死");
          break; 
      }
    }

    // 3. 校验最终结果
    if (remainSize == 0) {
        // STM32 收到最后一包后会自动重启，不需要额外发 CMD_OTA_END
        Serial.println("[OTA] 传输完成! STM32 准备重启...");
        delay(1000); 
        
        reportStatus(tid, 101, 100); 
        delay(500);
        reportStatus(tid, 201, 100);
        
        preferences.begin("ota_config", false);
        preferences.putString("ver", targetVer);
        preferences.end();
        current_version = targetVer;
        
        Serial.printf(">>> 升级成功! 当前版本已更新为: %s <<<\n", current_version.c_str());
    } else {
        Serial.printf("[OTA] 传输异常中断! 剩余未传字节: %ld\n", remainSize);
    }
  } else {
    Serial.printf("[OTA] 下载链接请求失败, HTTP Code: %d\n", httpCode);
  }
  http.end();
}

// ================= 修改：支持多种状态的 ACK 等待 =================
// 返回值: 1=ACK_OK(成功), 2=NACK_R(重传), 0=超时无响应
int waitForAck(unsigned long timeoutMs) {
  unsigned long start = millis();
  String rxBuffer = "";
  rxBuffer.reserve(64); 
  
  while (millis() - start < timeoutMs) {
    if (Serial2.available()) {
      char c = Serial2.read();
      rxBuffer += c;
      
      // 成功校验
      if (rxBuffer.indexOf("ACK_OK") != -1) {
        while(Serial2.available()) Serial2.read(); 
        return 1;
      }
      // 失败要求重发校验
      if (rxBuffer.indexOf("NACK_R") != -1) {
        while(Serial2.available()) Serial2.read(); 
        return 2;
      }

      if (rxBuffer.length() > 100) {
          rxBuffer = rxBuffer.substring(rxBuffer.length() - 20);
      }
    }
    delay(5); 
  }
  return 0; // 超时
}

void checkAndPerformOTA() {
  if(WiFi.status() != WL_CONNECTED) return;
  HTTPClient http;
  String url = api_base + "/check?type=1&version=" + current_version;
  Serial.println("[OTA] 正在检测升级任务...");
  http.begin(url);
  http.addHeader("Authorization", auth_token);
  http.setTimeout(5000);
  
  if (http.GET() == 200) {
    StaticJsonDocument<1024> doc;
    deserializeJson(doc, http.getString());
    if (doc["code"] == 0 && doc.containsKey("data")) {
      startDownloadAndFlash(doc["data"]["tid"], doc["data"]["size"], doc["data"]["target"].as<String>());
    } else {
      Serial.println("[OTA] 当前无新任务");
    }
  }
  http.end();
}

bool reportVersion(String ver) {
  HTTPClient http;
  http.begin(api_base + "/version");
  http.addHeader("Authorization", auth_token);
  http.addHeader("Content-Type", "application/json");
  StaticJsonDocument<200> doc;
  doc["s_version"] = ver;
  doc["f_version"] = ver;
  String json; serializeJson(doc, json);
  int code = http.POST(json);
  http.end(); return code == 200;
}

bool reportStatus(long tid, int step, int progress) {
  HTTPClient http;
  http.begin(api_base + "/" + String(tid) + "/status");
  http.addHeader("Authorization", auth_token);
  http.addHeader("Content-Type", "application/json");
  StaticJsonDocument<200> doc;
  doc["step"] = step;
  if(step == 10) doc["desc"] = "downloading";
  String json; serializeJson(doc, json);
  int code = http.POST(json);
  http.end(); return code == 200;
}

void setupWiFi() {
  delay(10);
  Serial.println();
  Serial.print(">>> 正在尝试连接 WiFi: ");
  Serial.println(ssid);
  
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED){
    delay(500);
    Serial.print("."); // 没连上就一直打印点
  }
  // 连上后的输出
  Serial.println("\n>>> WiFi 连接成功!");
  Serial.print(">>> 分配到的 IP 地址: ");
  Serial.println(WiFi.localIP());
}

void reconnect() {
  while (!client.connected()) {
    Serial.print(">>> 正在连接 OneNET MQTT 服务器...");
    if (client.connect(device_name, product_id, auth_token.c_str())){
      Serial.println("连接成功!");}else{
        Serial.print("连接失败, 错误码=");
        Serial.print(client.state());
        Serial.println(" ，将在 5 秒后重试...");
        }
    delay(5000);
  }
}

void parseAndUpload(String dataStr) {
  float t_val, h_val, l_val; int c_val;
  if (sscanf(dataStr.c_str(), "%f,%f,%d,%f", &t_val, &h_val, &c_val, &l_val) == 4) {
    StaticJsonDocument<512> doc;
    doc["id"] = String(millis());
    doc["version"] = "1.0";
    JsonObject params = doc.createNestedObject("params");
    params["temp"]["value"] = t_val;
    params["humi"]["value"] = h_val;
    params["command"]["value"] = c_val;
    params["light"]["value"] = l_val; 
    char jsonBuffer[512]; serializeJson(doc, jsonBuffer);
    client.publish(("$sys/" + String(product_id) + "/" + String(device_name) + "/thing/property/post").c_str(), jsonBuffer);
  }
}
