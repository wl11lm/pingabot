# Bill of Materials (BOM) - PingaBot

Este documento contém a lista completa de componentes, módulos e materiais utilizados no desenvolvimento do **PingaBot**, um carrinho garçom autônomo de doses baseado em ROS 2 e Micro-ROS.

## 1. Processamento e Controle

| Item | Componente | Descrição | Qtd | Preço Unit. (R$) | Total (R$) | Link/Referência |
| :--- | :--- | :--- | :---: | :---: | :---: | :--- |
| 1.1 | Raspberry Pi 4 | Processamento central: ROS 2 (Humble), visão computacional (detecção de mão levantada via webcam) e orquestração da missão (`mission_manager_node`). | 1 | - | - | |
| 1.2 | Raspberry Pi Pico | Controle de baixo nível via Micro-ROS: acionamento dos 4 motores mecanum, relé da bomba e buzzer; leitura do KY-003. | 1 | - | - | |
| 1.3 | Shield para Raspberry Pi Pico | Placa de expansão que facilita o acesso aos GPIOs e a fiação dos módulos (motores, relé, buzzer, KY-003). | 1 | - | - | |
| 1.4 | Push button (BOOTSEL) | Botão externo soldado ao pino BOOTSEL da Pico, permite entrar em modo de gravação sem abrir a carcaça. | 1 | - | - | |
| 1.5 | Push button (RESET) | Botão externo ligado ao RUN/RESET da Pico, permite reiniciar o firmware sem desconectar cabos. | 1 | - | - | |
| 1.6 | ESP32 devkit | Leitura do LiDAR LDS02RR e repasse dos dados via UART para a Raspberry Pi com o software do Kaia.ai. | 1 | - | - | |
| 1.7 | Cartão MicroSD (pelo menos 32GB Class 10) | Armazenamento do sistema operacional (Ubuntu Server + ROS 2 Humble) da Raspberry Pi. | 1 | - | - | |

## 2. Sensores, Visão e Sinalização

| Item | Componente | Descrição | Qtd | Preço Unit. (R$) | Total (R$) | Link/Referência |
| :--- | :--- | :--- | :---: | :---: | :---: | :--- |
| 2.1 | Câmera USB (Webcam) | Captura de imagem para detecção de mão levantada (pipeline Python na Raspberry Pi). | 1 | - | - | |
| 2.2 | LiDAR LDS02RR (com adaptador makerspet) | Leitura 2D de distância para seguir a linha virtual usando as paredes da mesa como referência. | 1 | - | - | |
| 2.3 | Sensor de efeito Hall KY-003 (Hall A3144) | Detecta o ímã na base do copo para disparar a dosagem automática (lido via ADC na Pico). | 1 | - | - | |
| 2.4 | Buzzer 3V | Sinalização sonora de 1s ao detectar mão levantada (GPIO27 da Pico, via `/pedido_dose`). | 1 | - | - | |

## 3. Locomoção e Atuadores

| Item | Componente | Descrição | Qtd | Preço Unit. (R$) | Total (R$) | Link/Referência |
| :--- | :--- | :--- | :---: | :---: | :---: | :--- |
| 3.1 | Motor DC comum | Tração omnidirecional (mixagem mecanum em software na Pico). | 4 | - | - | |
| 3.2 | Driver de motor TB6612FNG | Controle de sentido (IN1/IN2) e velocidade (PWM) - cada módulo controla 2 motores. | 2 | - | - | |
| 3.3 | Roda mecanum | Conjunto de rodas com roletes em 45° para movimento omnidirecional. | 4 | - | - | |

## 4. Sistema de Dispensa (Dose)

| Item | Componente | Descrição | Qtd | Preço Unit. (R$) | Total (R$) | Link/Referência |
| :--- | :--- | :--- | :---: | :---: | :---: | :--- |
| 4.1 | Bomba de líquido | Dosagem da bebida, acionada pela Pico via relé, de forma autônoma (KY-003) ou manual (`/bomba_dose`). | 1 | - | - | |
| 4.2 | Módulo relé 1 canal (com optoacoplador) | Chaveamento da bomba a partir do GPIO da Pico. **Alimentar a bobina (JD-VCC) direto da bateria de potência, sem unir o GND com a lógica** (ver `docs/troubleshooting.md`). | 1 | - | - | |
| 4.3 | Mangueira de silicone alimentar | Tubulação para transporte do líquido até o copo. | ~0.5m | - | - | |

## 5. Alimentação e Gerenciamento de Energia

| Item | Componente | Descrição | Qtd | Preço Unit. (R$) | Total (R$) | Link/Referência |
| :--- | :--- | :--- | :---: | :---: | :---: | :--- |
| 5.1 | Baterias 18650 | Alimentação do circuito de potência (relé/bomba e motores), **isolada** da alimentação lógica da Pico. | 2 | - | - | |
| 5.2 | Regulador de tensão Buck (Step-Down) LM2596 | Alimentação dos motores/drivers, bomba e relés em 5V a partir da bateria principal. | 1 | - | - | |
| 5.3 | Módulo de carga TP5100 (2 células) + BMS 2S | Carregamento e proteção do pack de baterias 18650. | 1 | - | - | |
| 5.4 | Powerbank USB (para a Pi) | Alimenta a Raspberry Pi, que por sua vez alimenta webcam, LiDAR e Pico via USB - separado da bateria de potência (mesmo GND, sem compartilhar trilha de corrente alta). | 1 | - | - | |
| 5.5 | Capacitor eletrolítico (1000µF) | Absorve o pico de corrente de partida da bomba/motores - 1 por driver, próximo à carga. | 1 por driver | - | - | |
| 5.6 | Diodo 1N4007 | Proteção contra tensão reversa/flyback na bomba. | 1 | - | - | |
| 5.7 | Chave Liga/Desliga (Interruptor) | Corte geral de energia do circuito de potência. | 1 | - | - | |

## 6. Estrutura, Impressão 3D e Miscelânea

| Item | Componente | Descrição | Qtd | Preço Unit. (R$) | Total (R$) | Link/Referência |
| :--- | :--- | :--- | :---: | :---: | :---: | :--- |
| 6.1 | Chassi (metal e impressão 3D) | Estrutura mecânica que comporta eletrônica, reservatório e base do copo. Peças em `3d_models/`. | 1 conjunto | - | - | |
| 6.2 | Jumpers (Macho/Fêmea) | Fiação de sinal entre Pico, sensores e módulos. | - | - | - | |
| 6.3 | Cabo USB ou UART (Pico↔RPi4, ESP32↔RPi4) | Comunicação serial entre os três controladores. | 2 | - | - | |
| 6.4 | Parafusos, porcas e espaçadores M3 | Fixação de placas e suportes no chassi. | - | - | - | |
| 6.5 | Ímã de neodímio (para a base do copo) | Acionador do KY-003 - fixado embaixo do copo. | 1+ | - | - | |

---
<!-- 
## Resumo Financeiro

* **Total Estimado em Hardware:** R$ 0,00
* **Total em Estrutura/Insumos:** R$ 0,00
* **Custo Total do Projeto:** **R$ 0,00** -->
