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
#include "i2c.h"
#include "spi.h"
#include "usart.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <stdbool.h>
#include <stdio.h>
#include "bmp280.h"
#include "gc9a01.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define UART_TX_TIMEOUT_MS   200U
#define SENSOR_PERIOD_MS     300U   /* longer than sensor cycle (250 ms + conversion) */
#define SENSOR_RETRY_MS      2000U
#define SENSOR_MAX_ERRORS    3U     /* failed reads before bus recovery */

/* LCD layout: the screen is round, so rows near the edges are shorter */
#define UI_TEXT_SIZE         24U   /* text buffer, including terminator */
#define UI_STATUS_Y          28
#define UI_STATUS_X          48
#define UI_STATUS_WIDTH      144
#define UI_TEMP_LABEL_Y      58
#define UI_TEMP_VALUE_Y      76
#define UI_PRESS_LABEL_Y     108
#define UI_PRESS_VALUE_Y     126
#define UI_HUM_LABEL_Y       158
#define UI_HUM_VALUE_Y       176
#define UI_RING_RADIUS       118
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */
static BMP280_HandleTypedef bmp280;
static float pressure, temperature, humidity;

static const GC9A01_HandleTypeDef lcd = {
  .hspi = &hspi2,
  .cs_port = LCD_CS_GPIO_Port,
  .cs_pin = LCD_CS_Pin,
  .dc_port = LCD_DC_GPIO_Port,
  .dc_pin = LCD_DC_Pin,
  .res_port = LCD_RES_GPIO_Port,
  .res_pin = LCD_RES_Pin
};
static bool lcdReady;
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */
static void Sensor_Start(void);
static void UI_Start(void);
static void UI_Check(HAL_StatusTypeDef status);
static void UI_ShowStatus(const char *text, uint16_t color);
static void UI_ShowSensorName(void);
static void UI_ShowValues(void);
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
int _write(int file, char *ptr, int len)
{
  (void)file;
  if (HAL_UART_Transmit(&huart2, (uint8_t *)ptr, (uint16_t)len, UART_TX_TIMEOUT_MS) != HAL_OK)
  {
    return -1;
  }
  return len;
}

/* Initializes the sensor, recovering the I2C bus between attempts. */
static void Sensor_Start(void)
{
  while (!bmp280_init(&bmp280, &bmp280.params))
  {
    printf("BMP280 initialization failed\r\n");
    UI_ShowStatus("Brak czujnika", GC9A01_RED);
    HAL_Delay(SENSOR_RETRY_MS);
    I2C1_Recover();
  }
  printf("BMP280: found %s\r\n", (bmp280.id == BME280_CHIP_ID) ? "BME280" : "BMP280");
  UI_ShowSensorName();
}

/* Turns the LCD output off after a failed SPI transfer, so that a display
   problem never stops the measurements. */
static void UI_Check(HAL_StatusTypeDef status)
{
  if (status != HAL_OK)
  {
    lcdReady = false;
    printf("LCD transfer failed\r\n");
  }
}

/* Initializes the LCD and draws the fixed part of the screen. */
static void UI_Start(void)
{
  lcdReady = true;
  UI_Check(GC9A01_Init(&lcd));
  if (lcdReady)
  {
    UI_Check(GC9A01_FillScreen(&lcd, GC9A01_BLACK));
  }
  if (lcdReady)
  {
    UI_Check(GC9A01_DrawCircle(&lcd, GC9A01_WIDTH / 2, GC9A01_HEIGHT / 2, UI_RING_RADIUS, GC9A01_GRAY));
  }
  if (lcdReady)
  {
    UI_Check(GC9A01_DrawStringCentered(&lcd, UI_TEMP_LABEL_Y, "Temperatura", &Font16, GC9A01_GRAY, GC9A01_BLACK));
  }
  if (lcdReady)
  {
    UI_Check(GC9A01_DrawStringCentered(&lcd, UI_PRESS_LABEL_Y, "Cisnienie", &Font16, GC9A01_GRAY, GC9A01_BLACK));
  }
}

/* Shows a short message (max 13 characters) in the top row. */
static void UI_ShowStatus(const char *text, uint16_t color)
{
  if (lcdReady)
  {
    UI_Check(GC9A01_FillRect(&lcd, UI_STATUS_X, UI_STATUS_Y, UI_STATUS_WIDTH, Font16.Height, GC9A01_BLACK));
  }
  if (lcdReady)
  {
    UI_Check(GC9A01_DrawStringCentered(&lcd, UI_STATUS_Y, text, &Font16, color, GC9A01_BLACK));
  }
}

static void UI_ShowSensorName(void)
{
  UI_ShowStatus((bmp280.id == BME280_CHIP_ID) ? "BME280" : "BMP280", GC9A01_GREEN);
}

/* Fixed field widths: each new value overwrites the old one, without flicker. */
static void UI_ShowValues(void)
{
  char text[UI_TEXT_SIZE];

  if (lcdReady)
  {
    (void)snprintf(text, sizeof(text), "%6.2f C", temperature);
    UI_Check(GC9A01_DrawStringCentered(&lcd, UI_TEMP_VALUE_Y, text, &Font24, GC9A01_ORANGE, GC9A01_BLACK));
  }
  if (lcdReady)
  {
    (void)snprintf(text, sizeof(text), "%7.2f hPa", pressure / 100.0f);
    UI_Check(GC9A01_DrawStringCentered(&lcd, UI_PRESS_VALUE_Y, text, &Font24, GC9A01_CYAN, GC9A01_BLACK));
  }
  if (bmp280.id == BME280_CHIP_ID)
  {
    if (lcdReady)
    {
      UI_Check(GC9A01_DrawStringCentered(&lcd, UI_HUM_LABEL_Y, "Wilgotnosc", &Font16, GC9A01_GRAY, GC9A01_BLACK));
    }
    if (lcdReady)
    {
      (void)snprintf(text, sizeof(text), "%6.2f %%", humidity);
      UI_Check(GC9A01_DrawStringCentered(&lcd, UI_HUM_VALUE_Y, text, &Font24, GC9A01_YELLOW, GC9A01_BLACK));
    }
  }
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
  MX_USART2_UART_Init();
  MX_I2C1_Init();
  MX_SPI2_Init();
  /* USER CODE BEGIN 2 */

  printf("Start\r\n");
  UI_Start();
  bmp280_init_default_params(&bmp280.params);
  bmp280.addr = BMP280_I2C_ADDRESS_0;
  bmp280.i2c = &hi2c1;
  Sensor_Start();

  uint32_t sensorErrors = 0;

  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {

    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
    HAL_Delay(SENSOR_PERIOD_MS);

    if (bmp280_read_float(&bmp280, &temperature, &pressure, &humidity))
    {
      if (sensorErrors > 0U)
      {
        UI_ShowSensorName();
      }
      sensorErrors = 0;
      UI_ShowValues();
      printf("Press: %.2f hPa, Temp.: %.2f C", pressure / 100.0f, temperature);
      if (bmp280.id == BME280_CHIP_ID)
      {
        printf(", Hum.: %.2f %%", humidity);
      }
      printf("\r\n");
    }
    else
    {
      printf("Sensor reading failed\r\n");
      UI_ShowStatus("Blad odczytu", GC9A01_RED);
      sensorErrors++;
      if (sensorErrors >= SENSOR_MAX_ERRORS)
      {
        I2C1_Recover();
        Sensor_Start();
        sensorErrors = 0;
      }
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
  if (HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI;
  RCC_OscInitStruct.PLL.PLLM = 1;
  RCC_OscInitStruct.PLL.PLLN = 10;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV7;
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
