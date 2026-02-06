/*
================================================================================
                            1. 模块初始化和配置
================================================================================

在main.c中的配置步骤：

1. 包含头文件：
   #include "SendMessage.h"

2. 确保串口已正确初始化：
   MX_USART1_UART_Init();  // CubeMX生成的串口初始化

3. 初始化SendMessage模块：
   SendMessage_Init();

================================================================================
                            2. 基础通信函数使用
================================================================================

// 发送字符串
SendMessage_String("Hello World!\r\n");

// 格式化输出（类似printf）
int value = 123;
SendMessage_Printf("Temperature: %d°C\r\n", value);

// 发送原始数据
uint8_t data[] = {0x01, 0x02, 0x03};
SendMessage_Data(data, 3);

// 发送命令（不带结束符）
SendMessage_Command("page 0");

// 发送命令（带0xFF结束符，用于串口屏）
SendMessage_CommandWithEnd("page 0");

================================================================================
                            3. 波形数据发送
================================================================================

3.1 发送内置正弦波：
    SendMessage_SendSineWave();

3.2 发送ADC采集数据（16位）：
    uint16_t adc_buff[1024];  // 12位ADC数据缓冲区
    
    // ADC采集完成后
    SendMessage_SendADCWave(adc_buff, 1024);

3.3 发送ADC采集数据（8位）：
    uint8_t adc_buff_8bit[1024];  // 8位数据缓冲区
    
    SendMessage_SendADCWave_8bit(adc_buff_8bit, 1024);													*/


#include "SendMessage.h"

static const uint8_t sin_table[SINE_TABLE_SIZE] = {
    128,131,135,139,143,147,151,155,
    158,162,166,170,173,177,181,184,
    188,191,194,198,201,204,207,210,
    213,216,219,221,224,227,229,231,
    234,236,238,240,241,243,245,246,
    248,249,250,251,252,253,253,254,
    254,255,255,255,255,255,255,254,
    254,253,252,251,250,249,248,247,
    245,244,242,240,238,236,234,232,
    230,228,225,222,220,217,214,211,
    208,205,202,199,196,192,189,186,
    182,178,175,171,167,164,160,156,
    152,149,145,141,137,133,129,125,
    121,117,113,110,106,102,98,94,
    90,87,83,79,76,72,69,65,
    62,59,55,52,49,46,43,40,
    37,35,32,29,27,25,22,20,
    18,16,14,13,11,9,8,7,
    5,4,3,3,2,1,1,0,
    0,0,0,0,0,1,1,2,
    2,3,4,5,6,8,9,11,
    12,14,16,18,20,22,24,27,
    29,31,34,37,40,42,45,48,
    52,55,58,61,65,68,72,75,
    79,82,86,90,93,97,101,105,
    109,113,117,120,124,128,132,136,
    140,144,148,152,155,159,163,167,
    170,174,178,181,185,188,192,195,
    198,202,205,208,211,214,217,219,
    222,225,227,229,232,234,236,238,
    240,242,243,245,246,248,249,250,
    251,252,253,254,254,254,255,255,
    255,255,255,254,254,254,253,252,
    251,250,249,248,246,245,243,242,
    240,238,236,234,232,229,227,225,
    222,219,217,214,211,208,205,202,
    198,195,192,188,185,181,178,174,
    170,167,163,159,155,152,148,144,
    140,136,132,128,124,120,117,113,
    109,105,101,97,93,90,86,82,
    79,75,72,68,65,61,58,55,
    52,48,45,42,40,37,34,31,
    29,27,24,22,20,18,16,14,
    12,11,9,8,6,5,4,3,
    2,2,1,1,0,0,0,0,
    0,0,1,1,2,3,3,4,
    5,7,8,9,11,13,14,16,
    18,20,22,25,27,29,32,35,
    37,40,43,46,49,52,55,59,
    62,65,69,72,76,79,83,87,
    90,94,98,102,106,110,113,117,
    121,125,129,133,137,141,145,149,
    152,156,160,164,167,171,175,178,
    182,186,189,192,196,199,202,205,
    208,211,214,217,220,222,225,228,
    230,232,234,236,238,240,242,244,
    245,247,248,249,250,251,252,253,
    254,254,255,255,255,255,255,255,
    254,254,253,253,252,251,250,249,
    248,246,245,243,241,240,238,236,
    234,231,229,227,224,221,219,216,
    213,210,207,204,201,198,194,191,
    188,184,181,177,173,170,166,162,
    158,155,151,147,143,139,135,131,
    128,124,120,116,112,108,104,100,
    97,93,89,85,82,78,74,71,
    67,64,61,57,54,51,48,45,
    42,39,36,34,31,28,26,24,
    21,19,17,15,14,12,10,9,
    7,6,5,4,3,2,2,1,
    1,0,0,0,0,0,0,1,
    1,2,3,4,5,6,7,8,
    10,11,13,15,17,19,21,23,
    25,27,30,33,35,38,41,44,
    47,50,53,56,59,63,66,69,
    73,77,80,84,88,91,95,99,
    103,106,110,114,118,122,126,130,
    134,138,142,145,149,153,157,161,
    165,168,172,176,179,183,186,190,
    193,196,200,203,206,209,212,215,
    218,220,223,226,228,230,233,235,
    237,239,241,242,244,246,247,248,
    250,251,252,252,253,254,254,255,
    255,255,255,255,255,254,254,253,
    253,252,251,250,249,247,246,244,
    243,241,239,237,235,233,231,228,
    226,224,221,218,215,213,210,207,
    203,200,197,194,190,187,183,180,
    176,173,169,165,162,158,154,150,
    146,142,138,135,131,127,123,119,
    115,111,107,103,100,96,92,88,
    85,81,77,74,70,67,63,60,
    57,53,50,47,44,41,38,36,
    33,30,28,26,23,21,19,17,
    15,13,12,10,9,7,6,5,
    4,3,2,1,1,1,0,0,
    0,0,0,1,1,1,2,3,
    4,5,6,7,9,10,12,13,
    15,17,19,21,23,26,28,30,
    33,36,38,41,44,47,50,53,
    57,60,63,67,70,74,77,81,
    85,88,92,96,100,103,107,111,
    115,119,123,127,131,135,138,142,
    146,150,154,158,162,165,169,173,
    176,180,183,187,190,194,197,200,
    203,207,210,213,215,218,221,224,
    226,228,231,233,235,237,239,241,
    243,244,246,247,249,250,251,252,
    253,253,254,254,255,255,255,255,
    255,255,254,254,253,252,252,251,
    250,248,247,246,244,242,241,239,
    237,235,233,230,228,226,223,220,
    218,215,212,209,206,203,200,196,
    193,190,186,183,179,176,172,168,
    165,161,157,153,149,145,142,138,
    134,130,126,122,118,114,110,106,
    103,99,95,91,88,84,80,77,
    73,69,66,63,59,56,53,50,
    47,44,41,38,35,33,30,27,
    25,23,21,19,17,15,13,11,
    10,8,7,6,5,4,3,2,
    1,1,0,0,0,0,0,0,
    1,1,2,2,3,4,5,6,
    7,9,10,12,14,15,17,19,
    21,24,26,28,31,34,36,39,
    42,45,48,51,54,57,61,64,
    67,71,74,78,82,85,89,93,
    97,100,104,108,112,116,120,124
};


/**
 * @brief  消息发送模块初始化
 * @param  None
 * @retval None
 */
void SendMessage_Init(void)
{
    // 串口本身的初始化在MX_USART1_UART_Init()中已完成
    // 这里可以发送初始化完成信息
    SendMessage_String("=== SendMessage Module Initialized ===\r\n");
}

/**
 * @brief  发送字符串消息
 * @param  message: 要发送的字符串
 * @retval None
 */
void SendMessage_String(const char* message)
{
    if(message != NULL)
    {
        HAL_UART_Transmit(&huart1, (uint8_t*)message, strlen(message), HAL_MAX_DELAY);
    }
}


/**
 * @brief  发送原始数据
 * @param  data: 要发送的数据指针
 * @param  length: 数据长度
 * @retval None
 */
void SendMessage_Data(uint8_t *data, uint16_t length)
{
    if(data != NULL && length > 0)
    {
        HAL_UART_Transmit(&huart1, data, length, HAL_MAX_DELAY);
    }
}

/**
 * @brief  格式化输出（类似printf）
 * @param  format: 格式化字符串
 * @param  ...: 可变参数
 * @retval None
 */
void SendMessage_Printf(const char* format, ...)
{
    char buffer[256];  // 根据需要调整缓冲区大小
    va_list args;
    
    if(format != NULL)
    {
        va_start(args, format);
        vsnprintf(buffer, sizeof(buffer), format, args);
        va_end(args);
        
        SendMessage_String(buffer);
    }
}


/**
 * @brief  发送命令（不带结束符）
 * @param  command: 要发送的命令字符串
 * @retval None
 */
void SendMessage_Command(const char* command)
{
    SendMessage_String(command);
}

/**
 * @brief  发送命令并添加结束符（用于串口屏等设备）
 * @param  command: 要发送的命令字符串
 * @retval None
 */
void SendMessage_CommandWithEnd(const char* command)
{
    // 发送命令
    SendMessage_String(command);
    
    // 发送三个0xFF结束符
    uint8_t end_cmd[3] = {0xFF, 0xFF, 0xFF};
    SendMessage_Data(end_cmd, 3);
}

/**
 * @brief  发送完整的正弦波数据到串口屏
 * @param  None
 * @retval None
 */
void SendMessage_SendSineWave(void)
{
    char command[WAVEFORM_COMMAND_BUFFER_SIZE];
    
    SendMessage_String("Starting to send sine wave data...\r\n");
    
    for(int i = 0; i < SINE_TABLE_SIZE; i++) {
        // 格式化命令字符串
        snprintf(command, sizeof(command), "add s0.id,0,%d", sin_table[i]);
        
        // 发送命令和结束符
        SendMessage_CommandWithEnd(command);
        
        //未添加延时，可能有问题
    }
    
}
/**
 * @brief  发送 ADC 采集的波形数据（16位数据）
 * @param  adc_data: ADC 数据数组指针
 * @param  size: 数据数组大小
 * @retval None
 */
void SendMessage_SendADCWave(const uint16_t* adc_data, uint16_t size)
{
    char command[WAVEFORM_COMMAND_BUFFER_SIZE];
    
    if(adc_data == NULL || size == 0)
    {
        SendMessage_String("Error: Invalid ADC data parameters\r\n");
        return;
    }
    
    SendMessage_String("Starting to send ADC wave data...\r\n");
    SendMessage_Printf("Total ADC points: %d\r\n", size);
    
    for(uint16_t i = 0; i < size; i++) {
        // 将 16 位 ADC 数据转换为适合显示的值
        // 假设 ADC 是 12 位 (0-4095)，转换为 0-255 用于显示
        uint8_t display_value = (uint8_t)(adc_data[i] >> 4);  // 12位转8位
        
        // 格式化命令字符串
        snprintf(command, sizeof(command), "add s0.id,0,%d", display_value);
        
        // 发送命令和结束符
        SendMessage_CommandWithEnd(command);
        
        // 添加适当延时
        HAL_Delay(10);
        
    }
    
    SendMessage_String("ADC wave data sent complete.\r\n\r\n");
}

/**
 * @brief  发送 ADC 采集的波形数据（8位数据）
 * @param  adc_data: ADC 数据数组指针
 * @param  size: 数据数组大小
 * @retval None
 */
void SendMessage_SendADCWave_8bit(const uint8_t* adc_data, uint16_t size)
{
    char command[WAVEFORM_COMMAND_BUFFER_SIZE];
    
    if(adc_data == NULL || size == 0)
    {
        SendMessage_String("Error: Invalid ADC data parameters\r\n");
        return;
    }
    
    SendMessage_String("Starting to send ADC wave data (8-bit)...\r\n");
    SendMessage_Printf("Total ADC points: %d\r\n", size);
    
    for(uint16_t i = 0; i < size; i++) {
        // 格式化命令字符串
        snprintf(command, sizeof(command), "add s0.id,0,%d", adc_data[i]);
        
        // 发送命令和结束符
        SendMessage_CommandWithEnd(command);
        
        // 添加适当延时
        HAL_Delay(10);
        
    }
    
    SendMessage_String("ADC wave data sent complete.\r\n\r\n");
}
