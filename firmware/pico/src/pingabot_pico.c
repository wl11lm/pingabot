#include <rcl/rcl.h>
#include <rcl/error_handling.h>
#include <rclc/rclc.h>
#include <rclc/executor.h>
#include <geometry_msgs/msg/twist.h>
#include <std_msgs/msg/int32.h>
#include <std_msgs/msg/empty.h>
#include <rmw_microros/rmw_microros.h>
#include "pico/stdlib.h"
#include "hardware/pwm.h"
#include "hardware/adc.h"
#include "pico_uart_transports.h"
#include <math.h>

// ---------------- Pinout ----------------
#define M1_PWM 21
#define M1_IN1 19
#define M1_IN2 20
#define M2_PWM  6
#define M2_IN1  7
#define M2_IN2  8
#define M3_PWM 13
#define M3_IN1 14
#define M3_IN2 15
#define M4_PWM 18
#define M4_IN1 16
#define M4_IN2 17
#define LED_PIN 25

// Relé da bomba de dosagem
// Se o seu módulo relé for ativo em NIVEL BAIXO (a maioria dos módulos
// de 1 canal baratos são), inverta os dois gpio_put(PUMP_PIN, ...) abaixo.
#define PUMP_PIN 9

// Buzzer — apita 1x por 1s quando alguém levanta a mão (evento vindo
// do script da webcam na RPi, via /pedido_dose). Não é PWM, é on/off simples.
#define BUZZER_PIN 27
#define BUZZER_DURATION_MS 1000

// Sensor de efeito Hall KY-003
// Medido na bancada: ~1.5V parado (sem copo) e ~0V com o copo/ímã presente.
// 1.5V cai bem em cima da zona indefinida do Schmitt trigger digital do
// RP2040 (limiar ~0.8V-1.9V), então lemos por ADC em vez de gpio_get().
// GPIO26 = ADC0 (única entrada ADC usada aqui).
#define KY003_PIN        26
#define KY003_ADC_INPUT  0     // ADC0 -> GPIO26
#define KY003_DEBOUNCE_MS 30

// Limiar de tensão para considerar "detectado": entre 0V (detectado) e
// 1.5V (repouso). Ajuste conforme sua medição real na bancada.
#define KY003_THRESHOLD_V 0.75f

const uint PWM_RANGE = 255;

// Timeout de segurança: se não chegar nenhum /cmd_vel neste intervalo,
// os motores param sozinhos (evita robô "fugindo" se o USB cair)
#define CMD_TIMEOUT_MS 500

// Duração da dosagem disparada pelo KY-003 (diferente da dosagem manual
// via /bomba_dose, que pode ter duração arbitrária)
#define KY003_DOSE_MS 2000

// Considera o robô "parado" se todas as componentes do último Twist
// recebido estiverem abaixo desse limiar
#define ZERO_VEL_EPS 0.02f

static volatile uint32_t last_cmd_time = 0;

// Estado não-bloqueante da bomba
static volatile bool     pump_active   = false;
static volatile uint32_t pump_end_time = 0;

// Estado não-bloqueante do buzzer
static volatile bool     buzzer_active   = false;
static volatile uint32_t buzzer_end_time = 0;

// Estado de movimento do robô, atualizado a cada /cmd_vel recebido
static volatile bool robot_moving = false;

// Bloqueia o processamento de /cmd_vel enquanto a dosagem automática
// (via KY-003) estiver em andamento
static volatile bool cmd_vel_blocked = false;

// ---------------- Máquina de estados do dosador automático ----------------
typedef enum {
    DOSER_IDLE = 0,      // esperando detectar o ímã
    DOSER_ACTIVE,        // bomba ligada, dosando
    DOSER_WAIT_CLEAR      // bomba já desligou, esperando o ímã sair de perto
} doser_state_t;

static volatile doser_state_t doser_state = DOSER_IDLE;

// Leitura "crua" e leitura "estável" (após debounce) do KY-003.
// true = ímã detectado.
static bool ky003_raw_state      = false;
static bool ky003_stable_state   = false;
static uint32_t ky003_last_change_time = 0;

// ---------------- Macros de checagem de erro ----------------
// Em vez de falhar silenciosamente, pisca o LED em padrão de erro e trava.
#define RCCHECK(fn) { \
    rcl_ret_t temp_rc = fn; \
    if ((temp_rc != RCL_RET_OK)) { \
        error_loop(); \
    } \
}
#define RCSOFTCHECK(fn) { \
    rcl_ret_t temp_rc = fn; \
    if ((temp_rc != RCL_RET_OK)) { \
        /* erro não fatal: apenas loga via blink rápido, mas segue */ \
    } \
}

static void error_loop(void) {
    while (true) {
        gpio_put(LED_PIN, 1);
        sleep_ms(100);
        gpio_put(LED_PIN, 0);
        sleep_ms(100);
    }
}

// ---------------- PWM / Motores ----------------
static void setup_pwm(uint pin) {
    gpio_init(pin);
    gpio_set_function(pin, GPIO_FUNC_PWM);
    uint slice = pwm_gpio_to_slice_num(pin);
    pwm_set_wrap(slice, PWM_RANGE);
    pwm_set_enabled(slice, true);
}

static void set_motor(uint pwm_pin, uint in1, uint in2, float speed) {
    if (speed >  1.0f) speed =  1.0f;
    if (speed < -1.0f) speed = -1.0f;

    uint slice = pwm_gpio_to_slice_num(pwm_pin);
    uint chan  = pwm_gpio_to_channel(pwm_pin);

    if (speed > 0.0f) {
        gpio_put(in1, 1); gpio_put(in2, 0);
        pwm_set_chan_level(slice, chan, (uint16_t)(speed * PWM_RANGE));
    } else if (speed < 0.0f) {
        gpio_put(in1, 0); gpio_put(in2, 1);
        pwm_set_chan_level(slice, chan, (uint16_t)(-speed * PWM_RANGE));
    } else {
        gpio_put(in1, 0); gpio_put(in2, 0);
        pwm_set_chan_level(slice, chan, 0);
    }
}

static void stop_all_motors(void) {
    set_motor(M1_PWM, M1_IN1, M1_IN2, 0.0f);
    set_motor(M2_PWM, M2_IN1, M2_IN2, 0.0f);
    set_motor(M3_PWM, M3_IN1, M3_IN2, 0.0f);
    set_motor(M4_PWM, M4_IN1, M4_IN2, 0.0f);
}

// ---------------- Bomba / Relé ----------------
static void pump_start(int32_t duration_ms) {
    if (duration_ms <= 0) {
        return;
    }
    gpio_put(PUMP_PIN, 0);          // liga relé (inverta se for ativo-baixo)
    pump_active   = true;
    pump_end_time = to_ms_since_boot(get_absolute_time()) + (uint32_t)duration_ms;
}

static void pump_stop(void) {
    gpio_put(PUMP_PIN, 1);          // desliga relé (inverta se for ativo-baixo)
    pump_active = false;
}

// Chamado a cada volta do loop principal; desliga a bomba quando o tempo
// pedido em /bomba_dose (ou pela dosagem automática) já passou.
// Não bloqueia o executor.
static void pump_update(void) {
    if (pump_active) {
        uint32_t now = to_ms_since_boot(get_absolute_time());
        if (now >= pump_end_time) {
            pump_stop();
        }
    }
}

// ---------------- Buzzer ----------------
static void buzzer_start(uint32_t duration_ms) {
    gpio_put(BUZZER_PIN, 1);
    buzzer_active   = true;
    buzzer_end_time = to_ms_since_boot(get_absolute_time()) + duration_ms;
}

static void buzzer_stop(void) {
    gpio_put(BUZZER_PIN, 0);
    buzzer_active = false;
}

// Chamado a cada volta do loop principal; desliga o buzzer sozinho após
// BUZZER_DURATION_MS. Não bloqueia o executor.
static void buzzer_update(void) {
    if (buzzer_active) {
        uint32_t now = to_ms_since_boot(get_absolute_time());
        if (now >= buzzer_end_time) {
            buzzer_stop();
        }
    }
}

// ---------------- ROS: subscribers ----------------
rcl_subscription_t subscriber;
geometry_msgs__msg__Twist twist_msg;

rcl_subscription_t pump_subscriber;
std_msgs__msg__Int32 dose_msg;

rcl_subscription_t pedido_dose_subscriber;
std_msgs__msg__Empty pedido_dose_msg;

// ---------------- ROS: publisher de evento da dosagem automática ----------------
// Publica em /bomba_evento (std_msgs/Int32):
//   1 -> dosagem automática iniciada (cmd_vel bloqueado)
//   0 -> dosagem automática concluída e ímã afastado (cmd_vel liberado)
rcl_publisher_t bomba_evento_publisher;
std_msgs__msg__Int32 bomba_evento_msg;

static void publish_bomba_evento(int32_t valor) {
    bomba_evento_msg.data = valor;
    RCSOFTCHECK(rcl_publish(&bomba_evento_publisher, &bomba_evento_msg, NULL));
}

void cmd_vel_callback(const void * msin) {
    const geometry_msgs__msg__Twist * m = (const geometry_msgs__msg__Twist *)msin;

    last_cmd_time = to_ms_since_boot(get_absolute_time());

    // Enquanto a dosagem automática estiver rolando, ignora qualquer
    // comando de movimento (o robô fica parado até o KY-003 liberar).
    if (cmd_vel_blocked) {
        return;
    }

    // DEBUG: pisca LED para confirmar que o callback foi chamado
    gpio_put(LED_PIN, 1);

    float x = -m->linear.x;  // invertido: frente/tras trocados
    float y = m->linear.y;
    float z = m->angular.z;

    // Atualiza se o robô está "em movimento" (usado pelo dosador automático
    // para só ligar a bomba quando o robô estiver parado)
    robot_moving = (fabsf(x) > ZERO_VEL_EPS) ||
                   (fabsf(y) > ZERO_VEL_EPS) ||
                   (fabsf(z) > ZERO_VEL_EPS);

    // Mesma lógica de mixagem (mecanum) do código original
    set_motor(M1_PWM, M1_IN1, M1_IN2, x + y + z);
    set_motor(M2_PWM, M2_IN1, M2_IN2, x - y - z);
    set_motor(M3_PWM, M3_IN1, M3_IN2, x + y - z);
    set_motor(M4_PWM, M4_IN1, M4_IN2, x - y + z);

    gpio_put(LED_PIN, 0);
}

// Callback do tópico /bomba_dose (std_msgs/msg/Int32)
// Ex.: ros2 topic pub --once /bomba_dose std_msgs/msg/Int32 "{data: 3000}"
// -> liga o relé por 3000 ms e desliga sozinho
// (continua funcionando independente do dosador automático via KY-003)
void bomba_dose_callback(const void * msgin) {
    const std_msgs__msg__Int32 * m = (const std_msgs__msg__Int32 *)msgin;
    pump_start(m->data);
}

// Callback do tópico /pedido_dose (std_msgs/msg/Empty)
// Disparado pelo script da webcam quando detecta a mão levantada.
// Só toca o buzzer por 1s — não mexe em motor nem bomba (isso é o
// mission_manager que decide, na RPi).
void pedido_dose_callback(const void * msgin) {
    (void) msgin; // Empty não carrega dado nenhum
    buzzer_start(BUZZER_DURATION_MS);
}

// ---------------- KY-003: leitura por ADC com debounce ----------------
static void ky003_update(void) {
    uint32_t now = to_ms_since_boot(get_absolute_time());

    uint16_t raw_adc = adc_read();                      // 0-4095 (12 bits)
    float voltage = (raw_adc * 3.3f) / 4095.0f;

    // "Detectado" = tensão caiu para perto de 0V (copo/ímã presente)
    bool raw = (voltage < KY003_THRESHOLD_V);

    if (raw != ky003_raw_state) {
        ky003_raw_state = raw;
        ky003_last_change_time = now;
    }

    if ((now - ky003_last_change_time) >= KY003_DEBOUNCE_MS) {
        ky003_stable_state = ky003_raw_state;
    }
}

// ---------------- Máquina de estados do dosador automático ----------------
static void doser_update(void) {
    switch (doser_state) {

        case DOSER_IDLE:
            // Só dispara se detectou o ímã E o robô está parado
            if (ky003_stable_state && !robot_moving) {
                cmd_vel_blocked = true;
                stop_all_motors();
                pump_start(KY003_DOSE_MS);
                doser_state = DOSER_ACTIVE;
                publish_bomba_evento(1); // avisa o ROS: dosagem iniciada
            }
            break;

        case DOSER_ACTIVE:
            // pump_update() já desliga a bomba sozinha após KY003_DOSE_MS
            if (!pump_active) {
                doser_state = DOSER_WAIT_CLEAR;
            }
            break;

        case DOSER_WAIT_CLEAR:
            // só libera o /cmd_vel quando o ímã não estiver mais por perto
            if (!ky003_stable_state) {
                cmd_vel_blocked = false;
                doser_state = DOSER_IDLE;
                publish_bomba_evento(0); // avisa o ROS: liberado
            }
            break;
    }
}

int main() {
    rmw_uros_set_custom_transport(
        true, NULL,
        pico_serial_transport_open,
        pico_serial_transport_close,
        pico_serial_transport_write,
        pico_serial_transport_read
    );

    // LED debug
    gpio_init(LED_PIN);
    gpio_set_dir(LED_PIN, GPIO_OUT);
    gpio_put(LED_PIN, 0);

    // Relé da bomba — começa desligado
    gpio_init(PUMP_PIN);
    gpio_set_dir(PUMP_PIN, GPIO_OUT);
    gpio_put(PUMP_PIN, 1);

    // Buzzer — começa desligado
    gpio_init(BUZZER_PIN);
    gpio_set_dir(BUZZER_PIN, GPIO_OUT);
    gpio_put(BUZZER_PIN, 0);

    // KY-003 — lido por ADC (ver comentário na definição de KY003_PIN)
    adc_init();
    adc_gpio_init(KY003_PIN);
    adc_select_input(KY003_ADC_INPUT);

    // Pinos de direção — todos a zero no boot
    uint dir_pins[] = {M1_IN1, M1_IN2, M2_IN1, M2_IN2,
                        M3_IN1, M3_IN2, M4_IN1, M4_IN2};
    for (int i = 0; i < 8; i++) {
        gpio_init(dir_pins[i]);
        gpio_set_dir(dir_pins[i], GPIO_OUT);
        gpio_put(dir_pins[i], 0);
    }

    setup_pwm(M1_PWM);
    setup_pwm(M2_PWM);
    setup_pwm(M3_PWM);
    setup_pwm(M4_PWM);

    // Pequena espera para o enumeramento USB estabilizar antes de tentar
    // falar com o agent (evita falha de handshake logo no boot)
    sleep_ms(2000);

    // --------- Espera ativa pelo micro-ROS agent na Raspberry Pi ---------
    // Fica piscando devagar até conseguir "pingar" o agent via USB serial.
    // Assim que você subir o `micro_ros_agent` na Raspberry Pi, o Pico conecta.
    while (rmw_uros_ping_agent(100, 1) != RMW_RET_OK) {
        gpio_put(LED_PIN, 1);
        sleep_ms(150);
        gpio_put(LED_PIN, 0);
        sleep_ms(150);
    }

    // Pisca 2x rápido para indicar que o agent respondeu (link OK)
    for (int i = 0; i < 2; i++) {
        gpio_put(LED_PIN, 1); sleep_ms(80);
        gpio_put(LED_PIN, 0); sleep_ms(80);
    }

    rcl_node_t      node;
    rcl_allocator_t allocator = rcl_get_default_allocator();
    rclc_support_t  support;

    RCCHECK(rclc_support_init(&support, 0, NULL, &allocator));
    RCCHECK(rclc_node_init_default(&node, "pingabot_pico", "", &support));

    geometry_msgs__msg__Twist__init(&twist_msg);
    std_msgs__msg__Int32__init(&dose_msg);
    std_msgs__msg__Int32__init(&bomba_evento_msg);
    std_msgs__msg__Empty__init(&pedido_dose_msg);

    RCCHECK(rclc_subscription_init_default(
        &subscriber, &node,
        ROSIDL_GET_MSG_TYPE_SUPPORT(geometry_msgs, msg, Twist), "/cmd_vel"
    ));

    RCCHECK(rclc_subscription_init_default(
        &pump_subscriber, &node,
        ROSIDL_GET_MSG_TYPE_SUPPORT(std_msgs, msg, Int32), "/bomba_dose"
    ));

    RCCHECK(rclc_subscription_init_default(
        &pedido_dose_subscriber, &node,
        ROSIDL_GET_MSG_TYPE_SUPPORT(std_msgs, msg, Empty), "/pedido_dose"
    ));

    // Publisher que avisa o ROS quando a dosagem automática (KY-003)
    // inicia (1) e termina (0)
    RCCHECK(rclc_publisher_init_default(
        &bomba_evento_publisher, &node,
        ROSIDL_GET_MSG_TYPE_SUPPORT(std_msgs, msg, Int32), "/bomba_evento"
    ));

    rclc_executor_t executor;
    RCCHECK(rclc_executor_init(&executor, &support.context, 3, &allocator));
    RCCHECK(rclc_executor_add_subscription(&executor, &subscriber, &twist_msg,
                                            &cmd_vel_callback, ON_NEW_DATA));
    RCCHECK(rclc_executor_add_subscription(&executor, &pump_subscriber, &dose_msg,
                                            &bomba_dose_callback, ON_NEW_DATA));
    RCCHECK(rclc_executor_add_subscription(&executor, &pedido_dose_subscriber, &pedido_dose_msg,
                                            &pedido_dose_callback, ON_NEW_DATA));

    last_cmd_time = to_ms_since_boot(get_absolute_time());

    while (true) {
        rclc_executor_spin_some(&executor, RCL_MS_TO_NS(10));

        // Watchdog de segurança: sem comando novo há muito tempo -> para tudo
        uint32_t now = to_ms_since_boot(get_absolute_time());
        if ((now - last_cmd_time) > CMD_TIMEOUT_MS) {
            stop_all_motors();
        }

        // Desliga a bomba quando o tempo pedido em /bomba_dose (ou pela
        // dosagem automática) terminar
        pump_update();

        // Desliga o buzzer sozinho depois de BUZZER_DURATION_MS
        buzzer_update();

        // Lê o KY-003 com debounce e roda a máquina de estados do dosador
        ky003_update();
        doser_update();
    }

    return 0;
}
