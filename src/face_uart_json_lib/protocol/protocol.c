#include "protocol.h"

#include <stdio.h>
#include <string.h>

#include "uart.h"
#include "cJSON.h"
#include "base64.h"
#include "sys_config.h"

volatile uint8_t recv_over_flag = 0; //接受一包完成

pkt_head_t g_pkt_head; //开启数据包校验时使用

static unsigned char cJSON_print_buf[PROTOCOL_BUF_LEN]; //cJSON 发送缓存
static unsigned char cJSON_recv_buf[PROTOCOL_BUF_LEN];  //cJSON 接收缓存

static uint8_t uart_recv_char = 0;  //在串口接受中使用
static uint32_t cur_pos = 0;        //接收缓存当前位置
static uint8_t start_recv_flag = 0; //接收到数据包头

static protocol_prase_pkt_callback prase_pkt_cb = NULL;

static int protocol_port_recv_cb(void *ctx);

#if PROTOCOL_TYPE_STR
char *protocol_str[PROTOCOL_MAX + 1] =
    {
        "init",

        "pkt_prase_failed_ret",

        "set_cfg",
        "set_cfg_ret",

        "get_cfg",
        "get_cfg_ret",

        "cal_pic_fea",
        "cal_pic_fea_ret",

        "del_user_by_uid",
        "del_user_by_uid_ret",

        "face_info",

        "query_face",
        "query_face_ret",

        "unknown",
};
#endif

void protocol_init(void)
{
    uart_config(PROTOCOL_PORT_NUM, 115200, 8, UART_STOP_1, UART_PARITY_NONE);
    uart_set_receive_trigger(PROTOCOL_PORT_NUM, UART_RECEIVE_FIFO_1);
    uart_irq_register(PROTOCOL_PORT_NUM, UART_RECEIVE, protocol_port_recv_cb, NULL, 1);

#if PROTOCOL_PKT_CRC /* pkt check crc*/
    g_pkt_head.pkt_json_start_1 = 0xaa;
    g_pkt_head.pkt_json_start_2 = 0x55;

    g_pkt_head.pkt_json_end_1 = 0x55;
    g_pkt_head.pkt_json_end_2 = 0xaa;

    g_pkt_head.pkt_jpeg_start_1 = 0xaa;
    g_pkt_head.pkt_jpeg_start_2 = 0x55;

    g_pkt_head.pkt_jpeg_end_1 = 0x55;
    g_pkt_head.pkt_jpeg_end_2 = 0xaa;
#else
    g_pkt_head.pkt_json_start_1 = 0; //start no sense
    g_pkt_head.pkt_json_start_2 = 0;

    g_pkt_head.pkt_json_end_1 = 0x0d;
    g_pkt_head.pkt_json_end_2 = 0x0a;

    g_pkt_head.pkt_jpeg_start_1 = 0xff;
    g_pkt_head.pkt_jpeg_start_2 = 0xd8;

    g_pkt_head.pkt_jpeg_end_1 = 0xff;
    g_pkt_head.pkt_jpeg_end_2 = 0xd9;
#endif
}

void protocol_register_cb(protocol_prase_pkt_callback cb)
{
    prase_pkt_cb = cb;
}

void protocol_unregister_cb(void)
{
    prase_pkt_cb = NULL;
}

//recv data
static int protocol_port_recv_cb(void *ctx)
{
    static uint8_t last_data = 0; //前一次串口接收到的数据

    do
    {
        if (uart_channel_getchar(PROTOCOL_PORT_NUM, &uart_recv_char) == 0)
            return 0;

        if (start_recv_flag)
        {
            cJSON_recv_buf[cur_pos++] = uart_recv_char;

            if (uart_recv_char == g_pkt_head.pkt_json_end_2 && cJSON_recv_buf[cur_pos - 2] == g_pkt_head.pkt_json_end_1)
            {
                cJSON_recv_buf[cur_pos - 2] = 0;
                recv_over_flag = 1;
                start_recv_flag = 0;
                return 0;
            }
        }
        else
        {
#if PROTOCOL_PKT_CRC /* pkt check crc*/
            if ((uart_recv_char == g_pkt_head.pkt_json_start_2) && (last_data == g_pkt_head.pkt_json_start_1))
            {
                cur_pos = 0;
                cJSON_recv_buf[cur_pos++] = g_pkt_head.pkt_json_start_1;
                cJSON_recv_buf[cur_pos++] = g_pkt_head.pkt_json_start_2;
                start_recv_flag = 1;
                last_data = 0;
            }
            else
            {
                last_data = uart_recv_char;
            }
#else
            if (uart_recv_char == '{')
            {
                last_data = 0;
                cur_pos = 0;
                cJSON_recv_buf[cur_pos++] = uart_recv_char;
                start_recv_flag = 1;
            }
#endif
        }
    } while (1);
}

//send data
static uint8_t protocol_send_obj(cJSON *send)
{
    uint16_t pkt_len = 0;

    if (!cJSON_PrintPreallocated(send, (char *)cJSON_print_buf, PROTOCOL_BUF_LEN, 1))
    {
        printf("[%d] --> cJSON_PrintPreallocated failed\r\n", __LINE__);
        return PROTOCOL_RET_FAIL;
    }

    cJSON_Minify((char *)cJSON_print_buf);
    pkt_len = strlen((char *)cJSON_print_buf);

#if PROTOCOL_PKT_CRC
    //AA 55 H(LEN) L(LEN) H(CRC16) L(CRC16) ...(data)... 55 AA
    uint16_t pkt_crc16 = 0;
    uint8_t pkt_head[6];

    pkt_crc16 = crc_check(cJSON_print_buf, pkt_len);
    // printf("pkt_crc16:%04X\r\n", pkt_crc16);

    pkt_head[0] = g_pkt_head.pkt_json_start_1;
    pkt_head[1] = g_pkt_head.pkt_json_start_2;

    pkt_head[2] = (uint8_t)(pkt_len >> 8 & 0xFF);
    pkt_head[3] = (uint8_t)(pkt_len & 0xFF);

    pkt_head[4] = (uint8_t)(pkt_crc16 >> 8 & 0xFF);
    pkt_head[5] = (uint8_t)(pkt_crc16 & 0xFF);

    uart_send_data(PROTOCOL_PORT_NUM, pkt_head, 6);
#endif

    //TODO: 对发送的数据进行压缩???
    uart_send_data(PROTOCOL_PORT_NUM, cJSON_print_buf, pkt_len);

#if PROTOCOL_PKT_CRC
    uint8_t pkt_end[2];
    pkt_end[0] = g_pkt_head.pkt_json_end_1;
    pkt_end[1] = g_pkt_head.pkt_json_end_2;
    uart_send_data(PROTOCOL_PORT_NUM, pkt_end, 2);
#endif

    return PROTOCOL_RET_SUCC;
}

uint8_t uart_send_jpeg(uint8_t *jpeg_data, size_t jpeg_size)
{
#if PROTOCOL_PKT_CRC
    uint8_t pkt_head[2];
    //AA 55 ...(data)... 55 AA
    pkt_head[0] = g_pkt_head.pkt_json_start_1;
    pkt_head[1] = g_pkt_head.pkt_json_start_2;

    uart_send_data(PROTOCOL_PORT_NUM, pkt_head, 2);
#endif

    uart_send_data(PROTOCOL_PORT_NUM, jpeg_data, jpeg_size);

#if PROTOCOL_PKT_CRC
    uint8_t pkt_end[2];
    pkt_end[0] = g_pkt_head.pkt_json_end_1;
    pkt_end[1] = g_pkt_head.pkt_json_end_2;
    uart_send_data(PROTOCOL_PORT_NUM, pkt_end, 2);
#endif
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
static uint16_t crc16_table[256] =
    {
        0x0000, 0x1189, 0x2312, 0x329b, 0x4624, 0x57ad, 0x6536, 0x74bf,
        0x8c48, 0x9dc1, 0xaf5a, 0xbed3, 0xca6c, 0xdbe5, 0xe97e, 0xf8f7,
        0x1081, 0x0108, 0x3393, 0x221a, 0x56a5, 0x472c, 0x75b7, 0x643e,
        0x9cc9, 0x8d40, 0xbfdb, 0xae52, 0xdaed, 0xcb64, 0xf9ff, 0xe876,
        0x2102, 0x308b, 0x0210, 0x1399, 0x6726, 0x76af, 0x4434, 0x55bd,
        0xad4a, 0xbcc3, 0x8e58, 0x9fd1, 0xeb6e, 0xfae7, 0xc87c, 0xd9f5,
        0x3183, 0x200a, 0x1291, 0x0318, 0x77a7, 0x662e, 0x54b5, 0x453c,
        0xbdcb, 0xac42, 0x9ed9, 0x8f50, 0xfbef, 0xea66, 0xd8fd, 0xc974,
        0x4204, 0x538d, 0x6116, 0x709f, 0x0420, 0x15a9, 0x2732, 0x36bb,
        0xce4c, 0xdfc5, 0xed5e, 0xfcd7, 0x8868, 0x99e1, 0xab7a, 0xbaf3,
        0x5285, 0x430c, 0x7197, 0x601e, 0x14a1, 0x0528, 0x37b3, 0x263a,
        0xdecd, 0xcf44, 0xfddf, 0xec56, 0x98e9, 0x8960, 0xbbfb, 0xaa72,
        0x6306, 0x728f, 0x4014, 0x519d, 0x2522, 0x34ab, 0x0630, 0x17b9,
        0xef4e, 0xfec7, 0xcc5c, 0xddd5, 0xa96a, 0xb8e3, 0x8a78, 0x9bf1,
        0x7387, 0x620e, 0x5095, 0x411c, 0x35a3, 0x242a, 0x16b1, 0x0738,
        0xffcf, 0xee46, 0xdcdd, 0xcd54, 0xb9eb, 0xa862, 0x9af9, 0x8b70,
        0x8408, 0x9581, 0xa71a, 0xb693, 0xc22c, 0xd3a5, 0xe13e, 0xf0b7,
        0x0840, 0x19c9, 0x2b52, 0x3adb, 0x4e64, 0x5fed, 0x6d76, 0x7cff,
        0x9489, 0x8500, 0xb79b, 0xa612, 0xd2ad, 0xc324, 0xf1bf, 0xe036,
        0x18c1, 0x0948, 0x3bd3, 0x2a5a, 0x5ee5, 0x4f6c, 0x7df7, 0x6c7e,
        0xa50a, 0xb483, 0x8618, 0x9791, 0xe32e, 0xf2a7, 0xc03c, 0xd1b5,
        0x2942, 0x38cb, 0x0a50, 0x1bd9, 0x6f66, 0x7eef, 0x4c74, 0x5dfd,
        0xb58b, 0xa402, 0x9699, 0x8710, 0xf3af, 0xe226, 0xd0bd, 0xc134,
        0x39c3, 0x284a, 0x1ad1, 0x0b58, 0x7fe7, 0x6e6e, 0x5cf5, 0x4d7c,
        0xc60c, 0xd785, 0xe51e, 0xf497, 0x8028, 0x91a1, 0xa33a, 0xb2b3,
        0x4a44, 0x5bcd, 0x6956, 0x78df, 0x0c60, 0x1de9, 0x2f72, 0x3efb,
        0xd68d, 0xc704, 0xf59f, 0xe416, 0x90a9, 0x8120, 0xb3bb, 0xa232,
        0x5ac5, 0x4b4c, 0x79d7, 0x685e, 0x1ce1, 0x0d68, 0x3ff3, 0x2e7a,
        0xe70e, 0xf687, 0xc41c, 0xd595, 0xa12a, 0xb0a3, 0x8238, 0x93b1,
        0x6b46, 0x7acf, 0x4854, 0x59dd, 0x2d62, 0x3ceb, 0x0e70, 0x1ff9,
        0xf78f, 0xe606, 0xd49d, 0xc514, 0xb1ab, 0xa022, 0x92b9, 0x8330,
        0x7bc7, 0x6a4e, 0x58d5, 0x495c, 0x3de3, 0x2c6a, 0x1ef1, 0x0f78};

//http://www.ip33.com/crc.html CRC-16/X25 X16+x12+x5+1
uint16_t crc_check(uint8_t *data, uint32_t length)
{
    unsigned short crc_reg = 0xFFFF;
    while (length--)
    {
        crc_reg = (crc_reg >> 8) ^ crc16_table[(crc_reg ^ *data++) & 0xff];
    }
    return (uint16_t)(~crc_reg) & 0xFFFF;
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void hex_str(uint8_t *inchar, uint16_t len, uint8_t *outtxt)
{
    uint16_t i;
    uint8_t hbit, lbit;

    for (i = 0; i < len; i++)
    {
        hbit = (*(inchar + i) & 0xf0) >> 4;
        lbit = *(inchar + i) & 0x0f;
        if (hbit > 9)
            outtxt[2 * i] = 'A' + hbit - 10;
        else
            outtxt[2 * i] = '0' + hbit;
        if (lbit > 9)
            outtxt[2 * i + 1] = 'A' + lbit - 10;
        else
            outtxt[2 * i + 1] = '0' + lbit;
    }
    outtxt[2 * i] = 0;
}

uint16_t str_hex(uint8_t *str, uint8_t *hex)
{
    uint8_t ctmp, ctmp1, half;
    uint16_t num = 0;
    do
    {
        do
        {
            half = 0;
            ctmp = *str;
            if (!ctmp)
                break;
            str++;
        } while ((ctmp == 0x20) || (ctmp == 0x2c) || (ctmp == '\t'));
        if (!ctmp)
            break;
        if (ctmp >= 'a')
            ctmp = ctmp - 'a' + 10;
        else if (ctmp >= 'A')
            ctmp = ctmp - 'A' + 10;
        else
            ctmp = ctmp - '0';
        ctmp = ctmp << 4;
        half = 1;
        ctmp1 = *str;
        if (!ctmp1)
            break;
        str++;
        if ((ctmp1 == 0x20) || (ctmp1 == 0x2c) || (ctmp1 == '\t'))
        {
            ctmp = ctmp >> 4;
            ctmp1 = 0;
        }
        else if (ctmp1 >= 'a')
            ctmp1 = ctmp1 - 'a' + 10;
        else if (ctmp1 >= 'A')
            ctmp1 = ctmp1 - 'A' + 10;
        else
            ctmp1 = ctmp1 - '0';
        ctmp += ctmp1;
        *hex = ctmp;
        hex++;
        num++;
    } while (1);
    if (half)
    {
        ctmp = ctmp >> 4;
        *hex = ctmp;
        num++;
    }
    return (num);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
static cJSON *protocol_alloc_header(protocol_type type)
{
    cJSON *root = NULL;
    root = cJSON_CreateObject();
    if (root == NULL)
    {
        printf("[%d] --> cJSON_CreateObject failed\r\n", __LINE__);
        return NULL;
    }

    cJSON_AddNumberToObject(root, "version", PROTOCOL_VERSION);
#if PROTOCOL_TYPE_STR
    cJSON_AddStringToObject(root, "type", protocol_str[type]);
#else
    cJSON_AddNumberToObject(root, "type", type);
#endif
    return root;
}

/* pkt_prase_ret */
static uint8_t protocol_send_result(uint8_t code, uint8_t cmd_type, char *msg)
{
#if PROTOCOL_TYPE_STR
    printf("prase pkt some where is error,cmd: %s\tcode: %d\r\nmsg: %s\r\n", protocol_str[cmd_type], code, msg);
#else
    printf("prase pkt some where is error,cmd: %d\tcode: %d\r\nmsg: %s\r\n", (int)cmd_type, code, msg);
    cJSON_AddNumberToObject(root, "cmd", cmd_type);
#endif
    return PROTOCOL_RET_SUCC;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

//接收到 `init` 表示模块初始化完成
static uint8_t protocol_prase_init_done(cJSON *root)
{
    cJSON *tmp = NULL;
    pkt_callback_t cb_data;

    /* get code*/
    tmp = cJSON_GetObjectItem(root, "code");
    if (tmp == NULL)
    {
        protocol_send_result(PROTOCOL_RET_FAIL, PROTOCOL_TYPE_PKT_PRASE_RET, "can not get code");
        goto _exit;
    }
    cb_data.code = tmp->valueint;

#if PROTOCOL_DEBUG
    printf("error code:%d\r\n", cb_data.code);
#endif

    /* get msg*/
    tmp = cJSON_GetObjectItem(root, "msg");
    if (tmp == NULL)
    {
        protocol_send_result(PROTOCOL_RET_FAIL, PROTOCOL_TYPE_INIT_DONE, "can not get msg");
        goto _exit;
    }
    cb_data.msg = tmp->valuestring;

#if PROTOCOL_DEBUG
    printf("msg:%s\r\n", cb_data.msg);
#endif

    if (prase_pkt_cb)
    {
        cb_data.cb_type = PROTOCOL_TYPE_INIT_DONE;
        cb_data.ctx = NULL;
        prase_pkt_cb(&cb_data);
    }

    return PROTOCOL_RET_SUCC;
_exit:
    return PROTOCOL_RET_FAIL;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//接收到    `pkt_prase_failed_ret` 表示MCU发送的pkt有错误
static uint8_t protocol_prase_pkt_prase_ret(cJSON *root)
{
    cJSON *tmp = NULL;

    /* get cmd*/
    tmp = cJSON_GetObjectItem(root, "cmd");
    if (tmp == NULL)
    {
        protocol_send_result(PROTOCOL_RET_FAIL, PROTOCOL_TYPE_PKT_PRASE_RET, "can not get cmd");
        goto _exit;
    }

    // #if PROTOCOL_DEBUG
    printf("cmd:%s error at", tmp->valuestring);
    // #endif

    /* get msg*/
    tmp = cJSON_GetObjectItem(root, "msg");
    if (tmp == NULL)
    {
        protocol_send_result(PROTOCOL_RET_FAIL, PROTOCOL_TYPE_PKT_PRASE_RET, "can not get msg");
        goto _exit;
    }
    printf("%s\r\n", tmp->valuestring);

    /* get code*/
    tmp = cJSON_GetObjectItem(root, "code");
    if (tmp == NULL)
    {
        protocol_send_result(PROTOCOL_RET_FAIL, PROTOCOL_TYPE_PKT_PRASE_RET, "can not get code");
        goto _exit;
    }

    // #if PROTOCOL_DEBUG
    printf("error code:%d\r\n", tmp->valueint);
    // #endif

    return PROTOCOL_RET_SUCC;
_exit:
    return PROTOCOL_RET_FAIL;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//发送 `set_cfg` 对模块进行设置
uint8_t protocol_send_set_cfg(mod_cfg_t *board_cfg)
{
    cJSON *root = NULL, *cfg = NULL;

    root = protocol_alloc_header(PROTOCOL_TYPE_SET_CONFIG);
    if (root == NULL)
    {
        printf("[%d] --> cJSON alloc header failed\r\n", __LINE__);
        goto _exit;
    }

    cfg = cJSON_AddObjectToObject(root, "cfg");
    if (cfg == NULL)
    {
        printf("[%d] --> cJSON alloc header failed\r\n", __LINE__);
        goto _exit;
    }

    cJSON_AddNumberToObject(cfg, "uart_baud", board_cfg->uart_baud);
    cJSON_AddNumberToObject(cfg, "open_delay", board_cfg->open_delay);
    cJSON_AddNumberToObject(cfg, "pkt_fix", board_cfg->pkt_fix);

    cJSON_AddNumberToObject(cfg, "out_feature", board_cfg->out_feature);
    cJSON_AddNumberToObject(cfg, "auto_out_feature", board_cfg->auto_out_feature);
    cJSON_AddNumberToObject(cfg, "out_interval_in_ms", board_cfg->out_interval_in_ms);

    protocol_send_obj(root);
    if (root)
        cJSON_Delete(root);

    return PROTOCOL_RET_SUCC;
_exit:
    if (root)
        cJSON_Delete(root);
    return PROTOCOL_RET_FAIL;
}

//接收到 `set_cfg_ret` 发送 `set_cfg` 之后模块的返回
static uint8_t protocol_prase_set_cfg_ret(cJSON *root)
{
    cJSON *tmp = NULL;

    pkt_callback_t cb_data;

    /* get msg */
    tmp = cJSON_GetObjectItem(root, "msg");
    if (tmp == NULL)
    {
        protocol_send_result(PROTOCOL_RET_FAIL, PROTOCOL_TYPE_SET_CONFIG_RET, "can not get msg");
        goto _exit;
    }
    cb_data.msg = tmp->valuestring;

#if PROTOCOL_DEBUG
    printf("set config ret msg: %s\r\n", cb_data.msg);
#endif

    /* get code */
    tmp = cJSON_GetObjectItem(root, "code");
    if (tmp == NULL)
    {
        protocol_send_result(PROTOCOL_RET_FAIL, PROTOCOL_TYPE_SET_CONFIG_RET, "can not get code");
        goto _exit;
    }
    cb_data.code = tmp->valueint;

#if PROTOCOL_DEBUG
    printf("set config ret code: %d\r\n", cb_data.code);
#endif

    if (prase_pkt_cb)
    {
        cb_data.cb_type = PROTOCOL_TYPE_SET_CONFIG_RET;
        cb_data.ctx = NULL;
        prase_pkt_cb(&cb_data);
    }

    return PROTOCOL_RET_SUCC;
_exit:
    return PROTOCOL_RET_FAIL;
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//获取模块当前的配置
uint8_t protocol_send_get_cfg(void)
{
    cJSON *root = NULL;

    root = protocol_alloc_header(PROTOCOL_TYPE_GET_CONFIG);
    if (root == NULL)
    {
        printf("[%d] --> cJSON alloc header failed\r\n", __LINE__);
        return PROTOCOL_RET_FAIL;
    }
    protocol_send_obj(root);
    if (root)
        cJSON_Delete(root);

    return PROTOCOL_RET_SUCC;
}

static uint8_t protocol_prase_get_cfg_ret(cJSON *root)
{
    cJSON *cfg = NULL, *tmp = NULL;
    pkt_callback_t cb_data;
    mod_cfg_t board_cfg;

    /* get msg*/
    tmp = cJSON_GetObjectItem(root, "msg");
    if (tmp == NULL)
    {
        protocol_send_result(PROTOCOL_RET_FAIL, PROTOCOL_TYPE_SET_CONFIG_RET, "can not get msg");
        goto _exit;
    }
    cb_data.msg = tmp->valuestring;

#if PROTOCOL_DEBUG
    printf("set config ret msg: %s\r\n", cb_data.msg);
#endif

    /* get code*/
    tmp = cJSON_GetObjectItem(root, "code");
    if (tmp == NULL)
    {
        protocol_send_result(PROTOCOL_RET_FAIL, PROTOCOL_TYPE_SET_CONFIG_RET, "can not get code");
        goto _exit;
    }
    cb_data.code = tmp->valueint;

#if PROTOCOL_DEBUG
    printf("set config ret code: %d\r\n", cb_data.code);
#endif

    /* get cfg */
    cfg = cJSON_GetObjectItem(root, "cfg");
    if (cfg == NULL)
    {
        protocol_send_result(PROTOCOL_RET_FAIL, PROTOCOL_TYPE_GET_CONFIG_RET, "can not get cfg");
        goto _exit;
    }

    /* get uart_baud */
    tmp = cJSON_GetObjectItem(cfg, "uart_baud");
    if (tmp == NULL)
    {
        protocol_send_result(PROTOCOL_RET_FAIL, PROTOCOL_TYPE_GET_CONFIG_RET, "can not get cfg.baud");
        goto _exit;
    }
    board_cfg.uart_baud = tmp->valueint;

#if PROTOCOL_DEBUG
    printf("module uart_baud: %d\r\n", board_cfg.uart_baud);
#endif

    /* get pkt_fix */
    tmp = cJSON_GetObjectItem(cfg, "pkt_fix");
    if (tmp == NULL)
    {
        protocol_send_result(PROTOCOL_RET_FAIL, PROTOCOL_TYPE_GET_CONFIG_RET, "can not get cfg.pkt_fix");
        goto _exit;
    }
    board_cfg.pkt_fix = tmp->valueint;

#if PROTOCOL_DEBUG
    printf("module pkt_fix: %d\r\n", board_cfg.pkt_fix);
#endif

    /* get open_delay */
    tmp = cJSON_GetObjectItem(cfg, "open_delay");
    if (tmp == NULL)
    {
        protocol_send_result(PROTOCOL_RET_FAIL, PROTOCOL_TYPE_GET_CONFIG_RET, "can not get cfg.open_delay");
        goto _exit;
    }
    board_cfg.open_delay = tmp->valueint;

#if PROTOCOL_DEBUG
    printf("module open_delay: %d\r\n", board_cfg.open_delay);
#endif

    /* get out_feature */
    tmp = cJSON_GetObjectItem(cfg, "out_feature");
    if (tmp == NULL)
    {
        protocol_send_result(PROTOCOL_RET_FAIL, PROTOCOL_TYPE_GET_CONFIG_RET, "can not get cfg.out_feature");
        goto _exit;
    }
    board_cfg.out_feature = tmp->valueint;

#if PROTOCOL_DEBUG
    printf("module out_feature: %d\r\n", board_cfg.out_feature);
#endif

    /* get auto_out_feature */
    tmp = cJSON_GetObjectItem(cfg, "auto_out_feature");
    if (tmp == NULL)
    {
        protocol_send_result(PROTOCOL_RET_FAIL, PROTOCOL_TYPE_GET_CONFIG_RET, "can not get cfg.auto_out_feature");
        goto _exit;
    }
    board_cfg.auto_out_feature = tmp->valueint;

#if PROTOCOL_DEBUG
    printf("module auto_out_feature: %d\r\n", board_cfg.auto_out_feature);
#endif

    /* get auto_out_feature */
    tmp = cJSON_GetObjectItem(cfg, "out_interval_in_ms"); //out_interval_in_ms
    if (tmp == NULL)
    {
        protocol_send_result(PROTOCOL_RET_FAIL, PROTOCOL_TYPE_GET_CONFIG_RET, "can not get cfg.out_interval_in_ms");
        goto _exit;
    }
    board_cfg.out_interval_in_ms = tmp->valueint;

#if PROTOCOL_DEBUG
    printf("module out_feature_interval_in_ms: %d\r\n", board_cfg.out_interval_in_ms);
#endif

    if (prase_pkt_cb)
    {
        cb_data.cb_type = PROTOCOL_TYPE_GET_CONFIG_RET;
        cb_data.ctx = &board_cfg;
        prase_pkt_cb(&cb_data);
    }

    return PROTOCOL_RET_SUCC;
_exit:
    return PROTOCOL_RET_FAIL;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//计算Jpeg文件中人脸的特征值
//pic_size: Jpeg图片的大小
//auto_add: 识别成功中自动添加到模块
//pic_sha256: 图片的sha256校验值
uint8_t protocol_send_cal_pic_fea(int pic_size, int auto_add, uint8_t *pic_sha256)
{
    cJSON *root = NULL, *img = NULL;
    uint8_t str_sha256[64 + 1];

    root = protocol_alloc_header(PROTOCOL_TYPE_CAL_PIC_FEA);
    if (root == NULL)
    {
        printf("[%d] --> cJSON alloc header failed\r\n", __LINE__);
        goto _exit;
    }

    img = cJSON_AddObjectToObject(root, "img");
    if (img == NULL)
    {
        printf("[%d] --> cJSON alloc header failed\r\n", __LINE__);
        goto _exit;
    }

    cJSON_AddNumberToObject(img, "size", pic_size);
    cJSON_AddNumberToObject(img, "auto_add", auto_add);

    hex_str(pic_sha256, 32, str_sha256);
    str_sha256[64] = 0;
    cJSON_AddStringToObject(img, "sha256", (char *)str_sha256);

    protocol_send_obj(root);
    if (root)
        cJSON_Delete(root);

    return PROTOCOL_RET_SUCC;
_exit:
    if (root)
        cJSON_Delete(root);
    return PROTOCOL_RET_FAIL;
}

static uint8_t protocol_prase_cal_pic_fea_ret(cJSON *root)
{
    cJSON *info = NULL, *tmp = NULL;

    size_t str_len;
    uint8_t hex_uid[16 + 1], *feature = NULL;

    pkt_callback_t cb_data;
    pic_fea_t data;

    /* get code */
    tmp = cJSON_GetObjectItem(root, "code");
    if (tmp == NULL)
    {
        protocol_send_result(PROTOCOL_RET_FAIL, PROTOCOL_TYPE_CAL_PIC_FEA_RET, "can not get root.code");
        goto _exit;
    }
    cb_data.code = tmp->valueint;

#if PROTOCOL_DEBUG
    printf("cal_pic_fea_ret code: %d\r\n", cb_data.code);
#endif

    /* get msg */
    tmp = cJSON_GetObjectItem(root, "msg");
    if (tmp == NULL)
    {
        protocol_send_result(PROTOCOL_RET_FAIL, PROTOCOL_TYPE_CAL_PIC_FEA_RET, "can not get root.msg");
        goto _exit;
    }
    cb_data.msg = tmp->valuestring;

#if PROTOCOL_DEBUG
    printf("cal_pic_fea_ret msg: %s\r\n", cb_data.msg);
#endif

    /* code 为0表示计算特征值成功，其他值不获取uid和fea */
    if (cb_data.code == 0)
    {
        /* get info */
        info = cJSON_GetObjectItem(root, "info");
        if (info == NULL)
        {
            protocol_send_result(PROTOCOL_RET_FAIL, PROTOCOL_TYPE_CAL_PIC_FEA_RET, "can not get info");
            goto _exit;
        }

        /* get uid */
        tmp = cJSON_GetObjectItem(info, "uid");
        if (tmp == NULL)
        {
            protocol_send_result(PROTOCOL_RET_FAIL, PROTOCOL_TYPE_CAL_PIC_FEA_RET, "can not get info.uid");
            goto _exit;
        }

#if PROTOCOL_DEBUG
        printf("cal_pic_fea_ret uid: %s\r\n", tmp->valuestring);
#endif

        if (strlen(tmp->valuestring) > 10)
        {
            str_len = str_hex((uint8_t *)tmp->valuestring, hex_uid);

#if PROTOCOL_DEBUG
            printf("uid len:%ld\r\n", str_len);
#endif

            data.uid = hex_uid;
            data.uid_len = str_len;
        }
        else
        {
            data.uid = NULL;
            data.uid_len = 0;
        }

        /* get feature */
        tmp = cJSON_GetObjectItem(info, "feature");
        if (tmp == NULL)
        {
            protocol_send_result(PROTOCOL_RET_FAIL, PROTOCOL_TYPE_CAL_PIC_FEA_RET, "can not get info.uid");
            goto _exit;
        }

#if PROTOCOL_DEBUG
        printf("cal_pic_fea_ret feature: %s\r\n", tmp->valuestring);
#endif

        if (strlen(tmp->valuestring) > 10)
        {
            feature = base64_decode((uint8_t *)tmp->valuestring, strlen(tmp->valuestring), &str_len);
            if (feature == NULL || str_len != 196 * 4)
            {
                printf("feature base64 decode failed!\r\n");
                data.feature = NULL;
                data.fea_len = 0;
            }
            data.feature = feature;
            data.fea_len = str_len;

#if PROTOCOL_DEBUG
            printf("feature len:%ld\r\n", str_len);
#endif
        }
        else
        {
            data.feature = NULL;
            data.fea_len = 0;
        }
    }
    else
    {
        data.feature = NULL;
        data.fea_len = 0;
        data.feature = NULL;
        data.fea_len = 0;
    }

    if (prase_pkt_cb)
    {
        cb_data.cb_type = PROTOCOL_TYPE_CAL_PIC_FEA_RET;
        cb_data.ctx = &data;
        prase_pkt_cb(&cb_data);
    }

    if (feature)
        free(feature);

    return PROTOCOL_RET_SUCC;
_exit:
    return PROTOCOL_RET_FAIL;
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//del_all 为1，表示删除所有，uid值无效
//uid 要删除的用户uid
uint8_t protocol_send_del_user_by_uid(uint8_t del_all, uint8_t uid[16 + 1])
{
    cJSON *root = NULL;
    uint8_t str_uid[32 + 1];

    root = protocol_alloc_header(PROTOCOL_TYPE_DEL_BY_UID);
    if (root == NULL)
    {
        printf("[%d] --> cJSON alloc header failed\r\n", __LINE__);
        goto _exit;
    }

    if (del_all)
    {
        for (uint8_t i = 0; i < 32; i++)
            str_uid[i] = 'F';
    }
    else
    {
        hex_str(uid, 16, str_uid);
    }

    str_uid[32] = 0;

    cJSON_AddStringToObject(root, "uid", (char *)str_uid);

    protocol_send_obj(root);
    if (root)
        cJSON_Delete(root);

    return PROTOCOL_RET_SUCC;
_exit:
    if (root)
        cJSON_Delete(root);
    return PROTOCOL_RET_FAIL;
}

static uint8_t protocol_prase_del_user_by_uid_ret(cJSON *root)
{
    cJSON *tmp = NULL;
    pkt_callback_t cb_data;

    /* get code */
    tmp = cJSON_GetObjectItem(root, "code");
    if (tmp == NULL)
    {
        protocol_send_result(PROTOCOL_RET_FAIL, PROTOCOL_TYPE_DEL_BY_UID_RET, "can not get code");
        goto _exit;
    }
    cb_data.code = tmp->valueint;

#if PROTOCOL_DEBUG
    printf("del_user_by_uid_ret code: %d\r\n", cb_data.code);
#endif

    /* get msg */
    tmp = cJSON_GetObjectItem(root, "msg");
    if (tmp == NULL)
    {
        protocol_send_result(PROTOCOL_RET_FAIL, PROTOCOL_TYPE_DEL_BY_UID_RET, "can not get msg");
        goto _exit;
    }
    cb_data.msg = tmp->valuestring;

#if PROTOCOL_DEBUG
    printf("del_user_by_uid_ret msg:%s\r\n", cb_data.msg);
#endif

    if (prase_pkt_cb)
    {
        cb_data.cb_type = PROTOCOL_TYPE_DEL_BY_UID_RET;
        cb_data.ctx = NULL;
        prase_pkt_cb(&cb_data);
    }

    return PROTOCOL_RET_SUCC;
_exit:
    return PROTOCOL_RET_FAIL;
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//get_total: 是否查询当前模块中人脸总数，其他字段没有意义
//start: 本次查询起始值，0----total-1
//end: 本次查询结束值, total
//out_feature: 本次查询是否输出特征值，如果输出特征值只能查询一个，即start那个值的人脸信息
uint8_t protocol_send_query_face(int get_total, int start, int end, int out_feature)
{
    cJSON *root = NULL, *query = NULL;

    root = protocol_alloc_header(PROTOCOL_TYPE_QUERY_FACE_INFO);
    if (root == NULL)
    {
        printf("[%d] --> cJSON alloc header failed\r\n", __LINE__);
        goto _exit;
    }

    query = cJSON_AddObjectToObject(root, "query");
    if (query == NULL)
    {
        printf("[%d] --> cJSON alloc header failed\r\n", __LINE__);
        goto _exit;
    }

    cJSON_AddNumberToObject(query, "total", get_total);
    cJSON_AddNumberToObject(query, "start", start);
    cJSON_AddNumberToObject(query, "end", end);
    cJSON_AddNumberToObject(query, "out_feature", out_feature);

    protocol_send_obj(root);
    if (root)
        cJSON_Delete(root);

    return PROTOCOL_RET_SUCC;
_exit:
    if (root)
        cJSON_Delete(root);
    return PROTOCOL_RET_FAIL;
}

static void del_qurey_face_infos(void *arg)
{
    qurey_face_infos_t *face_infos = (qurey_face_infos_t *)arg;

    if (face_infos)
    {
        for (uint8_t i = 0; i < face_infos->face_num; i++)
        {
            if (face_infos->face_info[i]->fea)
                free(face_infos->face_info[i]->fea);
            if (face_infos->face_info[i])
                free(face_infos->face_info[i]);
        }
        if (face_infos->face_info)
            free(face_infos->face_info);
    }
}

static uint8_t protocol_prase_query_face_ret(cJSON *root)
{
    cJSON *face = NULL, *info = NULL, *tmp = NULL, *tmp1 = NULL;

    pkt_callback_t cb_data;
    qurey_face_ret_t face_ret;

    size_t str_len;

    uint8_t error_in_info = 0, hex_uid[16 + 1], *feature = NULL;

    /* get code */
    tmp = cJSON_GetObjectItem(root, "code");
    if (tmp == NULL)
    {
        protocol_send_result(PROTOCOL_RET_FAIL, PROTOCOL_TYPE_QUERY_FACE_INFO_RET, "can not get code");
        goto _exit;
    }
    cb_data.code = tmp->valueint;

#if PROTOCOL_DEBUG
    printf("query_face_info code: %d\r\n", cb_data.code);
#endif

    /* get msg */
    tmp = cJSON_GetObjectItem(root, "msg");
    if (tmp == NULL)
    {
        protocol_send_result(PROTOCOL_RET_FAIL, PROTOCOL_TYPE_QUERY_FACE_INFO_RET, "can not get msg");
        goto _exit;
    }
    cb_data.msg = tmp->valuestring;

#if PROTOCOL_DEBUG
    printf("query_face_info msg:%s\r\n", cb_data.msg);
#endif

    /* get face */
    face = cJSON_GetObjectItem(root, "face");
    if (face == NULL)
    {
        protocol_send_result(PROTOCOL_RET_FAIL, PROTOCOL_TYPE_QUERY_FACE_INFO_RET, "can not get face");
        goto _exit;
    }

    /* get total */
    tmp = cJSON_GetObjectItem(face, "total");
    if (tmp == NULL)
    {
        protocol_send_result(PROTOCOL_RET_FAIL, PROTOCOL_TYPE_QUERY_FACE_INFO_RET, "can not get face.total");
        goto _exit;
    }

    // if (cb_data.code == 2)
    // {
    face_ret.total = tmp->valueint;
    // }
    // else
    // {
    //     face_ret.total = 0;
    // }

#if PROTOCOL_DEBUG
    printf("query_face_info face.total: %d\r\n", face_ret.total);
#endif

    /* get start */
    tmp = cJSON_GetObjectItem(face, "start");
    if (tmp == NULL)
    {
        protocol_send_result(PROTOCOL_RET_FAIL, PROTOCOL_TYPE_QUERY_FACE_INFO_RET, "can not get face.start");
        goto _exit;
    }
    face_ret.start = tmp->valueint;

#if PROTOCOL_DEBUG
    printf("query_face_info face.start: %d\r\n", face_ret.start);
#endif

    /* get end */
    tmp = cJSON_GetObjectItem(face, "end");
    if (tmp == NULL)
    {
        protocol_send_result(PROTOCOL_RET_FAIL, PROTOCOL_TYPE_QUERY_FACE_INFO_RET, "can not get face.end");
        goto _exit;
    }
    face_ret.end = tmp->valueint;

#if PROTOCOL_DEBUG
    printf("query_face_info face.end: %d\r\n", face_ret.end);
#endif

    /* get info */
    info = cJSON_GetObjectItem(face, "info");
    if (info == NULL || !cJSON_IsArray(info))
    {
        protocol_send_result(PROTOCOL_RET_FAIL, PROTOCOL_TYPE_QUERY_FACE_INFO_RET, "can not get face.info");
        goto _exit;
    }

    face_ret.face_infos.del = del_qurey_face_infos;
    face_ret.face_infos.face_num = cJSON_GetArraySize(info); /* face_ret.end - face_ret.start + 1; */

#if PROTOCOL_DEBUG
    printf("this query face num: %d\r\n", face_ret.face_infos.face_num);
#endif

    face_ret.face_infos.face_info = NULL;

    if (face_ret.face_infos.face_num > 0)
    {
        face_ret.face_infos.face_info = (void *)malloc(sizeof(void *) * face_ret.face_infos.face_num);

        for (uint8_t cnt = 0; cnt < face_ret.face_infos.face_num; cnt++)
        {
            tmp = cJSON_GetArrayItem(info, cnt);
            if (tmp == NULL)
            {
                error_in_info = 1;
                protocol_send_result(PROTOCOL_RET_FAIL, PROTOCOL_TYPE_QUERY_FACE_INFO_RET, "can not get face.info[xx] failed");
                goto _exit;
            }

            face_ret.face_infos.face_info[cnt] = (qurey_face_info_t *)malloc(sizeof(qurey_face_info_t));

            /* get order */
            tmp1 = cJSON_GetObjectItem(tmp, "order");
            if (tmp1 == NULL)
            {
                error_in_info = 1;
                protocol_send_result(PROTOCOL_RET_FAIL, PROTOCOL_TYPE_QUERY_FACE_INFO_RET, "can not get face.info[xx].order");
                goto _exit;
            }
            face_ret.face_infos.face_info[cnt]->order = tmp1->valueint;

#if PROTOCOL_DEBUG
            printf("face order: %d\r\n", face_ret.face_infos.face_info[cnt]->order);
#endif

            /* get uid */
            tmp1 = cJSON_GetObjectItem(tmp, "uid");
            if (tmp1 == NULL)
            {
                error_in_info = 1;
                protocol_send_result(PROTOCOL_RET_FAIL, PROTOCOL_TYPE_QUERY_FACE_INFO_RET, "can not get face.info[xx].uid");
                goto _exit;
            }
            if (strlen(tmp1->valuestring) > 10)
            {
                str_len = str_hex((uint8_t *)tmp1->valuestring, hex_uid);

#if PROTOCOL_DEBUG
                printf("uid len:%ld\r\n", str_len);
#endif

                memcpy(face_ret.face_infos.face_info[cnt]->uid, hex_uid, 16);
            }
            else
            {
                memset(face_ret.face_infos.face_info[cnt]->uid, 0, 16);
            }

            /* get feature */
            tmp1 = cJSON_GetObjectItem(tmp, "feature");
            if (tmp1 == NULL)
            {
                error_in_info = 1;
                protocol_send_result(PROTOCOL_RET_FAIL, PROTOCOL_TYPE_QUERY_FACE_INFO_RET, "can not get face.info[xx].feature");
                goto _exit;
            }
            if (strlen(tmp1->valuestring) > 10)
            {
                feature = base64_decode((uint8_t *)tmp1->valuestring, strlen(tmp1->valuestring), &str_len);
                if (feature == NULL || str_len != 196 * 4)
                {
                    printf("feature base64 decode failed!\r\n");
                    error_in_info = 1;
                    protocol_send_result(PROTOCOL_RET_FAIL, PROTOCOL_TYPE_QUERY_FACE_INFO_RET, "can not decode face.info[xx].feature");
                    goto _exit;
                }
                face_ret.face_infos.face_info[cnt]->fea = feature;
                face_ret.face_infos.face_info[cnt]->fea_len = str_len;
            }
            else
            {
                face_ret.face_infos.face_info[cnt]->fea = NULL;
                face_ret.face_infos.face_info[cnt]->fea_len = 0;
            }
        }
    }

    if (prase_pkt_cb)
    {
        cb_data.cb_type = PROTOCOL_TYPE_QUERY_FACE_INFO_RET;
        cb_data.ctx = &face_ret;
        prase_pkt_cb(&cb_data);
    }

    if (face_ret.face_infos.face_info)
    {
        face_ret.face_infos.del(&face_ret.face_infos);
    }

    return PROTOCOL_RET_SUCC;
_exit:
    if (error_in_info)
    {
        if (face_ret.face_infos.face_info)
        {
            face_ret.face_infos.del(&face_ret.face_infos);
        }
    }
    return PROTOCOL_RET_FAIL;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
static uint8_t protocol_prase_face_info(cJSON *root)
{
    cJSON *tmp = NULL, *info = NULL;
    pkt_callback_t cb_data;
    face_info_t face_info;

    size_t str_len;

    uint8_t hex_uid[16 + 1], *feature = NULL;

    /* get code */
    tmp = cJSON_GetObjectItem(root, "code");
    if (tmp == NULL)
    {
        protocol_send_result(PROTOCOL_RET_FAIL, PROTOCOL_TYPE_FACE_INFO, "can not get code");
        goto _exit;
    }
    cb_data.code = tmp->valueint;
#if PROTOCOL_DEBUG
    printf("query_face_info code: %d\r\n", cb_data.code);
#endif
    /* get msg */
    tmp = cJSON_GetObjectItem(root, "msg");
    if (tmp == NULL)
    {
        protocol_send_result(PROTOCOL_RET_FAIL, PROTOCOL_TYPE_FACE_INFO, "can not get msg");
        goto _exit;
    }
    cb_data.msg = tmp->valuestring;
#if PROTOCOL_DEBUG
    printf("query_face_info msg:%s\r\n", cb_data.msg);
#endif

    /* get info */
    info = cJSON_GetObjectItem(root, "info");
    if (info == NULL)
    {
        protocol_send_result(PROTOCOL_RET_FAIL, PROTOCOL_TYPE_FACE_INFO, "can not get info");
        goto _exit;
    }

    /* get info.pic */
    tmp = cJSON_GetObjectItem(info, "pic");
    if (tmp == NULL)
    {
        protocol_send_result(PROTOCOL_RET_FAIL, PROTOCOL_TYPE_FACE_INFO, "can not get info.pic");
        goto _exit;
    }
    face_info.pic_no = tmp->valuestring;
#if PROTOCOL_DEBUG
    printf("face pic no:%s\r\n", face_info.pic_no);
#endif
    /* get info.total */
    tmp = cJSON_GetObjectItem(info, "total");
    if (tmp == NULL)
    {
        protocol_send_result(PROTOCOL_RET_FAIL, PROTOCOL_TYPE_FACE_INFO, "can not get info.total");
        goto _exit;
    }
    face_info.total = tmp->valueint;
#if PROTOCOL_DEBUG
    printf("face total face num:%d\r\n", face_info.total);
#endif
    /* get info.current */
    tmp = cJSON_GetObjectItem(info, "current");
    if (tmp == NULL)
    {
        protocol_send_result(PROTOCOL_RET_FAIL, PROTOCOL_TYPE_FACE_INFO, "can not get info.current");
        goto _exit;
    }
    face_info.current = tmp->valueint;
#if PROTOCOL_DEBUG
    printf("face current face num:%d\r\n", face_info.current);
#endif
    /* get info.x1 */
    tmp = cJSON_GetObjectItem(info, "x1");
    if (tmp == NULL)
    {
        protocol_send_result(PROTOCOL_RET_FAIL, PROTOCOL_TYPE_FACE_INFO, "can not get info.x1");
        goto _exit;
    }
    face_info.x1 = tmp->valueint;
#if PROTOCOL_DEBUG
    printf("face x1: %d\r\n", face_info.x1);
#endif
    /* get info.y1 */
    tmp = cJSON_GetObjectItem(info, "y1");
    if (tmp == NULL)
    {
        protocol_send_result(PROTOCOL_RET_FAIL, PROTOCOL_TYPE_FACE_INFO, "can not get info.y1");
        goto _exit;
    }
    face_info.y1 = tmp->valueint;
#if PROTOCOL_DEBUG
    printf("face y1: %d\r\n", face_info.y1);
#endif
    /* get info.x2 */
    tmp = cJSON_GetObjectItem(info, "x2");
    if (tmp == NULL)
    {
        protocol_send_result(PROTOCOL_RET_FAIL, PROTOCOL_TYPE_FACE_INFO, "can not get info.x2");
        goto _exit;
    }
    face_info.x2 = tmp->valueint;
#if PROTOCOL_DEBUG
    printf("face x2: %d\r\n", face_info.x2);
#endif
    /* get info.y2 */
    tmp = cJSON_GetObjectItem(info, "y2");
    if (tmp == NULL)
    {
        protocol_send_result(PROTOCOL_RET_FAIL, PROTOCOL_TYPE_FACE_INFO, "can not get info.y2");
        goto _exit;
    }
    face_info.y2 = tmp->valueint;
#if PROTOCOL_DEBUG
    printf("face y2: %d\r\n", face_info.y2);
#endif
    /* get info.score */
    tmp = cJSON_GetObjectItem(info, "score");
    if (tmp == NULL)
    {
        protocol_send_result(PROTOCOL_RET_FAIL, PROTOCOL_TYPE_FACE_INFO, "can not get info.score");
        goto _exit;
    }
    face_info.score = tmp->valuedouble;
#if PROTOCOL_DEBUG
    printf("face score: %f\r\n", face_info.score);
#endif

    /*get uid */
    tmp = cJSON_GetObjectItem(info, "uid");
    if (tmp == NULL)
    {
        protocol_send_result(PROTOCOL_RET_FAIL, PROTOCOL_TYPE_FACE_INFO, "can not get info.uid");
        goto _exit;
    }

    if (strlen(tmp->valuestring) > 10)
    {
        str_len = str_hex((uint8_t *)tmp->valuestring, hex_uid);
        face_info.uid = hex_uid;
        face_info.uid_len = str_len;
#if PROTOCOL_DEBUG
        printf("face_info.uid_len:%d\r\n", face_info.uid_len);
#endif
    }
    else
    {
        face_info.uid = NULL;
        face_info.uid_len = 0;
    }

    /*get feature */
    tmp = cJSON_GetObjectItem(info, "feature");
    if (tmp == NULL)
    {
        protocol_send_result(PROTOCOL_RET_FAIL, PROTOCOL_TYPE_FACE_INFO, "can not get info.feature");
        goto _exit;
    }

    if (strlen(tmp->valuestring) > 10)
    {
        feature = base64_decode((uint8_t *)tmp->valuestring, strlen(tmp->valuestring), &str_len);
        if (feature == NULL || str_len != 196 * 4)
        {
            printf("feature base64 decode failed!\r\n");
            face_info.fea = NULL;
            face_info.fea_len = 0;
        }
        face_info.fea = feature;
        face_info.fea_len = str_len;
#if PROTOCOL_DEBUG
        printf("feature len:%ld\r\n", str_len);
#endif
    }
    else
    {
        face_info.fea = NULL;
        face_info.fea_len = 0;
    }

    //TODO: 将人脸信息放到结构体中
    if (prase_pkt_cb)
    {
        cb_data.cb_type = PROTOCOL_TYPE_FACE_INFO;
        cb_data.ctx = &face_info;
        prase_pkt_cb(&cb_data);
    }

    if (feature)
        free(feature);

    return PROTOCOL_RET_SUCC;
_exit:
    return PROTOCOL_RET_FAIL;
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void protocol_prase(void)
{
    int msg_type = 0xFF;

    char msg[128];

    cJSON *root = NULL, *tmp1 = NULL;

#if PROTOCOL_PKT_CRC
    uint16_t pkt_len = 0, pkt_crc16 = 0, cal_crc16 = 0;
    //AA 55 H(LEN) L(LEN) H(CRC16) L(CRC16)
    if (protocol_buf[0] != 0xaa || protocol_buf[1] != 0x55)
    {
        protocol_send_result(PROTOCOL_RET_FAIL, PROTOCOL_MAX, "pkt should start with 0xAA 0x55");
        return;
    }
    pkt_len = protocol_buf[2] << 8 | protocol_buf[3];
    printf("pkt_len:%d\r\n", pkt_len);

    pkt_crc16 = protocol_buf[4] << 8 | protocol_buf[5];
    printf("pkt_crc16:%d\r\n", pkt_crc16);

    cal_crc16 = crc_check(protocol_buf + 6, pkt_len);
    printf("pkt_crc16:%d\r\n", cal_crc16);

    if (pkt_crc16 != cal_crc16)
    {
        sprintf(msg, "pkt crc16 check failed, should be %04X", cal_crc16);
        protocol_send_result(PROTOCOL_RET_FAIL, PROTOCOL_MAX, msg);
        return;
    }
    root = cJSON_Parse(protocol_buf + 6);
#else
    root = cJSON_Parse((char *)cJSON_recv_buf);
#endif
#if PROTOCOL_DEBUG
    printf("buf:%s\r\n", cJSON_recv_buf);
#endif
    if (!root)
    {
        printf("error: %s\r\n", cJSON_GetErrorPtr());
        protocol_send_result(PROTOCOL_RET_FAIL, PROTOCOL_MAX, "json prase failed");
        return;
    }

    /* get version */
    tmp1 = cJSON_GetObjectItem(root, "version");
    if (!tmp1)
    {
        protocol_send_result(PROTOCOL_RET_FAIL, PROTOCOL_MAX, "no version vaild");
        goto _exit;
    }

    /* check version */
    if (tmp1->valueint != PROTOCOL_VERSION)
    {
        sprintf(msg, "version should be %d,but we get %d", PROTOCOL_VERSION, tmp1->valueint);
        protocol_send_result(PROTOCOL_RET_FAIL, PROTOCOL_MAX, msg);
        goto _exit;
    }

    /* get type */
    tmp1 = cJSON_GetObjectItem(root, "type");
    if (!tmp1 || !cJSON_IsString(tmp1))
    {
        protocol_send_result(PROTOCOL_RET_FAIL, PROTOCOL_MAX, "no type vaild");
        goto _exit;
    }

#if PROTOCOL_TYPE_STR
    for (uint8_t i = 0; i < PROTOCOL_MAX; i++)
    {
        if (strcmp(tmp1->valuestring, protocol_str[i]) == 0)
        {
            msg_type = i;
            break;
        }
    }
#else
    /* check type */
    if (tmp1->valueint >= PROTOCOL_MAX)
    {
        protocol_send_result(PROTOCOL_RET_FAIL, PROTOCOL_MAX, "not support this type");
        goto _exit;
    }

    msg_type = tmp1->valueint;
#endif

    switch (msg_type)
    {
    case PROTOCOL_TYPE_INIT_DONE: //MODULE --> MCU
    {
        protocol_prase_init_done(root);
    }
    break;
    case PROTOCOL_TYPE_PKT_PRASE_RET: //MODULE --> MCU
    {
        protocol_prase_pkt_prase_ret(root);
    }
    break;
    case PROTOCOL_TYPE_SET_CONFIG_RET: //MODULE --> MCU
    {
        protocol_prase_set_cfg_ret(root);
    }
    break;
    case PROTOCOL_TYPE_GET_CONFIG_RET: //MODULE --> MCU
    {
        protocol_prase_get_cfg_ret(root);
    }
    break;
    case PROTOCOL_TYPE_CAL_PIC_FEA_RET: //MODULE --> MCU
    {
        protocol_prase_cal_pic_fea_ret(root);
    }
    break;
    case PROTOCOL_TYPE_DEL_BY_UID_RET: //MODULE --> MCU
    {
        protocol_prase_del_user_by_uid_ret(root);
    }
    break;
    case PROTOCOL_TYPE_FACE_INFO: //MODULE --> MCU
    {
        protocol_prase_face_info(root);
    }
    break;
    case PROTOCOL_TYPE_QUERY_FACE_INFO_RET: //MODULE --> MCU
    {
        protocol_prase_query_face_ret(root);
    }
    break;
    case PROTOCOL_TYPE_SET_CONFIG:      //MCU --> MODULE
    case PROTOCOL_TYPE_GET_CONFIG:      //MCU --> MODULE
    case PROTOCOL_TYPE_CAL_PIC_FEA:     //MCU --> MODULE
    case PROTOCOL_TYPE_DEL_BY_UID:      //MCU --> MODULE
    case PROTOCOL_TYPE_QUERY_FACE_INFO: //MCU --> MODULE
    {
        printf("unsupport command!\r\n");
        goto _exit;
    }
    break;
    default:
    {
        printf("unknown command!\r\n");
        goto _exit;
    }
    break;
    }
_exit:
    if (root)
        cJSON_Delete(root);
    memset(cJSON_recv_buf, 0, sizeof(cJSON_recv_buf));
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
