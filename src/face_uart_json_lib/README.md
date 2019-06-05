# 说明

|文件|说明|
|-|-|
|`face_uart_54c6a3198dca8e3b3fa3f2ecfc9a235d_tx10_rx11.kfpkg`|烧录到maixgo中<br/>使用  `10` 和`11` 作为串口通信|
|`face_uart_54c6a3198dca8e3b3fa3f2ecfc9a235d_usb_uart.kfpkg`|烧录到maixgo中<br/>使用usb转串口作为串口通信|


这个解析库目前实现了基本的协议解析。

具体的上层应用，需要自己实现

用户使用 `main.c` 中的回调函数进行数据的获取等等，具体协议见协议文档


# `API` 说明

> 初始化解析库，及注册解析完成回调，用户在回调中处理数据

```C
void protocol_init(void);
void protocol_register_cb(protocol_prase_pkt_callback cb);
void protocol_unregister_cb(void);
```

> 当`recv_over_flag`为`1`时表示接受一包数据，需要调用函数进行解析

```C
void protocol_prase(void);
```


> 发送指令，计算图片特征值，如果获得正确的返回，必须调用 发送图片函数<br/>`pic_size` 图片大小<br/>`auto_add` 计算成功自动添加到模块<br/>`pic_sha256` 图片的 `sha256` 校验值（16进制）<br/>图片必须是320x240

```C
uint8_t protocol_send_cal_pic_fea(int pic_size, int auto_add, uint8_t *pic_sha256);
```

> 发送jpeg图片，发送计算图片特征值指令，获得正确返回之后调用<br/>`jpeg_data`图片数据<br/>`jpeg_size` 图片文件大小
```C
uint8_t uart_send_jpeg(uint8_t *jpeg_data, size_t jpeg_size);
```

>> 设置模块参数及获取模块参数

```C
uint8_t protocol_send_set_cfg(mod_cfg_t *board_cfg);

uint8_t protocol_send_get_cfg(void);
```

> 使用`uid`删除模块中保存的人脸信息<br/>`uid`为`16`进制数组， `del_all` 为1 时删除所有

```C
uint8_t protocol_send_del_user_by_uid(uint8_t del_all, uint8_t uid[16 + 1]);
```

> 查询模块中保存的人脸信息<br/> `get_total` 表示获取模块人脸总数，模块回复code为2表示查询成功<br/>`start` 查询起始数目<br/>`end` 查询结束数目<br/>`out_feature`本次查询是否输出人脸特征值，如果设置为`1`则只会返回`start`开始的人脸信息

```C
uint8_t protocol_send_query_face(int get_total, int start, int end, int out_feature);
```
