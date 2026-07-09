# Firmware ESP32 — LiDAR LDS02RR

Responsável por ler o LiDAR **Xiaomi LDS02RR** via UART e repassar as leituras (ângulo + distância) pela porta serial USB da ESP32 até a Raspberry Pi, onde o `pingabot_navigation` (nó `lidar_serial_node.py`) faz o parsing e publica em `/scan`.

## Origem do firmware

Este firmware **não é código nosso** - é o exemplo oficial da biblioteca [kaiaai/LDS](https://github.com/kaiaai/LDS), especificamente:

**[`all_lidars_lds02rr_esp32.ino`](https://github.com/kaiaai/LDS/blob/main/examples/all_lidars_lds02rr_esp32/all_lidars_lds02rr_esp32.ino)**

Optamos por **referenciar o exemplo original em vez de versionar uma cópia** dele aqui no repositório. Isso mantém o firmware sempre alinhado com correções e atualizações que a KAIA.AI publicar na lib, e evita a gente carregar/manter um código de terceiros com licença própria dentro do nosso repo.

## Como instalar

1. No Arduino IDE (ou PlatformIO), instale a placa **ESP32** (via Boards Manager, pacote da Espressif).
2. Instale a biblioteca **LDS** da KAIA.AI: clone https://github.com/kaiaai/LDS na pasta de bibliotecas do Arduino.
3. Abra o exemplo `File > Examples > LDS > all_lidars_lds02rr_esp32`.

## Configuração usada no PingaBot

No arquivo do exemplo, os únicos pontos que alteramos em relação ao padrão são:

```cpp
const uint8_t LIDAR_GPIO_EN  = 19; 
// const uint8_t LIDAR_GPIO_RX  = 16;
const uint8_t LIDAR_GPIO_TX  = 17;
const uint8_t LIDAR_GPIO_PWM = 15;

#define XIAOMI_LDS02RR   // modelo do lidar (deixar apenas essa linha descomentada)
```

Todas as outras `#define` de modelo (`SLAMTEC_RPLIDAR_A1`, `YDLIDAR_X4`, etc.) devem continuar comentadas.

## Pinout (ESP32 ↔ LiDAR)

| ESP32 GPIO | Função | Lidar |
| :---: | :--- | :--- |
| 19 | EN | Habilita/desabilita o motor do lidar |
| 17 | TX | Dados do lidar |
| 15 | PWM | Controle de velocidade do motor |
<!-- | 16 | RX | Recebe dados do lidar (lidar TX → ESP32 RX) | -->

## Saída esperada (Serial Monitor / USB)

Com a configuração padrão do exemplo, a cada ~20 pontos ele imprime `índice distância_mm ângulo_graus`, e ao fim de cada volta completa imprime a frequência de varredura:

```
220 1415.00 220.00
240 622.00 240.00
...
Scan completed; scans-per-second 5.02
```

Um valor de distância `0.00` indica leitura inválida naquele ângulo (sem retorno). É esse fluxo de texto que o `lidar_serial_node.py` (na Raspberry Pi) lê via `/dev/ttyUSB0` e converte em `sensor_msgs/LaserScan`.

## Baud rate

`SERIAL_MONITOR_BAUD = 115200` - mesmo valor usado do lado da Raspberry Pi ao abrir a porta serial (`serial.Serial('/dev/ttyUSB0', 115200, ...)` no `lidar_serial_node.py`, ou `minicom -D /dev/ttyUSB0 -b 115200` para debug manual).

## Alimentação — atenção

O LiDAR tem pico de corrente considerável na partida do motor. Já tivemos undervoltage na Raspberry Pi só de ligar o LiDAR nela - **recomendamos alimentar o LiDAR/ESP32 por uma fonte separada**, nunca direto do 5V da Pi, e mantenha o mesmo cuidado de ground/capacitor de bulk descrito em `docs/troubleshooting.md`.

## Debug

Se as leituras não aparecerem ou vierem estranhas, descomente uma das flags de debug no início do `.ino` (`DEBUG_GPIO`, `DEBUG_PACKETS`, `DEBUG_SERIAL_IN`, `DEBUG_SERIAL_OUT`) e **aumente o `SERIAL_MONITOR_BAUD`** para o máximo suportado, como indicado no próprio comentário do exemplo - em baud baixo, o log de debug pode atrasar demais o processamento e distorcer a leitura do lidar.

## Licença

O firmware referenciado (`all_lidars_lds02rr_esp32.ino`, biblioteca `kaiaai/LDS`) é licenciado sob **Apache License 2.0** © KAIA.AI. Nenhum código dele está copiado neste repositório - apenas a configuração de pinout/modelo documentada acima é nossa.