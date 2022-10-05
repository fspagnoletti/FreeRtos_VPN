/* WireGuard demo example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/event_groups.h>
#include <esp_event.h>
#include <esp_idf_version.h>
#include <esp_log.h>
#include <esp_system.h>
#include <esp_wifi.h>
#include <nvs_flash.h>
#include <lwip/netdb.h>

#include <esp_wireguard.h>
#include "sync_time.h"

#define EXAMPLE_ESP_WIFI_SSID      CONFIG_ESP_WIFI_SSID
#define EXAMPLE_ESP_WIFI_PASS      CONFIG_ESP_WIFI_PASSWORD
#define EXAMPLE_ESP_MAXIMUM_RETRY  CONFIG_ESP_MAXIMUM_RETRY

#if defined(CONFIG_IDF_TARGET_ESP8266)
#include <esp_netif.h>
#elif ESP_IDF_VERSION < ESP_IDF_VERSION_VAL(4, 2, 0)
#include <tcpip_adapter.h>
#else
#include <esp_netif.h>
#endif

/* FreeRTOS event group to signal when we are connected*/
static EventGroupHandle_t s_wifi_event_group;

/* The event group allows multiple bits for each event, but we only care about two events:
 * - we are connected to the AP with an IP
 * - we failed to connect after the maximum amount of retries */
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

static const char *TAG = "demo";
static int s_retry_num = 0;
static wireguard_config_t wg_config = ESP_WIREGUARD_CONFIG_DEFAULT();

static esp_err_t wireguard_setup(wireguard_ctx_t* ctx)
{
    esp_err_t err = ESP_FAIL;

    ESP_LOGI(TAG, "Initializing WireGuard.");
    wg_config.private_key = CONFIG_WG_PRIVATE_KEY;
    wg_config.listen_port = CONFIG_WG_LOCAL_PORT;
    wg_config.public_key = CONFIG_WG_PEER_PUBLIC_KEY;
    wg_config.allowed_ip = CONFIG_WG_LOCAL_IP_ADDRESS;
    wg_config.allowed_ip_mask = CONFIG_WG_LOCAL_IP_NETMASK;
    wg_config.endpoint = CONFIG_WG_PEER_ADDRESS;
    wg_config.port = CONFIG_WG_PEER_PORT;
    wg_config.persistent_keepalive = CONFIG_WG_PERSISTENT_KEEP_ALIVE;

    err = esp_wireguard_init(&wg_config, ctx);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_wireguard_init: %s", esp_err_to_name(err));
        goto fail;
    }

    ESP_LOGI(TAG, "Connecting to the peer.");
    err = esp_wireguard_connect(ctx);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_wireguard_connect: %s", esp_err_to_name(err));
        goto fail;
    }

    err = ESP_OK;
fail:
    return err;
}

static void event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry_num < EXAMPLE_ESP_MAXIMUM_RETRY) {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(TAG, "retry to connect to the AP");
        } else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
        ESP_LOGI(TAG,"connect to the AP fail");
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

#ifdef CONFIG_WIREGUARD_ESP_TCPIP_ADAPTER
static esp_err_t wifi_init_tcpip_adaptor(void)
{
    esp_err_t err = ESP_FAIL;
    s_wifi_event_group = xEventGroupCreate();

    tcpip_adapter_init();

    ESP_ERROR_CHECK(esp_event_loop_create_default());

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = EXAMPLE_ESP_WIFI_SSID,
            .password = EXAMPLE_ESP_WIFI_PASS
        },
    };

    /* Setting a password implies station will connect to all security modes including WEP/WPA.
        * However these modes are deprecated and not advisable to be used. Incase your Access point
        * doesn't support WPA2, these mode can be enabled by commenting below line */

    if (strlen((char *)wifi_config.sta.password)) {
        wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    }

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA) );
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config) );
    ESP_ERROR_CHECK(esp_wifi_start() );

    ESP_LOGI(TAG, "wifi_init_sta finished.");

    /* Waiting until either the connection is established (WIFI_CONNECTED_BIT) or connection failed for the maximum
     * number of re-tries (WIFI_FAIL_BIT). The bits are set by event_handler() (see above) */
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
            WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
            pdFALSE,
            pdFALSE,
            portMAX_DELAY);

    /* xEventGroupWaitBits() returns the bits before the call returned, hence we can test which event actually
     * happened. */
    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "connected to ap SSID:%s", EXAMPLE_ESP_WIFI_SSID);
    } else if (bits & WIFI_FAIL_BIT) {
        ESP_LOGI(TAG, "Failed to connect to SSID:%s", EXAMPLE_ESP_WIFI_SSID);
        err = ESP_FAIL;
        goto fail;
    } else {
        ESP_LOGE(TAG, "Unknown event");
        err = ESP_FAIL;
        goto fail;
    }

    ESP_ERROR_CHECK(esp_event_handler_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler));
    ESP_ERROR_CHECK(esp_event_handler_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler));
    vEventGroupDelete(s_wifi_event_group);

    err = ESP_OK;
fail:
    return err;
}
#endif // CONFIG_WIREGUARD_ESP_TCPIP_ADAPTER

#ifdef CONFIG_WIREGUARD_ESP_NETIF
static esp_err_t wifi_init_netif(void)
{
    esp_err_t err = ESP_FAIL;
    esp_netif_t *sta_netif;

    s_wifi_event_group = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_netif_init());

    ESP_ERROR_CHECK(esp_event_loop_create_default());
    sta_netif = esp_netif_create_default_wifi_sta();
    assert(sta_netif);

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_got_ip));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = EXAMPLE_ESP_WIFI_SSID,
            .password = EXAMPLE_ESP_WIFI_PASS,
            /* Setting a password implies station will connect to all security modes including WEP/WPA.
             * However these modes are deprecated and not advisable to be used. Incase your Access point
             * doesn't support WPA2, these mode can be enabled by commenting below line */
         .threshold.authmode = WIFI_AUTH_WPA2_PSK,

            .pmf_cfg = {
                .capable = true,
                .required = false
            },
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA) );
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config) );
    ESP_ERROR_CHECK(esp_wifi_start() );

    /* Waiting until either the connection is established (WIFI_CONNECTED_BIT) or connection failed for the maximum
     * number of re-tries (WIFI_FAIL_BIT). The bits are set by event_handler() (see above) */
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
            WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
            pdFALSE,
            pdFALSE,
            portMAX_DELAY);

    /* xEventGroupWaitBits() returns the bits before the call returned, hence we can test which event actually
     * happened. */
    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "Connected to ap SSID:%s", EXAMPLE_ESP_WIFI_SSID);
    } else if (bits & WIFI_FAIL_BIT) {
        ESP_LOGI(TAG, "Failed to connect to SSID:%s", EXAMPLE_ESP_WIFI_SSID);
        err = ESP_FAIL;
        goto fail;
    } else {
        ESP_LOGE(TAG, "Unknown event");
        err = ESP_FAIL;
        goto fail;
    }

    err = esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, instance_got_ip);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_event_handler_instance_unregister: %s", esp_err_to_name(err));
        goto fail;
    }
    err = esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, instance_any_id);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_event_handler_instance_unregister: %s", esp_err_to_name(err));
        goto fail;
    }
    vEventGroupDelete(s_wifi_event_group);

    err = ESP_OK;
fail:
    return err;
}
#endif // CONFIG_WIREGUARD_ESP_NETIF

static esp_err_t wifi_init_sta(void)
{
#if defined(CONFIG_WIREGUARD_ESP_TCPIP_ADAPTER)
    return wifi_init_tcpip_adaptor();
#endif
#if defined(CONFIG_WIREGUARD_ESP_NETIF)
    return wifi_init_netif();
#endif
}

static const char *strings[] = {"Hello from ESP32, please login\n"
                                "Username: ",
                                "Password: ",
                                "Authentication succeded\n"
                                "Select commands:\n"
                                "- Hello:  to get greetings\n"
                                "- Usage:  to read cpu load\n"
                                "- LogOut: Log Out\n"
								"> ",
                                "Authentication failed, try again...\n"
                                "Username: "};


typedef enum {S_ANON = 0, S_WAITPASS = 1, S_COMMANDS = 2} STATE_T;

#define N_USERS 4
static const char *users[] = {"Lucia", "Lorenzo", "Francesco", "Simone"};
static const char *users_passwords[] = {"Vencato98", "Chiola98", "Spagnoletti98", "Pistilli98"};


static void tcp_client_task(void *pvParameters)
{
    static const char *TAG = "TCP Comunication";
    //static const char host_ip[] = "192.168.77.1";
    static const char host_ip[] = CONFIG_EXAMPLE_PING_ADDRESS;
	static const unsigned short PORT = 3333;		//listening port for the tcp communication

    char rx_buffer[128] = "";
	char msg[128] = "";
	int err = 0;
	int addr_family = AF_INET;
	int ip_protocol = IPPROTO_IP;
	int sock = 0;
	int len = 0;
    STATE_T state = S_ANON;
    char username[32]= "";
    char password[32] = "";
	int uid = -1;
	char *temp = NULL;

	while (1) {			//in this loop there is the creation of the socket and the connection is established
        struct sockaddr_in dest_addr;
        dest_addr.sin_addr.s_addr = inet_addr(host_ip);
        dest_addr.sin_family = AF_INET;
        dest_addr.sin_port = htons(PORT);

        sock =  socket(addr_family, SOCK_STREAM, ip_protocol);
        if (sock < 0) {
            ESP_LOGE(TAG, "Unable to create TCP socket: errno %d", errno);
            return;
        }
        ESP_LOGI(TAG, "TCP socket created, connecting to %s:%d", host_ip, PORT);

        err = connect(sock, (struct sockaddr *)&dest_addr, sizeof(struct sockaddr_in6));
        if (err != 0) {
            ESP_LOGE(TAG, "Socket unable to connect TCP socket: errno %d", errno);
            return;
        }
        ESP_LOGI(TAG, "Successfully connected");

        //First message sent by the ESP32: LOGIN"
        err = send(sock, strings[0], strlen(strings[0]), 0);
        if (err < 0) {
            ESP_LOGE(TAG, "Error occurred during TCP send: errno %d", errno);
            return;
        }

        while (1) {			//In this loop it starts the communication: the esp32 sends messages and the user can answer

            len = recv(sock, rx_buffer, sizeof(rx_buffer) - 1, 0);
            if (len < 0) // Error occurred during receiving
            {
                ESP_LOGE(TAG, "TCP recv failed: errno %d", errno);
                return;
            }
            else // Data received
            {
                rx_buffer[len] = 0; // Null-terminate whatever we received and treat like a string
				//strchr(rx_buffer, '\r');
				temp = strpbrk(rx_buffer, "\r\n\t");
				len = temp - rx_buffer;
				rx_buffer[len] = '\0';
				
                ESP_LOGI(TAG, "Received %d bytes in TCP socket from %s:", len, host_ip);
                ESP_LOGI(TAG, "%s", rx_buffer);
            }


            switch (state) {			// the message to be sent is implemented like a fsm, in each state it is check the user input in order to send the correct output message
                case S_ANON:
					strncpy(username, rx_buffer, sizeof(username));
					username[sizeof(username)-1] = 0;
					//ESP_LOGE(TAG, "User %s is trying to log in", username);

					state = S_WAITPASS;
					err = send(sock, strings[1], strlen(strings[1]), 0);
					if (err < 0) {
						ESP_LOGE(TAG, "Error occurred during TCP send: errno %d", errno);
						return;
					}
                    break;

                case S_WAITPASS:
					strncpy(password, rx_buffer, sizeof(password));
					password[sizeof(password)-1] = 0;
					//ESP_LOGE(TAG, "User %s is trying to log in with password %s", username, password);

					//search Username
					for(uid=0; uid<N_USERS; uid++){
						//ESP_LOGE(TAG, "Trying user %s (uid %d) who has password %s", users[uid], uid, users_passwords[uid]);

						if( (strcasecmp(username, users[uid]) == 0)) {
							//if Users exist checks password
							//ESP_LOGI(TAG, "User %s has uid %d", username, uid);

							if((strcmp(password, users_passwords[uid]) == 0)){
								ESP_LOGI(TAG, "login successful by %s (uid %d)", users[uid], uid);
								state = S_COMMANDS;
								err = send(sock, strings[2], strlen(strings[2]), 0);
							} else {
								//ESP_LOGE(TAG, "login failed, %d's password is %s", uid, users_passwords[uid]);
								ESP_LOGE(TAG, "login rejected for %s (uid %d)", users[uid], uid);
								state = S_ANON;
								err = send(sock, strings[3], strlen(strings[3]), 0);
							}
							break;
						}
					}
					if (uid == N_USERS){
						ESP_LOGE(TAG, "user %s not found", username);
						state = S_ANON;
						err = send(sock, strings[3], strlen(strings[3]), 0);
					}

					if (err < 0) {
						ESP_LOGE(TAG, "Error occurred during TCP send: errno %d", errno);
						return;
					}
                    break;

                case S_COMMANDS:
                    if(strcasecmp(rx_buffer, "Hello") == 0){
                        sprintf(msg, "Hello %s\n> ", username);
                        err = send(sock, msg, strlen(msg), 0);
                        if (err < 0) {
                            ESP_LOGE(TAG, "Error occurred during TCP send: errno %d", errno);
                            return;
                        }
                    } else if (strcasecmp(rx_buffer, "Usage") == 0){
                        sprintf(msg, "TODO CPU USAGE\n> ");
                        err = send(sock, msg, strlen(msg), 0);
                        if (err < 0) {
                            ESP_LOGE(TAG, "Error occurred during TCP send: errno %d", errno);
                            return;
                        }
                    } else if (strcasecmp(rx_buffer, "LogOut") == 0){
                        state = S_ANON;
                        err = send(sock, strings[0], strlen(strings[0]), 0);
                        if (err < 0) {
                            ESP_LOGE(TAG, "Error occurred during TCP send: errno %d", errno);
                            return;
                        }
                    } else {
                        sprintf(msg, "Urecognized command.\n> ");
                        err = send(sock, msg, strlen(msg), 0);
                        if (err < 0) {
                            ESP_LOGE(TAG, "Error occurred during TCP send: errno %d", errno);
                            return;
                        }
					}
                    break;
            }

            vTaskDelay(2000 / portTICK_PERIOD_MS);
        }
        if (sock != -1) {
            ESP_LOGI(TAG, "Shutting down TCP socket and restarting...");
            shutdown(sock, 0);
            close(sock);
        }
    }
    vTaskDelete(NULL);
}


void app_main(void)
{
    esp_err_t err;
    time_t now;
    struct tm timeinfo;
    char strftime_buf[64];
    wireguard_ctx_t ctx = {0};

    err = nvs_flash_init();
#if defined(CONFIG_IDF_TARGET_ESP8266) && ESP_IDF_VERSION <= ESP_IDF_VERSION_VAL(3, 4, 0)
    if (err == ESP_ERR_NVS_NO_FREE_PAGES) {
#else
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
#endif
      ESP_ERROR_CHECK(nvs_flash_erase());
      err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    err = wifi_init_sta();			//WiFi initialization
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "wifi_init_sta: %s", esp_err_to_name(err));
        goto fail;
    }

    obtain_time();
    time(&now);

    setenv("TZ", "EST5EDT,M3.2.0/2,M11.1.0", 1);
    tzset();
    localtime_r(&now, &timeinfo);
    strftime(strftime_buf, sizeof(strftime_buf), "%c", &timeinfo);
    ESP_LOGI(TAG, "The current date/time in Turin is: %s", strftime_buf);

    err = wireguard_setup(&ctx);			//function to setup wireguard
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "wireguard_setup: %s", esp_err_to_name(err));
        goto fail;
    }

    while (1) {
        vTaskDelay(1000 / portTICK_PERIOD_MS);
        err = esp_wireguardif_peer_is_up(&ctx);
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "Peer is up");
            break;
        } else {
            ESP_LOGI(TAG, "Peer is down");
        }
    }


    

    xTaskCreate(tcp_client_task, "tcp_client", 4096, NULL, 5, NULL);		//FreeRTOS creates the application task



fail:
    //ESP_LOGE(TAG, "Halting due to error");
    while (1) {
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}
