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
int sw_coap_build_rst(coap_rw_buffer_t *scratch, const coap_packet_t *inpkt, coap_packet_t *pkt);

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

bool sw_coap_recv_request(struct espconn *udp, char *pdata, unsigned short len)
{
	bool ret = true;
	static char buf[2] = {0};
	coap_packet_t inpkt = {0}, outpkt = {0};
	static coap_rw_buffer_t send_data = {0}, content_type = {0};
	
	//解析收到的CoAP报文
	if(0 != coap_parse(&inpkt, (const uint8_t *)pdata, len)){
		ERROR("parse coap package error!\n");
		return false;
	}   
	
	char *p = malloc(COAP_DEFAULT_MAX_MESSAGE_SIZE);
	if(p == NULL){
		INFO("malloc server resp data error!\n");
		return false;
	}
	send_data.p = p;
	send_data.len = COAP_DEFAULT_MAX_MESSAGE_SIZE;
	memset(send_data.p, 0, COAP_DEFAULT_MAX_MESSAGE_SIZE);
	content_type.p = buf;
	content_type.len = sizeof(buf);
	memset(content_type.p, 0, sizeof(buf));

	if((inpkt.hdr.t == COAP_TYPE_CON) && (inpkt.hdr.code == 0)){
		//收到 type=con并且code=0.00的包就是ping包
		sw_coap_build_rst(&content_type, &inpkt, &outpkt);
	}else if(inpkt.hdr.t == COAP_TYPE_CON){
		//根据inpkt制作resp data和 resp pkt
		coap_handle_req(&content_type, &inpkt, &outpkt);
	}
	else
		return false;
	if(send_data.len == 0)
		return false;

	//发送制作好的resp data
	coap_dumpPacket(&outpkt);
	coap_build(send_data.p, &send_data.len, &outpkt);
	if(0 != coap_sendto(udp, &send_data, 1)){
		ERROR("coap sendto failed!\n");
		ret = false;
	}
	
	free(p);
	return ret;
}

static int coap_sendto(struct espconn *udp, coap_rw_buffer_t *data, int count)
{
	int ret = -1;
send_again:
	ret= espconn_sendto(udp, data->p, data->len);
	if(0 != ret){
		INFO("espconn sendto data error,ret=%d\n", ret);
		if(count <= 0)
			return ret;
		vTaskDelay(200 / portTICK_RATE_MS);
		count--;
		goto send_again;
	}
	INFO("sendto data len=%d\n", data->len);
	return 0;
}

int sw_coap_server_create(void)
{
	return 0;
}

int sw_coap_server_distroy(void)
{
	return 0;
}

int sw_coap_build_rst(coap_rw_buffer_t *scratch, const coap_packet_t *inpkt, coap_packet_t *pkt)
{
	pkt->hdr.ver = 0x01;
	pkt->hdr.t = COAP_TYPE_RESET;
	pkt->hdr.tkl = 0;
	pkt->hdr.code = 0;
	pkt->hdr.id[0] = inpkt->hdr.id[0];
	pkt->hdr.id[1] = inpkt->hdr.id[1];
	pkt->numopts = 1;

	// need token in response
	if (&inpkt->tok) {
		pkt->hdr.tkl = inpkt->tok.len;
		pkt->tok = inpkt->tok;
	}

	// safe because 1 < MAXOPT
	pkt->opts[0].num = COAP_OPTION_CONTENT_FORMAT;
	pkt->opts[0].buf.p = scratch->p;
	if (scratch->len < 2)
		return COAP_ERR_BUFFER_TOO_SMALL;
	pkt->opts[0].buf.len = 2;
	pkt->payload.p = NULL;
	pkt->payload.len = 0;
	return 0;

}

static int well_known_core(coap_rw_buffer_t *scratch, const coap_packet_t *inpkt, coap_packet_t *outpkt, uint8_t id_hi, uint8_t id_lo)
{
	char *payload = "support CoAP request:\n\"qlink/netinfo\"\n\"qlink/searchack\"\n\"qlink/addgw\"\n\"qlink/querygw\"\n\"device/command/control\"\n\"device/command/data\"\n\"device/command/unbind\"\n\"device/command/file\"\n";

	coap_make_response(scratch, outpkt, payload, strlen(payload), inpkt->hdr.id[0], inpkt->hdr.id[1], &inpkt->tok, COAP_RSPCODE_CONTENT, COAP_CONTENTTYPE_TEXT_PLAIN);
	INFO("make msg \"/.well-known/core\"\n");
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

