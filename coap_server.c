/*================================================================
*   Copyright (C) 2019 All rights reserved.
*   File name: coap_server.c
*   Author: zhaomingxin@sunniwell.net
*   Creation date: 2019-03-08
*   Describe: null
*
*===============================================================*/
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
#include "coap_server.h"
#include "coap_client.h"

#include "sw_common.h"

#define MAX_RESOURCE_NUM 10

//wifi配置引导接口
static int qlink_netinfo(coap_rw_buffer_t *scratch, const coap_packet_t *inpkt, coap_packet_t *outpkt, uint8_t id_hi, uint8_t id_lo);
static int link_searchack(coap_rw_buffer_t *scratch, const coap_packet_t *inpkt, coap_packet_t *outpkt, uint8_t id_hi, uint8_t id_lo);
static int qlink_addgw(coap_rw_buffer_t *scratch, const coap_packet_t *inpkt, coap_packet_t *outpkt, uint8_t id_hi, uint8_t id_lo);
static int qlink_querygw(coap_rw_buffer_t *scratch, const coap_packet_t *inpkt, coap_packet_t *outpkt, uint8_t id_hi, uint8_t id_lo);

//设备本地管控接口
static int device_command_control(coap_rw_buffer_t *scratch, const coap_packet_t *inpkt, coap_packet_t *outpkt, uint8_t id_hi, uint8_t id_lo);
static int device_command_data(coap_rw_buffer_t *scratch, const coap_packet_t *inpkt, coap_packet_t *outpkt, uint8_t id_hi, uint8_t id_lo);
static int device_command_unbind(coap_rw_buffer_t *scratch, const coap_packet_t *inpkt, coap_packet_t *outpkt, uint8_t id_hi, uint8_t id_lo);
static int device_command_file(coap_rw_buffer_t *scratch, const coap_packet_t *inpkt, coap_packet_t *outpkt, uint8_t id_hi, uint8_t id_lo);

static int well_known_core(coap_rw_buffer_t *scratch, const coap_packet_t *inpkt, coap_packet_t *outpkt, uint8_t id_hi, uint8_t id_lo);

static int coap_sendto(struct espconn *udp, coap_rw_buffer_t *data, int count);

//网关,应用 ---> 设备
const static coap_endpoint_path_t elems_path[MAX_RESOURCE_NUM] = {
	{2, ".well-known", "core", NULL},
	
	{2, "qlink", "netinfo"	, 	NULL },
	{2, "qlink", "searchack", 	NULL },
	{2, "qlink", "addgw"	, 	NULL },
	{2, "qlink", "querygw"	, 	NULL },
	
	{3, "device", "command", "control"	},
	{3, "device", "command", "data"		},
	{3, "device", "command", "unbind"	},
	{3, "device", "command", "file"		},
	
	{0, NULL, NULL, NULL}
};

//网关,应用 ---> 设备
const const coap_endpoint_t endpoints[MAX_RESOURCE_NUM] = {
	{COAP_METHOD_GET,  &well_known_core, 		&elems_path[0], NULL},
	
	{COAP_METHOD_POST, &qlink_netinfo, 			&elems_path[1], NULL},
	{COAP_METHOD_POST, &link_searchack, 		&elems_path[2], NULL},
	{COAP_METHOD_POST, &qlink_addgw, 			&elems_path[3],	NULL},
	{COAP_METHOD_POST, &qlink_querygw, 			&elems_path[4],	NULL},

	{COAP_METHOD_POST, &device_command_control, &elems_path[5],	NULL},
	{COAP_METHOD_POST, &device_command_data, 	&elems_path[6],	NULL},
	{COAP_METHOD_POST, &device_command_unbind, 	&elems_path[7],	NULL},
	{COAP_METHOD_POST, &device_command_file, 	&elems_path[8],	NULL},
	{0, NULL, NULL, NULL}
};


void endpoint_setup(void)
{
	;//set up endpoints
}

void coap_server_udp_recv_cb(void *arg, char *pdata, unsigned short len) 
{
	struct espconn *udp = arg;
	remot_info *premot = NULL;
	coap_packet_t inpkt = {0}, outpkt = {0};
	static coap_rw_buffer_t send_data = {0};

	if(espconn_get_connection_info(udp,&premot,0) == ESPCONN_OK)
	{    
		udp->proto.udp->remote_port = premot->remote_port;
		udp->proto.udp->remote_ip[0] = premot->remote_ip[0];
		udp->proto.udp->remote_ip[1] = premot->remote_ip[1];
		udp->proto.udp->remote_ip[2] = premot->remote_ip[2];
		udp->proto.udp->remote_ip[3] = premot->remote_ip[3];

		INFO("UDP_RECV_CB len:%d ip:%d.%d.%d.%d port:%d\n", len, udp->proto.udp->remote_ip[0],
				udp->proto.udp->remote_ip[1], udp->proto.udp->remote_ip[2],
				udp->proto.udp->remote_ip[3], udp->proto.udp->remote_port);
	}   
	
	//解析收到的CoAP报文
	if(0 != coap_parse(&inpkt, (const uint8_t *)pdata, len)){
		ERROR("parse coap package error!\n");
		return ;
	}   
	coap_dumpPacket(&inpkt);
	
	char *p = malloc(COAP_DEFAULT_MAX_MESSAGE_SIZE);
	if(p == NULL){
		INFO("malloc server resp data error!\n");
		return;
	}
	
	send_data.p = p;
	send_data.len = COAP_DEFAULT_MAX_MESSAGE_SIZE;

	//根据inpkt制作resp data和 resp pkt
	coap_handle_req(&send_data, &inpkt, &outpkt);
	coap_dumpPacket(&outpkt);
	
	//发送制作好的resp data
	coap_sendto(udp, &send_data, 1);
	
	free(p);
}

void coap_server_udp_send_cb(void* arg)
{
	struct espconn* udp = arg;

	return;

	INFO("UDP_SEND_CB ip:%d.%d.%d.%d port:%d\n", udp->proto.udp->remote_ip[0],
			udp->proto.udp->remote_ip[1], udp->proto.udp->remote_ip[2],
			udp->proto.udp->remote_ip[3], udp->proto.udp->remote_port\
		);
}


int coap_server_create(void)
{
	int ret = -1;
	struct espconn esp_sock;
	esp_udp udp;

	ret = sw_esp_create_udp(&esp_sock, &udp, &coap_server_udp_recv_cb, &coap_server_udp_send_cb);
	if(ret != 0){
		ERROR("create esp udp socket failed!\n");
		return -1;
	}
		INFO("create esp udp socket success!\n");

	return 0;
}

int coap_server_distroy()
{
	return 0;
}

static int well_known_core(coap_rw_buffer_t *scratch, const coap_packet_t *inpkt, coap_packet_t *outpkt, uint8_t id_hi, uint8_t id_lo)
{
	char *payload = {"xxxxxxx"};

	coap_make_response(scratch, outpkt, payload, strlen(payload), inpkt->hdr.id[0], inpkt->hdr.id[1], &inpkt->tok, COAP_RSPCODE_CONTENT, COAP_CONTENTTYPE_TEXT_PLAIN);
	coap_build(scratch->p, &scratch->len, outpkt);
	return 0;
}

/*
{
	“respCode”:xxxx, //1表示成功,0表示参数错误,1000鉴权失败
	“respCont”: “XXXXXX”//respCode=1时忽略
}
*/
//根据inpkt制作resp data和 resp pkt
//通知设备入网信息
static int qlink_netinfo(coap_rw_buffer_t *scratch, const coap_packet_t *inpkt, coap_packet_t *outpkt, uint8_t id_hi, uint8_t id_lo)
{
	
	return 0;
}

/*
若设备未注册(已注册的设备忽略该请求)，则返回如下响应：
{
	“searchAck”:”ANDLINK-DEVICE”,
	“andlinkVersion”:”V3”, 
	"deviceType":"******"
}
*/
//查找网关广播响应
static int link_searchack(coap_rw_buffer_t *scratch, const coap_packet_t *inpkt, coap_packet_t *outpkt, uint8_t id_hi, uint8_t id_lo)
{

	return 0;
}

//增加网关
static int qlink_addgw(coap_rw_buffer_t *scratch, const coap_packet_t *inpkt, coap_packet_t *outpkt, uint8_t id_hi, uint8_t id_lo)
{

	return 0;
}

//查询网关列表
static int qlink_querygw(coap_rw_buffer_t *scratch, const coap_packet_t *inpkt, coap_packet_t *outpkt, uint8_t id_hi, uint8_t id_lo)
{

	return 0;
}

//设备/子设备控制
static int device_command_control(coap_rw_buffer_t *scratch, const coap_packet_t *inpkt, coap_packet_t *outpkt, uint8_t id_hi, uint8_t id_lo)
{

	return 0;
}

//设备/子设备参数查询
static int device_command_data(coap_rw_buffer_t *scratch, const coap_packet_t *inpkt, coap_packet_t *outpkt, uint8_t id_hi, uint8_t id_lo)
{
	return 0;
}

//设备/子设备解绑
static int device_command_unbind(coap_rw_buffer_t *scratch, const coap_packet_t *inpkt, coap_packet_t *outpkt, uint8_t id_hi, uint8_t id_lo)
{
	return 0;
}

//设备文件操作
static int device_command_file(coap_rw_buffer_t *scratch, const coap_packet_t *inpkt, coap_packet_t *outpkt, uint8_t id_hi, uint8_t id_lo)
{
	return 0;
}
static int coap_sendto(struct espconn *udp, coap_rw_buffer_t *data, int count)
{
	int ret = -1;
send_again:
	ret= espconn_sendto(udp, data->p, data->len);
	if(0 != ret){
		INFO("espconn sendto data error,ret=%d\n", ret);
		if(count <= 0)
			return -1;
		vTaskDelay(200 / portTICK_RATE_MS);
		count--;
		goto send_again;
	}
	return 0;
}

