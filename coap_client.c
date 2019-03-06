/*================================================================
*   Copyright (C) 2019 All rights reserved.
*   File name: coap_client.c
*   Author: zhaomingxin@sunniwell.net
*   Creation date: 2019-03-01
*   Describe: null
*
*===============================================================*/
#include "coap_client.h"
#include "sw_common.h"

static coap_client_t **m_client_handle = NULL;
static m_client_max_num = 0;
static espconn_udp_t m_esp_sock = {0};

void udp_recv_cb(void *arg, char *pdata, unsigned short len) 
{
	int i = 0;
	coap_client_t *p;
	coap_packet_t pkt = {0};
	struct espconn *udp = arg;
	remot_info *premot = NULL;

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
	
	if(0 != coap_parse(&pkt, (const uint8_t *)pdata, len)){
		ERROR("parse coap package error!\n");
		return ;
	}
	for(i = 0; i < m_client_max_num; i++){
		p = m_client_handle[i];
		if((p != NULL) && (p->msgid[0] == pkt.hdr.id[0]) && (p->msgid[1] == pkt.hdr.id[1]) && (p->result == REQ_WAITTING))
			m_client_handle[i]->recv_time = time(NULL);
			memcpy(m_client_handle[i]->resp_data, pdata, len);
			
			xSemaphoreGive(m_client_handle[i]->recv_resp_sem);
			m_client_handle[i]->result = REQ_SUCCESS;
			
			INFO("find client handle[%d] success!\n", i);
			return;
	}
	ERROR("find client handle error\n");
}

void udp_send_cb(void* arg)
{
	struct espconn* udp = arg;

	return;

	INFO("UDP_SEND_CB ip:%d.%d.%d.%d port:%d\n", udp->proto.udp->remote_ip[0],
			udp->proto.udp->remote_ip[1], udp->proto.udp->remote_ip[2],
			udp->proto.udp->remote_ip[3], udp->proto.udp->remote_port\
			);
}

static bool esp_create_udp(struct espconn *p, esp_udp* udp)
{
	int res = -1;

	p->type = ESPCONN_UDP;
	p->proto.udp = udp;
	
	espconn_regist_recvcb(p, &udp_recv_cb);
	espconn_regist_sentcb(p, &udp_send_cb);

	res = espconn_create(p);
	if (res != 0){
		return false;
	}
	return true;
}

static bool esp_set_remote_ip(uint32_t dst_ip, uint16_t dst_port)
{
	m_esp_sock.udp.remote_port = dst_port;
	m_esp_sock.udp.remote_ip[3] = dst_ip & 0xff;
	m_esp_sock.udp.remote_ip[2] = (dst_ip & 0xff00) >> 8;
	m_esp_sock.udp.remote_ip[1] = (dst_ip & 0xff0000) >> 16;
	m_esp_sock.udp.remote_ip[0] = (dst_ip & 0xff000000) >> 24;

	return true;
}
static bool coap_client2packet(const coap_client_t *client, const char *tok, int tok_len, coap_packet_t *pkt)
{
	//head
	pkt->hdr.ver = 1;
	pkt->hdr.t = client->type;
	pkt->hdr.tkl = (0xf & tok_len);
	pkt->hdr.code = MAKE_RSPCODE(0, client->method);
	pkt->hdr.id[0] = client->msgid[0];
	pkt->hdr.id[1] = client->msgid[1];

	//token
	if(tok && (tok_len > 0)){
		pkt->tok.p =tok;
		pkt->tok.len = tok_len;
	}else{
		pkt->tok.p = NULL;
		pkt->tok.len = 0;
	}

	//opts
	pkt->numopts = 1;
	pkt->opts[0].buf.p = client->path;
	pkt->opts[0].buf.len = strlen(client->path);
	pkt->opts[0].num = 11;
	
	//msg
	pkt->payload.p = NULL;
	pkt->payload.len = 0;
	coap_dumpPacket(pkt);
	
	return true;
}

static int coap_client_create(uint32_t ip, uint16_t port, const char*path, coap_method_t method, coap_msgtype_t type, const char *tok, int tok_len)
{
	int i = 0, msg_len = 0, ret = -1;
	coap_client_t *p = NULL;
	coap_packet_t pkt;

	srand((unsigned)time(NULL));
	for(i = 0; i < m_client_max_num; i++){
		if(m_client_handle[i] == NULL){
			INFO("client handle enough,i=%d.\n", i);
			break;
		}
	}
	if(m_client_handle[i] != NULL){
		ERROR("client num already max,init client create failed!\n");
		return -1;
	}

	p = (coap_client_t *)malloc(sizeof(coap_client_t));
	if(p == NULL){
		ERROR("malloc client error!,init client create failed!");
		goto CRT_ERROR;
	}
	memset(p, 0, sizeof(coap_client_t));
	
	if(type == COAP_TYPE_CON){
		vSemaphoreCreateBinary(p->recv_resp_sem);
		xSemaphoreTake(p->recv_resp_sem, 0);
	}
	
	esp_set_remote_ip(ip, port);

	//填写数据到结构体,初始化信号量
	p->path = path;
	p->recv_time = 0;
	p->send_time = 0;
	p->send_count = 0;
	p->msgid[0] = (rand() & 0xff);
	p->msgid[1] = ((rand() & 0xff00) >> 8);
	p->method = method;
	p->type = type;
	p->result = REQ_UNUSE;
	p->msg_len = sizeof(p->msg_data);
	p->resp_len = sizeof(p->resp_data);

	//封装coap_client_t 生成coap_packet_t
	memset(&pkt, 0, sizeof(pkt));
	if(!coap_client2packet(p, tok, tok_len, &pkt)){
		ERROR("client to package failed!\n");
		goto CRT_ERROR;
	}
	//封装coap_packet_t 成CoAP的msg
	if(0 != (ret = coap_build(p->msg_data, &p->msg_len, &pkt))){
		ERROR("build coap msg failed,ret=%d\n", ret);
		goto CRT_ERROR;
	}
	p->result = REQ_INIT;
	m_client_handle[i] = p;

	return i;

CRT_ERROR:
	if(p){
		vSemaphoreDelete(p->recv_resp_sem);
		free(p);
	}
	return -1;
}

static int coap_send_wait_request(int client_index)
{
	int ret = -1;
	coap_client_t *p = m_client_handle[client_index];

send_again:
	//UDP发送数据,初始化发送时间
	if(p->result == REQ_INIT){
		if(0 != espconn_sendto(&m_esp_sock, p->msg_data, p->msg_len)){
			p->result = REQ_SEND_FAILED;
			p->send_count++;

			if(p->send_count < COAP_MAX_RETRANSMIT){
				vTaskDelay(500 / portTICK_RATE_MS);
				goto send_again;
			}

			p->result = REQ_FAILED;
			ERROR("send client handle[%d] failed,send len=%dret = %d", client_index, p->msg_len, ret);
			return -1;
		}
	}
	p->send_time = time(NULL);
	p->result = REQ_WAITTING;
	xSemaphoreTake(p->recv_resp_sem, portMAX_DELAY);
	INFO("send time=%d\n", p->send_time);
	return 0;
}

static int coap_analysis_response(int client_index, char *buf, int *buf_len, uint8_t *code)
{
	int ret = -1;
	coap_packet_t pkt;
	coap_client_t *p = m_client_handle[client_index];
	
	*code = 0xff;
	memset(&pkt, 0, sizeof(pkt));
	if(p->result != REQ_SUCCESS){
		INFO("client handle[%d], bad request!, result = %d\n", client_index, p->result);
		return -1;
	}

	if(0 != (ret = coap_parse(&pkt, p->resp_data, p->resp_len))){
		ERROR("carse coap msg failed!.ret = %d\n",ret);
		return -1;
	}
	coap_dumpPacket(&pkt);

	*code = pkt.hdr.code;
	if(*buf_len < pkt.payload.len){
		INFO("buffer too small,can not copy payload.\n");
		return -1;
	}
	*buf_len = 0;
	if(pkt.payload.len > 0){
		memcpy(buf, pkt.payload.p, pkt.payload.len);
		*buf_len = pkt.payload.len;
	}
	return 0;
}

static bool coap_client_delete(int client_index)
{
	coap_client_t *p = NULL;

	p = m_client_handle[client_index];
	if(p == NULL){
		INFO("client handle[%d]is null!\n",client_index);
	}

wait:	
	if((p->result != REQ_TIMEOUT) && (p->result != REQ_FAILED) && (p->result != REQ_SUCCESS)){
		vTaskDelay(1000 / portTICK_RATE_MS);
		goto wait;
	}
	vSemaphoreDelete(p->recv_resp_sem);
	free(p);
	m_client_handle[client_index] = NULL;

	INFO("free client handle[%d] success!\n", client_index);
	return true;
}

static void coap_client_destroy(coap_client_t *client_handle[])
{
	if(client_handle){
		free(client_handle);
		client_handle = NULL;
		INFO("client handle destroy success!\n");
	}else{
		ERROR("client_handle is NULL!\n");
	}
}

void ICACHE_FLASH_ATTR wait2timeout(void *pvParameters)
{
	int i = 0;
	coap_client_t *p = NULL;
	time_t now = 0, wait_time = 0;

	while(sntp_get_current_timestamp() == 0){
		vTaskDelay(1000 / portTICK_RATE_MS);;
	}
	INFO("time is sync,time=%d\n", time(NULL));

	while(1){
		now = time(NULL);

		for(i = 0; i < m_client_max_num; i++){
			p = m_client_handle[i];
			if((p != NULL) && (p->result == REQ_WAITTING)){
				wait_time = now - (p->send_time);
				if( (wait_time >= COAP_ACK_TIMEOUT / 1000) && (p->recv_time == 0) ){
					xSemaphoreGive(p->recv_resp_sem);
					p->result = REQ_TIMEOUT;
					INFO("client handle[%d] is timeout,waitting time=%d\n", i, wait_time);
				}
			}
		}
	}
	vTaskDelete(NULL);
}

bool sw_coap_client_init(unsigned int num)
{
	int ret;

	coap_client_t **p = (coap_client_t **)malloc(sizeof(coap_client_t *) * num);
	if(p == NULL){
		ERROR("malloc client handle error!,init client failed!");
		goto INIT_ERR;
	}
	
	memset(p, 0, sizeof(coap_client_t *) * num);
	memset(&m_esp_sock, 0, sizeof(m_esp_sock));
	
	m_client_handle = p;
	m_client_max_num = num;

	if(!esp_create_udp(&m_esp_sock.esp_8266, &m_esp_sock.udp)){
		ERROR("create esp udp failed!\n");
		goto INIT_ERR;
	}

	ret = xTaskCreate(wait2timeout, "wait2timeout_proc", 256, NULL, tskIDLE_PRIORITY, NULL);
	if (ret != pdPASS){
		INFO("create thread %s failed\n", "wait2timeout_proc");
		goto INIT_ERR;
	}
	INFO("init client success!\n");

	return true;

INIT_ERR:
	if(p){
		free(p);
		p = NULL;
	}
	return false;
}

/*coap://10.10.5.32:33200/root/dev/dev1*/
bool sw_coap_get_request(const char *url, coap_method_t method, coap_msgtype_t type, char *req_data, size_t *req_len, uint8_t *code)
{
	uint32_t ip = 0;
	uint16_t port = -1;
	char ip_str[32] = {0};
	int index = -1, ret = -1;
	struct in_addr addr = {0};
	const char *path = NULL, *p = NULL;
	char *s = NULL;
	
	INFO("url=%s\n", url);
	p = strstr(url, "coap://");
	if(p != NULL){
		s = strstr(p + 7, "/");
		if(s){
			strncpy(ip_str, p + 7, s - p - 7);
			path = s + 1;
			INFO("path=%s\n",path);
		}else{
			ERROR("url format error!\n");
			goto GET_ERROR;
		}
	}else{
		INFO("mast coap protocol stack 'coap://'.\n");
		goto GET_ERROR;
	}
	
	if( (s = strstr(ip_str, ":")) != NULL){
		*(s + 1) = 0;
		port = atoi(s + 1);
	}else{
		port = COAP_DEFAULT_COAP_PORT;
	}

	ip = CharIp_Trans_Int(ip_str);;
	if(ip == -1){
		INFO("ip is error!\n");
		goto GET_ERROR;
	}

	INFO("ip_str=%s\n",ip_str);
	INFO("port=%d\n",port);
	while(sntp_get_current_timestamp() == 0){
		vTaskDelay(1000 / portTICK_RATE_MS);;
	}
	INFO("time is sync,time=%d\n", time(NULL));
	
	index = coap_client_create(ip, port, path, method, type, NULL, 0);
	if(index < 0)
		goto GET_ERROR;
	INFO("create client handle[%d] success!\n", index);
	
	ret = coap_send_wait_request(index);
	if(ret < 0)
		goto GET_ERROR;
	INFO("send client handle[%d] success!\n", index);

	ret = coap_analysis_response(index, req_data, req_len, code);
	if(ret != 0)
		goto GET_ERROR;
	INFO("analysis client handle[%d] success!\n", index);
	
	coap_client_delete(index);
	return true;

GET_ERROR:
	if(index >= 0)
		coap_client_delete(index);
	return false;

}



