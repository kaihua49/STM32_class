# STM32_class
## 專案目錄結構

```text
Lab3/
├── Core/                            # 使用者核心程式碼與設定
│   ├── Inc/                         # 標頭檔 (.h) 目錄
│   │   ├── main.h                   # 全域引腳定義與主要標頭檔
│   │   ├── stm32f4xx_hal_conf.h     # HAL 庫模組啟用與硬體參數設定檔
│   │   └── stm32f4xx_it.h           # 中斷服務常式標頭檔
│   ├── Src/                         # 主程式與系統原始碼 (.c) 目錄
│   │   ├── main.c                   # 主程式（SystemClock、GPIO、外設初始化與主迴圈）
│   │   ├── stm32f4xx_hal_msp.c      # 微控制器支援套件 (MSP) 初始化程式
│   │   ├── stm32f4xx_it.c           # 中斷處理函式 (Interrupt Service Routines)
│   │   └── system_stm32f4xx.c       # 系統時鐘與核心底層初始化
│   └── Startup/                     # 晶片啟動程式碼
│       └── startup_stm32f4xx.s      # 組合語言啟動檔（設定中斷向量表與堆疊）
├── Drivers/                         # 晶片原廠與 CMSIS 驅動庫
│   ├── CMSIS/                       # ARM Cortex-M 核心標準介面
│   │   ├── Device/ST/STM32F4xx/     # 晶片暫存器定義與系統標頭檔
│   │   └── Include/                 # Cortex-M 核心暫存器與指令集定義
│   └── STM32F4xx_HAL_Driver/        # ST 官方 HAL 硬體抽象層驅動
│       ├── Inc/                     # HAL 庫標頭檔 (如 stm32f4xx_hal_gpio.h)
│       └── Src/                     # HAL 庫原始碼 (如 stm32f4xx_hal_gpio.c)
├── .cproject                        # Eclipse / STM32CubeIDE 專案編譯設定檔
├── .project                         # Eclipse / STM32CubeIDE 專案架構檔
├── Lab3.ioc                         # STM32CubeMX 圖形化周邊配置專案檔
├── STM32F401RETX_FLASH.ld           # 記憶體配置與鏈結腳本 (Linker Script)
└── README.md                        # 專案說明文件
