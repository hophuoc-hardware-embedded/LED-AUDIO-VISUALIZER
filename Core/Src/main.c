/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Audio Visualizer v2 - Sửa lỗi mất cảm biến âm thanh
  ******************************************************************************
  */
/* USER CODE END Header */

#include "main.h"
#include <string.h>
#include <math.h>
#include <stdlib.h>
#include <usart.h>
/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define NUMPIXELS 60
#define LED_DATA_SIZE (NUMPIXELS * 24)
#define RESET_SLOTS_BEGIN (50)
#define RESET_SLOTS_END (50)
#define WS2812_FREQ (800000) // 800khz
#define TIMER_CLOCK_FREQ (168000000) // 168MHz for TIM1 (APB2)
#define TIMER_PERIOD ((TIMER_CLOCK_FREQ / WS2812_FREQ) - 1)

#define NUM_EFFECTS 4

// LED Visualizer
#define LED_DATA_HEADER 0xAA
#define LED_DATA_FOOTER 0x55
#define SEND_LED_DATA_INTERVAL 20
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
extern ADC_HandleTypeDef hadc1;
extern DMA_HandleTypeDef hdma_adc1;

extern TIM_HandleTypeDef htim1;
extern DMA_HandleTypeDef hdma_tim1_ch1;

/* USER CODE BEGIN PV */
// LED Data Buffer
uint32_t LED_Data[LED_DATA_SIZE + RESET_SLOTS_BEGIN + RESET_SLOTS_END];
uint32_t LED_Mod[LED_DATA_SIZE + RESET_SLOTS_BEGIN + RESET_SLOTS_END];

// Audio processing variables
int thresholdOffset = 150;
int maxIntensityRange = 700;
int smoothedLevel = 0;
uint32_t lastBeatTime = 0;

// WS2812B timing
uint32_t pwmData_0;
uint32_t pwmData_1;

// Effect management
int currentEffect = 0;

typedef struct {
    int position;
    float brightness;
    int active;
    int colorType;
} WaveData;

#define MAX_WAVES 15
WaveData audioWaves[MAX_WAVES];
int audioWaveIndex = 0;

// Effect 3 - Center pulse variables
int centerPulseIntensity = 0;

// Effect 4 - Color temperature variables
int colorTemperature = 0;


// --- BIẾN BLUETOOTH ---
#define RX_BUFFER_SIZE 32
uint8_t rx_buffer[RX_BUFFER_SIZE];
uint8_t rx_index = 0;
volatile uint8_t rx_data;
volatile uint8_t command_received = 0;
char command_buffer[RX_BUFFER_SIZE];

// --- BIẾN CHO MANUAL BEAT ---
WaveData manualWaves[MAX_WAVES];
int manualWaveIndex = 0;
volatile uint8_t manualBeatTrigger = 0;
volatile uint8_t manualRainbowTrigger = 0;

// LED Visualizer
uint8_t LED_RGB_Buffer[NUMPIXELS * 3];
uint32_t lastSendTime = 0;
uint8_t uart_tx_buffer[183];
volatile uint8_t uart_tx_busy = 0;

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_DMA_Init(void);
static void MX_ADC1_Init(void);
static void MX_TIM1_Init(void);
extern void MX_USART1_UART_Init(void);

/* USER CODE BEGIN PFP */
void WS2812_Send(void);
void Set_LED(int LEDnum, int Red, int Green, int Blue);
void Reset_LED(void);
void WS2812_Init(void);
uint32_t millis(void);
void delay_ms(uint32_t ms);
int analogRead(void);
uint32_t pause_sending_until = 0;
uint32_t allow_send_time = 0;
// Effect functions
void effect1_ProgressiveBar(int intensity);
void effect2_WaveEffect(int intensity);
void effect3_CenterPulse(int intensity);
void effect4_ColorTemperature(int intensity);
void HSVtoRGB(float h, float s, float v, int* r, int* g, int* b);
void getWaveColor(int colorType, int brightness, int* r, int* g, int* b);

void addAudioWave(int intensity);
void updateAudioWaves(void);

void addManualBeatWave(void);
void addManualRainbowWave(void);
void updateManualWaves(void);
void drawManualWaves_Overlay(void);
void getRainbowColor(float fraction, int brightness, int* r, int* g, int* b);

void Process_Bluetooth_Command(void);
void Send_LED_Data_To_App(void);
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

volatile uint32_t uwTick_ms = 0;

uint32_t millis(void) {
    return uwTick_ms;
}

void delay_ms(uint32_t ms) {
    uint32_t start = millis();
    while ((millis() - start) < ms) { }
}

int analogRead(void) {
    HAL_ADC_Start(&hadc1);
    HAL_ADC_PollForConversion(&hadc1, HAL_MAX_DELAY);
    uint16_t value = HAL_ADC_GetValue(&hadc1);
    HAL_ADC_Stop(&hadc1);
    return (int)value;
}

void WS2812_Init(void) {
    pwmData_0 = (uint32_t)(TIMER_PERIOD * 0.32);
    pwmData_1 = (uint32_t)(TIMER_PERIOD * 0.64);

    for (int i = 0; i < RESET_SLOTS_BEGIN; i++) LED_Data[i] = 0;
    for (int i = LED_DATA_SIZE + RESET_SLOTS_BEGIN; i < LED_DATA_SIZE + RESET_SLOTS_BEGIN + RESET_SLOTS_END; i++) LED_Data[i] = 0;

    for (int i = 0; i < MAX_WAVES; i++) {
        audioWaves[i].active = 0;
        manualWaves[i].active = 0;
    }
    memset(LED_RGB_Buffer, 0, sizeof(LED_RGB_Buffer));
}

void Set_LED(int LEDnum, int Red, int Green, int Blue) {
    if (LEDnum >= NUMPIXELS || LEDnum < 0) return;

    // Update LED_RGB_Buffer for app
    int bufferIndex = LEDnum * 3;
    LED_RGB_Buffer[bufferIndex] = Red;
    LED_RGB_Buffer[bufferIndex + 1] = Green;
    LED_RGB_Buffer[bufferIndex + 2] = Blue;

    uint8_t color[3] = {Green, Red, Blue};
    int bit_pos_start = RESET_SLOTS_BEGIN + LEDnum * 24;

    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 8; j++) {
            int bit_pos = bit_pos_start + i * 8 + j;
            if (color[i] & (1 << (7 - j))) {
                LED_Data[bit_pos] = pwmData_1;
            } else {
                LED_Data[bit_pos] = pwmData_0;
            }
        }
    }
}
/* Tìm đến hàm Reset_LED và sửa lại như sau */

void Reset_LED(void) {
    // 1. Xóa dữ liệu LED vật lý (Code cũ của bạn)
    for (int i = RESET_SLOTS_BEGIN; i < LED_DATA_SIZE + RESET_SLOTS_BEGIN; i++) {
        LED_Data[i] = pwmData_0;
    }

    // 2. THÊM DÒNG NÀY: Xóa dữ liệu gửi lên App (Reset mảng về 0)
    memset(LED_RGB_Buffer, 0, sizeof(LED_RGB_Buffer));
}

void WS2812_Send(void) {
    HAL_TIM_PWM_Start_DMA(&htim1, TIM_CHANNEL_1, (uint32_t *)LED_Data,
                          LED_DATA_SIZE + RESET_SLOTS_BEGIN + RESET_SLOTS_END);

    while (htim1.hdma[TIM_DMA_ID_CC1]->State != HAL_DMA_STATE_READY) { }

    HAL_TIM_PWM_Stop_DMA(&htim1, TIM_CHANNEL_1);
}

/* Thay thế hàm cũ trong main.c */
void Send_LED_Data_To_App(void) {
    // 1. Kiểm tra cơ bản
    if (uart_tx_busy) return;
    if (millis() < allow_send_time) return;

    // 2. Rate limiting: 40ms = 25Hz (giảm từ 20ms để ổn định hơn)
    uint32_t currentTime = millis();
    if ((currentTime - lastSendTime) < 40) return;
    lastSendTime = currentTime;

    // 3. Kiểm tra UART state trước khi gửi
    if (huart1.gState != HAL_UART_STATE_READY) {
        return; // UART chưa sẵn sàng, bỏ qua frame này
    }

    // 4. Đóng gói dữ liệu
    uart_tx_buffer[0] = LED_DATA_HEADER;
    uart_tx_buffer[1] = LED_DATA_HEADER;
    memcpy(&uart_tx_buffer[2], LED_RGB_Buffer, NUMPIXELS * 3);
    uart_tx_buffer[182] = LED_DATA_FOOTER;

    // 5. Gửi
    uart_tx_busy = 1;
    if (HAL_UART_Transmit_IT(&huart1, uart_tx_buffer, 183) != HAL_OK) {
        uart_tx_busy = 0; // Nếu lỗi, phải nhả cờ
    }
}
void Reset_LED_Visualizer_State(void) {
    // 1. Dừng việc gửi dữ liệu tạm thời
    uart_tx_busy = 1;

    // 2. Chờ UART TX hoàn tất (timeout 10ms)
    uint32_t timeout = millis() + 10;
    while (huart1.gState == HAL_UART_STATE_BUSY_TX && millis() < timeout) {
        HAL_Delay(1);
    }

    // 3. Abort UART TX nếu vẫn còn busy
    if (huart1.gState == HAL_UART_STATE_BUSY_TX) {
        HAL_UART_AbortTransmit(&huart1);
    }

    // 4. Xóa toàn bộ buffer LED RGB (dữ liệu gửi lên app)
    memset(LED_RGB_Buffer, 0, sizeof(LED_RGB_Buffer));

    // 5. Xóa buffer UART TX
    memset(uart_tx_buffer, 0, sizeof(uart_tx_buffer));

    // 6. Reset timestamp để force gửi ngay lập tức
    lastSendTime = 0;

    // 7. Cho phép gửi lại
    uart_tx_busy = 0;

    // 8. Gửi 1 gói "all black" để reset màn hình app
    uart_tx_buffer[0] = LED_DATA_HEADER;
    uart_tx_buffer[1] = LED_DATA_HEADER;
    // bytes 2-181 đã là 0 rồi (từ memset)
    uart_tx_buffer[182] = LED_DATA_FOOTER;

    uart_tx_busy = 1;
    HAL_UART_Transmit_IT(&huart1, uart_tx_buffer, 183);
}
void Process_Bluetooth_Command(void) {
    if (command_received) {
        if (strcmp(command_buffer, "#B") == 0) {
            manualBeatTrigger = 1;
        }
        else if (strcmp(command_buffer, "#R") == 0) {
            manualRainbowTrigger = 1;
        }
        // ===== LỆNH RE-CALIBRATE MỚI =====
        else if (strcmp(command_buffer, "#C") == 0) {
            // Re-calibrate cảm biến âm thanh
            int minVal = 4095, maxVal = 0;

            // Tắt tất cả LED trước khi calibrate
            Reset_LED();
            WS2812_Send();

            // Đọc 200 mẫu trong 2 giây (10ms mỗi mẫu)
            for(int i = 0; i < 200; i++) {
                int reading = analogRead();
                if(reading < minVal) minVal = reading;
                if(reading > maxVal) maxVal = reading;
                HAL_Delay(10);
            }

            // Tính lại threshold dựa trên mức nhiễu đo được
            int noise_level = maxVal - minVal;
            thresholdOffset = noise_level + 150;

            // Reset các biến xử lý audio về trạng thái ban đầu
            smoothedLevel = 0;
            lastBeatTime = 0;

            // Hiển thị feedback bằng LED - Flash màu xanh lá 3 lần
            for(int i = 0; i < 3; i++) {
                // Bật tất cả LED màu xanh
                for(int j = 0; j < NUMPIXELS; j++) {
                    Set_LED(j, 0, 255, 0);  // Green
                }
                WS2812_Send();
                HAL_Delay(200);

                // Tắt LED
                Reset_LED();
                WS2812_Send();
                HAL_Delay(200);
            }
        }
        // Xử lý lệnh chọn hiệu ứng
        else if (command_buffer[0] == '#') {
            int effect_num = atoi(&command_buffer[1]);
            if (effect_num >= 1 && effect_num <= NUM_EFFECTS) {
                // ===== THÊM ĐOẠN NÀY =====
                int oldEffect = currentEffect;
                currentEffect = effect_num - 1;

                // Chỉ reset nếu THỰC SỰ đổi effect (tránh reset vô ích)
                if (oldEffect != currentEffect) {
                    Reset_LED_Visualizer_State();

                    // Reset các biến effect cũ
                    if (currentEffect == 1) { // Wave effect
                        for (int i = 0; i < MAX_WAVES; i++) {
                            audioWaves[i].active = 0;
                            manualWaves[i].active = 0;
                        }
                    }

                    // Reset LED vật lý
                    Reset_LED();
                    WS2812_Send();

                    // Delay nhỏ để app kịp nhận gói "all black"
                    HAL_Delay(50);
                }
                // ===== HẾT ĐOẠN THÊM =====
            }
        }

        command_received = 0;
        memset(command_buffer, 0, RX_BUFFER_SIZE);
    }
}


/* --- CÁC HÀM HIỆU ỨNG --- */

// --- (SỬA LỖI) Xóa Reset_LED() ---
void effect1_ProgressiveBar(int intensity) {
    // Reset_LED(); // <- ĐÃ XÓA
    int ledCount = (intensity * NUMPIXELS) / maxIntensityRange;
    if (ledCount > NUMPIXELS) ledCount = NUMPIXELS;
    for (int i = 0; i < ledCount; i++) {
        if (i < 10) Set_LED(i, 255, 0, 0);
        else if (i < 20) Set_LED(i, 255, 100, 0);
        else if (i < 30) Set_LED(i, 255, 255, 0);
        else if (i < 40) Set_LED(i, 0, 255, 0);
        else if (i < 50) Set_LED(i, 0, 100, 255);
        else Set_LED(i, 150, 0, 255);
    }
}

// Hàm getWaveColor giữ nguyên
void getWaveColor(int colorType, int brightness, int* r, int* g, int* b) {
    switch(colorType) {
        case 0: *r = 0; *g = 0; *b = brightness; break;
        case 1: *r = 0; *g = brightness; *b = brightness; break;
        case 2: *r = 0; *g = brightness; *b = 0; break;
        case 3: *r = brightness; *g = brightness; *b = 0; break;
        case 4: *r = brightness; *g = brightness / 2; *b = 0; break;
        case 5: *r = brightness; *g = 0; *b = 0; break;
        case 6: *r = brightness; *g = 0; *b = brightness; break;
        case 7: *r = brightness; *g = brightness; *b = brightness; break;
        case 8: *r = 255; *g = 255; *b = 255; break; // Placeholder
        default: *r = brightness; *g = brightness; *b = brightness; break;
    }
}

// addAudioWave (Giữ nguyên)
void addAudioWave(int intensity) {
    audioWaves[audioWaveIndex].active = 1;
    audioWaves[audioWaveIndex].position = 0;
    audioWaves[audioWaveIndex].brightness = 255.0f;

    int intensityLevel = (intensity * 8) / maxIntensityRange;
    if (intensityLevel > 7) intensityLevel = 7;
    audioWaves[audioWaveIndex].colorType = intensityLevel;

    audioWaveIndex = (audioWaveIndex + 1) % MAX_WAVES;
}

// updateAudioWaves (Giữ nguyên)
void updateAudioWaves(void) {
    for (int i = 0; i < MAX_WAVES; i++) {
        if (audioWaves[i].active) {
            audioWaves[i].position += 2;
            audioWaves[i].brightness *= 0.95f;

            if (audioWaves[i].position >= NUMPIXELS || audioWaves[i].brightness < 10.0f) {
                audioWaves[i].active = 0;
            }
        }
    }
}

// effect2_WaveEffect (Giữ nguyên, vốn đã không có Reset_LED)
void effect2_WaveEffect(int intensity) {
    static uint32_t lastWaveTime = 0;
    static int lastIntensity = 0;

    // 1. Thêm sóng âm thanh mới nếu có beat
    if (intensity > thresholdOffset &&
        (millis() - lastWaveTime) > 80 &&
        intensity > (lastIntensity + 20)) {

        addAudioWave(intensity - thresholdOffset);
        lastWaveTime = millis();
        lastIntensity = intensity;
    }

    if ((millis() - lastWaveTime) > 200) {
        lastIntensity = lastIntensity * 0.8;
    }

    // 2. Cập nhật vị trí và độ sáng sóng âm thanh
    updateAudioWaves();

    // 3. Vẽ sóng âm thanh
    for (int i = 0; i < MAX_WAVES; i++) {
        if (audioWaves[i].active) {
            int pos = audioWaves[i].position;
            if (pos >= 0 && pos < NUMPIXELS) {
                int r, g, b;
                getWaveColor(audioWaves[i].colorType, (int)audioWaves[i].brightness, &r, &g, &b);
                Set_LED(pos, r, g, b);
            }
        }
    }
}

// --- CÁC HÀM CHO SÓNG THỦ CÔNG (Giữ nguyên) ---

void addManualBeatWave(void) {
    manualWaves[manualWaveIndex].active = 1;
    manualWaves[manualWaveIndex].position = 0;
    manualWaves[manualWaveIndex].brightness = 255.0f;
    manualWaves[manualWaveIndex].colorType = rand() % 8;
    manualWaveIndex = (manualWaveIndex + 1) % MAX_WAVES;
}

void addManualRainbowWave(void) {
    manualWaves[manualWaveIndex].active = 1;
    manualWaves[manualWaveIndex].position = 0;
    manualWaves[manualWaveIndex].brightness = 255.0f;
    manualWaves[manualWaveIndex].colorType = 8;
    manualWaveIndex = (manualWaveIndex + 1) % MAX_WAVES;
}

void updateManualWaves(void) {
    for (int i = 0; i < MAX_WAVES; i++) {
        if (manualWaves[i].active) {
            manualWaves[i].position += 1;

            if (manualWaves[i].colorType == 8) {
                 manualWaves[i].brightness *= 0.98f;
            } else {
                 manualWaves[i].brightness *= 0.96f;
            }

            int end_position = (manualWaves[i].colorType == 8) ? (NUMPIXELS + 20) : NUMPIXELS;

            if (manualWaves[i].position >= end_position || manualWaves[i].brightness < 10.0f) {
                manualWaves[i].active = 0;
            }
        }
    }
}

void getRainbowColor(float fraction, int brightness, int* r, int* g, int* b) {
    float hue = fraction * 360.0f;
    float sat = 1.0f;
    float val = (float)brightness / 255.0f;
    HSVtoRGB(hue, sat, val, r, g, b);
}

void drawManualWaves_Overlay(void) {
    for (int i = 0; i < MAX_WAVES; i++) {
        if (manualWaves[i].active) {
            int pos = manualWaves[i].position;
            int r, g, b;
            int brightness_int = (int)manualWaves[i].brightness;

            if (manualWaves[i].colorType == 8) {
                int rainbow_length = 20;
                for (int j = 0; j < rainbow_length; j++) {
                    int led_pos = pos - j;
                    if (led_pos >= 0 && led_pos < NUMPIXELS) {
                        float fraction = (float)j / (float)rainbow_length;
                        getRainbowColor(fraction, brightness_int, &r, &g, &b);
                        Set_LED(led_pos, r, g, b);
                    }
                }
            }
            else {
                if (pos >= 0 && pos < NUMPIXELS) {
                    getWaveColor(manualWaves[i].colorType, brightness_int, &r, &g, &b);
                    Set_LED(pos, r, g, b);
                    if(pos > 0) Set_LED(pos-1, r/2, g/2, b/2);
                    if(pos > 1) Set_LED(pos-2, r/4, g/4, b/4);
                }
            }
        }
    }
}

// --- (SỬA LỖI) Xóa Reset_LED() ---
void effect3_CenterPulse(int intensity) {
    // Reset_LED(); // <- ĐÃ XÓA
    if (intensity > thresholdOffset) {
        centerPulseIntensity = intensity - thresholdOffset;
    } else {
        centerPulseIntensity = centerPulseIntensity * 0.9;
    }
    int center = NUMPIXELS / 2;
    int spread = (centerPulseIntensity * center) / maxIntensityRange;
    for (int i = 0; i < spread; i++) {
        int brightness = 255 * (spread - i) / spread;
        if (center - i >= 0) Set_LED(center - i, brightness, 0, brightness);
        if (center + i < NUMPIXELS) Set_LED(center + i, brightness, 0, brightness);
    }
}

// HSVtoRGB (Giữ nguyên)
void HSVtoRGB(float h, float s, float v, int* r, int* g, int* b) {
    float c = v * s;
    float x = c * (1 - fabsf(fmodf(h / 60.0f, 2) - 1));
    float m = v - c;
    float r_f, g_f, b_f;
    if (h >= 0 && h < 60) { r_f = c; g_f = x; b_f = 0; }
    else if (h >= 60 && h < 120) { r_f = x; g_f = c; b_f = 0; }
    else if (h >= 120 && h < 180) { r_f = 0; g_f = c; b_f = x; }
    else if (h >= 180 && h < 240) { r_f = 0; g_f = x; b_f = c; }
    else if (h >= 240 && h < 300) { r_f = x; g_f = 0; b_f = c; }
    else { r_f = c; g_f = 0; b_f = x; }
    *r = (int)((r_f + m) * 255);
    *g = (int)((g_f + m) * 255);
    *b = (int)((b_f + m) * 255);
}

// --- (SỬA LỖI) Xóa Reset_LED() ---
void effect4_ColorTemperature(int intensity) {
    // Reset_LED(); // <- ĐÃ XÓA
    if (intensity > thresholdOffset) {
        colorTemperature = intensity - thresholdOffset;
    } else {
        colorTemperature = colorTemperature * 0.95;
    }
    float hue = 240 - (colorTemperature * 240.0f) / maxIntensityRange;
    if (hue < 0) hue = 0;
    float saturation = 1.0f;
    float brightness = (float)colorTemperature / maxIntensityRange;
    if (brightness > 1.0f) brightness = 1.0f;
    if (brightness < 0.1f) brightness = 0.1f;
    int r, g, b;
    HSVtoRGB(hue, saturation, brightness, &r, &g, &b);
    for (int i = 0; i < NUMPIXELS; i++) {
        Set_LED(i, r, g, b);
    }
}

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{
    HAL_Init();
    SystemClock_Config();
    MX_GPIO_Init();
    MX_DMA_Init();
    MX_ADC1_Init();
    MX_TIM1_Init();
    MX_USART1_UART_Init();

    /* USER CODE BEGIN 2 */

    HAL_UART_Receive_IT(&huart1, (uint8_t *)&rx_data, 1);
    WS2812_Init();
    Reset_LED();
    WS2812_Send();

    // Logic căn chỉnh độ nhạy tự động
    int minVal = 4095, maxVal = 0;
    for(int i = 0; i < 200; i++) {
        int reading = analogRead();
        if(reading < minVal) minVal = reading;
        if(reading > maxVal) maxVal = reading;
        delay_ms(10);
    }
    int noise_level = maxVal - minVal;
    thresholdOffset = noise_level + 150;

	// --- (THÊM MỚI) Thêm seed cho rand() để màu ngẫu nhiên hơn ---
    srand(thresholdOffset); // Dùng chính giá trị nhiễu để làm seed

    currentEffect = 0;
    /* USER CODE END 2 */

    /* Infinite loop */
    while (1)
    {
        // 1. Xử lý lệnh Bluetooth (nếu có)
        Process_Bluetooth_Command();

        // 2. Kiểm tra và kích hoạt sóng thủ công
        if (manualBeatTrigger) {
            addManualBeatWave();
            manualBeatTrigger = 0;
        }
        if (manualRainbowTrigger) {
            addManualRainbowWave();
            manualRainbowTrigger = 0;
        }

        // 3. Luôn cập nhật vị trí sóng thủ công
        updateManualWaves();

        // 4. Xử lý âm thanh
        int maxReading = 0, minReading = 4095;
        for(int i = 0; i < 10; i++) {
            int reading = analogRead();
            if(reading > maxReading) maxReading = reading;
            if(reading < minReading) minReading = reading;
            HAL_Delay(1);
        }
        int amplitude = maxReading - minReading;
        if (amplitude < 80) {
                    amplitude = 0;
                }
        smoothedLevel = (smoothedLevel * 7 + amplitude * 3) / 10;

        int dynamicThreshold = thresholdOffset;
        int intensity = 0;
        if (smoothedLevel > dynamicThreshold) {
            intensity = smoothedLevel - dynamicThreshold;
            lastBeatTime = millis();
        }

        // 5. Chạy hiệu ứng

        // --- Reset LED TẠI ĐÂY (CHỈ 1 LẦN) ---
        Reset_LED();

        switch (currentEffect) {
            case 0:
                effect1_ProgressiveBar(smoothedLevel > dynamicThreshold ? intensity : 0);
                break;
            case 1:
                // Vẽ sóng âm thanh
                effect2_WaveEffect(smoothedLevel);
                // Vẽ đè sóng thủ công lên
                drawManualWaves_Overlay();
                break;
            case 2:
                effect3_CenterPulse(smoothedLevel);
                break;
            case 3:
                effect4_ColorTemperature(smoothedLevel);
                break;
        }

        // 6. Gửi dữ liệu ra LED
        WS2812_Send();
        Send_LED_Data_To_App();  // Bật nếu muốn LED ảo trên app
        delay_ms(20);
    }
}

/* ... (Phần còn lại của file: SystemClock_Config, MX_..._Init, Error_Handler, ...) ... */
/* ... (GIỮ NGUYÊN) ... */

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
    RCC_OscInitTypeDef RCC_OscInitStruct = {0};
    RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

    /** Configure the main internal regulator output voltage
    */
    __HAL_RCC_PWR_CLK_ENABLE();
    __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

    /** Initializes the RCC Oscillators according to the specified parameters
    * in the RCC_OscInitTypeDef structure.
    */
    RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    RCC_OscInitStruct.HSEState = RCC_HSE_ON;
    RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
    RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
    RCC_OscInitStruct.PLL.PLLM = 4;
    RCC_OscInitStruct.PLL.PLLN = 168;
    RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
    RCC_OscInitStruct.PLL.PLLQ = 4;
    if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
    {
        Error_Handler();
    }

    /** Initializes the CPU, AHB and APB buses clocks
    */
    RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                                |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
    RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
    RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV4;
    RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV2;

    if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_5) != HAL_OK)
    {
        Error_Handler();
    }
}

/**
  * @brief ADC1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_ADC1_Init(void)
{
    /* USER CODE BEGIN ADC1_Init 0 */

    /* USER CODE END ADC1_Init 0 */

    ADC_ChannelConfTypeDef sConfig = {0};

    /* USER CODE BEGIN ADC1_Init 1 */

    /* USER CODE END ADC1_Init 1 */

    /** Configure the global features of the ADC (Clock, Resolution, Data Alignment and number of conversion)
    */
    hadc1.Instance = ADC1;
    hadc1.Init.ClockPrescaler = ADC_CLOCK_SYNC_PCLK_DIV4;
    hadc1.Init.Resolution = ADC_RESOLUTION_12B;
    hadc1.Init.ScanConvMode = DISABLE;
    hadc1.Init.ContinuousConvMode = DISABLE;
    hadc1.Init.DiscontinuousConvMode = DISABLE;
    hadc1.Init.ExternalTrigConvEdge = ADC_EXTERNALTRIGCONVEDGE_NONE;
    hadc1.Init.ExternalTrigConv = ADC_SOFTWARE_START;
    hadc1.Init.DataAlign = ADC_DATAALIGN_RIGHT;
    hadc1.Init.NbrOfConversion = 1;
    hadc1.Init.DMAContinuousRequests = DISABLE;
    hadc1.Init.EOCSelection = ADC_EOC_SINGLE_CONV;
    if (HAL_ADC_Init(&hadc1) != HAL_OK)
    {
        Error_Handler();
    }

    /** Configure for the selected ADC regular channel its corresponding rank in the sequencer and its sample time.
    */
    sConfig.Channel = ADC_CHANNEL_1; // PA1
    sConfig.Rank = 1;
    sConfig.SamplingTime = ADC_SAMPLETIME_15CYCLES; // Increased sampling time
    if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK)
    {
        Error_Handler();
    }
    /* USER CODE BEGIN ADC1_Init 2 */

    /* USER CODE END ADC1_Init 2 */
}

/**
  * @brief TIM1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_TIM1_Init(void)
{
    /* USER CODE BEGIN TIM1_Init 0 */

    /* USER CODE END TIM1_Init 0 */

    TIM_ClockConfigTypeDef sClockSourceConfig = {0};
    TIM_MasterConfigTypeDef sMasterConfig = {0};
    TIM_OC_InitTypeDef sConfigOC = {0};
    TIM_BreakDeadTimeConfigTypeDef sBreakDeadTimeConfig = {0};

    /* USER CODE BEGIN TIM1_Init 1 */

    /* USER CODE END TIM1_Init 1 */
    htim1.Instance = TIM1;
    htim1.Init.Prescaler = 0;
    htim1.Init.CounterMode = TIM_COUNTERMODE_UP;
    htim1.Init.Period = TIMER_PERIOD;
    htim1.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    htim1.Init.RepetitionCounter = 0;
    htim1.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
    if (HAL_TIM_Base_Init(&htim1) != HAL_OK)
    {
        Error_Handler();
    }
    sClockSourceConfig.ClockSource = TIM_CLOCKSOURCE_INTERNAL;
    if (HAL_TIM_ConfigClockSource(&htim1, &sClockSourceConfig) != HAL_OK)
    {
        Error_Handler();
    }
    if (HAL_TIM_PWM_Init(&htim1) != HAL_OK)
    {
        Error_Handler();
    }
    sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
    sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
    if (HAL_TIMEx_MasterConfigSynchronization(&htim1, &sMasterConfig) != HAL_OK)
    {
        Error_Handler();
    }
    sConfigOC.OCMode = TIM_OCMODE_PWM1;
    sConfigOC.Pulse = 0;
    sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
    sConfigOC.OCNPolarity = TIM_OCNPOLARITY_HIGH;
    sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;
    sConfigOC.OCIdleState = TIM_OCIDLESTATE_RESET;
    sConfigOC.OCNIdleState = TIM_OCNIDLESTATE_RESET;
    if (HAL_TIM_PWM_ConfigChannel(&htim1, &sConfigOC, TIM_CHANNEL_1) != HAL_OK)
    {
        Error_Handler();
    }
    sBreakDeadTimeConfig.OffStateRunMode = TIM_OSSR_DISABLE;
    sBreakDeadTimeConfig.OffStateIDLEMode = TIM_OSSI_DISABLE;
    sBreakDeadTimeConfig.LockLevel = TIM_LOCKLEVEL_OFF;
    sBreakDeadTimeConfig.DeadTime = 0;
    sBreakDeadTimeConfig.BreakState = TIM_BREAK_DISABLE;
    sBreakDeadTimeConfig.BreakPolarity = TIM_BREAKPOLARITY_HIGH;
    sBreakDeadTimeConfig.AutomaticOutput = TIM_AUTOMATICOUTPUT_DISABLE;
    if (HAL_TIMEx_ConfigBreakDeadTime(&htim1, &sBreakDeadTimeConfig) != HAL_OK)
    {
        Error_Handler();
    }
    /* USER CODE BEGIN TIM1_Init 2 */

    /* USER CODE END TIM1_Init 2 */
    HAL_TIM_MspPostInit(&htim1);
}

/**
  * @brief  Enable DMA controller clock
  * @param  None
  * @retval None
  */
static void MX_DMA_Init(void)
{
    /* DMA controller clock enable */
    __HAL_RCC_DMA1_CLK_ENABLE();
    __HAL_RCC_DMA2_CLK_ENABLE();

    /* DMA interrupt init */
    /* DMA1_Stream5_IRQn interrupt configuration */
    HAL_NVIC_SetPriority(DMA1_Stream5_IRQn, 0, 0);
    HAL_NVIC_EnableIRQ(DMA1_Stream5_IRQn);
    /* DMA2_Stream0_IRQn interrupt configuration */
    HAL_NVIC_SetPriority(DMA2_Stream0_IRQn, 0, 0);
    HAL_NVIC_EnableIRQ(DMA2_Stream0_IRQn);
    /* DMA2_Stream1_IRQn interrupt configuration */
    HAL_NVIC_SetPriority(DMA2_Stream1_IRQn, 0, 0);
    HAL_NVIC_EnableIRQ(DMA2_Stream1_IRQn);
}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
    /* GPIO Ports Clock Enable */
    __HAL_RCC_GPIOH_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOE_CLK_ENABLE(); // Added for PE9
}

/* USER CODE BEGIN 4 */

// --- HÀM UART RX CALLBACK (Giữ nguyên) ---
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart) {
    if (huart->Instance == USART1) {

        if (rx_data == ';') {
            if (rx_index < RX_BUFFER_SIZE - 1) {
                rx_buffer[rx_index] = '\0';
                strcpy(command_buffer, (char*)rx_buffer);
                command_received = 1;
                rx_index = 0;
                memset(rx_buffer, 0, RX_BUFFER_SIZE);
            } else {
                rx_index = 0;
                memset(rx_buffer, 0, RX_BUFFER_SIZE);
            }
        }
        else if (rx_data == '\r' || rx_data == '\n') {
            // Bỏ qua
        }
        else {
            if (rx_index < RX_BUFFER_SIZE - 1) {
                rx_buffer[rx_index++] = rx_data;
            }
        }
        HAL_UART_Receive_IT(&huart1, (uint8_t *)&rx_data, 1);
    }
}

void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart) {
    if (huart->Instance == USART1) {
        uart_tx_busy = 0;
    }
}
// Override SysTick handler (Giữ nguyên)
void HAL_IncTick(void) {
    uwTick += 1U;
    uwTick_ms += 1U;
}

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
    /* USER CODE BEGIN Error_Handler_Debug */
    /* User can add his own implementation to report the HAL error return state */
    __disable_irq();
    while (1)
    {
    }
    /* USER CODE END Error_Handler_Debug */
}


#ifdef  USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  * where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
    /* USER CODE BEGIN 6 */
    /* User can add his own implementation to report the file name and line number,
       ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
    /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
