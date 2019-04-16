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

#include "sw_common.h"
#include "coap.h"

//CoAP协议绑定的UDP端口
#define COAP_DEFAULT_COAP_PORT 5683
//ACK消息超时时间/毫秒
#define COAP_ACK_TIMEOUT 30000
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
#define COAP_DEFAULT_MAX_MESSAGE_SIZE 512
//消息块长
#define COAP_DEFAULT_DEFAULT_BLOCK_SIZE 512

typedef struct espconn_udp_ {
	struct espconn esp_8266;
	esp_udp udp;
} espconn_udp_t;

typedef void (*esp_recv_cb_t)(void *arg, char *pdata, unsigned short len);
typedef void (*esp_sendto_cb_t)(void* arg);

////////////////////////////////////////

typedef struct coap_result_ {
	coap_packet_t pkt;
	uint8_t *d;
	uint32_t dl;
	uint8_t code;
} coap_result_t;

/*********************************************************************************************
function	创建一个ESP8266 UDP 套接字
param	p			ESP8266 套接字结构体指针
param	udp			ESP8266 UDP协议结构体指针
param	arg_recv	UDP接收数据回调函数
param	arg_send	UDP发送数据回调函数
*********************************************************************************************/
bool sw_esp_create_udp(struct espconn *p, esp_udp* udp, esp_recv_cb_t arg_recv, esp_sendto_cb_t arg_send);

/*********************************************************************************************
//初始化CoAP协议客户端，num为同时支持访问的个数
*********************************************************************************************/
bool sw_coap_client_init(unsigned int num);

/*********************************************************************************************
function	CoAP协议访问一个URL

param url  		CoAP资源定位符,格式coap://10.10.18.253:port/time
param method	访问方式 COAP_METHOD_GET COAP_METHOD_POST COAP_METHOD_PUT COAP_METHOD_DELETE
param type 		访问是否需要回复 需回复:COAP_TYPE_CON  不需回复:COAP_TYPE_NONCON 
param payload 	request请求携带的msg数据
param pl	 	request请求携带的msg数据长度

return  成功返回请求到的结果,失败返回NULL
*********************************************************************************************/
coap_result_t *sw_coap_get_request(const char *url, coap_method_t method, coap_msgtype_t type, const char *payload, size_t pl);

/*********************************************************************************************
function	释放掉请求到的数据
*********************************************************************************************/
void sw_coap_result_free(coap_result_t *rel);

/*********************************************************************************************
function	ping一个CoAP服务器
param ip_str	CopAP服务器的ip地址
return 	返回0,ping超时;
		返回1,ping成功;
		返回-1,函数调用失败
*********************************************************************************************/
int sw_coap_ping(char *ip_str);

/*********************************************************************************************
function	销毁创建好的client客户端(需在sw_coap_client_init成功的时候调用),释放客户端资源并销毁UDP套接字
*********************************************************************************************/
void sw_coap_destory(void);

#endif //__COAP_CLIENT_H__
