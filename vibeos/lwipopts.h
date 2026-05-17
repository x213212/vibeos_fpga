
#define SYS_LIGHTWEIGHT_PROT 0
#define NO_SYS 1                   // 不使用RTOS，裸機模式
#define LWIP_ARP 1                // 啟用ARP
#define LWIP_ICMP 1                // 啟用ICMP
#define LWIP_TCP 1                // 啟用TCP
#define LWIP_UDP 1                 // 啟用UDP
#define LWIP_ALTCP 1
#define LWIP_ALTCP_TLS 1
#define LWIP_ALTCP_TLS_MBEDTLS 1
#define LWIP_DNS 1
#define LWIP_RAND() lwip_rand()
void vibe_sntp_set_system_time(unsigned int sec);
#define SNTP_SERVER_DNS 0
#define SNTP_SUPPORT_MULTIPLE_SERVERS 0
#define SNTP_MAX_SERVERS 1
#define SNTP_CHECK_RESPONSE 1
#define SNTP_UPDATE_DELAY 3600000
#define SNTP_RETRY_TIMEOUT 3000
#define SNTP_RETRY_TIMEOUT_EXP 1
#define SNTP_SET_SYSTEM_TIME(sec) vibe_sntp_set_system_time((unsigned int)(sec))
#define MEM_ALIGNMENT 4
#define LWIP_DHCP 1                // DHCP可選
#define ALTCP_MBEDTLS_AUTHMODE MBEDTLS_SSL_VERIFY_NONE
#define ALTCP_MBEDTLS_DEBUG LWIP_DBG_ON
#define ALTCP_MBEDTLS_LIB_DEBUG LWIP_DBG_ON
#define ALTCP_MBEDTLS_LIB_DEBUG_LEVEL_MIN 1
#define LWIP_NETCONN 0
#define LWIP_SOCKET 0
#define MEM_SIZE (512 * 1024)      // 從 64KB 加大到 512KB
#define MEMP_NUM_PBUF 256
#define MEMP_NUM_TCP_PCB 16
#define MEMP_NUM_TCP_PBUF 256
#define MEMP_NUM_TCP_SEG 256
#define PBUF_POOL_SIZE 256
#define TCP_MSS 1460
#define TCP_WND (16 * TCP_MSS)     // 加大視窗大小
#define TCP_SND_BUF (16 * TCP_MSS)
#define TCP_WND_UPDATE_THRESHOLD 1
#define LWIP_DEBUG 0
#define LWIP_PLATFORM_DIAG(x) do { } while(0)
#define LWIP_DEBUG 0
#define LWIP_NO_STDOUT 1
#define LWIP_STATS 0
// #define LWIP_SOCKET     1
// #define LWIP_IPV4       1
