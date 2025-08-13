// bibliotecas padrão da linguagem C
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

// bibliotecas do FreeRTOS para gerenciamento de tarefas
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// Inclusão dos drivers de hardware do ESP-IDF
#include "driver/gpio.h" // Para controle dos pinos de I/O
#include "driver/adc.h"  // Para o conversor analógico-digital
#include "esp_adc/adc_oneshot.h" // API mais recente para o ADC
#include "driver/spi_master.h" // Para comunicação SPI com o display

// Inclusão das APIs de sistema do ESP-IDF
#include "esp_log.h"
#include "esp_system.h"
#include "esp_wifi.h"       // Para funcionalidades Wi-Fi
#include "esp_event.h"      // Para o loop de eventos
#include "nvs_flash.h"      // Para armazenamento não-volátil (necessário para o Wi-Fi)
#include "esp_netif.h"      // Para a interface de rede

//  Inclusão de bibliotecas de aplicação
#include "mqtt_client.h"    // Para o cliente MQTT
#include "dht.h"            // Para o sensor de temperatura e umidade DHT11/22
#include "font8x8_basic.h"  // Arquivo com a definição da fonte 8x8 ASCII para o display

//  Configurações de Rede e MQTT
#define WIFI_SSID         "Nome da rede WIFI"                   // Nome da sua rede Wi-Fi
#define WIFI_PASS         "Senha da WIFI"               // Senha da sua rede Wi-Fi
#define MQTT_BROKER_URI   "mqtt://IP SERVIDOR BROKER:1883"     // Endereço do seu broker MQTT
#define MQTT_USER         "USUARIO"                   // Usuário do broker MQTT
#define MQTT_PASS         "SENHA"                   // Senha do broker MQTT
#define MQTT_TOPIC_DATA   "/ifpe/ads/embarcados/esp32/station/data" // Tópico para publicar os dados

//  Mapeamento de Pinos dos Sensores
#define DHT_PIN           GPIO_NUM_4                 // Pino para o sensor DHT11
#define LDR_ADC_CHANNEL   ADC_CHANNEL_4              // Canal ADC para o LDR (GPIO32)
#define CHUVA_ADC_CHANNEL ADC_CHANNEL_6              // Canal ADC para o sensor de chuva (GPIO34)
#define KY028_ADC_CHANNEL ADC_CHANNEL_7              // Canal ADC para o sensor KY-028 (GPIO35)

//  Configurações do Display LCD (ST7735S)
#define LCD_WIDTH         128                        // Largura do display em pixels
#define LCD_HEIGHT        160                        // Altura do display em pixels
#define PIN_NUM_MOSI      GPIO_NUM_23                // Pino SPI MOSI
#define PIN_NUM_CLK       GPIO_NUM_18                // Pino SPI Clock (SCLK)
#define PIN_NUM_CS        GPIO_NUM_5                 // Pino SPI Chip Select (CS)
#define PIN_NUM_DC        GPIO_NUM_2                 // Pino Data/Command (DC)
#define PIN_NUM_RST       GPIO_NUM_21                // Pino de Reset

//  Definições de Cores (formato RGB565)
#define COLOR_BLACK       0x0000
#define COLOR_WHITE       0xFFFF
#define COLOR_BLUE        0x001F

//  Variáveis Globais
static const char *TAG = "ESTACAO_DISPLAY";       // Tag para logs no monitor serial
static adc_oneshot_unit_handle_t g_adc1_handle; // Handle para a unidade ADC1
static esp_mqtt_client_handle_t client;         // Handle para o cliente MQTT
static spi_device_handle_t spi;                 // Handle para o dispositivo SPI (display)



//Seção de Funções para Controle do Display via SPI
// Envia um byte de comando para o display
void send_command(uint8_t cmd) {
    gpio_set_level(PIN_NUM_DC, 0); // Pino DC em nível baixo indica que um comando será enviado
    spi_transaction_t t = {.length = 8, .tx_buffer = &cmd};
    spi_device_polling_transmit(spi, &t);
}

// Envia um buffer de dados para o display
void send_data(const uint8_t *data, int len) {
    if (len == 0) return;
    gpio_set_level(PIN_NUM_DC, 1); // Pino DC em nível alto indica que dados serão enviados
    spi_transaction_t t = {.length = len * 8, .tx_buffer = data};
    spi_device_polling_transmit(spi, &t);
}

// Realiza o reset do display
void lcd_reset() {
    gpio_set_level(PIN_NUM_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(100));
    gpio_set_level(PIN_NUM_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(100));
}

// Inicializa o barramento SPI e o display LCD
void lcd_init() {
    ESP_LOGI(TAG, "Inicializando display ST7735S...");
    // Configura os pinos DC e RST como saída
    gpio_config_t io_conf = {.mode = GPIO_MODE_OUTPUT, .pin_bit_mask = (1ULL << PIN_NUM_DC) | (1ULL << PIN_NUM_RST)};
    gpio_config(&io_conf);

    // Configura o barramento SPI
    spi_bus_config_t buscfg = {
        .mosi_io_num = PIN_NUM_MOSI,
        .miso_io_num = -1,
        .sclk_io_num = PIN_NUM_CLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = LCD_WIDTH * LCD_HEIGHT * 2 + 8,
    };
    spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO);

    // Adiciona o display como um dispositivo no barramento SPI
    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = 26000000, 
        .mode = 0,                  
        .spics_io_num = PIN_NUM_CS,
        .queue_size = 7,
    };
    spi_bus_add_device(SPI2_HOST, &devcfg, &spi);

    // Sequência de inicialização do display ST7735S
    lcd_reset();
    send_command(0x01); vTaskDelay(pdMS_TO_TICKS(150)); // Software reset
    send_command(0x11); vTaskDelay(pdMS_TO_TICKS(255)); // Sai do modo de suspensão
    send_command(0x3A); send_data((const uint8_t[]){0x05}, 1); // Define formato de cor para 16-bit (RGB565)
    send_command(0x29); // Liga o display
}

// Define a "janela" (área) da tela onde os dados de pixel serão escritos
void set_address_window(uint8_t x0, uint8_t y0, uint8_t x1, uint8_t y1) {
    send_command(0x2A); // Column Address Set
    uint8_t data[] = {0x00, x0, 0x00, x1};
    send_data(data, 4);

    send_command(0x2B); // Page Address Set
    uint8_t data2[] = {0x00, y0, 0x00, y1};
    send_data(data2, 4);

    send_command(0x2C); // Memory Write (prepara para receber os dados dos pixels)
}

// Desenha um único caractere na tela
void draw_char(uint8_t x, uint8_t y, char c, uint16_t color) {
    if (c < 32 || c > 127) c = '?'; // Caractere padrão para fora do range ASCII
    set_address_window(x, y, x + 7, y + 7); // Define a janela de 8x8 pixels
    uint8_t pixels[8];
    memcpy(pixels, font8x8_basic[(int)c], 8); // Copia o bitmap do caractere da fonte

    // Itera por cada pixel do caractere 8x8
    for (int row = 0; row < 8; row++) {
        for (int col = 0; col < 8; col++) {
            uint8_t bit = pixels[row] & (1 << col); // Verifica se o bit do pixel está ativo
            // Converte a cor de 16 bits para 2 bytes (big-endian)
            uint8_t data[] = {
                bit ? (color >> 8) : 0x00,   // Byte mais significativo
                bit ? (color & 0xFF) : 0x00  // Byte menos significativo
            };
            send_data(data, 2); // Envia a cor do pixel
        }
    }
}

// Desenha uma string (texto) na tela
void draw_text(uint8_t x, uint8_t y, const char *text, uint16_t color) {
    while (*text) { // Loop até o fim da string
        draw_char(x, y, *text++, color);
        x += 8; // Avança 8 pixels para o próximo caractere
        // Quebra de linha automática
        if (x > LCD_WIDTH - 8) {
            x = 0;
            y += 8;
        }
    }
}

// Preenche a tela inteira com uma cor sólida
void fill_screen(uint16_t color) {
    set_address_window(0, 0, LCD_WIDTH - 1, LCD_HEIGHT - 1);
    uint8_t color_data[2] = {color >> 8, color & 0xFF}; // Cor em 2 bytes
    uint8_t line[LCD_WIDTH * 2]; // Buffer para uma linha de pixels
    // Cria um buffer com a cor repetida para uma linha inteira
    for (int i = 0; i < LCD_WIDTH; i++) {
        line[i * 2] = color_data[0];
        line[i * 2 + 1] = color_data[1];
    }
    // Envia a linha repetidamente para preencher a tela
    for (int y = 0; y < LCD_HEIGHT; y++) {
        send_data(line, sizeof(line));
    }
}

// Formata e exibe os dados dos sensores no display
void display_data(float temperatura, float umidade, int chuva, double ky028, int luminosidade) {
    char buffer[64]; // Buffer para formatar as strings
    fill_screen(COLOR_BLUE); // Limpa a tela com a cor azul

    // Exibe Temperatura
    sprintf(buffer, "Temperatura:");
    draw_text(10, 10, buffer, COLOR_WHITE);
    sprintf(buffer, "%.1f C", temperatura);
    draw_text(10, 20, buffer, COLOR_WHITE);

    // Exibe Umidade
    sprintf(buffer, "Umidade:");
    draw_text(10, 40, buffer, COLOR_WHITE);
    sprintf(buffer, "%.1f %%", umidade);
    draw_text(10, 50, buffer, COLOR_WHITE);
    
    // Exibe KY-028
    sprintf(buffer, "KY-028:");
    draw_text(10, 70, buffer, COLOR_WHITE);
    sprintf(buffer, "%.0f", ky028); // Exibe o valor bruto do ADC
    draw_text(10, 80, buffer, COLOR_WHITE);
    
    // Exibe Luminosidade
    sprintf(buffer, "Luminosidade:");
    draw_text(10, 100, buffer, COLOR_WHITE);
    sprintf(buffer, "%d %%", luminosidade);
    draw_text(10, 110, buffer, COLOR_WHITE);

    // Exibe Chuva
    sprintf(buffer, "Chuva:");
    draw_text(10, 130, buffer, COLOR_WHITE);
    sprintf(buffer, "%d %%", chuva);
    draw_text(10, 140, buffer, COLOR_WHITE);

    // Imprime os mesmos dados no log para depuração
    ESP_LOGI(TAG, "Temperatura:%.1f | Umidade:%.1f | Chuva:%d%% | KY028:%.0f | luminosidade:%d%%",
             temperatura, umidade, chuva, ky028, luminosidade);
}



// Seção de Funções de Conectividade (Wi-Fi e MQTT)   


// Manipulador de eventos para o cliente MQTT
static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data) {
    esp_mqtt_event_handle_t event = event_data;
    switch ((esp_mqtt_event_id_t)event_id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "MQTT conectado!");
            break;
        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGW(TAG, "MQTT desconectado!");
            break;
        case MQTT_EVENT_ERROR:
            ESP_LOGE(TAG, "Erro no MQTT!");
            break;
        default:
            break;
    }
}

// Configura e inicia o cliente MQTT
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
}

// Manipulador de eventos para o Wi-Fi
static void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect(); // Tenta conectar ao AP quando o Wi-Fi inicia
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        esp_wifi_connect(); // Tenta reconectar se a conexão for perdida
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ESP_LOGI(TAG, "Conectado ao Wi-Fi! Endereço IP obtido.");
        mqtt_app_start(); // Inicia o MQTT somente após obter um IP
    }
}


//  Seção de Configuração dos Sensores 


// Configura a unidade e os canais do ADC
void setup_adc() {
    // Configura a unidade ADC1
    adc_oneshot_unit_init_cfg_t init_config1 = {.unit_id = ADC_UNIT_1};
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_config1, &g_adc1_handle));

    // Configura cada canal do ADC
    adc_oneshot_chan_cfg_t config = {
        .bitwidth = ADC_BITWIDTH_DEFAULT, // Resolução padrão (12 bits)
        .atten = ADC_ATTEN_DB_12          // Atenuação para ler até ~3.3V
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(g_adc1_handle, LDR_ADC_CHANNEL, &config));
    ESP_ERROR_CHECK(adc_oneshot_config_channel(g_adc1_handle, CHUVA_ADC_CHANNEL, &config));
    ESP_ERROR_CHECK(adc_oneshot_config_channel(g_adc1_handle, KY028_ADC_CHANNEL, &config));
}



//  Tarefa Principal da Estação Meteorológica


void station_task(void *pvParameters) {
    while (1) { // Loop infinito da tarefa
        float temperatura = 0, umidade = 0;
        int luminosidade_raw = 0, chuva_raw = 0, ky028_raw = 0;

        // Lê o sensor de temperatura e umidade DHT11
        if (dht_read_float_data(DHT_TYPE_DHT11, DHT_PIN, &umidade, &temperatura) != ESP_OK) {
            ESP_LOGE(TAG, "Falha ao ler o sensor DHT!");
            temperatura = -1; umidade = -1; // Valores de erro
        }

        // Lê os sensores analógicos
        adc_oneshot_read(g_adc1_handle, LDR_ADC_CHANNEL, &luminosidade_raw);
        adc_oneshot_read(g_adc1_handle, CHUVA_ADC_CHANNEL, &chuva_raw);
        adc_oneshot_read(g_adc1_handle, KY028_ADC_CHANNEL, &ky028_raw);

        // Converte os valores brutos dos sensores de LDR e chuva para porcentagem.
        // A lógica é invertida porque um valor ADC maior significa menos luz/chuva.
        int ldr_percent = (int)(((4095.0 - luminosidade_raw) / 4095.0) * 100);
        int chuva_percent = (int)(((4095.0 - chuva_raw) / 4095.0) * 100);

        // Monta a string JSON com os dados dos sensores
        char payload[256];
        snprintf(payload, sizeof(payload),
                 "{\"temperatura\":%.1f,\"umidade\":%.1f,\"chuva\":%d,\"ky028\":%d,\"luminosidade\":%d}",
                 temperatura, umidade, chuva_percent, ky028_raw, ldr_percent);

        // Publica os dados no tópico MQTT se o cliente estiver conectado
        if (client) {
            esp_mqtt_client_publish(client, MQTT_TOPIC_DATA, payload, 0, 1, 0);
        }

        // Atualiza os dados no display
        display_data(temperatura, umidade, chuva_percent, ky028_raw, ldr_percent);

        // Aguarda 5 segundos antes da próxima leitura
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}


// Função Principal (Ponto de Entrada da Aplicação)


void app_main(void) {
    // 1. Inicializa o NVS (Non-Volatile Storage) - necessário para o Wi-Fi
    ESP_ERROR_CHECK(nvs_flash_init());
    
    // 2. Inicializa a pilha de rede TCP/IP
    ESP_ERROR_CHECK(esp_netif_init());
    
    // 3. Cria o loop de eventos padrão
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // 4. Configuração e inicialização do Wi-Fi em modo Station (cliente)
    esp_netif_create_default_wifi_sta();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    
    // 5. Registra os manipuladores de eventos para Wi-Fi e IP
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL));

    // 6. Define as credenciais e configurações do Wi-Fi
    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASS,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    
    // 7. Inicia o Wi-Fi
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_LOGI(TAG, "Wi-Fi inicializado. Aguardando conexão...");

    // 8. Inicializa os periféricos
    lcd_init();      // Inicializa o display
    fill_screen(COLOR_BLACK); // Limpa a tela
    setup_adc();     // Inicializa o ADC

    // 9. Cria e inicia a tarefa principal da estação
    xTaskCreate(station_task, "station_task", 4096, NULL, 5, NULL);
}
