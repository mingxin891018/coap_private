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
#include "coap_client.h"
#include "cJSON.h"

#include "sw_common.h"

#define MAX_RESOURCE_NUM 10

//资源响应函数，调用成功后返回1,失败返回负数
static int well_known_core(coap_rw_buffer_t *scratch, const coap_packet_t *inpkt, coap_packet_t *outpkt, uint8_t id_hi, uint8_t id_lo);

const static coap_endpoint_path_t elems_path[MAX_RESOURCE_NUM] = {
	{2, ".well-known", "core", NULL},
	{0, NULL, NULL, NULL}
};

const coap_endpoint_t endpoints[MAX_RESOURCE_NUM] = {
	{COAP_METHOD_GET,  &well_known_core, 		&elems_path[0], NULL},
	{0, NULL, NULL, NULL}
};

static int well_known_core(coap_rw_buffer_t *scratch, const coap_packet_t *inpkt, coap_packet_t *outpkt, uint8_t id_hi, uint8_t id_lo)
{
	char *payload = "support CoAP request:\n\"qlink/netinfo\"\n\"qlink/searchack\"\n\"qlink/addgw\"\n\"qlink/querygw\"\n\"device/command/control\"\n\"device/command/data\"\n\"device/command/unbind\"\n\"device/command/file\"\n";

	coap_make_response(scratch, outpkt, payload, strlen(payload), inpkt->hdr.id[0], inpkt->hdr.id[1], &inpkt->tok, COAP_RSPCODE_CONTENT, COAP_CONTENTTYPE_TEXT_PLAIN);
	INFO("make msg \"/.well-known/core\"\n");

	return 1;
}



