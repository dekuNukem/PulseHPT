/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.h
  * @brief          : Header for main.c file.
  *                   This file contains the common defines of the application.
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

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "stm32f0xx_hal.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Exported types ------------------------------------------------------------*/
/* USER CODE BEGIN ET */

/* USER CODE END ET */

/* Exported constants --------------------------------------------------------*/
/* USER CODE BEGIN EC */

/* USER CODE END EC */

/* Exported macro ------------------------------------------------------------*/
/* USER CODE BEGIN EM */

/* USER CODE END EM */

/* Exported functions prototypes ---------------------------------------------*/
void Error_Handler(void);

/* USER CODE BEGIN EFP */

/* USER CODE END EFP */

/* Private defines -----------------------------------------------------------*/
#define USER_BUTTON_Pin GPIO_PIN_8
#define USER_BUTTON_GPIO_Port GPIOB
#define LIGHT_SENSOR_Pin GPIO_PIN_0
#define LIGHT_SENSOR_GPIO_Port GPIOF
#define LIGHT_SENSOR_EXTI_IRQn EXTI0_1_IRQn
#define OLED_CS_Pin GPIO_PIN_1
#define OLED_CS_GPIO_Port GPIOF
#define OLED_RESET_Pin GPIO_PIN_0
#define OLED_RESET_GPIO_Port GPIOA
#define OLED_DC_Pin GPIO_PIN_1
#define OLED_DC_GPIO_Port GPIOA
#define USER_LED_Pin GPIO_PIN_3
#define USER_LED_GPIO_Port GPIOA
#define PC_SYNC_Pin GPIO_PIN_4
#define PC_SYNC_GPIO_Port GPIOA
#define PC_SYNC_EXTI_IRQn EXTI4_15_IRQn
#define HOTSHOE_Pin GPIO_PIN_1
#define HOTSHOE_GPIO_Port GPIOB
#define HOTSHOE_EXTI_IRQn EXTI0_1_IRQn

/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
