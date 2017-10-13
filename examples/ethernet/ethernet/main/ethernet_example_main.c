/* ethernet Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"


#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_err.h"
#include "esp_event_loop.h"
#include "esp_event.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "esp_eth.h"

#include "rom/ets_sys.h"
#include "rom/gpio.h"

#include "soc/dport_reg.h"
#include "soc/io_mux_reg.h"
#include "soc/rtc_cntl_reg.h"
#include "soc/gpio_reg.h"
#include "soc/gpio_sig_map.h"

#include "tcpip_adapter.h"
#include "nvs_flash.h"
#include "driver/gpio.h"


#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include "lwip/netdb.h"
#include "lwip/dns.h"



/* The event group allows multiple bits for each event,
 *    but we only care about one event - are we connected
 *       to the AP with an IP? */
const int CONNECTED_BIT = BIT0;
static EventGroupHandle_t wifi_event_group;


#define EXAMPLE_WIFI_SSID "CONFIG_WIFI_SSID"
#define EXAMPLE_WIFI_PASS "CONFIG_WIFI_PASSWORD"
/* Constants that aren't configurable in menuconfig */
#define WEB_SERVER "x2o.net"
#define WEB_PORT 80
#define WEB_URL "http://x2o.net/"

static const char *REQUEST = "GET " WEB_URL " HTTP/1.0\r\n"
    "Host: "WEB_SERVER"\r\n"
    "User-Agent: esp-idf/1.0 esp32\r\n"
    "\r\n";


static esp_err_t event_handler(void *ctx, system_event_t *event)
{
    switch(event->event_id) {
    case SYSTEM_EVENT_STA_START:
        esp_wifi_connect();
        break;
    case SYSTEM_EVENT_STA_GOT_IP:
        xEventGroupSetBits(wifi_event_group, CONNECTED_BIT);
        break;
    case SYSTEM_EVENT_STA_DISCONNECTED:
        /* This is a workaround as ESP32 WiFi libs don't currently
           auto-reassociate. */
        esp_wifi_connect();
        xEventGroupClearBits(wifi_event_group, CONNECTED_BIT);
        break;
    default:
        break;
    }
    return ESP_OK;
}





#ifdef CONFIG_PHY_LAN8720
#include "eth_phy/phy_lan8720.h"
#define DEFAULT_ETHERNET_PHY_CONFIG phy_lan8720_default_ethernet_config
#endif
#ifdef CONFIG_PHY_TLK110
#include "eth_phy/phy_tlk110.h"
#define DEFAULT_ETHERNET_PHY_CONFIG phy_tlk110_default_ethernet_config
#endif

static const char *TAG = "eth_example";

#define PIN_PHY_POWER CONFIG_PHY_POWER_PIN
#define PIN_SMI_MDC   CONFIG_PHY_SMI_MDC_PIN
#define PIN_SMI_MDIO  CONFIG_PHY_SMI_MDIO_PIN

#ifdef CONFIG_PHY_USE_POWER_PIN
/* This replaces the default PHY power on/off function with one that
   also uses a GPIO for power on/off.

   If this GPIO is not connected on your device (and PHY is always powered), you can use the default PHY-specific power
   on/off function rather than overriding with this one.
*/
static void phy_device_power_enable_via_gpio(bool enable)
{
    ESP_LOGI(TAG, "phy_device_power_enable_via_gpio Start");
    assert(DEFAULT_ETHERNET_PHY_CONFIG.phy_power_enable);

    if (!enable) {
        /* Do the PHY-specific power_enable(false) function before powering down */
        DEFAULT_ETHERNET_PHY_CONFIG.phy_power_enable(false);
    }

    gpio_pad_select_gpio(PIN_PHY_POWER);
    gpio_set_direction(PIN_PHY_POWER,GPIO_MODE_OUTPUT);
    if(enable == true) {
        gpio_set_level(PIN_PHY_POWER, 1);
        ESP_LOGI(TAG, "phy_device_power_enable(TRUE)");
    } else {
        gpio_set_level(PIN_PHY_POWER, 0);
        ESP_LOGI(TAG, "power_enable(FALSE)");
    }

    // Allow the power up/down to take effect, min 300us
    vTaskDelay(1);

    if (enable) {
        /* Run the PHY-specific power on operations now the PHY has power */
        DEFAULT_ETHERNET_PHY_CONFIG.phy_power_enable(true);
    }
}
#endif

static void eth_gpio_config_rmii(void)
{
    // RMII data pins are fixed:
    // TXD0 = GPIO19
    // TXD1 = GPIO22
    // TX_EN = GPIO21
    // RXD0 = GPIO25
    // RXD1 = GPIO26
    // CLK == GPIO0
    phy_rmii_configure_data_interface_pins();
    // MDC is GPIO 23, MDIO is GPIO 18
    phy_rmii_smi_configure_pins(PIN_SMI_MDC, PIN_SMI_MDIO);
}

void eth_task(void *pvParameter)
{
    const struct addrinfo hints = {
        .ai_family = AF_INET,
        .ai_socktype = SOCK_STREAM,
    };
    struct addrinfo *res;
    struct in_addr *addr;
    int s, r;
    char recv_buf[64];


    ESP_LOGI(TAG, "eth_task Start");
    tcpip_adapter_ip_info_t ip;
    memset(&ip, 0, sizeof(tcpip_adapter_ip_info_t));
    //vTaskDelay(2000 / portTICK_PERIOD_MS);

    while (1) {


        if (tcpip_adapter_get_ip_info(ESP_IF_ETH, &ip) == 0) {
            ESP_LOGI(TAG, "~~~~~~~~~~~");
            ESP_LOGI(TAG, "ETHIP:"IPSTR, IP2STR(&ip.ip));
            ESP_LOGI(TAG, "ETHPMASK:"IPSTR, IP2STR(&ip.netmask));
            ESP_LOGI(TAG, "ETHPGW:"IPSTR, IP2STR(&ip.gw));
            ESP_LOGI(TAG, "~~~~~~~~~~~");

	    int err = getaddrinfo(WEB_SERVER, "80", &hints, &res);

	    if(err != 0 || res == NULL) {
		ESP_LOGE(TAG, "DNS lookup failed err=%d res=%p", err, res);
		vTaskDelay(1000 / portTICK_PERIOD_MS);
		continue;
	    }

	    /* Code to print the resolved IP.

	       Note: inet_ntoa is non-reentrant, look at ipaddr_ntoa_r for "real" code */
	    addr = &((struct sockaddr_in *)res->ai_addr)->sin_addr;
	    ESP_LOGI(TAG, "DNS lookup succeeded. IP=%s", inet_ntoa(*addr));

	    s = socket(res->ai_family, res->ai_socktype, 0);
	    if(s < 0) {
		ESP_LOGE(TAG, "... Failed to allocate socket.");
		freeaddrinfo(res);
		vTaskDelay(1000 / portTICK_PERIOD_MS);
		continue;
	    }
	    ESP_LOGI(TAG, "... allocated socket");

	    if(connect(s, res->ai_addr, res->ai_addrlen) != 0) {
		ESP_LOGE(TAG, "... socket connect failed errno=%d", errno);
		close(s);
		freeaddrinfo(res);
		vTaskDelay(4000 / portTICK_PERIOD_MS);
		continue;
	    }

	    ESP_LOGI(TAG, "... connected");
	    freeaddrinfo(res);

	    if (write(s, REQUEST, strlen(REQUEST)) < 0) {
		ESP_LOGE(TAG, "... socket send failed");
		close(s);
		vTaskDelay(4000 / portTICK_PERIOD_MS);
		continue;
	    }
	    ESP_LOGI(TAG, "... socket send success");

	    struct timeval receiving_timeout;
	    receiving_timeout.tv_sec = 5;
	    receiving_timeout.tv_usec = 0;
	    if (setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &receiving_timeout,
		    sizeof(receiving_timeout)) < 0) {
		ESP_LOGE(TAG, "... failed to set socket receiving timeout");
		close(s);
		vTaskDelay(4000 / portTICK_PERIOD_MS);
		continue;
	    }
	    ESP_LOGI(TAG, "... set socket receiving timeout success");

	    /* Read HTTP response */
	    do {
		bzero(recv_buf, sizeof(recv_buf));
		r = read(s, recv_buf, sizeof(recv_buf)-1);
		for(int i = 0; i < r; i++) {
		    putchar(recv_buf[i]);
		}
	    } while(r > 0);

	    ESP_LOGI(TAG, "... done reading from socket. Last read return=%d errno=%d\r\n", r, errno);
	    close(s);
/*
	    for(int countdown = 10; countdown >= 0; countdown--) {
		ESP_LOGI(TAG, "%d... ", countdown);
		vTaskDelay(1000 / portTICK_PERIOD_MS);
	    }
*/
	    ESP_LOGI(TAG, "Starting again!");

        }
        vTaskDelay(2000 / portTICK_PERIOD_MS);
    }
}


static void initialise_wifi(void)
{
    wifi_event_group = xEventGroupCreate();
return;
    tcpip_adapter_init();
    ESP_ERROR_CHECK( esp_event_loop_init(event_handler, NULL) );
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK( esp_wifi_init(&cfg) );
    ESP_ERROR_CHECK( esp_wifi_set_storage(WIFI_STORAGE_RAM) );
    wifi_config_t wifi_config = {
        .sta = {
            .ssid = EXAMPLE_WIFI_SSID,
            .password = EXAMPLE_WIFI_PASS,
        },
    };
    ESP_LOGI(TAG, "Setting WiFi configuration SSID %s...", wifi_config.sta.ssid);
    ESP_ERROR_CHECK( esp_wifi_set_mode(WIFI_MODE_STA) );
    ESP_ERROR_CHECK( esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config) );
    ESP_ERROR_CHECK( esp_wifi_start() );
    ESP_LOGI(TAG, "Setting done?");
}

void app_main()
{
    ESP_ERROR_CHECK( nvs_flash_init() );

    esp_err_t ret = ESP_OK;
    tcpip_adapter_init();
    esp_event_loop_init(NULL, NULL);

    eth_config_t config = DEFAULT_ETHERNET_PHY_CONFIG;
    /* Set the PHY address in the example configuration */
    config.phy_addr = CONFIG_PHY_ADDRESS;
    config.gpio_config = eth_gpio_config_rmii;
    config.tcpip_input = tcpip_adapter_eth_input;

#ifdef CONFIG_PHY_USE_POWER_PIN
    /* Replace the default 'power enable' function with an example-specific
       one that toggles a power GPIO. */
    config.phy_power_enable = phy_device_power_enable_via_gpio;
#endif

    ret = esp_eth_init(&config);
    esp_log_level_set("*", ESP_LOG_DEBUG);        // set all components to ERROR level


    if(ret == ESP_OK) {
        esp_eth_enable();
        xTaskCreate(eth_task, "eth_task", 2048, NULL, (tskIDLE_PRIORITY + 2), NULL);
    }

}
