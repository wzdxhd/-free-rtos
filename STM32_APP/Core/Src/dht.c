#include "dht11.h"

// 简单的微秒延时函数，使用DWT计数器（适配STM32F4 168MHz）
// 比普通的 for 循环延时精准得多
void Delay_us(uint32_t us)
{
    uint32_t start_tick = DWT->CYCCNT;
    // SystemCoreClock 是系统主频 (168000000)
    // us * (SystemCoreClock / 1000000) 就是需要的时钟周期数
    uint32_t delay_ticks = us * (SystemCoreClock / 1000000);

    while (DWT->CYCCNT - start_tick < delay_ticks);
}

// 设置引脚为输出模式
static void DHT11_Mode_Out_PP(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    GPIO_InitStruct.Pin = DHT11_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP; // 推挽输出
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    HAL_GPIO_Init(DHT11_PORT, &GPIO_InitStruct);
}

// 设置引脚为输入模式
static void DHT11_Mode_Input(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    GPIO_InitStruct.Pin = DHT11_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;     // 输入模式
    GPIO_InitStruct.Pull = GPIO_NOPULL;         // 浮空输入 (外部通常有上拉电阻，如果没有建议改为 PULLUP)
    HAL_GPIO_Init(DHT11_PORT, &GPIO_InitStruct);
}

// 初始化DHT11
void DHT11_Init(void)
{
    // 1. 开启 DWT 计数器用于延时
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

    // 2. 开启 GPIO 时钟
    DHT11_CLK_ENABLE();
    
    // 3. 默认拉高总线
    DHT11_Mode_Out_PP();
    HAL_GPIO_WritePin(DHT11_PORT, DHT11_PIN, GPIO_PIN_SET);
    HAL_Delay(1000); // 上电后稍微等一下传感器稳定
}

// 读取一个字节
static uint8_t DHT11_Read_Byte(void)
{
    uint8_t i, dat = 0;
    for (i = 0; i < 8; i++)
    {
        dat <<= 1;
        // 等待变为高电平（等待低电平结束）
        // 添加超时防止死循环
        uint32_t timeout = 0;
        while (HAL_GPIO_ReadPin(DHT11_PORT, DHT11_PIN) == GPIO_PIN_RESET) {
            timeout++;
            if(timeout > 1000) return 0; // 超时退出
            Delay_us(1);
        }

        // 高电平持续时间决定是0还是1
        // 0: 26-28us, 1: 70us
        Delay_us(40); // 延时40us后再检测

        if (HAL_GPIO_ReadPin(DHT11_PORT, DHT11_PIN) == GPIO_PIN_SET)
        {
            dat |= 1;
            // 等待变为低电平（等待高电平结束）
            while (HAL_GPIO_ReadPin(DHT11_PORT, DHT11_PIN) == GPIO_PIN_SET);
        }
    }
    return dat;
}

// 读取一次完整数据
// 返回值：0-成功，1-失败
uint8_t DHT11_Read_Data(DHT11_Data_t *DHT11_Data)
{
    uint8_t buf[5];
    uint8_t i;

    // === 1. 主机发送开始信号 ===
    DHT11_Mode_Out_PP();
    HAL_GPIO_WritePin(DHT11_PORT, DHT11_PIN, GPIO_PIN_RESET); // 拉低
    Delay_us(20000); 
    HAL_GPIO_WritePin(DHT11_PORT, DHT11_PIN, GPIO_PIN_SET);   // 拉高
    Delay_us(30);  // 等待20-40us

    // === 2. 切换为输入，检测DHT11响应 ===
    DHT11_Mode_Input();
    
    // 检测DHT11是否拉低了总线 (响应信号)
    if (HAL_GPIO_ReadPin(DHT11_PORT, DHT11_PIN) == GPIO_PIN_RESET)
    {
        // 等待DHT11释放总线（变高）
        // 80us低电平
        uint32_t timeout = 0;
        while (HAL_GPIO_ReadPin(DHT11_PORT, DHT11_PIN) == GPIO_PIN_RESET) {
            timeout++;
            if(timeout > 100) return 1; // 响应超时
            Delay_us(1);
        }

        // 等待DHT11开始传输数据（变低）
        // 80us高电平
        timeout = 0;
        while (HAL_GPIO_ReadPin(DHT11_PORT, DHT11_PIN) == GPIO_PIN_SET) {
            timeout++;
            if(timeout > 100) return 1; // 准备超时
            Delay_us(1);
        }

        // === 3. 开始读取40位数据 ===
        for (i = 0; i < 5; i++)
        {
            buf[i] = DHT11_Read_Byte();
        }

        // === 4. 校验数据 ===
        // 校验和 = 湿高 + 湿低 + 温高 + 温低
        if (buf[4] == (uint8_t)(buf[0] + buf[1] + buf[2] + buf[3]))
        {
            DHT11_Data->humidity = buf[0];
            DHT11_Data->humi_dec = buf[1];
            DHT11_Data->temperature = buf[2];
            DHT11_Data->temp_dec = buf[3];
            return 0; // 成功
        }
    }
    
    return 1; // 失败
}
