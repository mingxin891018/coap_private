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
static int m_client_max_num = 0;
static espconn_udp_t m_esp_sock = {0};
static xSemaphoreHandle m_mutex = NULL;
static unsigned int m_rand = 0;

void udp_recv_cb(void *arg, char *pdata, unsigned short len) 
{
	int i = 0;
	coap_client_t *p = NULL;
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
	coap_dumpPacket(&pkt);

	if((pkt.hdr.t == COAP_TYPE_ACK) || (pkt.hdr.t == COAP_TYPE_RESET)){
		xSemaphoreTake(m_mutex, portMAX_DELAY);
		for(i = 0; i < m_client_max_num; i++){
			p = m_client_handle[i];
			if((p != NULL) && (p->msgid[0] == pkt.hdr.id[0]) && (p->msgid[1] == pkt.hdr.id[1]) && (p->result == REQ_WAITTING)){
				INFO("find client handle[%d] success,MSDID=%02x%02x,pkt.hdr.id=%02x%02x,result=%d.\n", i, p->msgid[0], p->msgid[1], pkt.hdr.id[0], pkt.hdr.id[1],p->result);
				p->recv_time = time(NULL);
				memcpy(p->resp_data, pdata, len);
				p->result = REQ_SUCCESS;
				xSemaphoreGive(m_mutex);
				xSemaphoreGive(p->recv_resp_sem);
				return;
			}
		}
		xSemaphoreGive(m_mutex);
		INFO("is request ,sendto response!\n");
	}
	else
		sw_coap_recv_request(udp, pdata, len);
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

bool sw_esp_create_udp(struct espconn *p, esp_udp* udp, esp_recv_cb_t arg_recv, esp_sendto_cb_t arg_send)
{
	int res = -1;

	p->type = ESPCONN_UDP;
	p->proto.udp = udp;

	espconn_regist_recvcb(p, arg_recv);
	espconn_regist_sentcb(p, arg_send);

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

static int path2opts(const char *path, coap_option_t *opts)
{
	int i = 0;
	const char *p = strstr(path, "/"), *q = path;
	while(p != NULL){
		if(*(p + 1) == '\0')
			return -1;
		if(i >= 16)
			return -1;
		opts[i].buf.p = q;
		opts[i].buf.len = p - q;
		opts[i].num = COAP_OPTION_URI_PATH;

		q = p + 1;
		p = strstr(p + 1, "/");
		i++;
	}

	opts[i].buf.p = q;
	opts[i].buf.len = strlen(q);
	opts[i].num = COAP_OPTION_URI_PATH;
	i++;
	return i;
}

static bool coap_client2packet(const coap_client_t *client, const char *tok, int tok_len, coap_packet_t *pkt)
{
	int ret = 0;

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
	if(client->path == NULL)
		pkt->numopts = 0;
	else{
		ret = path2opts(client->path, &pkt->opts[0]);
		if(ret == -1)
			return false;
		pkt->numopts = ret;
	}
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
	
relloc:
	p = (coap_client_t *)malloc(sizeof(coap_client_t));
	if(p == NULL){
		ERROR("malloc client error!\n");
		vTaskDelay(1000 / portTICK_RATE_MS);
		goto relloc;
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
	p->msgid[0] = (m_rand & 0xff);
	p->msgid[1] = ((m_rand & 0xff00) >> 8);
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
	
	xSemaphoreTake(m_mutex, portMAX_DELAY);
	for(i = 0; i < m_client_max_num; i++){
		if(m_client_handle[i] == NULL){
			INFO("client handle enough,i=%d.\n", i);
			break;
		}
	}
	if(m_client_handle[i] != NULL){
		ERROR("client num already max,init client create failed!\n");
		xSemaphoreGive(m_mutex);
		goto CRT_ERROR;
	}
	m_client_handle[i] = p;
	xSemaphoreGive(m_mutex);
	INFO("create coap client handle[%d] MSGID=%2x%2x.\n", i, p->msgid[0], p->msgid[1]);

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
	if(0 != espconn_sendto(&m_esp_sock, p->msg_data, p->msg_len)){
		p->send_count++;
		if(p->send_count < COAP_MAX_RETRANSMIT){
			vTaskDelay(1000 / portTICK_RATE_MS);
			goto send_again;
		}

		p->result = REQ_FAILED;
		ERROR("send client handle[%d] failed,send len=%dret = %d", client_index, p->msg_len, ret);
		return -1;
	}
	p->result = REQ_WAITTING;
	
	p->send_time = time(NULL);
	INFO("send time=%d\n", p->send_time);
	xSemaphoreTake(p->recv_resp_sem, portMAX_DELAY);
	return 0;
}

static int coap_analysis_response(int client_index, char *buf, int *buf_len, uint8_t *code)
{
	int ret = -1;
	coap_packet_t pkt;
	coap_client_t *p = m_client_handle[client_index];
	
	memset(&pkt, 0, sizeof(pkt));
	
	if(m_client_handle[client_index]->result != REQ_SUCCESS){
		INFO("client handle[%d], bad request!, result = %d\n", client_index, p->result);
		return -1;
	}

	if(0 != (ret = coap_parse(&pkt, p->resp_data, p->resp_len))){
		ERROR("carse coap msg failed!.ret = %d\n",ret);
		return -1;
	}
	
	if(pkt.hdr.t == COAP_TYPE_RESET){
		INFO("type = COAP_TYPE_RESET\n");
		*code = 0xff;
		return 0;
	}
	else
		*code = pkt.hdr.code;

	if(buf == NULL || *buf_len == 0){
		ERROR("buf error, can not analysis.\n");
		return -1;
	}
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

wait:
	xSemaphoreTake(m_mutex, portMAX_DELAY);
	p = m_client_handle[client_index];

	if((p->result != REQ_TIMEOUT) && (p->result != REQ_FAILED) && (p->result != REQ_SUCCESS)){
		xSemaphoreGive(m_mutex);
		vTaskDelay(1000 / portTICK_RATE_MS);
		goto wait;
	}
	
	m_client_handle[client_index] = NULL;
	xSemaphoreGive(m_mutex);
	
	vSemaphoreDelete(p->recv_resp_sem);
	free(p);
	
	INFO("free client handle[%d] success!\n", client_index);
	return true;
}

static void coap_client_destroy(coap_client_t *client_handle[])
{
	int i = 0;

	for(i = 0; i < m_client_max_num; i++){
again:
		if(client_handle[i] != NULL){
			vTaskDelay(1000 / portTICK_RATE_MS);
			goto again;
		}
	}

	INFO("client handle is already free over!\n");
	if(client_handle){
		free(client_handle);
		client_handle = NULL;
		INFO("client handle destroy success!\n");
	}else{
		ERROR("client_handle is NULL!\n");
	}
	
}

static void ICACHE_FLASH_ATTR wait2timeout(void *pvParameters)
{
	int i = 0;
	coap_client_t *p = NULL;
	time_t now = 0, wait_time = 0;

	while(sntp_get_current_timestamp() == 0){
		vTaskDelay(1000 / portTICK_RATE_MS);
	}
	INFO("time is sync,time=%d\n", time(NULL));

	while(1){
		now = time(NULL);
		
		xSemaphoreTake(m_mutex, portMAX_DELAY);
		for(i = 0; i < m_client_max_num; i++){
			p = m_client_handle[i];
			if((p != NULL) && (p->result == REQ_WAITTING)){
				wait_time = now - (p->send_time);
				if( (wait_time >= COAP_ACK_TIMEOUT / 1000) && (p->recv_time == 0) ){
					p->result = REQ_TIMEOUT;
					xSemaphoreGive(p->recv_resp_sem);
					INFO("client handle[%d] is timeout,waitting time=%d\n", i, wait_time);
				}
			}
		}
		xSemaphoreGive(m_mutex);
		vTaskDelay(1000 / portTICK_RATE_MS);
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
	
	m_esp_sock.udp.local_port = COAP_DEFAULT_COAP_PORT;
	if(!sw_esp_create_udp(&m_esp_sock.esp_8266, &m_esp_sock.udp, &udp_recv_cb, &udp_send_cb)){
		ERROR("create esp udp failed!\n");
		goto INIT_ERR;
	}
	
	//m_mutex = xSemaphoreCreateCounting(1,1);
	m_mutex = xSemaphoreCreateMutex();
	if(m_mutex == NULL){
		ERROR("create client handle mutex failed!\n");
		goto INIT_ERR;
	}

	xSemaphoreTake(m_mutex, portMAX_DELAY);
	m_client_handle = p;
	xSemaphoreGive(m_mutex);
	m_client_max_num = num;

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

static bool coap_request(int32_t ip, uint16_t port, const char *path, coap_method_t method, coap_msgtype_t type, char *req_data, size_t *req_len, uint8_t *code)
{
	int index = -1, ret = -1;
	
	while(sntp_get_current_timestamp() == 0){
		vTaskDelay(1000 / portTICK_RATE_MS);;
	}
	INFO("time is sync,time=%d\n", time(NULL));
	
	m_rand = rand();
	m_rand += 1;
	*code = 0xfe;
	index = coap_client_create(ip, port, path, method, type, NULL, 0);
	if(index < 0)
		goto GET_ERROR;
	INFO("create client handle[%d] success!\n", index);
	
	ret = coap_send_wait_request(index);
	if(ret < 0)
		goto GET_ERROR;
	INFO("get resq success,client handle[%d].\n", index);

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

static int coap_url_analysis(const char *url, int32_t *ip, uint16_t *port, const char **path)
{
	char ip_str[32] = {0};
	const char *p = NULL;
	char *s = NULL;
	
	p = strstr(url, "coap://");
	if(p != NULL){
		s = strstr(p + 7, "/");
		if(s){
			strncpy(ip_str, p + 7, s - p - 7);
			*path = (s + 1);
		}else{
			ERROR("url format error!\n");
			goto URL_ERROR;
		}
	}else{
		ERROR("mast coap protocol stack 'coap://'.\n");
		goto URL_ERROR;
	}
	
	if( (s = strstr(ip_str, ":")) != NULL){
		*(s + 1) = 0;
		*port = atoi(s + 1);
	}else{
		*port = COAP_DEFAULT_COAP_PORT;
	}

	*ip = CharIp_Trans_Int(ip_str);
	if(*ip == -1){
		INFO("ip is error!\n");
		goto URL_ERROR;
	}

	INFO("ip_str=%s,port=%d,path=%s\n",ip_str, *port, *path);
	return 0;

URL_ERROR:
	return -1;
}

/*coap://10.10.5.32:33200/root/dev/dev1*/
bool sw_coap_get_request(const char *url, coap_method_t method, coap_msgtype_t type, char *req_data, size_t *req_len, uint8_t *code)
{
	const char *path = NULL;
	int32_t ip = -1;
	uint16_t port = 0;

	*code = 0xff;
	if(coap_url_analysis(url, &ip, &port, &path) != 0)
		return false;
	return coap_request(ip, port, path, method, type, req_data, req_len, code);
}

int sw_coap_ping(char *ip_str)
{
	uint8_t code;
	int32_t ip = -1;

	ip = CharIp_Trans_Int(ip_str);
	if(ip == -1){
		INFO("ip is error!\n");
		return -1;
	}
	
	if(!coap_request(ip, COAP_DEFAULT_COAP_PORT, NULL, 0, COAP_TYPE_CON, NULL, 0, &code))
		return -1;
	
	if(code == 0xff)
		return 1;
	else
		return 0;
}

