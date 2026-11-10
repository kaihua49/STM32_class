/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <string.h>   // strlen長度、strcmp 比較字串是否相同 等字串函式
#include <ctype.h>    // toupper：把小寫字母轉成大寫
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */
typedef enum {
    STATE_IDLE = 0,    // 待機：綠燈亮，蜂鳴器靜音，等按鍵
    STATE_PLAYING,     // 播放中：按鍵後蜂鳴器響 100ms 或 300ms
    STATE_WAIT_CHAR    // 等待接續：放開後倒數 700ms，看會不會再按
} MorseState;
/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
UART_HandleTypeDef hlpuart1;

TIM_HandleTypeDef htim2;
TIM_HandleTypeDef htim3;

/* USER CODE BEGIN PV */
uint8_t rx_byte;
char rx_buf[32];
uint8_t rx_idx = 0;
volatile uint8_t cmd_ready = 0;
// ---- 摩斯碼查表 ----
// 字母 A~Z，用法：morse_letters[c - 'A']:把 A 當作起點 0，算出這個字母離 A 有幾格。剛好就是陣列的索引。
const char *morse_letters[26] = {//字串指標陣列 const :這東西不會被修改，放進 Flash，morse_letters[0] 是 A
    ".-","-...","-.-.","-..",".","..-.","--.","....","..",".---",
    "-.-",".-..","--","-.","---",".--.","--.-",".-.","...","-",
    "..-","...-",".--","-..-","-.--","--.."
};
// 數字 0~9，用法：morse_digits[c - '0']
const char *morse_digits[10] = {
    "-----",".----","..---","...--","....-",
    ".....","-....","--...","---..","----."
};

// ---- 板端按鍵輸入用的變數 ----
// volatile：告訴編譯器「這個變數會在中斷裡被改」，要一直讀
volatile MorseState state = STATE_IDLE;  // 目前狀態

volatile uint32_t tone_remaining_ms = 0; // 目前這個音還要響幾 ms（Timer 每 1ms 減 1）
volatile uint32_t silence_ms = 0;  // 放開按鍵後已經靜默幾 ms

volatile char     pending_elem = 0;      // 按鍵中斷留下的紀錄：'.'、'-'，0 代表沒有
volatile uint8_t  char_timeout = 0;      // 旗標：靜默滿 700ms，該解碼了
volatile uint8_t  space_timeout = 0;     // 旗標：靜默滿 1800ms，該補空格了

volatile uint8_t  pc_playing = 0;        // 旗標：電腦指令 playtext()正在播放中，此時忽略按鍵

char    morse_buffer[8];                 // 暫存目前這個字母的點劃，例如 ".-"
uint8_t morse_len = 0;                   // morse_buffer 目前長度
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_LPUART1_UART_Init(void);
static void MX_TIM2_Init(void);
static void MX_TIM3_Init(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
// 一次設定三顆狀態燈：1 = 亮、0 = 滅
static void set_leds(uint8_t g, uint8_t y, uint8_t b)
{
    HAL_GPIO_WritePin(LED_GREEN_GPIO_Port,  LED_GREEN_Pin,  g ? GPIO_PIN_SET : GPIO_PIN_RESET);
    HAL_GPIO_WritePin(LED_YELLOW_GPIO_Port, LED_YELLOW_Pin, y ? GPIO_PIN_SET : GPIO_PIN_RESET);
    HAL_GPIO_WritePin(LED_BLUE_GPIO_Port,   LED_BLUE_Pin,   b ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

// 蜂鳴器 + 板載 LD2 一起開／關，達成「邊響邊亮」
static void tone_on(void)
{
    HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_1);                          // PB4 輸出 800Hz 方波
    HAL_GPIO_WritePin(LD2_GPIO_Port, LD2_Pin, GPIO_PIN_SET);
}
static void tone_off(void)
{
    HAL_TIM_PWM_Stop(&htim3, TIM_CHANNEL_1);                           // 停止方波
    HAL_GPIO_WritePin(LD2_GPIO_Port, LD2_Pin, GPIO_PIN_RESET);
}

// 傳一整串文字到電腦
static void uart_print(const char *s)
{
    HAL_UART_Transmit(&hlpuart1, (uint8_t*)s, strlen(s), 100);
}

// 字元 → 摩斯碼字串。找不到（例如標點）回傳 NULL
static const char* char_to_morse(char c)
{
    c = toupper((unsigned char)c);                  // 統一轉大寫
    if (c >= 'A' && c <= 'Z') return morse_letters[c - 'A'];
    if (c >= '0' && c <= '9') return morse_digits[c - '0'];
    return NULL;
}

// 摩斯碼字串 → 字元（解碼用）。查不到回傳 '?'
static char morse_to_char(const char *code)
{
    for (int i = 0; i < 26; i++)                    // 先比對字母表
        if (strcmp(code, morse_letters[i]) == 0) return 'A' + i;
    for (int i = 0; i < 10; i++)                    // 再比對數字表
        if (strcmp(code, morse_digits[i]) == 0) return '0' + i;
    return '?';
}

// 【模式二】播放一整串英文字（電腦 → 板端）
// 這裡用 HAL_Delay 阻塞式播放，簡單易懂；播放期間主迴圈會暫停
static void play_text(const char *s)
{
    pc_playing = 1;                                 // 告訴按鍵中斷：現在別理按鍵
    set_leds(0, 1, 0);                              // 黃燈 = 播放中

    for (int i = 0; s[i] != '\0'; i++) {
        if (s[i] == ' ') {                          // 空格：單字間隔
            HAL_Delay(400);                         // 加上前面字母間隔 300ms，共約 700ms
            continue;
        }
        const char *m = char_to_morse(s[i]);		//查表
        if (m == NULL) continue;                    // 不認得的字元直接略過（也順便擋掉雜訊）

        for (int j = 0; m[j] != '\0'; j++) {
            tone_on();
            HAL_Delay(m[j] == '.' ? 100 : 300);     // 點 100ms、劃 300ms
            tone_off();
            HAL_Delay(100);                         // 音符之間靜默 100ms
        }
        HAL_Delay(200);                             // 字母之間再補 200ms（連同上面 100ms 共 300ms）
    }

    pending_elem = 0;                               // 清掉播放期間可能殘留的按鍵紀錄
    set_leds(1, 0, 0);                              // 回到綠燈待機
    pc_playing = 0;
}
/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_LPUART1_UART_Init();
  MX_TIM2_Init();
  MX_TIM3_Init();
  /* USER CODE BEGIN 2 */
	HAL_UART_Receive_IT(&hlpuart1, &rx_byte, 1);//收到 1 個位元組就存到 rx_byte，然後打斷 CPU 通知我。
    HAL_TIM_Base_Start_IT(&htim2);   // 啟動 TIM2，並開啟「每 1ms 一次」的中斷
    set_leds(1, 0, 0);               // 開機先亮綠燈（待機）
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
	  while (1)
	  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
		    // ========== 【模式二】電腦傳來一行字串 ==========
		    if (cmd_ready) {
		        cmd_ready = 0;
		        if (strlen(rx_buf) > 0) {                   // 空行（例如 Python 沖雜訊用的）直接忽略
		            uart_print("RX:");                      // 先回報收到什麼
		            uart_print(rx_buf);
		            uart_print("\r\n");
		            play_text(rx_buf);                      // 用蜂鳴器和燈播放摩斯碼
		            uart_print("DONE\r\n");                 // 播完通知電腦
		        }
		    }

		    // ========== 【模式一】板端按鍵輸入 ==========
		    // (a) 有按鍵紀錄，而且目前沒在響 → 開始發聲
		    if (pending_elem != 0 && state != STATE_PLAYING && !pc_playing) {
		        char e = pending_elem;
		        pending_elem = 0;

		        char_timeout = 0;                           // 又有新按鍵，之前的超時旗標作廢
		        space_timeout = 0;

		        if (morse_len < sizeof(morse_buffer) - 1) { // 防止緩衝區溢位
		            morse_buffer[morse_len++] = e;          // 記下這個點或劃
		            morse_buffer[morse_len] = '\0';
		        }

		        tone_remaining_ms = (e == '.') ? 100 : 300; // 點響 100ms、劃響 300ms
		        set_leds(0, 0, 0);
		        tone_on();
		        state = STATE_PLAYING;                      // 之後由 TIM2 中斷負責倒數、停止
		    }

		    // (b) 靜默滿 700ms：字母結束 → 解碼並傳給電腦
		    if (char_timeout) {
		        char_timeout = 0;
		        if (morse_len > 0) {
		            set_leds(0, 0, 1);                      // 藍燈 = 解碼中
		            char c = morse_to_char(morse_buffer);   // 查表
		            HAL_UART_Transmit(&hlpuart1, (uint8_t*)&c, 1, 100);   // 把字母送到 Git Bash
		            morse_len = 0;                          // 清空緩衝，準備接下一個字母
		            morse_buffer[0] = '\0';
		        }
		    }

		    // (c) 靜默滿 1800ms：單字結束 → 補一個空格，回待機
		    if (space_timeout) {
		        space_timeout = 0;
		        HAL_UART_Transmit(&hlpuart1, (uint8_t*)" ", 1, 100);
		        set_leds(1, 0, 0);                          // 回綠燈
		    }
	  }
  /* USER CODE END 3 */
}

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
  HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1_BOOST);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI;
  RCC_OscInitStruct.PLL.PLLM = RCC_PLLM_DIV4;
  RCC_OscInitStruct.PLL.PLLN = 85;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
  RCC_OscInitStruct.PLL.PLLQ = RCC_PLLQ_DIV2;
  RCC_OscInitStruct.PLL.PLLR = RCC_PLLR_DIV2;
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
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_4) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief LPUART1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_LPUART1_UART_Init(void)
{

  /* USER CODE BEGIN LPUART1_Init 0 */

  /* USER CODE END LPUART1_Init 0 */

  /* USER CODE BEGIN LPUART1_Init 1 */

  /* USER CODE END LPUART1_Init 1 */
  hlpuart1.Instance = LPUART1;
  hlpuart1.Init.BaudRate = 115200;
  hlpuart1.Init.WordLength = UART_WORDLENGTH_8B;
  hlpuart1.Init.StopBits = UART_STOPBITS_1;
  hlpuart1.Init.Parity = UART_PARITY_NONE;
  hlpuart1.Init.Mode = UART_MODE_TX_RX;
  hlpuart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  hlpuart1.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
  hlpuart1.Init.ClockPrescaler = UART_PRESCALER_DIV1;
  hlpuart1.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
  if (HAL_UART_Init(&hlpuart1) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_UARTEx_SetTxFifoThreshold(&hlpuart1, UART_TXFIFO_THRESHOLD_1_8) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_UARTEx_SetRxFifoThreshold(&hlpuart1, UART_RXFIFO_THRESHOLD_1_8) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_UARTEx_DisableFifoMode(&hlpuart1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN LPUART1_Init 2 */

  /* USER CODE END LPUART1_Init 2 */

}

/**
  * @brief TIM2 Initialization Function
  * @param None
  * @retval None
  */
static void MX_TIM2_Init(void)
{

  /* USER CODE BEGIN TIM2_Init 0 */

  /* USER CODE END TIM2_Init 0 */

  TIM_ClockConfigTypeDef sClockSourceConfig = {0};
  TIM_MasterConfigTypeDef sMasterConfig = {0};

  /* USER CODE BEGIN TIM2_Init 1 */

  /* USER CODE END TIM2_Init 1 */
  htim2.Instance = TIM2;
  htim2.Init.Prescaler = 169;
  htim2.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim2.Init.Period = 999;
  htim2.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim2.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if (HAL_TIM_Base_Init(&htim2) != HAL_OK)
  {
    Error_Handler();
  }
  sClockSourceConfig.ClockSource = TIM_CLOCKSOURCE_INTERNAL;
  if (HAL_TIM_ConfigClockSource(&htim2, &sClockSourceConfig) != HAL_OK)
  {
    Error_Handler();
  }
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim2, &sMasterConfig) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN TIM2_Init 2 */

  /* USER CODE END TIM2_Init 2 */

}

/**
  * @brief TIM3 Initialization Function
  * @param None
  * @retval None
  */
static void MX_TIM3_Init(void)
{

  /* USER CODE BEGIN TIM3_Init 0 */

  /* USER CODE END TIM3_Init 0 */

  TIM_MasterConfigTypeDef sMasterConfig = {0};
  TIM_OC_InitTypeDef sConfigOC = {0};

  /* USER CODE BEGIN TIM3_Init 1 */

  /* USER CODE END TIM3_Init 1 */
  htim3.Instance = TIM3;
  htim3.Init.Prescaler = 169;
  htim3.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim3.Init.Period = 1249;
  htim3.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim3.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if (HAL_TIM_PWM_Init(&htim3) != HAL_OK)
  {
    Error_Handler();
  }
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim3, &sMasterConfig) != HAL_OK)
  {
    Error_Handler();
  }
  sConfigOC.OCMode = TIM_OCMODE_PWM1;
  sConfigOC.Pulse = 625;
  sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
  sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;
  if (HAL_TIM_PWM_ConfigChannel(&htim3, &sConfigOC, TIM_CHANNEL_1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN TIM3_Init 2 */

  /* USER CODE END TIM3_Init 2 */
  HAL_TIM_MspPostInit(&htim3);

}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  /* USER CODE BEGIN MX_GPIO_Init_1 */

  /* USER CODE END MX_GPIO_Init_1 */

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOF_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOA, LD2_Pin|LED_BLUE_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOB, LED_YELLOW_Pin|LED_GREEN_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pins : PA0 PA1 */
  GPIO_InitStruct.Pin = GPIO_PIN_0|GPIO_PIN_1;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_FALLING;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /*Configure GPIO pins : LD2_Pin LED_BLUE_Pin */
  GPIO_InitStruct.Pin = LD2_Pin|LED_BLUE_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /*Configure GPIO pins : LED_YELLOW_Pin LED_GREEN_Pin */
  GPIO_InitStruct.Pin = LED_YELLOW_Pin|LED_GREEN_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /* EXTI interrupt init*/
  HAL_NVIC_SetPriority(EXTI0_IRQn, 3, 0);
  HAL_NVIC_EnableIRQ(EXTI0_IRQn);

  HAL_NVIC_SetPriority(EXTI1_IRQn, 3, 0);
  HAL_NVIC_EnableIRQ(EXTI1_IRQn);

  /* USER CODE BEGIN MX_GPIO_Init_2 */

  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
//HAL_UART_Receive_IT(&hlpuart1, &rx_byte, 1)告訴 UART：「收到 1 個位元組就存到 rx_byte，然後打斷 CPU 通知我。」
{
    if (huart->Instance == LPUART1) {
        if (rx_byte == '\n') {	    				// 收到換行：一行結束
            rx_buf[rx_idx] = '\0';  				// 字串結尾
            rx_idx = 0;								// 下次從頭存(清空_rx_idx的內容
            cmd_ready = 1;		   					// 通知主迴圈：有一行可處理
        } else if (rx_byte != '\r' && rx_idx < sizeof(rx_buf) - 1)
        {//32 格，留 1 格給 \0），超過就丟棄，防止寫到陣列外面（越界）
            rx_buf[rx_idx++] = rx_byte;				// 存進緩衝區
        }
        HAL_UART_Receive_IT(&hlpuart1, &rx_byte, 1);  // 繼續收下一個
    }
}






// 按鍵中斷（下降緣）：PA0 = 短音鍵（點）、PA1 = 長音鍵（劃）
// 原則：中斷裡只「做紀錄」，不做耗時的事
// 實際處理按鍵的函式
static void handle_button(uint16_t GPIO_Pin)//去彈跳
{
    static uint32_t last_tick = 0;
    uint32_t now = HAL_GetTick();

    if (now - last_tick < 200) return;                 // 間隔拉長到 200ms

    // 確認腳位現在真的是低電位（被按著），不是放開時的抖動
    if (HAL_GPIO_ReadPin(GPIOA, GPIO_Pin) != GPIO_PIN_RESET) return;

    if (state == STATE_PLAYING || pc_playing) return;
    if (pending_elem != 0) return;//上一個按鍵主迴圈還沒取走，不重複記

    if (GPIO_Pin == GPIO_PIN_0)      pending_elem = '.';
    else if (GPIO_Pin == GPIO_PIN_1) pending_elem = '-';
    else return;

    last_tick = now;
}

// 新版 HAL 呼叫這個
void HAL_GPIO_EXTI_Falling_Callback(uint16_t GPIO_Pin) { handle_button(GPIO_Pin); }
// 舊版 HAL 呼叫這個
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)         { handle_button(GPIO_Pin); }

// TIM2 每 1ms 進來一次：負責「音長倒數」和「靜默計時」
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
    if (htim->Instance != TIM2) return;             // 只處理 TIM2

    if (state == STATE_PLAYING) {
        if (tone_remaining_ms > 0) tone_remaining_ms--;
        if (tone_remaining_ms == 0) {               // 這個音響夠久了
            tone_off();                             // 蜂鳴器和 LD2 一起關
            silence_ms = 0;                         // 靜默計時從 0 開始
            state = STATE_WAIT_CHAR;                // 進入「等待接續」
        }
    }
    else if (state == STATE_WAIT_CHAR) {
        silence_ms++;
        if (silence_ms == 700)  char_timeout = 1;   // 滿 700ms：通知主迴圈解碼
        if (silence_ms == 1800) {                   // 滿 1800ms：通知主迴圈補空格
            space_timeout = 1;
            state = STATE_IDLE;                     // 回待機，不再計時
        }
    }
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
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
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
