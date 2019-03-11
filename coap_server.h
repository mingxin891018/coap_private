/*================================================================
*   Copyright (C) 2019 All rights reserved.
*   File name: coap_server.h
*   Author: zhaomingxin@sunniwell.net
*   Creation date: 2019-03-08
*   Describe: null
*
*===============================================================*/
#ifndef __COAP_SERVER_H__
#define __COAP_SERVER_H__

int sw_coap_server_create(void);
int sw_coap_server_distroy(void);
bool sw_coap_recv_request(struct espconn *udp, char *pdata, unsigned short len);

#endif //__COAP_SERVER_H__
