#!/usr/bin/env python3
import rclpy
from rclpy.node import Node
from geometry_msgs.msg import Twist
from std_msgs.msg import Empty, Int32
import time
from enum import Enum, auto

VELOCIDADE = 0.8
TEMPO_1M = 1.8
NUM_PARADAS = 2
TEMPO_ESPERA_COPO = 6.0

class Estado(Enum):
    IDLE = auto()
    INDO = auto()
    AGUARDANDO_COPO = auto()
    DOSANDO = auto()
    RETORNANDO = auto()

class MissionManager(Node):
    def __init__(self):
        super().__init__('mission_manager')
        self.cmd_pub = self.create_publisher(Twist, '/cmd_vel', 10)
        self.create_subscription(Empty, '/pedido_dose', self.on_pedido, 10)
        self.create_subscription(Int32, '/bomba_evento', self.on_bomba_evento, 10)

        self.estado = Estado.IDLE
        self.dose_em_andamento = False
        self.parada_atual = 0
        self.estado_inicio_ts = 0.0

        # Timer principal: roda a FSM a 10Hz. NUNCA colocar sleep aqui dentro.
        self.create_timer(0.1, self.tick)

        self.get_logger().info('mission_manager pronto. Aguardando /pedido_dose.')

    # ---------------- Callbacks (rapidas, so atualizam estado) ----------------
    def on_pedido(self, msg):
        if self.estado != Estado.IDLE:
            self.get_logger().info('Pedido ignorado, ja em rota.')
            return
        self.get_logger().info('Pedido recebido! Iniciando rota.')
        self.parada_atual = 0
        self.mudar_estado(Estado.INDO)

    def on_bomba_evento(self, msg):
        self.dose_em_andamento = (msg.data == 1)

    # ---------------- Helpers ----------------
    def publicar_vel(self, x):
        t = Twist()
        t.linear.x = x
        self.cmd_pub.publish(t)

    def mudar_estado(self, novo):
        self.get_logger().info(f'{self.estado.name} -> {novo.name}')
        self.estado = novo
        self.estado_inicio_ts = time.time()

    def tempo_no_estado(self):
        return time.time() - self.estado_inicio_ts

    # ---------------- FSM: chamada a cada 0.1s, nunca bloqueia ----------------
    def tick(self):
        if self.estado == Estado.IDLE:
            self.publicar_vel(0.0)

        elif self.estado == Estado.INDO:
            self.publicar_vel(VELOCIDADE)
            if self.tempo_no_estado() >= TEMPO_1M:
                self.publicar_vel(0.0)
                self.parada_atual += 1
                self.mudar_estado(Estado.AGUARDANDO_COPO)

        elif self.estado == Estado.AGUARDANDO_COPO:
            self.publicar_vel(0.0)
            if self.dose_em_andamento:
                self.mudar_estado(Estado.DOSANDO)
            elif self.tempo_no_estado() >= TEMPO_ESPERA_COPO:
                self.get_logger().info('Ninguem colocou copo, seguindo.')
                self.avancar_ou_retornar()

        elif self.estado == Estado.DOSANDO:
            self.publicar_vel(0.0)
            if not self.dose_em_andamento:
                self.get_logger().info('Dose concluida.')
                self.avancar_ou_retornar()

        elif self.estado == Estado.RETORNANDO:
            self.publicar_vel(-VELOCIDADE)
            if self.tempo_no_estado() >= TEMPO_1M * NUM_PARADAS:
                self.publicar_vel(0.0)
                self.mudar_estado(Estado.IDLE)
                self.get_logger().info('Rota concluida. Aguardando proxima mao.')

    def avancar_ou_retornar(self):
        if self.parada_atual < NUM_PARADAS:
            self.mudar_estado(Estado.INDO)
        else:
            self.mudar_estado(Estado.RETORNANDO)

def main():
    rclpy.init()
    rclpy.spin(MissionManager())

if __name__ == '__main__':
    main()
