Utilização do FreeRTOS

Exemplo semáforo de Contagem com Display OLED.

Este programa exemplifica o uso de um semáforo de contagem (counting semaphore) no FreeRTOS, aplicado na placa BitDogLab. O sistema também utiliza um display OLED SSD1306 via I2C para exibir mensagens ao usuário.

Objetivo
Registrar e processar múltiplos eventos gerados pelo botão A (GPIO 5).
Cada vez que o botão é pressionado, o programa contabiliza o evento e atualiza o display com o total de eventos processados, mesmo que várias pressões ocorram em sequência rápida.

O Display OLED SSD1306: exibe mensagens e o número de eventos.

Semáforo de contagem: controla a fila de eventos aguardando processamento.

Tarefa única (vContadorTask): consome os eventos e atualiza o display.

Funcionamento do Programa:
O sistema inicializa e exibe: "Aguardando evento..." no display.
Quando o botão A (GPIO 5) é pressionado a ISR é acionada.
O semáforo de contagem é incrementado com xSemaphoreGiveFromISR().
O semáforo pode acumular vários eventos consecutivos (até o limite definido, nete caso 10).

A tarefa vContadorTask fica bloqueada em xSemaphoreTake(...) até que um evento esteja disponível.

Ao receber o semáforo, incrementa a variável eventosProcessados. Exibe no display Evento recebido! Eventos: N
Aguarda 1.5 segundos simulando tempo de processamento.

Retorna à mensagem "Aguardando evento...".

Neste exemplo, o semáforo de contagem captura todos os pulsos, inclusive os gerados por bounce mecânico do botão A. Isso evidencia o efeito do rebote e mostra a importância de implementar algum tipo de tratamento. 