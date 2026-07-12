# Troubleshooting — PingaBot

Problemas já enfrentados e resolvidos (ou em observação), documentados para não precisarmos redescobrir tudo de novo. Se resolver algo novo relevante, adicione aqui.

## 1. Bomba/relé resetava a Pico (mesmo com optoacoplador)

**Sintoma**: acionar o relé da bomba — mesmo manualmente, com um único fio — reiniciava a Pico.

**Causa raiz**: o optoacoplador do módulo relé isola apenas o **sinal de controle** (GPIO → bobina do relé). Como a bateria 18650 (alimentação da bomba) e a Pico (alimentada via USB) **compartilhavam o mesmo GND**, o pico de corrente de partida da bomba causava *ground bounce* — uma queda de tensão momentânea no fio de GND compartilhado — que a Pico (e o link USB, sensível a essa referência) sentia como um brownout e resetava.

Isso **não é o mesmo problema** que o diodo de flyback resolve (que protege contra tensão reversa ao desligar a *bobina do relé*, não contra o inrush de corrente da carga).

**Correção aplicada**:
- Verificar/remover o jumper `VCC`/`JD-VCC` do módulo relé, se existir — alimentar a bobina do relé (`JD-VCC`) direto pela bateria de potência, não pela Pico.
- Auditar toda fiação de GND: o (−) da bateria de potência **não pode tocar** o GND da Pico/USB em nenhum ponto.
- Capacitor eletrolítico de bulk (1000-2200µF) perto dos terminais da bomba, para absorver o pico de corrente de partida.
- Fios da bomba trançados e curtos, afastados do cabo USB/UART (reduz acoplamento por EMI).

**Como confirmar se o problema voltou**: com o circuito desenergizado, medir continuidade (multímetro em modo de resistência) entre o GND da Pico e o (−) da bateria de potência. Se marcar ~0Ω, o link indevido está de volta.

## 2. Raspberry Pi 4 deu undervoltage ao ligar o LiDAR

**Sintoma**: aviso de undervoltage na Raspberry Pi no momento em que o LiDAR (motor interno) começa a girar.

**Causa**: mesmo fenômeno do item 1 — pico de corrente de partida do motor do LiDAR, alimentado pela mesma fonte/trilha da Raspberry Pi.

**Correção**: alimentar o LiDAR/ESP32 por uma fonte separada da Raspberry Pi, nunca direto do 5V dela. Undervoltage recorrente na Pi pode corromper o cartão SD com o tempo — não é só um aviso cosmético.

## 3. `/cmd_vel` acima de ~0.5 causava reset da Pico

**Sintoma**: publicar `linear.x` acima de aproximadamente 0.5 fazia a Pico reiniciar no meio do movimento.

**Causa provável**: mesma família de problema dos itens 1 e 2 — 4 motores partindo simultaneamente em PWM alto puxam corrente de pico suficiente para derrubar a tensão que alimenta a Pico, se a alimentação de lógica e de potência não estiverem bem separadas.

**Status**: `mission_node.py` atualmente usa `VELOCIDADE = 0.8` (ver `docs/calibration.md`) — **maior** que o limite observado originalmente. Isso só é seguro se a separação de alimentação (lógica vs. potência, capacitor de bulk) já tiver sido aplicada e **revalidada** em bancada. Se ainda não foi revalidado, teste em incrementos (0.3 → 0.5 → 0.8) antes de confiar nesse valor em demonstração ao vivo.

**Mitigação permanente recomendada**: saturar a velocidade máxima no próprio firmware da Pico (não só confiar em nunca publicar um valor alto), como cinto de segurança:
```c
#define MAX_SPEED 0.8f  // ajustar conforme validado em bancada
// dentro de set_motor():
if (speed >  MAX_SPEED) speed =  MAX_SPEED;
if (speed < -MAX_SPEED) speed = -MAX_SPEED;
```

## 4. `mission_node.py` travava e não processava mais nada

**Sintoma**: a máquina de estados da missão parava de reagir a qualquer tópico no meio da execução.

**Causa**: uso de `time.sleep()` (ou `rate.sleep()`) **dentro de uma callback** do nó. Por padrão o `rclpy.spin()` roda single-threaded — enquanto uma callback não retorna, nenhuma outra é processada, inclusive as que entregariam a informação que o próprio código estava esperando.

**Correção aplicada**: reescrever como máquina de estados dirigida por `timer` (10Hz), onde cada callback só atualiza variáveis de estado (instantâneo, nunca bloqueia) e um `tick()` periódico decide as transições comparando `time.time()` — o mesmo padrão não-bloqueante usado no firmware da Pico (`pump_update()`, `doser_update()`, `buzzer_update()`).

**Regra geral**: nunca usar `sleep()` bloqueante dentro de callback ROS2 nem dentro do loop principal da Pico. Sempre "checar quanto tempo passou desde X", nunca "esperar X segundos".

## 5. Checklist de debug ROS2 (sem câmera/lidar disponíveis)

```bash
# o que está rodando
ros2 node list
ros2 node info /mission_manager

# tópicos disponíveis e tipo
ros2 topic list -t

# ver mensagens em tempo real
ros2 topic echo /bomba_evento
ros2 topic echo /cmd_vel --once      # 1 mensagem e sai — bom para /scan também

# só a frequência, sem poluir o terminal (ótimo para /scan)
ros2 topic hz /scan

# quantos publishers/subscribers existem num tópico
ros2 topic info /pedido_dose

# publicar manualmente
ros2 topic pub --once /pedido_dose std_msgs/msg/Empty "{}"
ros2 topic pub --once /bomba_evento std_msgs/msg/Int32 "{data: 1}"
ros2 topic pub --rate 10 /cmd_vel geometry_msgs/msg/Twist "{linear: {x: 0.3}}"

# ver os campos de um tipo de mensagem
ros2 interface show geometry_msgs/msg/Twist
```

Fluxo típico para testar o `mission_manager` isolado (robô no ar, sem tocar o chão): `ros2 topic echo /cmd_vel` num terminal, dispara `/pedido_dose` no outro, e no meio da rota simula `/bomba_evento` (`{data: 1}` seguido de `{data: 0}` alguns segundos depois) para ver se ele espera certo antes de continuar.

## 6. Leitura do LiDAR via minicom (debug bruto, fora do ROS)

```bash
sudo minicom -D /dev/ttyUSB0 -b 115200
```
Mostra o texto cru que o firmware da ESP32 imprime (`ângulo distância_mm ângulo`, `Scan completed; scans-per-second X.XX`). Útil para confirmar que o lidar está respondendo **antes** de suspeitar do `lidar_serial_node.py`.