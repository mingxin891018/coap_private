/*================================================================
*   Copyright (C) 2019 All rights reserved.
*   File name: coap_client.c
*   Author: zhaomingxin@sunniwell.net
*   Creation date: 2019-03-01
*   Describe: null
*
*===============================================================*/
#include "coap_client.h"

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

typedef struct req_buffer_ {
	char *p;
	size_t pl;
	size_t inl;
}req_buffer_t;

typedef struct param_node_ {
	char *n;
	uint32_t nl;
	char *v;
	uint32_t vl;
	char *data;
	struct param_node_ *next;
} param_node;

typedef struct param_list_ {
	uint8_t format;
	uint32_t param_num;
	param_node *next;
}param_list_t;

typedef struct {
	//const char *path;
	
	param_list_t *plist;
	
	int send_count;
	os_timer_t timer2timeout;
	xSemaphoreHandle recv_resp_sem;

	uint8_t msgid[2];
	coap_method_t method;
	coap_msgtype_t type;

	req_resule_t result;

	char msg_data[512+128];
	size_t msg_len;

	char resp_data[1024];
	size_t resp_len;

}coap_client_t;

static coap_client_t **m_client_handle = NULL;
static int m_client_max_num = 0;
static espconn_udp_t m_esp_sock = {0};
static xSemaphoreHandle m_mutex = NULL;
static unsigned int m_rand = 0;

static void  coap_wait2timeout(void* ptr);

static param_node *new_node(const char *name, uint32_t nl, const char *value, uint32_t vl)
{
	char *data = NULL;
	param_node *new = NULL;
	
	if(name == NULL || nl <= 0){
		INFO("node name is NULL\n");
		return NULL;
	}
	
	new = (param_node *)malloc(sizeof(param_node));
	if(new == NULL)
		goto err;
	
	data = malloc(nl + 1 + vl + 1); //+\0
	if(data == NULL)
		goto err;

	memset(new, 0, sizeof(param_node));
	memset(data, 0, nl + 1 + vl + 1);

	strncpy(data, name, nl);
	strncpy(data + nl, "=", 1);
	strncpy(data + nl + 1, value, vl);

	new->n = data;
	new->v = data + nl + 1;
	new->nl = nl;
	new->vl = vl;
	new->data = data;
	new->next = NULL;

	return new;

err:
	if(data)
		free(data);
	if(new)
		free(new);
	return NULL;
}

static void param_node_free(param_node *node)
{
	if(node == NULL)
		return ;
	if(node->n != NULL)
		free(node->n);
	if(node->v != NULL)
		free(node->v);
	free(node);
}

static void param_print(param_list_t *list)
{
	int i = 0;
	param_node *node = NULL;

	if(list == NULL)
		return ;

	INFO("param number=%d\n", list->param_num);
	for(node = list->next; node != NULL; node = node->next)
		INFO("data:%s\n", node->data);
}

static bool param_list_add_node(param_list_t *list, param_node *node)
{
	param_node *pn = NULL;

	if(list ==NULL || node == NULL)
		return false;
	if(list->next == NULL){
		list->next = node;
		list->param_num ++;
		INFO("add node success,param_num=%d\n", list->param_num);
		return true;
	}
	for(pn = list->next; pn->next != NULL; pn = pn->next);
	
	pn->next = node;
	node->next = NULL;
	list->param_num ++;
	INFO("add node success,param_num=%d\n", list->param_num);
}

static param_list_free(param_list_t *list)
{
	int i = 0;
	param_node *node = NULL, *next = NULL;
	if(list == NULL)
		return;

	node = list->next;
	while(node != NULL){
		next = node->next;
		if(node->data)
			free(node->data);
		free(node);
		node = next;
		i++;
	}

	INFO("list[%p] has %d nodes,free over\n", list, i);
	list->param_num = 0;
	list->next = NULL;
}

static param_node *param_find_node(param_list_t *list, const char *name, uint32_t nl)
{
	param_node *pn = NULL;

	for(pn = list->next; pn != NULL; pn = pn->next){
		if((memcmp(name, pn->n, nl) == 0) && (pn->nl == nl))
			return pn;
	}
	
	return NULL;
}

static bool param_delete_node(param_list_t *list, param_node *node)
{
	param_node *pb = NULL;
	param_node *pn = param_find_node(list, node->n, node->nl);
	if(pn == NULL){
		ERROR("can not find node=%s\n", node->n);
		return false;
	}
	if(list->next == pn){
		list->next = pn->next;
		param_node_free(pn);
	}

	pb = list->next;
	while(pb->next != pn)
		pb = pb->next;
	pb->next = pn->next;
	free(pn);

	return true;
}

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
				memcpy(p->resp_data, pdata, len);
				p->result = REQ_SUCCESS;
				xSemaphoreGive(m_mutex);
				xSemaphoreGive(p->recv_resp_sem);
				return;
			}
		}
		xSemaphoreGive(m_mutex);
		INFO("can not find handle!\n");
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

static bool is_equal(param_node *node, const char *str)
{
	int len = strlen(str);
	if((0 == memcmp(node->n, str, node->nl)) && (len == node->nl))
		return true;
	return false;
}

static int str2num(const char *str)
{
	if(!strcmp(str, "text/plain"))
		return 0;
	else if(!strcmp(str, "application/link-format"))
		return 40;
	else if(!strcmp(str, "application/xml"))
		return 41;
	else if(!strcmp(str, "application/octet-stream"))
		return 42;
	else if(!strcmp(str, "application/exi"))
		return 47;
	else if(!strcmp(str, "application/json"))
		return 50;
	else
		return 0;
}

static int list2opts(param_list_t *list, coap_option_t *opts)
{
	int i = 0;
	const char *p = NULL, *q = NULL;
	
	if(list == NULL)
		return 0;

	//opt 11
	param_node *node = param_find_node(list, "path", strlen("path"));
	if(node != NULL){
		q = node->v;
		p = strstr(node->v, "/"); 
		while(p != NULL){
			if(*(p + 1) == '\0')
				return -1;
			if(i >= MAXOPT)
				return -1;
			opts[i].buf.p = q;
			opts[i].buf.len = p - q;
			opts[i].num = COAP_OPTION_URI_PATH;
			INFO("i=%d,path=%s,len=%d\n", i, q, opts[i].buf.len);

			q = p + 1;
			p = strstr(p + 1, "/");
			i++;
		}

		opts[i].buf.p = q;
		opts[i].buf.len = strlen(q);
		opts[i].num = COAP_OPTION_URI_PATH;
		INFO("i=%d,path=%s,len=%d\n", i, q, opts[i].buf.len);
		i++;
	}

	//opt 12
	node = param_find_node(list, "Content-Format", strlen("Content-Format"));
	if(node != NULL && i < MAXOPT - 1){
		list->format = str2num(node->v);
		opts[i].buf.p = (char *)&list->format;
		opts[i].buf.len = sizeof(list->format);
		opts[i].num = COAP_OPTION_CONTENT_FORMAT;
		INFO("i=%d,opt12=%d,len=%d\n", i, list->format, opts[i].buf.len);
		i++;
	}

	//opt 15
	node = list->next;
	while(node != NULL && i < MAXOPT - 1){
		if(is_equal(node, "Content-Format")){
			INFO("find param[Content-Format]\n");
			node = node->next;
			continue;
		}
		if(is_equal(node, "path")){
			INFO("find param[path]\n");
			node = node->next;
			continue;
		}
		opts[i].buf.p = node->data;
		opts[i].buf.len = node->vl + node->nl + 1;
		opts[i].num = COAP_OPTION_URI_QUERY;
		node = node->next;
		INFO("i=%d,path=%s,len=%d\n", i, opts[i].buf.p, opts[i].buf.len);
		i++;
	}
	return i;
}

static bool coap_client2packet(coap_client_t *client, const char *tok, int tok_len, coap_packet_t *pkt, char *p, int len)
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
	ret = list2opts(client->plist, &pkt->opts[0]);
	pkt->numopts = ret;
	INFO("numopts=%d\n", pkt->numopts);

	//msg
	pkt->payload.p = p;
	pkt->payload.len = len;
	coap_dumpPacket(pkt);

	return true;
}

static int coap_client_create(uint32_t ip, uint16_t port, param_list_t *list, coap_method_t method, coap_msgtype_t type, const char *tok, int tok_len, char *pdata, int len)
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
		
		os_timer_disarm(&p->timer2timeout);
		os_timer_setfn(&p->timer2timeout, (os_timer_func_t *)coap_wait2timeout, p);
	}
	
	esp_set_remote_ip(ip, port);

	//填写数据到结构体,初始化信号量
	p->plist = list;
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
	if(!coap_client2packet(p, tok, tok_len, &pkt, pdata, len)){
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
	INFO("create coap client handle[%d]=%p MSGID=%2x%2x.\n", i, m_client_handle[i], p->msgid[0], p->msgid[1]);

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

	if(p->type == COAP_TYPE_CON){
		os_timer_arm(&p->timer2timeout, COAP_ACK_TIMEOUT, 0);
		p->result = REQ_WAITTING;
		INFO("set waitting timer.\n");

		xSemaphoreTake(p->recv_resp_sem, portMAX_DELAY);
	}else
		p->result = REQ_SUCCESS;
	
	return 0;
}

static int coap_analysis_response(int client_index, char *buf, int *buf_len, uint8_t *code)
{
	int ret = -1;
	coap_packet_t pkt;
	coap_client_t *p = m_client_handle[client_index];
	
	memset(&pkt, 0, sizeof(pkt));
	
	if(p->result != REQ_SUCCESS){
		INFO("client handle[%d], bad request!, result = %d\n", client_index, p->result);
		return -1;
	}
	
	if(p->type == COAP_TYPE_NONCON){
		memset(buf, 0, *buf_len);
		*buf_len = 0;
		*code = 0x45;
		INFO("request type is COAP_TYPE_NONCON\n");
		return 0;
	}

	os_timer_disarm(&p->timer2timeout);
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
	
	if(p->type == COAP_TYPE_CON)
		vSemaphoreDelete(p->recv_resp_sem);
	
	m_client_handle[client_index] = NULL;
	xSemaphoreGive(m_mutex);
	
	free(p);
	
	INFO("free client handle[%d] success!\n", client_index);
	return true;
}

void sw_coap_destory(void)
{
	int i = 0;
	if(m_client_handle == NULL)
		return;

	for(i = 0; i < m_client_max_num; i++){
again:
		if(m_client_handle[i] != NULL){
			vTaskDelay(1000 / portTICK_RATE_MS);
			goto again;
		}
	}
	espconn_delete(&m_esp_sock.esp_8266);

	if(m_client_handle){
		free(m_client_handle);
		m_client_handle = NULL;
		INFO("client handle free success!\n");
	}else{
		ERROR("client_handle is NULL!\n");
	}
	
}

static void  coap_wait2timeout(void* ptr)
{
	coap_client_t *client = ( coap_client_t *)ptr;
	
	xSemaphoreTake(m_mutex, portMAX_DELAY);
	client->result = REQ_TIMEOUT;
	xSemaphoreGive(m_mutex);
	
	xSemaphoreGive(client->recv_resp_sem);

	INFO("client handle:%p is timeout!\n", ptr);
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

	INFO("init client success!\n");
	
	return true;

INIT_ERR:
	if(p){
		free(p);
		p = NULL;
	}
	return false;
}

static bool coap_request(int32_t ip, uint16_t port, param_list_t *list, coap_method_t method, coap_msgtype_t type, char *req_data, size_t buf_len, size_t *req_len, uint8_t *code)
{
	int index = -1, ret = -1;
	
	while(sntp_get_current_timestamp() == 0){
		vTaskDelay(1000 / portTICK_RATE_MS);;
	}
	INFO("time is sync,time=%d\n", time(NULL));
	
	m_rand = rand();
	m_rand += 1;
	*code = 0xfe;
	index = coap_client_create(ip, port, list, method, type, NULL, 0, req_data, buf_len);
	if(index < 0)
		goto GET_ERROR;
	INFO("create client handle[%d] success!\n", index);
	
	ret = coap_send_wait_request(index);
	if(ret < 0)
		goto GET_ERROR;
	INFO("get resq success,client handle[%d].\n", index);
	
	memset(req_data, 0, *req_len);
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

//coap://10.10.5.32:33200/root/dev/dev1?param1=a&param2=2&format=3
static bool is_coap(const char *url)
{
	char *p = strstr(url, "coap://");
	if(*url == *p)
		return true;
	return false;
}

static int32_t find_ip(const char *url)
{
	const char *ip = url + 7;
	char host[64] = {0};
	char ip_str[32] = {0};
	memset(host, 0, sizeof(host));
	memset(ip_str, 0, sizeof(ip_str));
	
	char *p = strstr(ip, ":");
	if(p == NULL)
		p = strstr(ip, "/");
	
	if(p != NULL){
		memcpy(host, ip, p - ip);
		INFO("host=%s\n", host);
		if(is_address(host))
			return CharIp_Trans_Int(ip_str);
		else if(GetServerIp(host, ip_str) == 0)
			return CharIp_Trans_Int(ip_str);
		else 
			return -1;
	}
	return -1;
}

static int find_port(const char *url)
{
	char str[8] = {0};
	char *p = strstr(url, ":");
	char *q = strstr(url, "/");

	memset(str, 0, sizeof(str));
	if(p != NULL && q != NULL && ((q - p) > 0)){
		memcpy(str, p, q - p);
		INFO("port_str=%s\n", str);
		return atoi(str);
	}
	return -1;
}

static bool check_param(const char *url)
{
	int len = strlen(url);
	char *p = strstr(url, "?");
	if(p == NULL){
		if(url[len - 1] == '/'){
			ERROR("check_param error!\n");
			return false;
		}
	}else{
		if(*(p - 1) == '/'){
			ERROR("check_param error!\n");
			return false;
		}
	}
	
	return true;
}

static void add_param(param_list_t *list, const char *str, int len)
{
	if(len <= 1 || list == NULL)
		return ;
	
	param_node *node = NULL;
	char *p = strstr(str, "=");
	if(p == NULL){
		ERROR("param format error.\n");
		return;
	}else if(*p == *str){
		ERROR("param format error.\n");
		return;
	}
	node = new_node(str, p - str, p + 1, len - (p - str) - 1);
	param_list_add_node(list, node);
	INFO("param[%s] add success!\n", node->data);
}

//coap://10.10.18.253/.well-known/core?ddxx=sdfd==&asdfd=dfdsfsd=dsfd=d
static bool param2list(const char *url, param_list_t *list)
{
	int l = 0, pl = 0;
	param_node *node = NULL;
	char *p = strstr(url, "?"), *q = strstr(url, "/");
	
	if(q == NULL)
		return false;
	
	if(p == NULL){
		//path
		l = strlen(q) - 1;
		if(l > 0){
			node = new_node("path", strlen("path"), (const char *)(q + 1), l);
			param_list_add_node(list, node);
		}
		return true;
	}
	else{
		//path
		node = new_node("path", strlen("path"), (const char *)(q + 1), p - q - 1);
		param_list_add_node(list, node);

		//第一个param '?xxx=xxx&'
		l = p - q - 1; //防止路径为'/?'
		if(l > 0){
			pl = strlen(p); //防止url以'?'结尾
			if(pl <= 2){
				return true;
			}
			q = strstr(url, "&");
			if(q == NULL) { //只有一个参数'?xxx=xxx'
				add_param(list, (const char *)(p + 1), strlen(p + 1));
				return true;
			}
			else{
				add_param(list, (const char *)(p + 1), q - p - 1); //, 两个或者以上参数 添加第一个参数
			}
		}
	}

	//param
	p = strstr(q + 1, "&");
	while(p != NULL){
		INFO("node=%s.len=%d\n", (q + 1), p - q - 1);
		add_param(list, (q + 1), p - q - 1);
		INFO("node=%s.len=%d\n", (q + 1), p - q - 1);
		q = p;
		p = strstr(q+1, "&");
	}
	INFO("node=%s.len=%d\n",q+1, strlen(q) - 1);
	add_param(list, (q + 1), strlen(q) - 1);
	INFO("node=%s.len=%d\n",q+1, strlen(q) - 1);

	return true;
}

//coap://10.10.5.32:33200/root/dev/dev1?param1=a&param2=2&format=3
static int coap_url_analysis(const char *url, int32_t *ip, uint16_t *port, param_list_t *list)
{
	char *param = NULL, *end = NULL;
	
	if(url == NULL || !is_coap(url))
		return -1;
	
	*ip = find_ip(url);
	if(*ip == -1)
		return -1;
	
	*port = find_port(url);
	if(*port <= 0)
		*port = COAP_DEFAULT_COAP_PORT;
	
	INFO("coap ip=%d,port=%d\n", *ip, *port);
	
	if(check_param(url) && param2list((url + 7), list)){
		INFO("url analysis success!\n");
		param_print(list);
		return 0;
	}
	
	return -1;
}

/*coap://10.10.5.32:33200/root/dev/dev1?param1=a&param2=2&format=3*/
//int sw_coap_get_request(const char *url, coap_method_t method, coap_msgtype_t type,  req_buffer_t data,uint8_t *code)

//*req_len :缓冲区长度
//buf_len :发送的payload长度
bool sw_coap_get_request(const char *url, coap_method_t method, coap_msgtype_t type, char *req_data,size_t *req_len, size_t buf_len, uint8_t *code)
{
	const char *path = NULL;
	int32_t ip = -1;
	uint16_t port = 0;
	param_list_t list;

	INFO("url = %s\n",url);
	*code = 0xff;

	memset(&list, 0, sizeof(param_list_t));
	list.format = 0;
	list.param_num = 0;
	list.next = NULL;
	if(coap_url_analysis(url, &ip, &port, &list) != 0)
		return false;
	bool ret = coap_request(ip, port, &list, method, type, req_data, buf_len, req_len, code);
	param_list_free(&list);

	return ret;
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
	
	if(!coap_request(ip, COAP_DEFAULT_COAP_PORT, NULL, 0, COAP_TYPE_CON, NULL, 0, NULL, &code))
		return -1;
	
	if(code == 0xff)
		return 1;
	else
		return 0;
}

