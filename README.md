# PingaBot

Carrinho garçom autônomo de doses, baseado em **ROS 2 (Humble)** e **Micro-ROS**. O robô identifica quando alguém levanta a mão via webcam, percorre um trajeto pré-calibrado seguindo as paredes de uma mesa com auxílio de um LiDAR, e serve uma dose automaticamente ao detectar um copo em cada parada.

## Como funciona (visão geral)

1. Uma **webcam** na Raspberry Pi 4 roda um pipeline de visão computacional que detecta o gesto de mão levantada.
2. Ao detectar, a Raspberry Pi dispara a **rota da missão**: o robô se move em paradas pré-definidas, usando leituras de um **LiDAR LDS02RR** (via ESP32) para se guiar pelas paredes da mesa.
3. Em cada parada, o robô espera a colocação de um copo. Um sensor de efeito Hall (**KY-003**) na base detecta o ímã do copo e a **Raspberry Pi Pico** aciona a bomba de dosagem **de forma autônoma** - sem depender do ROS para essa parte crítica de tempo. Retornando à rota apenas quando o copo tenha sido removido (caso colocado).
4. Concluídas as paradas, o robô retorna à posição inicial e volta a monitorar a webcam.

```
Webcam ──USB──> Raspberry Pi 4 ──UART──> Raspberry Pi Pico (motores, bomba, buzzer, sensor hall)
                      │
LiDAR LDS02RR ──> ESP32 ──UART──> Raspberry Pi 4
```

Mais detalhes da arquitetura de nós/tópicos ROS2 em [`docs/architecture.md`](docs/architecture.md). *(a ser implementado)*

📹 **Vídeo de demonstração:** [https://youtu.be/_JYmyTu3Q5M](https://youtu.be/_JYmyTu3Q5M)

## Estrutura do repositório

```
pingabot/
├── firmware/
│   ├── pico/            # firmware da Raspberry Pi Pico (Micro-ROS): motores, bomba, buzzer, KY-003
│   └── esp32_lidar/      # firmware da ESP32 (leitura do LiDAR LDS02RR, biblioteca kaiaai/LDS)
│
├── ros2_ws/
│   └── src/
│       ├── pingabot_bringup/     # launch files e configs
│       ├── pingabot_vision/      # detecção de mão levantada (webcam)
│       ├── pingabot_navigation/  # leitura do LiDAR + navegação
│       └── pingabot_mission/     # máquina de estados da missão (orquestrador)
│
├── hardware/             # esquemas elétricos, fiação, BOM
├── 3d_models/            # peças impressas em 3D (STL e gcode utilizado)
└── docs/                 # arquitetura, calibração, troubleshooting
```

## Hardware

Lista completa de componentes em [`hardware/bom.md`](hardware/bom.md).

Resumo:
- **Raspberry Pi 4** - processamento central, visão computacional, orquestração da missão.
- **Raspberry Pi Pico** - controle de baixo nível via Micro-ROS (motores mecanum, bomba, buzzer, sensor hall).
- **ESP32** - leitura do LiDAR LDS02RR, repassada via UART.
- **4x motor DC + roda mecanum** - locomoção omnidirecional.
- **Bomba + relé** - sistema de dosagem, acionado localmente pela Pico.
- **KY-003** (sensor hall) - detecção do copo via ímã na base.

## Pré-requisitos

- Ubuntu com **ROS 2 Humble** instalado na Raspberry Pi.
- [Micro-ROS Agent](https://micro.ros.org/) para a comunicação com a Pico.
- Pico SDK configurado para compilar o firmware em `firmware/pico/`.
- Arduino IDE (ou PlatformIO) + biblioteca [`kaiaai/LDS`](https://github.com/kaiaai/LDS) para a ESP32 - ver [`firmware/esp32_lidar/README.md`](firmware/esp32_lidar/README.md).
- Python 3 com `rclpy`, `opencv-python` e a lib de detecção de mãos usada em `pingabot_vision`.

## Quickstart

Com todo o hardware ligado (Pico e ESP32 conectadas via USB/serial na Raspberry Pi), abra 4 terminais:

```bash
# 1. Ponte com a Pico (motores, bomba, buzzer, KY-003)
ros2 run micro_ros_agent micro_ros_agent serial --dev /dev/ttyACM0

# 2. Leitura do LiDAR (ESP32 -> /scan)
python3 ros2_ws/src/pingabot_navigation/lidar_serial_node.py

# 3. Detecção de mão levantada (webcam -> /pedido_dose)
python3 ros2_ws/src/pingabot_vision/hand_detector_node.py

# 4. Orquestrador da missão
python3 ros2_ws/src/pingabot_mission/mission_node.py
```

Para testar sem câmera/lidar, dá pra simular os eventos manualmente:

```bash
# simula "mão levantada"
ros2 topic pub --once /pedido_dose std_msgs/msg/Empty "{}"

# simula "copo detectado / dose concluída"
ros2 topic pub --once /bomba_evento std_msgs/msg/Int32 "{data: 1}"
ros2 topic pub --once /bomba_evento std_msgs/msg/Int32 "{data: 0}"

# aciona a bomba manualmente por X ms
ros2 topic pub --once /bomba_dose std_msgs/msg/Int32 "{data: 3000}"
```

Mais comandos de debug (tópicos, nós, frequência) em [`docs/troubleshooting.md`](docs/troubleshooting.md).

## Status do projeto

🚧 Em desenvolvimento. Já funcionando: controle dos motores com watchdog de segurança, dosagem autônoma via KY-003, leitura do LiDAR, buzzer de aviso. Em andamento: navegação seguindo paredes/linha virtual calibrada, integração completa da máquina de estados da missão.

## Documentação adicional

- [`docs/architecture.md`](docs/architecture.md) — grafo de nós, tópicos e máquina de estados da missão.
- [`docs/calibration.md`](docs/calibration.md) — tempos de trajeto, limiares do KY-003, velocidades seguras.
- [`docs/troubleshooting.md`](docs/troubleshooting.md) — problemas já resolvidos (reset por queda de tensão, ground bounce da bomba, etc.) e comandos de debug do ROS2.

## Integrantes
 
- Arthur Roberto da Silva
- Maria Eduarda Santana Marques
- Pedro Nobrega Damacena
- William Marreiro Brito

## Licença

Ver [`LICENSE`](LICENSE). O firmware da ESP32 referencia a biblioteca [`kaiaai/LDS`](https://github.com/kaiaai/LDS) (Apache License 2.0, © KAIA.AI), não incluída/copiada neste repositório.