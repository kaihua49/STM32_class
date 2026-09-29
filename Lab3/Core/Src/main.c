/* USER CODE BEGIN Header */
/**
  * @file           : main.c
  * @brief          : Main program body
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN PV */

/* =========================
   State 定義
   使用 int 管理狀態
   0 = NORMAL
   1 = WARNING
   2 = EMERGENCY
   ========================= */
#define STATE_NORMAL      0
#define STATE_WARNING     1
#define STATE_EMERGENCY   2

int state = STATE_NORMAL;

/* =========================
   Timing 變數
   now      = 目前時間
   previous = 上一次 LED Toggle 的時間
   ========================= */
uint32_t now = 0;
uint32_t previous = 0;
uint32_t count = 0;
/* =========================
   STOP Event Flag
   由 EXTI Interrupt 設定
   ========================= */
volatile int STOP = 0;

/* =========================
   MODE Event
   ========================= */
int MODE_event = 0;

/* =========================
   MODE Button 狀態
   PC9 使用 Pull-up
   未按 = HIGH
   按下 = LOW
   利用 previous / current
   判斷按下瞬間
   ========================= */
GPIO_PinState mode_previous = GPIO_PIN_SET;
GPIO_PinState mode_current = GPIO_PIN_SET;

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);

/**
  * @brief  程式進入點
  * @retval int
  */
int main(void)
{
  /* =========================
     MCU 基本初始化
     ========================= */
  HAL_Init();
  SystemClock_Config();
  MX_GPIO_Init();

  /* USER CODE BEGIN 2 */
  /* =========================
     Initial State = NORMAL
     開機後：
     Green LED 亮
     Yellow LED 滅
     Red LED 滅
     ========================= */
  previous = HAL_GetTick();
  HAL_GPIO_WritePin(GPIOC, GPIO_PIN_8, GPIO_PIN_SET);    // Green ON
  HAL_GPIO_WritePin(GPIOC, GPIO_PIN_5, GPIO_PIN_RESET);  // Yellow OFF
  HAL_GPIO_WritePin(GPIOC, GPIO_PIN_6, GPIO_PIN_RESET);  // Red OFF
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* =====================================================
       Step 1：Read Events
       讀取輸入事件與目前時間
       ===================================================== */

    /* 每一圈先清除 MODE Event */
    MODE_event = 0;

    /* =========================
       MODE Button
       PC9
       Pull-up
       Active Low
       Polling
       ========================= */
    mode_current = HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_9);

    /* =========================
       偵測按下瞬間
       前一次 = HIGH
       現在   = LOW
       代表按鍵剛被按下
       ========================= */
    if ((mode_previous == GPIO_PIN_SET) && (mode_current == GPIO_PIN_RESET))
    {
        /* =========================
           Debounce
           HAL_Delay 僅用於按鍵防彈跳
           ========================= */
        HAL_Delay(50);

        /* 50 ms 後再次確認按鍵仍然被按下 */
        if (HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_9) == GPIO_PIN_RESET)
        {
            MODE_event = 1;
        }
    }

    /* 儲存這一圈的按鍵狀態 給下一圈比較 */
    mode_previous = mode_current;

    /* 取得目前系統時間 */
    now = HAL_GetTick();

    /* =====================================================
       Step 2：Write Event / Transition
       根據：
       Current State + Event
       決定 Next State
       ===================================================== */

    /* =========================
       NORMAL + MODE
       → WARNING
       ========================= */
    if ((state == STATE_NORMAL) && (MODE_event == 1))
    {
        state = STATE_WARNING;
        /* 進入新 State 時重新開始計時 */
        previous = now;
        count = now;
        /* WARNING 初始輸出 */
        HAL_GPIO_WritePin(GPIOC, GPIO_PIN_8, GPIO_PIN_RESET); // Green OFF
        HAL_GPIO_WritePin(GPIOC, GPIO_PIN_5, GPIO_PIN_SET);   // Yellow ON
        HAL_GPIO_WritePin(GPIOC, GPIO_PIN_6, GPIO_PIN_RESET); // Red OFF
    }
    /* =========================
       WARNING + MODE
       → NORMAL
       ========================= */
    else if ((state == STATE_WARNING) && ((MODE_event == 1)||(now - count >= 5000)))
    {
        state = STATE_NORMAL;
        /* 進入新 State 時重新開始計時 */
        previous = now;
        /* NORMAL 初始輸出 */
        HAL_GPIO_WritePin(GPIOC, GPIO_PIN_8, GPIO_PIN_SET);   // Green ON
        HAL_GPIO_WritePin(GPIOC, GPIO_PIN_5, GPIO_PIN_RESET); // Yellow OFF
        HAL_GPIO_WritePin(GPIOC, GPIO_PIN_6, GPIO_PIN_RESET); // Red OFF
    }
    /* =========================
       EMERGENCY + MODE
       → 保持 EMERGENCY
       MODE 在 Emergency 狀態無效
       ========================= */
    else if ((state == STATE_EMERGENCY) && (MODE_event == 1))
    {
        state = STATE_EMERGENCY;
    }

    /* =========================
       STOP Event
       任意 State → EMERGENCY
       STOP 優先處理
       ========================= */
    if (STOP == 1)
    {
        state = STATE_EMERGENCY;
        /* Event 處理完成後清除 Flag */
        STOP = 0;
        /* EMERGENCY 輸出 */
        HAL_GPIO_WritePin(GPIOC, GPIO_PIN_8, GPIO_PIN_RESET); // Green OFF
        HAL_GPIO_WritePin(GPIOC, GPIO_PIN_5, GPIO_PIN_RESET); // Yellow OFF
        HAL_GPIO_WritePin(GPIOC, GPIO_PIN_6, GPIO_PIN_SET);   // Red ON
    }

    /* =====================================================
       Step 3：Write Behavior
       根據目前 State 執行對應行為
       ===================================================== */

    /* =========================
       NORMAL
       Green LED = 1 Hz Blink
       每 500 ms Toggle 一次
       ON 500 ms / OFF 500 ms
       完整週期 = 1 秒
       ========================= */
    if (state == STATE_NORMAL)
    {
        if ((now - previous) >= 500)
        {
            previous = now;
            HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_8);
        }
    }
    /* =========================
       WARNING
       Yellow LED = 2 Hz Blink
       每 250 ms Toggle 一次
       ON 250 ms / OFF 250 ms
       完整週期 = 0.5 秒
       ========================= */
    else if (state == STATE_WARNING)
    {
        if ((now - previous) >= 250)
        {
            previous = now;
            HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_5);
        }
    }
    /* =========================
       EMERGENCY
       Red LED Always ON
       Green / Yellow OFF
       ========================= */
    else if (state == STATE_EMERGENCY)
    {
        HAL_GPIO_WritePin(GPIOC, GPIO_PIN_8, GPIO_PIN_RESET); // Green OFF
        HAL_GPIO_WritePin(GPIOC, GPIO_PIN_5, GPIO_PIN_RESET); // Yellow OFF
        HAL_GPIO_WritePin(GPIOC, GPIO_PIN_6, GPIO_PIN_SET);   // Red ON
    }
  }
  /* USER CODE END WHILE */
}

/**
  * @brief 系統時脈設定
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /* 設定 MCU 電壓等級 */
  HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1_BOOST);

  /* =========================
     使用 HSI + PLL
     設定系統時脈
     ========================= */
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

  /* 設定 CPU / AHB / APB Clock */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
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
  * @brief GPIO 初始化
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};

  /* =========================
     開啟 GPIO Port Clock
     ========================= */
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOF_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /* =========================
     LED 初始輸出 LOW
     ========================= */
  HAL_GPIO_WritePin(GPIOC, GPIO_PIN_5 | GPIO_PIN_6 | GPIO_PIN_8, GPIO_PIN_RESET);

  /* =====================================================
     PC13 STOP Button
     Pull-down / 未按=LOW / 按下=HIGH / Active High
     Rising Edge 觸發 EXTI
     ===================================================== */
  GPIO_InitStruct.Pin = GPIO_PIN_13;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_RISING;
  GPIO_InitStruct.Pull = GPIO_PULLDOWN;
  HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

  /* =====================================================
     LED Output
     PC5 = Yellow LED
     PC6 = Red LED
     PC8 = Green LED
     ===================================================== */
  GPIO_InitStruct.Pin = GPIO_PIN_5 | GPIO_PIN_6 | GPIO_PIN_8;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

  /* =====================================================
     PC9 MODE Button
     Pull-up / 未按=HIGH / 按下=LOW / Active Low
     使用 Polling
     ===================================================== */
  GPIO_InitStruct.Pin = GPIO_PIN_9;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

  /* =====================================================
     啟用 PC13 EXTI Interrupt
     PC13 屬於 EXTI15_10
     ===================================================== */
  HAL_NVIC_SetPriority(EXTI15_10_IRQn, 1, 0);
  HAL_NVIC_EnableIRQ(EXTI15_10_IRQn);
}

/* USER CODE BEGIN 4 */
/* =========================================================
   STOP Button EXTI Callback
   Interrupt 發生時：不直接執行 Emergency Behavior
   只設定 STOP Event Flag
   之後由 main loop 處理 State Transition
   ========================================================= */
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
  /* 確認 Interrupt 來源為 PC13 */
  if (GPIO_Pin == GPIO_PIN_13)
  {
    STOP = 1;
  }
}
/* USER CODE END 4 */

/**
  * @brief 錯誤處理
  * @retval None
  */
void Error_Handler(void)
{
  __disable_irq();
  while (1)
  {
  }
}

#ifdef USE_FULL_ASSERT
void assert_failed(uint8_t *file, uint32_t line)
{
}
#endif
