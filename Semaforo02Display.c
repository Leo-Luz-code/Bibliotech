#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/i2c.h"
#include "lib/ssd1306.h"
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "pico/bootrom.h"
#include "stdio.h"

// --- Definições de Hardware ---
#define I2C_PORT i2c1
#define I2C_SDA 14
#define I2C_SCL 15
#define OLED_ADDR 0x3C

#define BOTAO_ENTRADA 5 // Botão A (GPIO5)
#define BOTAO_SAIDA 6   // Botão B (GPIO6)
#define BOTAO_RESET 22  // Botão do Joystick (GPIO22)

#define LED_VERDE 11
#define LED_AZUL 12
#define LED_VERMELHO 13
#define BUZZER 21

#define MAX_USUARIOS 8 // Capacidade máxima

// --- Variáveis Globais ---
ssd1306_t ssd;
SemaphoreHandle_t xSemContador;  // Semáforo de contagem
SemaphoreHandle_t xSemReset;     // Semáforo binário (reset)
SemaphoreHandle_t xSemEntrada;   // Semáforo para entrada
SemaphoreHandle_t xSemSaida;     // Semáforo para saída
SemaphoreHandle_t xMutexDisplay; // Mutex para o OLED

static volatile uint32_t current_time; // Tempo atual (usado para debounce)
static volatile uint32_t last_time_button = 0;

// --- Protótipos de Funções ---
void vTaskEntrada(void *pvParameters);
void vTaskSaida(void *pvParameters);
void vTaskReset(void *pvParameters);
void atualizarLED();
void beep(uint16_t duracao_ms);
void gpio_callback(uint gpio, uint32_t events);

// --- Inicialização do Hardware ---
void initHardware()
{
    // Configura I2C e OLED
    i2c_init(I2C_PORT, 400 * 1000);
    gpio_set_function(I2C_SDA, GPIO_FUNC_I2C);
    gpio_set_function(I2C_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(I2C_SDA);
    gpio_pull_up(I2C_SCL);
    ssd1306_init(&ssd, WIDTH, HEIGHT, false, OLED_ADDR, I2C_PORT);
    ssd1306_config(&ssd);
    ssd1306_send_data(&ssd);

    // Configura botões (pull-up)
    gpio_init(BOTAO_ENTRADA);
    gpio_init(BOTAO_SAIDA);
    gpio_init(BOTAO_RESET);
    gpio_set_dir(BOTAO_ENTRADA, GPIO_IN);
    gpio_set_dir(BOTAO_SAIDA, GPIO_IN);
    gpio_set_dir(BOTAO_RESET, GPIO_IN);
    gpio_pull_up(BOTAO_ENTRADA);
    gpio_pull_up(BOTAO_SAIDA);
    gpio_pull_up(BOTAO_RESET);

    // Configura interrupções
    gpio_set_irq_enabled_with_callback(BOTAO_RESET, GPIO_IRQ_EDGE_FALL, true, &gpio_callback);
    gpio_set_irq_enabled(BOTAO_ENTRADA, GPIO_IRQ_EDGE_FALL, true);
    gpio_set_irq_enabled(BOTAO_SAIDA, GPIO_IRQ_EDGE_FALL, true);

    // Configura LED RGB (saída)
    gpio_init(LED_VERDE);
    gpio_init(LED_AZUL);
    gpio_init(LED_VERMELHO);
    gpio_set_dir(LED_VERDE, GPIO_OUT);
    gpio_set_dir(LED_AZUL, GPIO_OUT);
    gpio_set_dir(LED_VERMELHO, GPIO_OUT);

    // Configura Buzzer
    gpio_init(BUZZER);
    gpio_set_dir(BUZZER, GPIO_OUT);
}

// --- Tarefa de Entrada (Botão A) ---
void vTaskEntrada(void *pvParameters)
{
    while (1)
    {
        if (xSemaphoreTake(xSemEntrada, portMAX_DELAY))
        {
            if (uxSemaphoreGetCount(xSemContador) < MAX_USUARIOS)
            {
                xSemaphoreGive(xSemContador);

                // Atualiza display (protegido por Mutex)
                if (xSemaphoreTake(xMutexDisplay, portMAX_DELAY) == pdTRUE)
                {
                    ssd1306_fill(&ssd, false);
                    ssd1306_draw_string(&ssd, "Entrada OK!", 5, 20);
                    ssd1306_draw_string(&ssd, "Usuarios:", 5, 40);
                    char buffer[10];
                    sprintf(buffer, "%d/%d", uxSemaphoreGetCount(xSemContador), MAX_USUARIOS);
                    ssd1306_draw_string(&ssd, buffer, 80, 40);
                    ssd1306_send_data(&ssd);
                    xSemaphoreGive(xMutexDisplay); // LIBERA o mutex
                }

                atualizarLED();
            }
            else
            {
                // Sistema cheio - beep curto
                beep(200);
            }
        }
    }
    vTaskDelay(pdMS_TO_TICKS(100));
}

// --- Tarefa de Saída (Botão B) ---
void vTaskSaida(void *pvParameters)
{
    while (1)
    {
        if (xSemaphoreTake(xSemSaida, portMAX_DELAY))
        {
            if (xSemaphoreTake(xSemContador, portMAX_DELAY))
            {
                // Atualiza display
                if (xSemaphoreTake(xMutexDisplay, portMAX_DELAY) == pdTRUE)
                {
                    ssd1306_fill(&ssd, false);
                    ssd1306_draw_string(&ssd, "Saida OK!", 5, 20);
                    ssd1306_draw_string(&ssd, "Usuarios:", 5, 40);
                    char buffer[10];
                    sprintf(buffer, "%d/%d", uxSemaphoreGetCount(xSemContador), MAX_USUARIOS);
                    ssd1306_draw_string(&ssd, buffer, 80, 40);
                    ssd1306_send_data(&ssd);
                    xSemaphoreGive(xMutexDisplay); // LIBERA o mutex
                }

                atualizarLED();
            }
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

// --- Tarefa de Reset (Joystick) ---
void vTaskReset(void *pvParameters)
{
    while (1)
    {
        if (xSemaphoreTake(xSemReset, portMAX_DELAY))
        {
            // Zera a contagem
            xSemContador = xSemaphoreCreateCounting(MAX_USUARIOS, 0);

            // Beep duplo
            beep(100);
            vTaskDelay(pdMS_TO_TICKS(150));
            beep(100);

            // Atualiza display
            if (xSemaphoreTake(xMutexDisplay, portMAX_DELAY) == pdTRUE)
            {
                ssd1306_fill(&ssd, false);
                ssd1306_draw_string(&ssd, "Resetado!", 5, 20);
                ssd1306_draw_string(&ssd, "Usuarios: ", 5, 40);
                char buffer[10];
                sprintf(buffer, "%d/%d", uxSemaphoreGetCount(xSemContador), MAX_USUARIOS);
                ssd1306_draw_string(&ssd, buffer, 80, 40);
                ssd1306_send_data(&ssd);
                xSemaphoreGive(xMutexDisplay); // LIBERA o mutex
            }

            atualizarLED();
        }
    }
}

// --- Atualiza LED RGB conforme ocupação ---
void atualizarLED()
{
    gpio_put(LED_VERDE, 0);
    gpio_put(LED_AZUL, 0);
    gpio_put(LED_VERMELHO, 0);

    if (uxSemaphoreGetCount(xSemContador) == 0)
    {
        gpio_put(LED_AZUL, 1); // Azul (0 usuários)
    }
    else if (uxSemaphoreGetCount(xSemContador) <= MAX_USUARIOS - 2)
    {
        gpio_put(LED_VERDE, 1); // Verde (1 a MAX-2)
    }
    else if (uxSemaphoreGetCount(xSemContador) == MAX_USUARIOS - 1)
    {
        gpio_put(LED_VERDE, 1);    // Amarelo (MAX-1)
        gpio_put(LED_VERMELHO, 1); // (Verde + Vermelho = Amarelo)
    }
    else
    {
        gpio_put(LED_VERMELHO, 1); // Vermelho (MAX)
    }
}

// --- Gera beep no buzzer ---
void beep(uint16_t duracao_ms)
{
    gpio_put(BUZZER, 1);
    vTaskDelay(pdMS_TO_TICKS(duracao_ms));
    gpio_put(BUZZER, 0);
}

// --- ISR para os botões ---
void gpio_callback(uint gpio, uint32_t events)
{
    current_time = to_us_since_boot(get_absolute_time());

    if (current_time - last_time_button > 200000)
    {
        BaseType_t xHigherPriorityTaskWoken = pdFALSE;

        last_time_button = current_time;

        if (gpio == BOTAO_RESET)
        {
            xSemaphoreGiveFromISR(xSemReset, &xHigherPriorityTaskWoken);
        }

        else if (gpio == BOTAO_ENTRADA)
        {
            xSemaphoreGiveFromISR(xSemEntrada, &xHigherPriorityTaskWoken);
        }
        else if (gpio == BOTAO_SAIDA)
        {
            xSemaphoreGiveFromISR(xSemSaida, &xHigherPriorityTaskWoken);
        }

        portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
    }
}

// --- Main (Inicialização do FreeRTOS) ---
int main()
{
    stdio_init_all();
    initHardware();

    // Cria semáforos e mutex
    xSemContador = xSemaphoreCreateCounting(MAX_USUARIOS, 0);
    xSemReset = xSemaphoreCreateBinary();
    xSemEntrada = xSemaphoreCreateBinary();
    xSemSaida = xSemaphoreCreateBinary();
    xMutexDisplay = xSemaphoreCreateMutex();

    // Tela inicial
    ssd1306_fill(&ssd, 0);
    ssd1306_draw_string(&ssd, "Aguardando       evento...", 5, 25);
    ssd1306_send_data(&ssd);

    // Cria tarefas
    xTaskCreate(vTaskEntrada, "Entrada", 256, NULL, 1, NULL);
    xTaskCreate(vTaskSaida, "Saida", 256, NULL, 1, NULL);
    xTaskCreate(vTaskReset, "Reset", 256, NULL, 2, NULL); // Prioridade maior para reset

    // Inicia o escalonador
    vTaskStartScheduler();
    panic_unsupported();

    while (1)
    {
    } // Nunca deve chegar aqui
}