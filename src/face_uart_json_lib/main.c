#include <stdio.h>

#include "sys_config.h"
#include "protocol.h"
#include "fpioa.h"
#include "uart.h"
#include "sysctl.h"
#include "sha256.h"

#include "img.h"

volatile uint8_t can_send_jpeg = 0;

volatile int total_face_num;
volatile uint8_t can_query_next_face = 0;

extern unsigned char czd_face_data[];

int protocol_prase_callback(void *cb_data)
{
    pkt_callback_t *pkt = (pkt_callback_t *)cb_data;

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

        if (pkt->code == 0)
        {
            printf("set module success!\r\n");
        }
        else
        {
            printf("set module failed!\r\n");
        }
    }
    break;
    case PROTOCOL_TYPE_GET_CONFIG_RET:
    {
        mod_cfg_t *board_cfg = (mod_cfg_t *)pkt->ctx;

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
        pic_fea_t *pic_fea = (pic_fea_t *)pkt->ctx;

        if (pkt->code == 1) //可以发送jpeg数据
        {
            can_send_jpeg = 1;
            break;
        }

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
            for (uint8_t i = 0; i < 32 /*face_info->fea_len*/; i++)
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

        if (pkt->code == 2)
        {
            total_face_num = query_face->total;
            printf("query total face : %d\r\n", total_face_num);
            can_query_next_face = 1;
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

        can_query_next_face = 1; //可以进行下一次查询
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
            for (uint8_t i = 0; i < 32 /*face_info->fea_len*/; i++)
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

void io_mux_init(void)
{
    fpioa_set_function(PROTOCOL_PORT_TX_PIN, FUNC_UART1_TX + PROTOCOL_PORT_NUM * 2);
    fpioa_set_function(PROTOCOL_PORT_RX_PIN, FUNC_UART1_RX + PROTOCOL_PORT_NUM * 2);
    return;
}

void io_bank_pwr_init(void)
{
    sysctl_set_power_mode(SYSCTL_POWER_BANK6, SYSCTL_POWER_V18);
    sysctl_set_power_mode(SYSCTL_POWER_BANK7, SYSCTL_POWER_V18);
    return;
}

/*
    K210(master)    |   K210(modeule)   |   说明    |   
    -------------------------------------------------
    10              |   11              |   tx--rx  |
    11              |   10              |   rx--tx  |
*/

int main(void)
{
    static int current_query;

    io_mux_init();
    io_bank_pwr_init();

    plic_init();
    sysctl_enable_irq();

    protocol_init();
    protocol_register_cb(protocol_prase_callback);

    printf("init\r\n");

    ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    protocol_send_get_cfg();
    ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    // mod_cfg_t board_cfg;

    // board_cfg.uart_baud = 115200;
    // board_cfg.open_delay = 10;
    // board_cfg.pkt_fix = 0;
    // board_cfg.out_feature = 0;
    // board_cfg.out_interval_in_ms = 800;
    // board_cfg.auto_out_feature = 0;

    // protocol_send_set_cfg(&board_cfg);
    ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    // uint8_t face_sha256[32];
    // sha256_hard_calculate(czd_face_data, sizeof(czd_face_data), face_sha256);
    // protocol_send_cal_pic_fea(sizeof(czd_face_data), 1, face_sha256);
    ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    // protocol_send_del_user_by_uid(1, NULL);
    ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    // current_query = 0;
    // protocol_send_query_face(1, 0, 0, 0);
    ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

    while (1)
    {
        if (recv_over_flag)
        {
            protocol_prase();
            recv_over_flag = 0;
        }

        if (can_send_jpeg)
        {
            printf("start send jpeg\r\n");
            uart_send_jpeg(czd_face_data, sizeof(czd_face_data));
            printf("end send jpeg\r\n");
            can_send_jpeg = 0;
        }

        if (can_query_next_face)
        {
            protocol_send_query_face(0, current_query, 0, 1);

            if (current_query < (total_face_num - 1))
            {
                current_query++;
            }
            else
            {
                current_query = 0;
            }
            can_query_next_face = 0;
        }
    }
}
