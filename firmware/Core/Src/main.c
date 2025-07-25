/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2023 STMicroelectronics.
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
#include <stdio.h>
#include "ssd1306.h"
#include <string.h>
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */
int stdin_getchar (void)
{
  return 0;
}

int stdout_putchar (int ch) {
  return 0;
}
void ttywrch (int ch) {
  return;
}
/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/*
2023 11 27 0.2.0
removed OLED dimming
no display fraction when > 600ms when single source
blank out result screen during a new measurement

2023 11 30 0.2.1
skipped blanking out at first measurement

2025 07 20
0.2.2
added 115us of light sensor compensation
*/

uint8_t fw_version_major = 0;
uint8_t fw_version_minor = 2;
uint8_t fw_version_patch = 2;


#define SHUTTER_STATE_IDLE 0
#define SHUTTER_STATE_TRIGGERED 1
#define SHUTTER_STATE_BOUNCE_DETECT 2
#define SHUTTER_STATE_TIMEOUT 3
#define SHUTTER_STATE_RESULT_AVAILABLE 4

#define PIN_STATE_NO_CHANGE 0
#define PIN_STATE_ACTIVATE 1
#define PIN_STATE_RELEASE 2

typedef struct
{
  uint32_t current_state;
  uint32_t trigger_ts;
  uint32_t release_ts;
  uint32_t duration;
  uint32_t bounce_count;
} shutter_state_machine;

void reset_ssm(shutter_state_machine* ssm)
{
  ssm->current_state = SHUTTER_STATE_IDLE;
  ssm->trigger_ts = 0;
  ssm->release_ts = 0;
  ssm->duration = 0;
  ssm->bounce_count = 0;
}

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */


#define micros() (htim2.Instance->CNT)

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
SPI_HandleTypeDef hspi1;

TIM_HandleTypeDef htim2;

UART_HandleTypeDef huart2;

/* USER CODE BEGIN PV */

int fputc(int ch, FILE *f)
{
    HAL_UART_Transmit(&huart2, (unsigned char *)&ch, 1, 100);
    return ch;
}

#define SSM_SOURCE_SIZE 3

#define SSM_SOURCE_HOTSHOE 0
#define SSM_SOURCE_PC 1
#define SSM_SOURCE_LIGHT_SENSOR 2

shutter_state_machine all_ssms[SSM_SOURCE_SIZE];

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_SPI1_Init(void);
static void MX_USART2_UART_Init(void);
static void MX_TIM2_Init(void);
/* USER CODE BEGIN PFP */

#define SHUTTER_TIMEOUT_SEC 10
#define SHUTTER_BOUNCE_TIMEOUT_MS 250

uint8_t ssm_update(shutter_state_machine* ssm, uint16_t pin_state)
{
  if(ssm->current_state == SHUTTER_STATE_IDLE && pin_state == PIN_STATE_ACTIVATE)
  {
    ssm->current_state = SHUTTER_STATE_TRIGGERED;
    ssm->trigger_ts = micros();
  }
  else if(ssm->current_state == SHUTTER_STATE_TRIGGERED && pin_state == PIN_STATE_RELEASE)
  {
    ssm->current_state = SHUTTER_STATE_BOUNCE_DETECT;
    ssm->release_ts = micros();
  }
  else if(ssm->current_state == SHUTTER_STATE_BOUNCE_DETECT && pin_state == PIN_STATE_ACTIVATE)
  {
    ssm->current_state = SHUTTER_STATE_TRIGGERED;
    ssm->bounce_count++;
  }
  else if(ssm->current_state == SHUTTER_STATE_TRIGGERED && pin_state == PIN_STATE_NO_CHANGE && micros() - ssm->trigger_ts > SHUTTER_TIMEOUT_SEC * 1000 * 1000)
  {
    ssm->current_state = SHUTTER_STATE_TIMEOUT;
  }
  else if(ssm->current_state == SHUTTER_STATE_BOUNCE_DETECT && pin_state == PIN_STATE_NO_CHANGE && micros() - ssm->release_ts > SHUTTER_BOUNCE_TIMEOUT_MS*1000)
  {
    ssm->duration = ssm->release_ts - ssm->trigger_ts;
    ssm->current_state = SHUTTER_STATE_RESULT_AVAILABLE;
  }
  return ssm->current_state;
}

uint8_t pinstate_translate(uint16_t idr_value)
{
  if(idr_value == 0)
  {
    USER_LED_GPIO_Port -> BSRR = USER_LED_Pin;
    return PIN_STATE_ACTIVATE;
  }
  else
  {
    USER_LED_GPIO_Port -> BRR = USER_LED_Pin;
    return PIN_STATE_RELEASE;
  }
}

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
  if(GPIO_Pin == HOTSHOE_Pin)
    ssm_update(&all_ssms[SSM_SOURCE_HOTSHOE], pinstate_translate(HOTSHOE_GPIO_Port->IDR & HOTSHOE_Pin));
  if(GPIO_Pin == PC_SYNC_Pin)
    ssm_update(&all_ssms[SSM_SOURCE_PC], pinstate_translate(PC_SYNC_GPIO_Port->IDR & PC_SYNC_Pin));
  if(GPIO_Pin == LIGHT_SENSOR_Pin)
    ssm_update(&all_ssms[SSM_SOURCE_LIGHT_SENSOR], pinstate_translate(!(LIGHT_SENSOR_GPIO_Port->IDR & LIGHT_SENSOR_Pin)));
}

#define TEMP_BUF_SIZE 32
char temp_str_buf1[TEMP_BUF_SIZE];
char temp_str_buf2[TEMP_BUF_SIZE];

void format_usec(char* buf, uint32_t time_micros) 
{
  if (time_micros >= 10000000) // More than 10 seconds
    sprintf(buf, "%.1fs", time_micros / 1000000.0);
  else if (time_micros >= 1000000) // More than 1 second
    sprintf(buf, "%.2fs", time_micros / 1000000.0);
  else if (time_micros >= 10000) // More than 10 milliseconds
    sprintf(buf, "%ums", time_micros / 1000);
  else if (time_micros >= 1000) // Between 1 and 10 milliseconds
    sprintf(buf, "%.1fms", time_micros / 1000.0);
  else // Less than 1 millisecond
    sprintf(buf, "%uus", time_micros);
}

float usec_to_fraction(uint32_t usec) 
{
  if(usec == 0)
    return 0;
  return 1000000.0 / usec;
}

void format_fraction(char* buf, uint32_t time_micros) 
{
  float fraction = 0;
  if(time_micros != 0)
    fraction = 1000000.0 / time_micros;

  if(time_micros < 100)
    sprintf(buf, "---");
  else if (fraction >= 10)
    sprintf(buf, "1/%.0f", fraction);
  else
    sprintf(buf, "1/%.1f", fraction);
}

uint8_t center_line(uint8_t line_len, uint8_t char_width_pixels, uint8_t oled_width_pixels)
{
  int16_t start_pixel = (oled_width_pixels - line_len * char_width_pixels) / 2;
  if(start_pixel < 0)
    start_pixel = 0;
  return start_pixel;
}

char* oled_str_hotshoe = "Hot Shoe";
char* oled_str_pc_socket = "PC Socket";
char* oled_str_light_sensor = "Light Sensor";

void print_single_result(char* title, shutter_state_machine* ssm)
{
  if(ssm->duration < 500)
    return;

  ssd1306_Fill(Black);
  ssd1306_SetCursor(center_line(strlen(title), 7, SSD1306_WIDTH), 0);
  ssd1306_WriteString(title, Font_7x10, White);

  format_usec(temp_str_buf1, ssm->duration);
  format_fraction(temp_str_buf2, ssm->duration);

  if(ssm->duration > 600*1000)
    memset(temp_str_buf2, 0, TEMP_BUF_SIZE);
  else
    strcat(temp_str_buf2, " ");

  uint8_t line_start = center_line(strlen(temp_str_buf1)+strlen(temp_str_buf2), 11, SSD1306_WIDTH);
  ssd1306_SetCursor(line_start, 11);

  ssd1306_WriteString(temp_str_buf2, Font_11x18, White);
  ssd1306_WriteString(temp_str_buf1, Font_11x18, White);

  ssd1306_UpdateScreen();
}

void print_double_result(char* title1, shutter_state_machine* ssm1, char* title2, shutter_state_machine* ssm2)
{
  ssd1306_Fill(Black);

  ssd1306_Line(63,0,63,32,White);

  ssd1306_SetCursor(center_line(strlen(title1), 7, SSD1306_WIDTH/2), 0);
  ssd1306_WriteString(title1, Font_7x10, White);

  ssd1306_SetCursor(center_line(strlen(title2), 7, SSD1306_WIDTH/2) + 64, 0);
  ssd1306_WriteString(title2, Font_7x10, White);

  format_usec(temp_str_buf1, ssm1->duration);
  format_fraction(temp_str_buf2, ssm1->duration);

  ssd1306_SetCursor(center_line(strlen(temp_str_buf1), 7, SSD1306_WIDTH/2), 11);
  ssd1306_WriteString(temp_str_buf1, Font_7x10, White);

  ssd1306_SetCursor(center_line(strlen(temp_str_buf2), 7, SSD1306_WIDTH/2), 22);
  ssd1306_WriteString(temp_str_buf2, Font_7x10, White);

  format_usec(temp_str_buf1, ssm2->duration);
  format_fraction(temp_str_buf2, ssm2->duration);

  ssd1306_SetCursor(center_line(strlen(temp_str_buf1), 7, SSD1306_WIDTH/2) + 64, 11);
  ssd1306_WriteString(temp_str_buf1, Font_7x10, White);

  ssd1306_SetCursor(center_line(strlen(temp_str_buf2), 7, SSD1306_WIDTH/2) + 64, 22);
  ssd1306_WriteString(temp_str_buf2, Font_7x10, White);

  ssd1306_UpdateScreen();
}

void print_triple_result(void)
{
  ssd1306_Fill(Black);

  ssd1306_SetCursor(center_line(strlen("HS"), 7, SSD1306_WIDTH/3), 0);
  ssd1306_WriteString("HS", Font_7x10, White);

  ssd1306_SetCursor(center_line(strlen("PC"), 7, SSD1306_WIDTH/3) + 42, 0);
  ssd1306_WriteString("PC", Font_7x10, White);

  ssd1306_SetCursor(center_line(strlen("LS"), 7, SSD1306_WIDTH/3) + 84, 0);
  ssd1306_WriteString("LS", Font_7x10, White);

  //-------------HOT SHOE-----------------
  format_usec(temp_str_buf1, all_ssms[SSM_SOURCE_HOTSHOE].duration);
  format_fraction(temp_str_buf2, all_ssms[SSM_SOURCE_HOTSHOE].duration);

  ssd1306_SetCursor(center_line(strlen(temp_str_buf1), 7, SSD1306_WIDTH/3), 11);
  ssd1306_WriteString(temp_str_buf1, Font_7x10, White);

  ssd1306_SetCursor(center_line(strlen(temp_str_buf2), 7, SSD1306_WIDTH/3), 22);
  ssd1306_WriteString(temp_str_buf2, Font_7x10, White);

  //-------------PC SOCKET-----------------
  format_usec(temp_str_buf1, all_ssms[SSM_SOURCE_PC].duration);
  format_fraction(temp_str_buf2, all_ssms[SSM_SOURCE_PC].duration);

  ssd1306_SetCursor(center_line(strlen(temp_str_buf1), 7, SSD1306_WIDTH/3) + 42, 11);
  ssd1306_WriteString(temp_str_buf1, Font_7x10, White);

  ssd1306_SetCursor(center_line(strlen(temp_str_buf2), 7, SSD1306_WIDTH/3) + 42 , 22);
  ssd1306_WriteString(temp_str_buf2, Font_7x10, White);

  //-------------LIGHT SENSOR-----------------
  format_usec(temp_str_buf1, all_ssms[SSM_SOURCE_LIGHT_SENSOR].duration);
  format_fraction(temp_str_buf2, all_ssms[SSM_SOURCE_LIGHT_SENSOR].duration);

  ssd1306_SetCursor(center_line(strlen(temp_str_buf1), 7, SSD1306_WIDTH/3) + 84, 11);
  ssd1306_WriteString(temp_str_buf1, Font_7x10, White);

  ssd1306_SetCursor(center_line(strlen(temp_str_buf2), 7, SSD1306_WIDTH/3) + 84 , 22);
  ssd1306_WriteString(temp_str_buf2, Font_7x10, White);

  ssd1306_Line(42,0,42,32,White);
  ssd1306_Line(85,0,85,32,White);

  ssd1306_UpdateScreen();
}

char* oled_str_ready = "READY";

void print_ready(void)
{
  ssd1306_Fill(Black);
  ssd1306_SetCursor(center_line(strlen(oled_str_ready), 11, SSD1306_WIDTH), 0);
  ssd1306_WriteString(oled_str_ready, Font_11x18, White);

  memset(temp_str_buf1, 0, TEMP_BUF_SIZE);
  sprintf(temp_str_buf1, "PulseHPT.com %d.%d.%d", fw_version_major, fw_version_minor, fw_version_patch);
  ssd1306_SetCursor(0, 20);
  ssd1306_WriteString(temp_str_buf1, Font_7x10, White);
  ssd1306_UpdateScreen();
}

void delay_us(uint32_t delay)
{
  uint32_t end_time = micros() + delay;
  while(micros() < end_time);
}

uint8_t count_state(uint8_t state)
{
  uint8_t bitfield = 0;
  for (uint8_t i = 0; i < SSM_SOURCE_SIZE; i++)
    if(all_ssms[i].current_state == state)
      bitfield |= 1 << i;
  return bitfield;
}

void ssm_reset_all(void)
{
  reset_ssm(&all_ssms[SSM_SOURCE_HOTSHOE]);
  reset_ssm(&all_ssms[SSM_SOURCE_PC]);
  reset_ssm(&all_ssms[SSM_SOURCE_LIGHT_SENSOR]);
}

void ssm_update_all(void)
{
  ssm_update(&all_ssms[SSM_SOURCE_HOTSHOE], PIN_STATE_NO_CHANGE);
  ssm_update(&all_ssms[SSM_SOURCE_PC], PIN_STATE_NO_CHANGE);
  ssm_update(&all_ssms[SSM_SOURCE_LIGHT_SENSOR], PIN_STATE_NO_CHANGE);
}

#define LIGHT_SENSOR_COMPENSATION_US 115

void print_results_all_sources(void)
{
  uint8_t active_sources = count_state(SHUTTER_STATE_RESULT_AVAILABLE);
  /*
  4  3  2  1
  X  LS PC HS
  */

  all_ssms[SSM_SOURCE_LIGHT_SENSOR].duration -= LIGHT_SENSOR_COMPENSATION_US;

  if(active_sources == 1) // 0001 hot shoe only
    print_single_result(oled_str_hotshoe, &all_ssms[SSM_SOURCE_HOTSHOE]);

  else if(active_sources == 2) // 0010 pc only
    print_single_result(oled_str_pc_socket, &all_ssms[SSM_SOURCE_PC]);

  else if(active_sources == 4) // 0100 light sensor only
    print_single_result(oled_str_light_sensor, &all_ssms[SSM_SOURCE_LIGHT_SENSOR]);

  else if(active_sources == 3) // 0011 hot shoe AND pc
    print_double_result("HotShoe", &all_ssms[SSM_SOURCE_HOTSHOE], "PC", &all_ssms[SSM_SOURCE_PC]);

  else if(active_sources == 5) // 0101 hot shoe AND light sensor
    print_double_result("HotShoe", &all_ssms[SSM_SOURCE_HOTSHOE], "L Sensor", &all_ssms[SSM_SOURCE_LIGHT_SENSOR]);

  else if(active_sources == 6) // 0110 pc AND light sensor
    print_double_result("PC", &all_ssms[SSM_SOURCE_PC], "L Sensor", &all_ssms[SSM_SOURCE_LIGHT_SENSOR]);

  else if(active_sources == 7) // 0111 all of them
    print_triple_result();
}

void print_timeout(void)
{
  ssd1306_Fill(Black);
  memset(temp_str_buf1, 0, TEMP_BUF_SIZE);
  sprintf(temp_str_buf1, " >%ds", SHUTTER_TIMEOUT_SEC);
  ssd1306_SetCursor(0, 7);
  ssd1306_WriteString("TIMEOUT", Font_11x18, White);
  ssd1306_SetCursor(80, 11);
  ssd1306_WriteString(temp_str_buf1, Font_7x10, White);
  ssd1306_UpdateScreen();
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
  MX_SPI1_Init();
  MX_USART2_UART_Init();
  MX_TIM2_Init();
  /* USER CODE BEGIN 2 */
  ssd1306_Init();
  HAL_TIM_Base_Start(&htim2);
  ssm_reset_all();
  ssd1306_SetContrast(128); // 0 least bright, 255 full brightness

  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  printf("PulseHPT dekuNukem 2023 %d.%d.%d\n", fw_version_major, fw_version_minor, fw_version_patch);
  print_ready();
  uint8_t is_first_measurement = 1;
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
    HAL_Delay(10);
    ssm_update_all();

    if(count_state(SHUTTER_STATE_TIMEOUT))
    {
      __disable_irq();
      print_timeout();
      delay_us(250*1000);
      ssm_reset_all();
      __enable_irq();
      continue;
    }

    if(count_state(SHUTTER_STATE_RESULT_AVAILABLE) == 0)
      continue;

    if(!is_first_measurement)
    {
      ssd1306_FillRectangle(0,11,127,31,Black);
      ssd1306_UpdateScreen();
    }
    
    // at least one source has result 
    // wait a bit more and see if other sources has results too
    uint32_t entry_time = micros();
    while(micros() - entry_time < 250*1000)
    {
      HAL_Delay(10);
      ssm_update_all();
    }

    __disable_irq();
    print_results_all_sources();
    delay_us(100*1000);
    ssm_reset_all();
    __enable_irq();
    is_first_measurement = 0;
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
                              |RCC_CLOCKTYPE_PCLK1;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_HSI;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_0) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief SPI1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_SPI1_Init(void)
{

  /* USER CODE BEGIN SPI1_Init 0 */

  /* USER CODE END SPI1_Init 0 */

  /* USER CODE BEGIN SPI1_Init 1 */

  /* USER CODE END SPI1_Init 1 */
  /* SPI1 parameter configuration*/
  hspi1.Instance = SPI1;
  hspi1.Init.Mode = SPI_MODE_MASTER;
  hspi1.Init.Direction = SPI_DIRECTION_2LINES;
  hspi1.Init.DataSize = SPI_DATASIZE_8BIT;
  hspi1.Init.CLKPolarity = SPI_POLARITY_LOW;
  hspi1.Init.CLKPhase = SPI_PHASE_1EDGE;
  hspi1.Init.NSS = SPI_NSS_SOFT;
  hspi1.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_2;
  hspi1.Init.FirstBit = SPI_FIRSTBIT_MSB;
  hspi1.Init.TIMode = SPI_TIMODE_DISABLE;
  hspi1.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
  hspi1.Init.CRCPolynomial = 7;
  hspi1.Init.CRCLength = SPI_CRC_LENGTH_DATASIZE;
  hspi1.Init.NSSPMode = SPI_NSS_PULSE_ENABLE;
  if (HAL_SPI_Init(&hspi1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN SPI1_Init 2 */

  /* USER CODE END SPI1_Init 2 */

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
  htim2.Init.Prescaler = 7;
  htim2.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim2.Init.Period = 4294967295;
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
  * @brief USART2 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART2_UART_Init(void)
{

  /* USER CODE BEGIN USART2_Init 0 */

  /* USER CODE END USART2_Init 0 */

  /* USER CODE BEGIN USART2_Init 1 */

  /* USER CODE END USART2_Init 1 */
  huart2.Instance = USART2;
  huart2.Init.BaudRate = 115200;
  huart2.Init.WordLength = UART_WORDLENGTH_8B;
  huart2.Init.StopBits = UART_STOPBITS_1;
  huart2.Init.Parity = UART_PARITY_NONE;
  huart2.Init.Mode = UART_MODE_TX;
  huart2.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart2.Init.OverSampling = UART_OVERSAMPLING_16;
  huart2.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
  huart2.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
  if (HAL_HalfDuplex_Init(&huart2) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART2_Init 2 */

  /* USER CODE END USART2_Init 2 */

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
  __HAL_RCC_GPIOF_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(OLED_CS_GPIO_Port, OLED_CS_Pin, GPIO_PIN_SET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOA, OLED_RESET_Pin|OLED_DC_Pin, GPIO_PIN_SET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(USER_LED_GPIO_Port, USER_LED_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin : USER_BUTTON_Pin */
  GPIO_InitStruct.Pin = USER_BUTTON_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(USER_BUTTON_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : LIGHT_SENSOR_Pin */
  GPIO_InitStruct.Pin = LIGHT_SENSOR_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_RISING_FALLING;
  GPIO_InitStruct.Pull = GPIO_PULLDOWN;
  HAL_GPIO_Init(LIGHT_SENSOR_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : OLED_CS_Pin */
  GPIO_InitStruct.Pin = OLED_CS_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(OLED_CS_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pins : OLED_RESET_Pin OLED_DC_Pin USER_LED_Pin */
  GPIO_InitStruct.Pin = OLED_RESET_Pin|OLED_DC_Pin|USER_LED_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /*Configure GPIO pin : PC_SYNC_Pin */
  GPIO_InitStruct.Pin = PC_SYNC_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_RISING_FALLING;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(PC_SYNC_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : HOTSHOE_Pin */
  GPIO_InitStruct.Pin = HOTSHOE_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_RISING_FALLING;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(HOTSHOE_GPIO_Port, &GPIO_InitStruct);

  /* EXTI interrupt init*/
  HAL_NVIC_SetPriority(EXTI0_1_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(EXTI0_1_IRQn);

  HAL_NVIC_SetPriority(EXTI4_15_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(EXTI4_15_IRQn);

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

#ifdef  USE_FULL_ASSERT
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
