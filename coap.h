#ifndef __COAP_H__
#define __COAP_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define MAXOPT 16

//http://tools.ietf.org/html/rfc7252#section-3
typedef struct
{
    uint8_t ver;                /* CoAP version number */
    uint8_t t;                  /* CoAP Message Type */
    uint8_t tkl;                /* Token length: indicates length of the Token field */
    uint8_t code;               /* CoAP status code. Can be request (0.xx), success reponse (2.xx), 
                                 * client error response (4.xx), or rever error response (5.xx) 
                                 * For possible values, see http://tools.ietf.org/html/rfc7252#section-12.1 */
    uint8_t id[2];
} coap_header_t;

typedef struct
{
    const uint8_t *p;
    size_t len;
} coap_buffer_t;

typedef struct
{
    uint8_t *p;
    size_t len;
} coap_rw_buffer_t;

typedef struct
{
    uint8_t num;                /* Option number. See http://tools.ietf.org/html/rfc7252#section-5.10 */
    coap_buffer_t buf;          /* Option value */
} coap_option_t;

typedef struct
{
    coap_header_t hdr;          /* Header of the packet */
    coap_buffer_t tok;          /* Token value, size as specified by hdr.tkl */
    uint8_t numopts;            /* Number of options */
    coap_option_t opts[MAXOPT]; /* Options of the packet. For possible entries see
                                 * http://tools.ietf.org/html/rfc7252#section-5.10 */
    coap_buffer_t payload;      /* Payload carried by the packet */
} coap_packet_t;

/////////////////////////////////////////

//http://tools.ietf.org/html/rfc7252#section-12.2
typedef enum
{
    COAP_OPTION_IF_MATCH = 1,
    COAP_OPTION_URI_HOST = 3,
    COAP_OPTION_ETAG = 4,
    COAP_OPTION_IF_NONE_MATCH = 5,
    COAP_OPTION_OBSERVE = 6,
    COAP_OPTION_URI_PORT = 7,
    COAP_OPTION_LOCATION_PATH = 8,
    COAP_OPTION_URI_PATH = 11,
    COAP_OPTION_CONTENT_FORMAT = 12,
    COAP_OPTION_MAX_AGE = 14,
    COAP_OPTION_URI_QUERY = 15,
    COAP_OPTION_ACCEPT = 17,
    COAP_OPTION_LOCATION_QUERY = 20,
    COAP_OPTION_PROXY_URI = 35,
    COAP_OPTION_PROXY_SCHEME = 39
} coap_option_num_t;

//http://tools.ietf.org/html/rfc7252#section-12.1.1
typedef enum
{
    COAP_METHOD_GET = 1,
    COAP_METHOD_POST = 2,
    COAP_METHOD_PUT = 3,
    COAP_METHOD_DELETE = 4
} coap_method_t;

//http://tools.ietf.org/html/rfc7252#section-12.1.1
typedef enum
{
    COAP_TYPE_CON = 0,
    COAP_TYPE_NONCON = 1,
    COAP_TYPE_ACK = 2,
    COAP_TYPE_RESET = 3
} coap_msgtype_t;

//http://tools.ietf.org/html/rfc7252#section-5.2
//http://tools.ietf.org/html/rfc7252#section-12.1.2
#define MAKE_RSPCODE(clas, det) ((clas << 5) | (det))
typedef enum
{
    COAP_RSPCODE_CONTENT = MAKE_RSPCODE(2, 5),
    COAP_RSPCODE_NOT_FOUND = MAKE_RSPCODE(4, 4),
    COAP_RSPCODE_BAD_REQUEST = MAKE_RSPCODE(4, 0),
    COAP_RSPCODE_CHANGED = MAKE_RSPCODE(2, 4)
} coap_responsecode_t;

//http://tools.ietf.org/html/rfc7252#section-12.3
typedef enum
{
    COAP_CONTENTTYPE_NONE = -1, // bodge to allow us not to send option block
    COAP_CONTENTTYPE_TEXT_PLAIN = 0,
    COAP_CONTENTTYPE_APPLICATION_LINKFORMAT = 40,
} coap_content_type_t;

///////////////////////

typedef enum
{
    COAP_ERR_NONE = 0,
    COAP_ERR_HEADER_TOO_SHORT = 1,
    COAP_ERR_VERSION_NOT_1 = 2,
    COAP_ERR_TOKEN_TOO_SHORT = 3,
    COAP_ERR_OPTION_TOO_SHORT_FOR_HEADER = 4,
    COAP_ERR_OPTION_TOO_SHORT = 5,
    COAP_ERR_OPTION_OVERRUNS_PACKET = 6,
    COAP_ERR_OPTION_TOO_BIG = 7,
    COAP_ERR_OPTION_LEN_INVALID = 8,
    COAP_ERR_BUFFER_TOO_SMALL = 9,
    COAP_ERR_UNSUPPORTED = 10,
    COAP_ERR_OPTION_DELTA_INVALID = 11,
} coap_error_t;

///////////////////////

typedef int (*coap_endpoint_func)(coap_rw_buffer_t *scratch, const coap_packet_t *inpkt, coap_packet_t *outpkt, uint8_t id_hi, uint8_t id_lo);
#define MAX_SEGMENTS 3  // 2 = /foo/bar, 3 = /foo/bar/baz
typedef struct
{
    int count;
    const char *elems[MAX_SEGMENTS];
} coap_endpoint_path_t;

typedef struct
{
    coap_method_t method;               /* (i.e. POST, PUT or GET) */
    coap_endpoint_func handler;         /* callback function which handles this 
                                         * type of endpoint (and calls 
                                         * coap_make_response() at some point) */
    const coap_endpoint_path_t *path;   /* path towards a resource (i.e. foo/bar/) */ 
    const char *core_attr;              /* the 'ct' attribute, as defined in RFC7252, section 7.2.1.:
                                         * "The Content-Format code "ct" attribute 
                                         * provides a hint about the 
                                         * Content-Formats this resource returns." 
                                         * (Section 12.3. lists possible ct values.) */
} coap_endpoint_t;


//function 打印coap_packet_t 对象
void coap_dumpPacket(coap_packet_t *pkt);

//function 打印buf中的二进制数据
//param bare 是否换行
void coap_dump(const uint8_t *buf, size_t buflen, bool bare);

//function 将buf中的coap协议包解析成coap_packet_t结构体数据
int coap_parse(coap_packet_t *pkt, const uint8_t *buf, size_t buflen);

//function 将coap_buffer_t结构体中的数据copy到strbuf中
int coap_buffer_to_string(char *strbuf, size_t strbuflen, const coap_buffer_t *buf);

//function 在coap_packet_t结构体中找到T为num的option数据并返回
const coap_option_t *coap_findOptions(const coap_packet_t *pkt, uint8_t num, uint8_t *count);

//function 将coap_packet_t对象封装成CoAP协议包,并存储到buf中
int coap_build(uint8_t *buf, size_t *buflen, const coap_packet_t *pkt);

//function	将参数所提供的数据封装成一个coap_packet_t对象
//param scratch 		将opt12数据写入其中
//param pkt 			输出的封装好的coap_packet_t对象
//param content 		payload数据
//param content_len 	payload数据的长度
//param msgid_hi		Message ID的高字节
//param msgid_lo		Message ID的低字节
//param tok				tok值对象
//param rspcode			响应码
//param content_type 	opt12的值,响应报文的编码格式
//return 返回0说明解析参数正确,并生成coap_packet_t对象,返回COAP_ERR_BUFFER_TOO_SMALL说明scratch空间太小,scratch空间最小2byte
int coap_make_response(coap_rw_buffer_t *scratch, coap_packet_t *pkt, const uint8_t *content, size_t content_len, uint8_t msgid_hi, uint8_t msgid_lo, const coap_buffer_t* tok, coap_responsecode_t rspcode, coap_content_type_t content_type);

//function 处理收到的CoAP包,并根据设置的资源对此包进行回复
//param scratch 	回复消息的编码格式
//param inpkt		收到的CoAP包
//param outpkt		根据inpkt制作好的response包存放在outpkt空间中,outpkt在此函数中被发送
int coap_handle_req(coap_rw_buffer_t *scratch, const coap_packet_t *inpkt, coap_packet_t *outpkt);

//function 计算option结构中T和L的扩展方式
//当值为13：有一个8-bit无符号整型（extended）跟随在第一个字节之后，本option的实际delta是这个8-bit值加13。
//当值为14：有一个16-bit无符号整型（网络字节序）（extended）跟随在第一个字节之后，本option的实际delta是这个16-bit值加269。
//当值为15：为payload标识符而保留。如果这个字段被设置为值15，但这个字节不是payload标识符，那么必须当作消息格式错误来处理。
//传入T和L的实际值,将扩展方式回填到参数nibble中
void coap_option_nibble(uint32_t value, uint8_t *nibble);

//function 设置coap
void coap_setup(void);

//function 设置全局变量数组 endpoints
void endpoint_setup(void);

#ifdef __cplusplus
}
#endif

#endif
