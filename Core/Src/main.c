/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * <h2><center>&copy; Copyright (c) 2019 STMicroelectronics.
  * All rights reserved.</center></h2>
  *
  * This software component is licensed by ST under BSD 3-Clause license,
  * the "License"; You may not use this file except in compliance with the
  * License. You may obtain a copy of the License at:
  *                        opensource.org/licenses/BSD-3-Clause
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "usart.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "string.h"
#include "protocol.h"

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* for printf */
int fputc(int ch, FILE *f)
{
    uint8_t temp[1] = {ch};
    HAL_UART_Transmit(&huart1, temp, 1, 2);
    return 0;
}

int protocol_prase_callback(void *cb_data)
{
    pkt_callback_t *pkt = (pkt_callback_t*)cb_data;

    printf("type:%s\r\n", protocol_str[pkt->cb_type]);

    switch (pkt->cb_type)
    {
        case PROTOCOL_TYPE_INIT_DONE:
            {
                printf("module init!\r\n");
            }
            break;
        case PROTOCOL_TYPE_SET_CONFIG_RET:
            {
                printf("code:%d\r\n", pkt->code);
                printf("msg:%s\r\n", pkt->msg);
                //需要判断code
                printf("set module success!\r\n");
            }
            break;
        case PROTOCOL_TYPE_GET_CONFIG_RET:
            {
                brd_cfg *board_cfg = (brd_cfg*)pkt->ctx;

                if (board_cfg == NULL)
                {
                    printf("%d board_cfg is null\r\n", __LINE__);
                    break;
                }
                printf("uart_baud: %d\r\n", board_cfg->uart_baud);
                printf("open_delay: %d\r\n", board_cfg->open_delay);
                printf("pkt_fix: %d\r\n", board_cfg->pkt_fix);

                printf("out_feature: %d\r\n", board_cfg->out_feature);
                printf("out_interval_in_ms: %d\r\n", board_cfg->out_interval_in_ms);
                printf("auto_out_feature: %d\r\n", board_cfg->auto_out_feature);
            }
            break;
        case PROTOCOL_TYPE_CAL_PIC_FEA_RET:
            {
                pic_fea_t *pic_fea = (pic_fea_t *)pkt->ctx;;

                //TODO: 判断code，是否可以发送，以及错误处理
                printf("cal jpeg file:\r\n");
                if (pic_fea->uid_len)
                {
                    printf("uid:");
                    for (uint8_t i = 0; i < pic_fea->uid_len; i++)
                    {
                        printf("%02X", pic_fea->uid[i]);
                    }
                    printf("\r\n");
                }
                else
                {
                    printf("uid:null\r\n");
                }

                if (pic_fea->fea_len)
                {
                    printf("feature:");
                    for (uint8_t i = 0; i < 32/*face_info->fea_len*/; i++)
                    {
                        printf("%02X", pic_fea->feature[i]);
                    }
                    printf("\r\n");
                }
                else
                {
                    printf("feature:null\r\n");
                }
            }
            break;
        case PROTOCOL_TYPE_DEL_BY_UID_RET:
            {
                //TODO: 判断code，是否可以发送，以及错误处理
                printf("code:%d\r\n", pkt->code);
                printf("msg:%s\r\n", pkt->msg);
            }
            break;
        case PROTOCOL_TYPE_QUERY_FACE_INFO_RET:
            {
                qurey_face_ret_t *query_face = (qurey_face_ret_t *)pkt->ctx;

                if (query_face == NULL)
                {
                    printf("%d query_face is null\r\n", __LINE__);
                    break;
                }

                printf("total: %d\r\n", query_face->total);
                printf("start: %d\r\n", query_face->start);
                printf("end: %d\r\n", query_face->end);

                printf("face_num: %d\r\n", query_face->face_infos.face_num);

                for (uint8_t i = 0; i < query_face->face_infos.face_num; i++)
                {
                    printf("[%04d]: uid:", i);
                    for (uint8_t j = 0; j < 16; j++)
                    {
                        printf("%02X", query_face->face_infos.face_info[i]->uid[j]);
                    }
                    printf("\r\n");

                    printf("[%04d]: feature_len:%d\t", i, query_face->face_infos.face_info[i]->fea_len);
                    for (uint8_t k = 0; k < 16; k++)
                    {
                        printf("%02X", query_face->face_infos.face_info[i]->fea[k]);
                    }
                    printf("\r\n");
                }
            }
            break;
        case PROTOCOL_TYPE_FACE_INFO:
            {
                face_info_t *face_info = (face_info_t *)pkt->ctx;
                if (face_info == NULL)
                {
                    printf("%d face_info is null\r\n", __LINE__);
                    break;
                }
                printf("pic_no:%s\r\n", face_info->pic_no);
                printf("total:%d\r\n", face_info->total);
                printf("current:%d\r\n", face_info->current);
                printf("x1:%d\r\n", face_info->x1);
                printf("y1:%d\r\n", face_info->y1);
                printf("x2:%d\r\n", face_info->x2);
                printf("y2:%d\r\n", face_info->y2);
                printf("score:%f\r\n", face_info->score);

                if (face_info->uid_len)
                {
                    printf("uid:");
                    for (uint8_t i = 0; i < face_info->uid_len; i++)
                    {
                        printf("%02X", face_info->uid[i]);
                    }
                    printf("\r\n");
                }
                else
                {
                    printf("uid:null\r\n");
                }

                if (face_info->fea_len)
                {
                    printf("feature:");
                    for (uint8_t i = 0; i < 32/*face_info->fea_len*/; i++)
                    {
                        printf("%02X", face_info->fea[i]);
                    }
                    printf("\r\n");
                }
                else
                {
                    printf("feature:null\r\n");
                }
            }
            break;
        default:
            {
                printf("unknown cmd??? %d\r\n", pkt->cb_type);
            }
            break;
    }
    return 0;
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
    MX_USART1_UART_Init();
    MX_USART2_UART_Init();
    /* USER CODE BEGIN 2 */

    printf("init\r\n");
    /* USER CODE END 2 */

    protocol_init();
    protocol_register_cb(protocol_prase_callback);

    HAL_UART_Transmit(&huart2, (uint8_t *)"hello\r\n", strlen("hello\r\n"), 20);
    HAL_UART_Receive_IT(&huart2, (uint8_t *)&uart_recv_char, 1); /* 开启串口2 接受中断 */

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    recv_over_flag = 0;
//    protocol_send_get_cfg();//get cfg
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
		protocol_send_query_face(1,0,1,1);
    /* Infinite loop */
    /* USER CODE BEGIN WHILE */
    while (1)
    {
        /* USER CODE END WHILE */
        if (recv_over_flag)
        {
            protocol_prase();
            recv_over_flag = 0;
            HAL_UART_Receive_IT(&huart2, (uint8_t *)&uart_recv_char, 1); /* 开启串口2 接受中断 */
        }
        /* USER CODE BEGIN 3 */
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

    /** Initializes the CPU, AHB and APB busses clocks
    */
    RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    RCC_OscInitStruct.HSEState = RCC_HSE_ON;
    RCC_OscInitStruct.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
    RCC_OscInitStruct.HSIState = RCC_HSI_ON;
    RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
    RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
    RCC_OscInitStruct.PLL.PLLMUL = RCC_PLL_MUL9;
    if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
    {
        Error_Handler();
    }
    /** Initializes the CPU, AHB and APB busses clocks
    */
    RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK
                                  | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
    RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
    RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

    if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
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
       tex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
    /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */

/************************ (C) COPYRIGHT STMicroelectronics *****END OF FILE****/
