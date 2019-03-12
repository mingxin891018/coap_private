/*================================================================
*   Copyright (C) 2019 All rights reserved.
*   File name: coap_client.h
*   Author: zhaomingxin@sunniwell.net
*   Creation date: 2019-03-01
*   Describe: null
*
*===============================================================*/
#ifndef __COAP_CLIENT_H__
#define __COAP_CLIENT_H__

#include "freertos/FreeRTOS.h" 
#include "freertos/task.h" 
#include "freertos/semphr.h" 
#include "freertos/portmacro.h"

#include "lwip/sys.h"
#include "lwip/mem.h"
#include "lwip/sockets.h"
#include "lwip/inet.h"
#include "esp_common.h"
#include "c_types.h"
#include "espconn/espconn.h"

#include "coap.h"

//CoAP协议绑定的UDP端口
#define COAP_DEFAULT_COAP_PORT 5683
//ACK消息超时时间
#define COAP_ACK_TIMEOUT 60000
//ACK超时随机指数
#define COAP_ACK_RANDOM_FACTOR	1.5
//消息重传间隔指数
#define COAP_ACK_TIMEOUT_SCALE 2
//最大传输等待时间
#define COAP_MAX_TRANSMIT_WAIT 93000
//消息重传最大次数
#define COAP_MAX_RETRANSMIT 4
//使用随机消息ID作为起始值
#define COAP_USE_RANDOM_MID_START 1
//使用随机ID作为Token起始值
#define COAP_USE_RANDOM_TOKEN_START 1
//消息(payload)最大长度
#define COAP_DEFAULT_MAX_MESSAGE_SIZE 4098
//消息块长度
#define COAP_DEFAULT_DEFAULT_BLOCK_SIZE 512

///////////////////////
typedef enum
{
	REQ_UNUSE = 1       ,   
	REQ_INIT            ,   
	REQ_SEND_FAILED     ,   
	REQ_WAITTING        ,   

	REQ_TIMEOUT         ,   
	REQ_FAILED          ,   
	REQ_SUCCESS
} req_resule_t;

typedef void (*udp_send_cb_t)(void* arg);
typedef void (*udp_recv_cb_t)(void *arg, char *pdata, unsigned short len);

typedef struct espconn_udp_ {
	struct espconn esp_8266;
	esp_udp udp;
} espconn_udp_t;

typedef struct
{
	const char *path;
	time_t send_time;
	time_t recv_time;
	int send_count;
	xSemaphoreHandle recv_resp_sem;

	uint8_t msgid[2];
	coap_method_t method;
	coap_msgtype_t type;
	
	req_resule_t result;

	char msg_data[512];
	size_t msg_len;

	char resp_data[4098];
	size_t resp_len;

}coap_client_t;

typedef void (*esp_recv_cb_t)(void *arg, char *pdata, unsigned short len);
typedef void (*esp_sendto_cb_t)(void* arg);

//初始化CoAP协议客户端，num为同时支持访问的个数
bool sw_coap_client_init(unsigned int num);

//CoAP协议访问一个URL
//param url  CoAP资源定位符,格式coap://10.10.18.253:port/time
//param type 访问方式 COAP_METHOD_GET COAP_METHOD_POST COAP_METHOD_PUT COAP_METHOD_DELETE
//param  req_data 收到request的存储位置
//param  req_len 存储位置长度
//param  code 返回码2.05  == 200 OK
//return 是否访问成功
bool sw_coap_get_request(const char *url, coap_method_t method, coap_msgtype_t type, char *req_data, size_t *req_len, uint8_t *code);
int sw_coap_ping(char *ip_str);

bool sw_esp_create_udp(struct espconn *p, esp_udp* udp, esp_recv_cb_t arg_recv, esp_sendto_cb_t arg_send);

#endif //__COAP_CLIENT_H__
