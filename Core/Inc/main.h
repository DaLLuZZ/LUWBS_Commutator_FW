/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.h
  * @brief          : Header for main.c file.
  *                   This file contains the common defines of the application.
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

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "stm32f4xx_hal.h"

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
#define R_D_AN_Pin GPIO_PIN_7
#define R_D_AN_GPIO_Port GPIOF
#define R_D_AP_Pin GPIO_PIN_8
#define R_D_AP_GPIO_Port GPIOF
#define R_D_VN_Pin GPIO_PIN_9
#define R_D_VN_GPIO_Port GPIOF
#define R_C_AN_Pin GPIO_PIN_2
#define R_C_AN_GPIO_Port GPIOC
#define LED_GREEN_Pin GPIO_PIN_0
#define LED_GREEN_GPIO_Port GPIOB
#define R_C_AP_Pin GPIO_PIN_1
#define R_C_AP_GPIO_Port GPIOB
#define R_D_VP_Pin GPIO_PIN_1
#define R_D_VP_GPIO_Port GPIOG
#define LED_RED_Pin GPIO_PIN_14
#define LED_RED_GPIO_Port GPIOB
#define R_C_VP_Pin GPIO_PIN_3
#define R_C_VP_GPIO_Port GPIOG
#define R_A_AP_Pin GPIO_PIN_8
#define R_A_AP_GPIO_Port GPIOC
#define R_A_AN_Pin GPIO_PIN_9
#define R_A_AN_GPIO_Port GPIOC
#define R_A_VP_Pin GPIO_PIN_10
#define R_A_VP_GPIO_Port GPIOC
#define R_B_VN_Pin GPIO_PIN_11
#define R_B_VN_GPIO_Port GPIOC
#define R_B_AN_Pin GPIO_PIN_12
#define R_B_AN_GPIO_Port GPIOC
#define R_B_VP_Pin GPIO_PIN_2
#define R_B_VP_GPIO_Port GPIOD
#define R_B_VNB3_Pin GPIO_PIN_3
#define R_B_VNB3_GPIO_Port GPIOB
#define R_B_AP_Pin GPIO_PIN_5
#define R_B_AP_GPIO_Port GPIOB
#define LED_BLUE_Pin GPIO_PIN_7
#define LED_BLUE_GPIO_Port GPIOB

/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
