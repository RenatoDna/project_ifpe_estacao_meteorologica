// blibliotecas nescessarias
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h" 
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "mqtt_client.h" 
#include "driver/gpio.h"
#include "dht.h" 

// Definições de conexão: Pino, Wi-Fi e MQTT
#define PINO_LED_INTERNO 2             // Led esp32
#define WIFI_SSID          "NOME WIFI"    
#define WIFI_PASS          "SENHA WIFI"
#define MQTT_USER          "USUARIO MQTT"    
#define MQTT_PASS          "SENHA MQTT"
#define MQTT_BROKER_URI    "mqtt://IP SERVIDOR MQTT"
#define MQTT_TOPIC_COMANDO "/ifpe/ads/embarcados/esp32/led"

// --- CONFIGURAÇÃO DO DHT11 ---
#define DHT_GPIO 4
// ---------------------------

static const char *TAG = "PROJETO_LED";
static int s_retry_num = 0;

// --- Variáveis Globais para armazenar os dados do sensor ---
// Usamos um "mutex" para proteger o acesso a estas variáveis,
// já que elas serão usadas por duas tarefas diferentes.
static float temperature = -100.0;
static float humidity = -100.0;
static SemaphoreHandle_t data_mutex;
// Handle do cliente MQTT
esp_mqtt_client_handle_t client;
// =================================================================
// TAREFA DO SENSOR DHT11
// =================================================================
// Esta tarefa roda em segundo plano, lendo o sensor a cada 3 segundos.
void dht_task(void *pvParameters)
{
    ESP_LOGI(TAG, "Iniciando a tarefa do DHT11...");
    while (1)
    {
        float temp_local, hum_local;
        if (dht_read_float_data(DHT_TYPE_DHT11, DHT_GPIO, &hum_local, &temp_local) == ESP_OK)
        {
            // Se a leitura for bem-sucedida, atualizamos as variáveis globais.
            // Usamos o mutex para garantir que a atualização seja segura.
            if (xSemaphoreTake(data_mutex, portMAX_DELAY) == pdTRUE)
            {
                temperature = temp_local;
                humidity = hum_local;
                xSemaphoreGive(data_mutex);
                ESP_LOGI(TAG, "Leitura do DHT11: Umidade=%.1f%%, Temperatura=%.1f°C", hum_local, temp_local);
                char payload[64];
                snprintf(payload, sizeof(payload), "{\"temperatura\":%.1f,\"umidade\":%.1f}", temp_local, hum_local);
                esp_mqtt_client_publish(client, "/ifpe/ads/embarcados/esp32/dht11", payload, 0, 1, 0);
            }
        }
        else
        {
            ESP_LOGE(TAG, "Falha ao ler o sensor DHT11");
        }

        // Aguarda 3 segundos para a próxima leitura
        vTaskDelay(pdMS_TO_TICKS(3000));
    }
}
// Função para lidar com eventos do MQTT
static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data) {
    esp_mqtt_event_handle_t event = event_data;

    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "MQTT conectado com sucesso!");
        esp_mqtt_client_subscribe(client, MQTT_TOPIC_COMANDO, 0); 
        break;

    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGW(TAG, "MQTT desconectado!");
        break;

    case MQTT_EVENT_DATA:
        ESP_LOGI(TAG, "Mensagem MQTT recebida");
        //**
        // Verifica mensagem recebida e o desejado
        if (strncmp(event->topic, MQTT_TOPIC_COMANDO, event->topic_len) == 0) {
            // Verifica o conteúdo da mensagem
            if (strncmp(event->data, "1", event->data_len) == 0) {
                gpio_set_level(PINO_LED_INTERNO, 1); // Liga o LED
                ESP_LOGI(TAG, "ON: LED Ligado!");
                
                static bool dht_task_started = false;
                if(!dht_task_started){
                    xTaskCreate(dht_task, "dht_task",4096,NULL,5,NULL);
                    dht_task_started = true;
                }

            } else if (strncmp(event->data, "0", event->data_len) == 0) {
                gpio_set_level(PINO_LED_INTERNO, 0); // Desliga o LED
                ESP_LOGI(TAG, "OFF: LED Desligado!");
            }
        }
        break;

    case MQTT_EVENT_ERROR:
        ESP_LOGE(TAG, "Erro no MQTT!");
        break;

    default:
        break;
    }
}

// inicialização e configuração do cliente MQTT
static void mqtt_app_start(void) {
    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = MQTT_BROKER_URI,
        .credentials = {
            .username = MQTT_USER,
            .authentication.password = MQTT_PASS,
        },
    };

    client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL); 
    esp_mqtt_client_start(client); 
    ESP_LOGI(TAG, "Cliente MQTT iniciado.");
}

//  Wi-Fi/IP (conectando, desconectando, IP obtido)
static void event_handler_wifi(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect(); 
        ESP_LOGI(TAG, "Tentando conectar ao Wi-Fi...");
    } 
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGW(TAG, "Wi-Fi desconectado. Tentando reconectar...");
        esp_wifi_connect(); 
    } 
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "Wi-Fi conectado!");
        ESP_LOGI(TAG, "Endereço IP obtido: " IPSTR, IP2STR(&event->ip_info.ip));
        // Inicia o cliente MQTT após conectar na rede e obter ip 
        mqtt_app_start(); 
    }
}

// Função principal
void app_main(void) {
    ESP_LOGI(TAG, "Iniciando aplicativo...");

    // Inicializa o NVS (armazenamento necessário para Wi-Fi)
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase()); // Apaga e reinicia se necessário
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Configura o pino do LED como saída
    gpio_reset_pin(PINO_LED_INTERNO);
    gpio_set_direction(PINO_LED_INTERNO, GPIO_MODE_OUTPUT);

    // Cria o Mutex para proteger o acesso aos dados
    data_mutex = xSemaphoreCreateMutex();

    // Inicializa a pilha de rede e cria loop de eventos
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    // Inicializa o driver Wi-Fi com configuração padrão
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    // Registra o manipulador de eventos de Wi-Fi e IP
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler_wifi, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler_wifi, NULL));

    // Configura credenciais da rede Wi-Fi
    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASS,
        },
    };

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA)); 
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start()); 

    ESP_LOGI(TAG, " Aguardando conexão...");
}
