#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/i2c.h"
#include "hardware/pwm.h"
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

#define MAX_USUARIOS 10 // Capacidade máxima

#define BUZZER 21
uint32_t last_buzzer_time = 0;
uint32_t buzzer_interval = 250;       // Intervalo para desligar o buzzer
uint32_t buzzer_interval_short = 100; // Intervalo curto para beep
bool buzzer_state = false;            // Estado atual do buzzer
const float DIVIDER_PWM = 16.0;       // Divisor de clock para PWM
const uint16_t PERIOD = 4096;         // Período do PWM
uint slice_buzzer;                    // Slice do PWM para o buzzer

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
void buzzer_on();
void buzzer_off();

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
    gpio_set_function(BUZZER, GPIO_FUNC_PWM);
    slice_buzzer = pwm_gpio_to_slice_num(BUZZER);
    pwm_set_clkdiv(slice_buzzer, DIVIDER_PWM);
    pwm_set_wrap(slice_buzzer, PERIOD);
    pwm_set_gpio_level(BUZZER, 0);
    pwm_set_enabled(slice_buzzer, true);
}

void buzzer_on()
{
    pwm_set_gpio_level(BUZZER, 300);
    buzzer_state = true;
    last_buzzer_time = time_us_64();
}

void buzzer_off()
{
    pwm_set_gpio_level(BUZZER, 0);
    buzzer_state = false;
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
                    ssd1306_draw_string(&ssd, "Biblioteca", 12, 5);
                    ssd1306_line(&ssd, 0, 13, 128, 13, true);
                    ssd1306_draw_string(&ssd, "Entrada OK!", 10, 20);
                    ssd1306_draw_string(&ssd, "Usuarios: ", 10, 40);
                    char buffer[10];
                    sprintf(buffer, "%d/%d", uxSemaphoreGetCount(xSemContador), MAX_USUARIOS);
                    ssd1306_draw_string(&ssd, buffer, 82, 40);
                    ssd1306_draw_string(&ssd, "Bem-vindo(a)!", 10, 55);
                    ssd1306_send_data(&ssd);
                    xSemaphoreGive(xMutexDisplay); // LIBERA o mutex
                }

                atualizarLED();
            }
            else
            {
                // Sistema cheio - beep curto
                beep(buzzer_interval_short);

                // Atualiza display (protegido por Mutex)
                if (xSemaphoreTake(xMutexDisplay, portMAX_DELAY) == pdTRUE)
                {
                    ssd1306_fill(&ssd, false);
                    ssd1306_draw_string(&ssd, "Biblioteca", 12, 5);
                    ssd1306_line(&ssd, 0, 13, 128, 13, true);
                    ssd1306_draw_string(&ssd, "Esta cheio!", 10, 20);
                    ssd1306_draw_string(&ssd, "Usuarios: ", 10, 40);
                    char buffer[10];
                    sprintf(buffer, "%d/%d", uxSemaphoreGetCount(xSemContador), MAX_USUARIOS);
                    ssd1306_draw_string(&ssd, buffer, 82, 40);
                    ssd1306_draw_string(&ssd, "Aguarde saidas", 10, 55);
                    ssd1306_send_data(&ssd);
                    xSemaphoreGive(xMutexDisplay); // LIBERA o mutex
                }
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
            if (uxSemaphoreGetCount(xSemContador) > 0)
            {
                if (xSemaphoreTake(xSemContador, portMAX_DELAY))
                {
                    // Atualiza display
                    if (xSemaphoreTake(xMutexDisplay, portMAX_DELAY) == pdTRUE)
                    {
                        ssd1306_fill(&ssd, false);
                        ssd1306_draw_string(&ssd, "Biblioteca", 12, 5);
                        ssd1306_line(&ssd, 0, 13, 128, 13, true);
                        ssd1306_draw_string(&ssd, "Saida OK!", 10, 20);
                        ssd1306_draw_string(&ssd, "Usuarios: ", 10, 40);
                        char buffer[10];
                        sprintf(buffer, "%d/%d", uxSemaphoreGetCount(xSemContador), MAX_USUARIOS);
                        ssd1306_draw_string(&ssd, buffer, 82, 40);
                        ssd1306_draw_string(&ssd, "Volte sempre!", 10, 55);
                        ssd1306_send_data(&ssd);
                        xSemaphoreGive(xMutexDisplay); // LIBERA o mutex
                    }

                    atualizarLED();
                }
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
            // Zera o semáforo de forma atômica e sem bloqueio
            while (xSemaphoreTake(xSemContador, 0) == pdTRUE)
            {
                // Continua retirando até que não haja mais usuários
            }

            // Beep duplo
            beep(buzzer_interval);
            vTaskDelay(pdMS_TO_TICKS(150));
            beep(buzzer_interval);

            // Atualiza display
            if (xSemaphoreTake(xMutexDisplay, portMAX_DELAY) == pdTRUE)
            {
                ssd1306_fill(&ssd, false);
                ssd1306_draw_string(&ssd, "Biblioteca", 12, 5);
                ssd1306_line(&ssd, 0, 13, 128, 13, true);
                ssd1306_draw_string(&ssd, "RESET", 10, 20);
                ssd1306_draw_string(&ssd, "Usuarios: ", 10, 40);
                char buffer[10];
                sprintf(buffer, "%d/%d", uxSemaphoreGetCount(xSemContador), MAX_USUARIOS);
                ssd1306_draw_string(&ssd, buffer, 82, 40);
                ssd1306_draw_string(&ssd, "Aguardando...", 10, 55);
                ssd1306_send_data(&ssd);
                xSemaphoreGive(xMutexDisplay); // LIBERA o mutex
            }

            atualizarLED();
        }
        vTaskDelay(pdMS_TO_TICKS(100));
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
    buzzer_on();
    vTaskDelay(pdMS_TO_TICKS(duracao_ms)); // Bloqueia a tarefa pelo tempo do beep
    buzzer_off();
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
    ssd1306_draw_string(&ssd, "Biblioteca", 12, 5);
    ssd1306_line(&ssd, 0, 13, 128, 13, true);
    ssd1306_draw_string(&ssd, "Aguardando       pessoas...", 10, 20);
    ssd1306_draw_string(&ssd, "BotaoA+ BotaoB-", 5, 45);
    ssd1306_draw_string(&ssd, "BotaoJoy-RESET", 5, 55);
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