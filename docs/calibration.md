# Calibração — PingaBot

Valores usados atualmente no firmware e nos nós ROS2, extraídos direto do código versionado. Sempre que recalibrar algo na bancada, atualize este arquivo junto — ele existe pra não perdermos esses números de novo.

## Movimento (`mission_node.py`)

```python
VELOCIDADE = 0.8          # m/s, publicado em /cmd_vel
TEMPO_1M = 1.8             # segundos para andar 1m nessa velocidade
NUM_PARADAS = 2
TEMPO_ESPERA_COPO = 6.0    # segundos esperando copo antes de seguir sem servir
```

> ⚠️ **Atenção**: `VELOCIDADE = 0.8` está **acima** do limite de segurança identificado nos primeiros testes de bancada, onde valores de `linear.x` acima de ~0.5 causavam reset da Pico (queda de tensão no arranque dos 4 motores simultâneos). Se esse valor foi elevado para 0.8 **depois** de isolar a alimentação de potência da alimentação lógica (ver `docs/troubleshooting.md`), documente aqui a validação feita — quantos ciclos completos rodaram sem reset, em que condição de bateria, etc. Se ainda não foi revalidado, o mais seguro é voltar a testar em incrementos pequenos (0.3 → 0.5 → 0.8) antes de confiar nesse valor em demonstração.

**Como recalibrar `TEMPO_1M`**: publique `/cmd_vel` numa velocidade fixa e cronometre o tempo até o robô andar 1m (fita métrica no chão):
```bash
ros2 topic pub --rate 10 /cmd_vel geometry_msgs/msg/Twist "{linear: {x: 0.8}}"
```
`Ctrl+C` para parar quando completar 1m — o watchdog da Pico (500ms) zera os motores sozinho ao parar de publicar.

## Firmware da Pico (`firmware/pico/src/pingabot_pico.c`)

| Constante | Valor | O que controla |
| :--- | :---: | :--- |
| `CMD_TIMEOUT_MS` | 500 | Watchdog — sem `/cmd_vel` novo nesse tempo, motores param sozinhos |
| `ZERO_VEL_EPS` | 0.02 | Abaixo disso, `linear`/`angular` é considerado "robô parado" (usado pelo dosador automático) |
| `KY003_THRESHOLD_V` | 0.75 V | Abaixo = ímã/copo detectado. Baseado em medição de bancada: ~1.5V repouso, ~0V com ímã |
| `KY003_DEBOUNCE_MS` | 30 | Tempo de estabilização antes de aceitar a leitura do KY-003 como válida |
| `KY003_DOSE_MS` | 2000 | Duração da dosagem **automática** (via KY-003) |
| `BUZZER_DURATION_MS` | 1000 | Duração do apito ao receber `/pedido_dose` |
| `PWM_RANGE` | 255 | Resolução do PWM dos motores |

**`KY003_THRESHOLD_V` é específico de cada unidade** — se o sensor for trocado ou a fiação mudar, remeça a tensão de repouso (sem ímã) e com ímã antes de reconfiar nesse valor. Um multímetro no pino do GPIO26 já resolve.

**Dose manual** (`/bomba_dose`) não usa `KY003_DOSE_MS` — aceita qualquer duração em ms via `data`, independente da dose automática:
```bash
ros2 topic pub --once /bomba_dose std_msgs/msg/Int32 "{data: 2000}"
```

## Detecção de gesto (`hand_detector_node.py`)

| Parâmetro | Valor | Nota |
| :--- | :---: | :--- |
| `COOLDOWN_S` | 4.0 s | Tempo mínimo entre dois disparos de `/pedido_dose` |
| Resolução de captura | 320x240 | Reduzida de propósito para custo de CPU na RPi |
| FPS de captura | 15 | |
| Frequência do loop | 10 Hz | |
| `model_complexity` (MediaPipe) | 0 | Modelo mais leve, adequado a CPU sem GPU |
| `min_detection_confidence` | 0.7 | |
| `min_tracking_confidence` | 0.6 | |

Gesto reconhecido: **apenas indicador levantado** (número 1), polegar ignorado. Se quiser trocar o gesto de disparo (ex: mão aberta, "joia"), ajuste `e_numero_1()` — a lógica de landmarks (`TIPS`/`BASES`) já está pronta para outras combinações de dedos.

## LiDAR (`lidar_serial_node.py` + firmware ESP32)

| Parâmetro | Valor |
| :--- | :---: |
| Baud rate | 115200 |
| `angle_min` / `angle_max` | 0° / 340° |
| `angle_increment` | 20° (18 pontos por volta) |
| `range_min` / `range_max` | 0.05 m / 8.0 m |

Esses valores refletem o formato de saída do firmware `all_lidars_lds02rr_esp32.ino` (ver `firmware/esp32_lidar/README.md`) — se o firmware mudar a granularidade de ângulo, esses parâmetros do `LaserScan` precisam acompanhar.

## Pendente de calibração

- **Navegação por paredes/LiDAR**: ainda não implementada — hoje o movimento é só por tempo. Quando for implementada, este arquivo deve ganhar uma seção com os limiares de distância às paredes, ganho do controlador (P/PID), e setores angulares usados do `/scan`.
- **Waypoints reais da mesa**: o `NUM_PARADAS = 2` e o `TEMPO_1M` assumem uma mesa específica — recalibrar sempre que o layout físico mudar (mesa diferente, posição da base do copo, etc).