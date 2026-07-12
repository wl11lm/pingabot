# Arquitetura - PingaBot

## Visão geral de comunicação física

```
Webcam ──USB──> Raspberry Pi 4 ──UART──> Raspberry Pi Pico (motores, bomba, buzzer, KY-003)
                      │
LiDAR LDS02RR ──> ESP32 ──UART──> Raspberry Pi 4
```

A Raspberry Pi 4 é o único nó "ROS2 completo" do sistema. A Pico e a ESP32 são satélites que falam serial puro - a Pico via **Micro-ROS** (já fala ROS2 nativamente através do `micro_ros_agent`), a ESP32 via **texto puro** (linhas `ângulo distância ângulo`, sem ROS embarcado), que é convertido em ROS2 por um nó Python na Raspberry Pi.

## Grafo de nós ROS2

| Nó | Onde roda | Publica | Assina |
| :--- | :--- | :--- | :--- |
| `hand_detector_node` | RPi4 (Python) | `/pedido_dose` (`std_msgs/Empty`) | — (lê a webcam via OpenCV) |
| `lidar_serial_node` | RPi4 (Python) | `/scan` (`sensor_msgs/LaserScan`) | — (lê `/dev/ttyUSB0`) |
| `mission_manager` (`mission_node.py`) | RPi4 (Python) | `/cmd_vel` (`geometry_msgs/Twist`) | `/pedido_dose`, `/bomba_evento` |
| firmware da Pico | Pico (Micro-ROS) | `/bomba_evento` (`std_msgs/Int32`) | `/cmd_vel`, `/bomba_dose`, `/pedido_dose` |

## Detalhe de cada peça

### `hand_detector_node` (`ros2_ws/src/pingabot_vision/hand_detector_node.py`)

Usa **MediaPipe Hands** para reconhecer especificamente o **gesto de "número 1"** (indicador levantado, médio/anelar/mínimo abaixados — o polegar é ignorado por variar demais entre pessoas). Não é "qualquer mão levantada": é um gesto específico, o que reduz falsos positivos de alguém só passando a mão na frente da câmera.

Publica em `/pedido_dose` apenas na **borda de subida** do gesto (transição "sem gesto" → "gesto detectado"), com um **cooldown de 3s** (`COOLDOWN_S`) entre disparos — evita disparar a rota várias vezes seguidas enquanto a pessoa mantém o gesto.

Roda a 10Hz, captura em 320x240 a 15fps (resolução reduzida de propósito, para não sobrecarregar a CPU da Raspberry Pi).

### `lidar_serial_node` (`ros2_ws/src/pingabot_navigation/lidar_serial_node.py`)

Lê `/dev/ttyUSB0` a 115200 baud, faz o parsing das linhas que o firmware da ESP32 imprime (`ângulo distância_mm ângulo`) e publica em `/scan` como `sensor_msgs/LaserScan` a cada `Scan completed` (uma volta completa do lidar).

> ⚠️ **Estado atual**: o `/scan` já está sendo publicado, mas **ainda não é consumido por nenhum nó de navegação**. O `mission_manager` atual não lê o LiDAR — ele se move por **tempo calibrado** (ver `docs/calibration.md`). A navegação por paredes/linha virtual usando o `/scan` é a próxima etapa planejada, ainda não implementada em `pingabot_navigation`.

### `mission_manager` (`ros2_ws/src/pingabot_mission/mission_node.py`)

O orquestrador da missão. Máquina de estados rodando via `timer` a 10Hz (sem `sleep()` bloqueante em nenhum lugar — ver `docs/troubleshooting.md` sobre por que isso importa).

```
IDLE
  └─ /pedido_dose recebido ──> INDO
INDO
  └─ tempo_no_estado >= TEMPO_1M ──> AGUARDANDO_COPO (parada_atual += 1)
AGUARDANDO_COPO
  ├─ /bomba_evento == 1 (copo detectado) ──> DOSANDO
  └─ timeout (TEMPO_ESPERA_COPO) sem copo ──> INDO (próxima parada) ou RETORNANDO
DOSANDO
  └─ /bomba_evento == 0 (dose concluída) ──> INDO (próxima parada) ou RETORNANDO
RETORNANDO
  └─ tempo_no_estado >= TEMPO_1M * NUM_PARADAS ──> IDLE
```

Note que o `mission_manager` **não decide quando a bomba liga** — ele só reage ao `/bomba_evento` que a Pico publica sozinha ao detectar o ímã via KY-003. Isso mantém o tempo crítico da dosagem fora do ROS, como decidido desde o início do projeto.

### Firmware da Pico (`firmware/pico/src/pingabot_pico.c`)

Detalhado no [`firmware/pico/README.md`](../firmware/pico/README.md). Resumo do que roda lá:

- **Watchdog de `/cmd_vel`**: sem comando novo em 500ms, motores param sozinhos.
- **Dosagem autônoma via KY-003**: só dispara se o robô estiver parado; bloqueia `/cmd_vel` durante a dose; libera só quando o ímã se afasta.
- **`/bomba_dose`**: dosagem manual, independente do KY-003 — útil para testes.
- **`/pedido_dose`**: aciona só o buzzer (1s) — o mesmo tópico que o `mission_manager` escuta para iniciar a rota, então os dois reagem ao mesmo evento.

## Tópicos — referência rápida

| Tópico | Tipo | Publicado por | Assinado por |
| :--- | :--- | :--- | :--- |
| `/pedido_dose` | `std_msgs/Empty` | `hand_detector_node` | `mission_manager`, Pico (buzzer) |
| `/cmd_vel` | `geometry_msgs/Twist` | `mission_manager` | Pico (motores) |
| `/bomba_dose` | `std_msgs/Int32` | (manual / debug) | Pico (bomba) |
| `/bomba_evento` | `std_msgs/Int32` | Pico | `mission_manager` |
| `/scan` | `sensor_msgs/LaserScan` | `lidar_serial_node` | *(nenhum ainda — ver nota acima)* |

## Pacotes ainda vazios (planejados)

- `pingabot_bringup/launch/` — sem launch files ainda; hoje os 4 nós + agent são subidos manualmente em terminais separados (ver Quickstart no README raiz).
- `hardware/schematics/` e `hardware/wiring/` — sem arquivos ainda.