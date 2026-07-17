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
#include "e220.h"
#include <stdio.h>
#include <string.h>
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
/* ----------------------------------------------------------------------------
 *  E220 pin haritasi (NUCLEO-F446RE)
 *    M0  = PB0   (GPIO cikis)
 *    M1  = PB1   (GPIO cikis)
 *    AUX = PA8   (GPIO giris)
 *    LD2 = PA5   (durum LED'i - her gonderimde toggle)
 * -------------------------------------------------------------------------- */
#define E220_M0_PORT    GPIOB
#define E220_M0_PIN     GPIO_PIN_0
#define E220_M1_PORT    GPIOB
#define E220_M1_PIN     GPIO_PIN_1
#define E220_AUX_PORT   GPIOA
#define E220_AUX_PIN    GPIO_PIN_8

#define LD2_PORT        GPIOA
#define LD2_PIN         GPIO_PIN_5

/* ----------------------------------------------------------------------------
 *  Uygulama modu secimi (E220_APP_MODE):
 *    E220_APP_READ  = STM32 acilista modulu SADECE OKUR (g_e220_cfg'ye) ve
 *                     mevcut ayari korur. Hicbir sey yazmaz.
 *    E220_APP_WRITE = Asagida "WRITE modu" blogunda VERDIGIN degerleri mod_le
 *                     YAZAR. Geri okuma / dogrulama YAPMAZ.
 *
 *  Kullanim: modu degistirmek icin sadece E220_APP_MODE satirini duzenle.
 * -------------------------------------------------------------------------- */
#define E220_APP_READ    0
#define E220_APP_WRITE   1

#define E220_APP_MODE    E220_APP_READ
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */
/* ============================================================================
 *  CubeMX (.ioc) AYAR OZETI  -  NUCLEO-F446RE / STM32F446RET6
 * ----------------------------------------------------------------------------
 *  RCC   : HSI (16 MHz), PLL yok. SYSCLK = HCLK = APB1 = APB2 = 16 MHz.
 *          (9600 ve 115200 baud icin yeterli hassasiyet saglar.)
 *
 *  SYS   : Debug -> Serial Wire (SWD), Timebase -> SysTick.
 *
 *  USART1 (Connectivity):
 *      Mode = Asynchronous, Baud = 9600, 8 bit, Parity None, 1 stop.
 *      Pinler: PA9 = USART1_TX, PA10 = USART1_RX (AF7).
 *      -> E220 modulu ile haberlesme (config + veri).
 *      NOT: Config modu daima 9600'dur. Veri modu baud'u konfigurasyona bagli
 *           degisir; surucu (e220.c) mod degisiminde huart1 baud'unu otomatik
 *           ayarlar.
 *
 *  (USART2/printf debug ciktisi kaldirildi - izleme Live Expressions ile.)
 *
 *  GPIO (System Core -> GPIO):
 *      PB0 = GPIO_Output (E220 M0)   -> Push-Pull, No pull, Low speed
 *      PB1 = GPIO_Output (E220 M1)   -> Push-Pull, No pull, Low speed
 *      PA8 = GPIO_Input  (E220 AUX)  -> No pull
 *      PA5 = GPIO_Output (LD2 LED)   -> Push-Pull, No pull, Low speed
 *
 *  Not: USART clock'lari ve GPIO clock'lari HAL MSP (stm32f4xx_hal_msp.c) ve
 *       MX_GPIO_Init() icinde otomatik etkinlestirilir.
 * ==========================================================================*/
/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
UART_HandleTypeDef huart1;

/* USER CODE BEGIN PV */
/* E220 surucu tutamaci */
static E220_Handle e220;

/* Live Expressions'ta izlenebilmesi icin config yapisini dosya-seviyesi
   static yaptik. Boylece her zaman kapsam icindedir ve pencerede alanlari
   (channel, tx_power, air_rate ...) surekli gorunur.
     - READ  modunda : moduldan OKUNAN aktif konfigurasyon
     - WRITE modunda : mod_le YAZILAN konfigurasyon                        */
static E220_Config g_e220_cfg;

/* --- Live Expressions izleme degiskenleri (printf yerine) ---------------- *
 *  g_read_status / g_write_status : ilgili islemin dönüş kodu (0 = E220_OK)
 *  g_tx_count         : gönderilen paket sayacı (2 sn'de bir artar - heartbeat)
 *  g_last_send_status : son E220_SendData dönüş kodu
 *  g_last_rx_len      : son alınan paketin bayt sayısı
 *  g_rx_buf           : son alınan paketin içeriği (metin) */
#if (E220_APP_MODE == E220_APP_READ)
static volatile E220_Status g_read_status        = E220_ERR_TIMEOUT;
#else
static volatile E220_Status g_write_status        = E220_ERR_TIMEOUT;
#endif
static volatile uint32_t    g_tx_count           = 0;
static volatile E220_Status g_last_send_status    = E220_OK;
static volatile uint16_t    g_last_rx_len         = 0;
static uint8_t              g_rx_buf[64];
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_USART1_UART_Init(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* USART2/printf debug ciktisi kaldirildi; izleme yalnizca Live Expressions
   uzerinden yapiliyor (asagidaki g_* global degiskenleri). */

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
  MX_USART1_UART_Init();
  /* USER CODE BEGIN 2 */

  /* --- E220 surucusunu baslat (UART + M0/M1/AUX pinleri) --------------- */
  E220_Init(&e220, &huart1,
            E220_M0_PORT, E220_M0_PIN,
            E220_M1_PORT, E220_M1_PIN,
            E220_AUX_PORT, E220_AUX_PIN);

#if (E220_APP_MODE == E220_APP_READ)
  /* ====================== READ MODU ==================================== *
   *  Modulde HALIHAZIRDA ne varsa oku ve koru. Hicbir sey yazma.
   *  Live Expr: g_e220_cfg (okunan ayarlar), g_read_status (0 = OK).       */
  g_read_status = E220_ReadConfig(&e220, &g_e220_cfg);

#else /* E220_APP_MODE == E220_APP_WRITE */
  /* ====================== WRITE MODU =================================== *
   *  Asagidaki degerleri mod_le YAZAR. Geri okuma / dogrulama YOK.
   *  >>> KENDI DEGERLERINI BURADA DUZENLE <<<
   *  Not: WriteConfig 8 register'in tamamini yazar; bu yuzden tum alanlar
   *       set edilmelidir. permanent=true => kalici (flash), false => gecici.
   *  Not: uart_baud, modul<->STM32 VERI modu hizi; 9600 disi secersen surucu
   *       Normal moda gecerken huart1'i otomatik ayarlar (config yine 9600). */
  g_e220_cfg.addh              = 0x00;
  g_e220_cfg.addl              = 49;                 /* adres dusuk bayt     */
  g_e220_cfg.uart_baud         = E220_UART_115200;     /* veri modu UART hizi  */
  g_e220_cfg.parity            = E220_PARITY_8N1;
  g_e220_cfg.air_rate          = E220_AIR_2K4;      /* havadaki hiz         */
  g_e220_cfg.sub_packet        = E220_SUBPKT_200;
  g_e220_cfg.rssi_ambient      = false;
  g_e220_cfg.tx_power          = E220_TXP_13dBm;
  g_e220_cfg.channel           = 59;                 /* 410.125 + 55 = 465.125 MHz */
  g_e220_cfg.rssi_byte         = true;
  g_e220_cfg.fixed_transmission = true;             /* transparan           */
  g_e220_cfg.lbt               = true;
  g_e220_cfg.wor_cycle         = E220_WOR_500MS;

  g_write_status = E220_WriteConfig(&e220, &g_e220_cfg, true /* kalici */);
#endif

  /* Veri gonderimi icin Normal moda gec */
  E220_SetMode(&e220, E220_MODE_NORMAL);
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  uint32_t last_tx = 0;
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
    /* --- Her 2 saniyede bir gonder (Live Expr: g_tx_count, g_last_send_status) */
    if ((HAL_GetTick() - last_tx) >= 2000U) {
      last_tx = HAL_GetTick();

      char msg[48];
      int  n = snprintf(msg, sizeof(msg), "Hello LoRa #%lu\r\n",
                        (unsigned long)g_tx_count);
      g_last_send_status = E220_SendData(&e220, (uint8_t *)msg, (uint16_t)n);
      if (g_last_send_status == E220_OK) {
        g_tx_count++;
        HAL_GPIO_TogglePin(LD2_PORT, LD2_PIN);  /* her gonderimde LED toggle */
      }
    }

    /* --- Gelen veriyi (varsa) dinle (Live Expr: g_last_rx_len, g_rx_buf) --- */
    uint16_t rxlen = 0;
    if (E220_ReceiveData(&e220, g_rx_buf, sizeof(g_rx_buf) - 1, &rxlen, 50U) == E220_OK
        && rxlen > 0) {
      g_rx_buf[rxlen] = '\0';   /* Live Expr'de metin olarak okunabilsin */
      g_last_rx_len = rxlen;
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
  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE3);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_NONE;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_HSI;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_0) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief USART1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART1_UART_Init(void)
{

  /* USER CODE BEGIN USART1_Init 0 */

  /* USER CODE END USART1_Init 0 */

  /* USER CODE BEGIN USART1_Init 1 */

  /* USER CODE END USART1_Init 1 */
  huart1.Instance = USART1;
  huart1.Init.BaudRate = 9600;
  huart1.Init.WordLength = UART_WORDLENGTH_8B;
  huart1.Init.StopBits = UART_STOPBITS_1;
  huart1.Init.Parity = UART_PARITY_NONE;
  huart1.Init.Mode = UART_MODE_TX_RX;
  huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart1.Init.OverSampling = UART_OVERSAMPLING_16;
  if (HAL_UART_Init(&huart1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART1_Init 2 */

  /* USER CODE END USART1_Init 2 */

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
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_0|GPIO_PIN_1, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level : PA5 (LD2) */
  HAL_GPIO_WritePin(LD2_PORT, LD2_PIN, GPIO_PIN_RESET);

  /*Configure GPIO pins : PB0 PB1 (E220 M0 / M1 -> cikis) */
  GPIO_InitStruct.Pin = GPIO_PIN_0|GPIO_PIN_1;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /*Configure GPIO pin : PA8 (E220 AUX -> giris) */
  GPIO_InitStruct.Pin = GPIO_PIN_8;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /*Configure GPIO pin : PA5 (LD2 durum LED'i -> cikis) */
  GPIO_InitStruct.Pin = LD2_PIN;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(LD2_PORT, &GPIO_InitStruct);

  /* USER CODE BEGIN MX_GPIO_Init_2 */

  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */

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
