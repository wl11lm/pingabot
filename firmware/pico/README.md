# Firmware Raspberry Pi Pico - PingaBot

Controle de baixo nível do robô via **Micro-ROS**: acionamento dos 4 motores mecanum, bomba de dosagem (relé), buzzer de aviso e leitura do sensor de efeito Hall (KY-003) para detecção do copo.

## Base do projeto

Este firmware é construído em cima do exemplo oficial **[micro-ROS/micro_ros_raspberrypi_pico_sdk](https://github.com/micro-ROS/micro_ros_raspberrypi_pico_sdk)**. Reaproveitamos a estrutura de build e o transporte serial do exemplo (`pico_uart_transport.c`, `libmicroros/`) e escrevemos nossa própria lógica de aplicação em `pingabot_pico.c` + `CMakeLists.txt` customizado.

## Estrutura desta pasta

```
firmware/pico/
└── src/
    ├── CMakeLists.txt        # nosso — configura o alvo pingabot_pico
    └── pingabot_pico.c        # nosso — toda a logica do robo (motores, bomba, buzzer, KY-003)
```

Só isso é versionado aqui. `pico_uart_transport.c` e a pasta `libmicroros/` **não estão neste repositório** - são do exemplo oficial e precisam ser copiados antes de compilar, dentro de `firmware/pico/src/` (passo abaixo, no mesmo nível do `CMakeLists.txt` — ele referencia esses dois caminhos relativos). Isso mantém o repositório leve e sempre alinhado com a versão mais atual da lib Micro-ROS pré-compilada.

## Dependências

- **Pico SDK** instalado e configurado (`PICO_TOOLCHAIN_PATH`, `PICO_SDK_PATH`), conforme o [README do repositório base](https://github.com/micro-ROS/micro_ros_raspberrypi_pico_sdk#getting-started).
- `arm-none-eabi-gcc` (versão compatível com a lib Micro-ROS pré-compilada do repositório base).

```bash
sudo apt install cmake g++ gcc-arm-none-eabi doxygen libnewlib-arm-none-eabi git python3
git clone --recurse-submodules https://github.com/raspberrypi/pico-sdk.git $HOME/pico-sdk
echo "export PICO_SDK_PATH=$HOME/pico-sdk" >> ~/.bashrc
source ~/.bashrc
```

## Preparar os arquivos do repositório base (só na primeira vez)

Antes de compilar, clone o repositório base do micro-ROS e copie os dois itens que faltam para dentro desta pasta:

```bash
git clone https://github.com/micro-ROS/micro_ros_raspberrypi_pico_sdk /tmp/microros_base

cp /tmp/microros_base/pico_uart_transport.c firmware/pico/src/
cp -r /tmp/microros_base/libmicroros firmware/pico/src/
```

Depois disso, `firmware/pico/src/` deve ter os 4 itens (`CMakeLists.txt`, `pingabot_pico.c`, `pico_uart_transport.c`, `libmicroros/`) e você pode seguir pra compilação normalmente.

## Como compilar

```bash
cd firmware/pico/src
mkdir -p build
cd build
cmake ..
make
```

Isso gera `pingabot_pico.uf2` dentro de `build/`.

## Como gravar (flash)

1. Segure o botão BOOTSEL da Pico e conecte o cabo USB ao computador (ou use o botão de reset externo junto com o BOOTSEL).
2. Identifique o dispositivo e monte o drive USB manualmente:
   * Use o `lsblk` para localizar o nome do dispositivo correspondente à Pico (geralmente algo como `/dev/sda1`).
   * Crie um diretório e monte o dispositivo:
     ```bash
     sudo mkdir -p /media/$USER/RPI-RP2
     sudo mount /dev/sdb1 /media/$USER/RPI-RP2
     ```
3. Copie o firmware e garanta a escrita dos dados:
   ```bash
   cp pingabot_pico.uf2 /media/$USER/RPI-RP2
   sync
   ```

## Pinout

| Pino GPIO | Função |
| :---: | :--- |
| 21 / 19 / 20 | M1 — PWM / IN1 / IN2 |
| 6 / 7 / 8 | M2 — PWM / IN1 / IN2 |
| 13 / 14 / 15 | M3 — PWM / IN1 / IN2 |
| 18 / 16 / 17 | M4 — PWM / IN1 / IN2 |
| 9 | Relé da bomba de dosagem |
| 27 | Buzzer |
| 26 (ADC0) | KY-003 (sensor Hall - leitura analógica) |
| 25 | LED onboard (debug) |

## Tópicos ROS 2

| Tópico | Tipo | Direção | Descrição |
| :--- | :--- | :---: | :--- |
| `/cmd_vel` | `geometry_msgs/msg/Twist` | Sub | Velocidade dos motores (mixagem mecanum). Ignorado enquanto a dosagem automática estiver em andamento. |
| `/bomba_dose` | `std_msgs/msg/Int32` | Sub | Aciona a bomba manualmente por N ms (`data`). |
| `/pedido_dose` | `std_msgs/msg/Empty` | Sub | Disparado ao detectar mão levantada (webcam) - toca o buzzer por 1s. |
| `/bomba_evento` | `std_msgs/msg/Int32` | Pub | `1` = dosagem automática (KY-003) iniciada; `0` = concluída e copo removido. |

## Comportamento de segurança

- **Watchdog de `/cmd_vel`**: se nenhum comando chegar em 500ms (`CMD_TIMEOUT_MS`), os motores param sozinhos.
- **Dosagem autônoma**: o KY-003 só dispara a bomba se o robô estiver parado (`robot_moving == false`), e bloqueia novos `/cmd_vel` até o copo ser retirado - evita o robô se mover com a bomba ligada.
- **`error_loop()`**: qualquer falha de inicialização do Micro-ROS trava o firmware piscando o LED onboard em padrão de erro (100ms on/off).

## Testando isoladamente (sem o resto do sistema)

Com o `micro_ros_agent` rodando na Raspberry Pi:

```bash
ros2 topic pub --once /pedido_dose std_msgs/msg/Empty "{}"                  # buzzer 1s
ros2 topic pub --once /bomba_dose std_msgs/msg/Int32 "{data: 3000}"         # bomba 3s
ros2 topic pub --rate 5 /cmd_vel geometry_msgs/msg/Twist "{linear: {x: 0.25}}"  # motores
ros2 topic echo /bomba_evento                                                # ver eventos do KY-003
```

Mais comandos de debug em [`docs/troubleshooting.md`](../../../docs/troubleshooting.md).

## Ajustes conhecidos por hardware

Alguns `#define` no topo de `pingabot_pico.c` provavelmente precisam de calibração por unidade montada:

- `KY003_THRESHOLD_V` — depende da tensão de repouso medida no seu KY-003 específico.
- `KY003_DOSE_MS` — tempo de dosagem automática.
- Velocidade máxima segura do `/cmd_vel` — testada e limitada externamente (ver `docs/calibration.md`); valores muito altos (~0.8 ou mais) já causaram reset da placa por queda de tensão no arranque dos motores.

## Nota sobre o `.gitignore`

Como `pico_uart_transport.c` e `libmicroros/` são copiados localmente dentro de `firmware/pico/src/` e não devem ir pro repositório, adicione ao `.gitignore` do projeto (ainda não existe um — ver `docs/troubleshooting.md`):

```gitignore
firmware/pico/src/pico_uart_transport.c
firmware/pico/src/libmicroros/
firmware/pico/src/build/
```

## Licença

O código de `pingabot_pico.c` e o `CMakeLists.txt` desta pasta são deste projeto. `pico_uart_transport.c` e `libmicroros/` (copiados localmente, não versionados) são do repositório [micro-ROS/micro_ros_raspberrypi_pico_sdk](https://github.com/micro-ROS/micro_ros_raspberrypi_pico_sdk), licenciado sob Apache License 2.0.