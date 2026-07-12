#!/usr/bin/env python3
import time
import cv2
import mediapipe as mp
import rclpy
from rclpy.node import Node
from std_msgs.msg import Empty


class HandDetectorNode(Node):

    # ─── Índices dos landmarks (MediaPipe Hand) ────────────────────────────
    # Ponta dos dedos: 4 (polegar), 8 (indicador), 12 (médio), 16 (anelar), 20 (mínimo)
    # Base dos dedos: 3 (polegar), 6 (indicador), 10 (médio), 14 (anelar), 18 (mínimo)
    TIPS = [4, 8, 12, 16, 20]
    BASES = [3, 6, 10, 14, 18]

    # Resolução reduzida = menos custo de processamento por frame.
    # 320x240 é suficiente para reconhecer o gesto a curta/média distância.
    LARGURA = 320
    ALTURA = 240

    # Tempo mínimo (em segundos) entre dois disparos consecutivos
    COOLDOWN_S = 3.0

    def __init__(self):
        super().__init__('hand_detector_node')

        # ─── Publisher ROS2 ────────────────────────────────────────────────
        self.pub = self.create_publisher(Empty, '/pedido_dose', 10)

        # ─── Controle de estado e cooldown ────────────────────────────────
        self.hand_was_up = False
        self.last_trigger_ts = 0.0

        # ─── Inicialização do MediaPipe ────────────────────────────────────
        # max_num_hands=1: só precisamos detectar 1 mão, reduz custo de inferência.
        # model_complexity=0: modelo mais leve (o padrão é 1), ideal para CPU do Pi.
        self.mp_hands = mp.solutions.hands
        self.hands = self.mp_hands.Hands(
            static_image_mode=False,
            max_num_hands=1,
            model_complexity=0,
            min_detection_confidence=0.7,
            min_tracking_confidence=0.6,
        )

        # ─── Captura de vídeo ───────────────────────────────────────────────
        self.cap = cv2.VideoCapture(0)
        self.cap.set(cv2.CAP_PROP_FRAME_WIDTH, self.LARGURA)
        self.cap.set(cv2.CAP_PROP_FRAME_HEIGHT, self.ALTURA)
        self.cap.set(cv2.CAP_PROP_FPS, 15)

        if not self.cap.isOpened():
            self.get_logger().error('Não foi possível abrir a câmera.')
            raise RuntimeError('Não foi possível abrir a câmera.')

        # Loop periódico (10 Hz — suficiente para o gesto, sem sobrecarregar a CPU)
        self.timer = self.create_timer(0.1, self.loop)
        self.get_logger().info('Hand Detector Node iniciado (modo headless). Publicando em /pedido_dose.')

    def dedos_levantados(self, landmarks, largura: int) -> list:
        """
        Retorna uma lista de 5 booleanos indicando se cada dedo está levantado.
        Ordem: [polegar, indicador, médio, anelar, mínimo]
        """
        levantado = []

        # Polegar — compara X (horizontal) porque dobra lateralmente
        polegar_ponta = landmarks[self.TIPS[0]].x * largura
        polegar_base = landmarks[self.BASES[0]].x * largura
        levantado.append(polegar_ponta < polegar_base)

        # Demais dedos — compara Y (vertical); ponta acima da base = levantado
        for i in range(1, 5):
            ponta = landmarks[self.TIPS[i]].y
            base = landmarks[self.BASES[i]].y
            levantado.append(ponta < base)

        return levantado

    def e_numero_1(self, levantado: list) -> bool:
        """
        Verifica se a configuração dos dedos representa o número 1:
        - Indicador levantado
        - Médio, anelar e mínimo abaixados
        - Polegar ignorado (posição varia muito entre pessoas)
        """
        _, indicador, medio, anelar, minimo = levantado
        return indicador and not medio and not anelar and not minimo

    def detectar_mao(self, frame) -> bool:
        """
        Retorna True se detectar o gesto do número 1 no frame.
        Não desenha landmarks nem faz qualquer render — só processa e responde.
        """
        h, w, _ = frame.shape
        rgb = cv2.cvtColor(frame, cv2.COLOR_BGR2RGB)
        resultado = self.hands.process(rgb)

        if not resultado.multi_hand_landmarks:
            return False

        for hand_lm in resultado.multi_hand_landmarks:
            levantado = self.dedos_levantados(hand_lm.landmark, w)
            if self.e_numero_1(levantado):
                return True

        return False

    def loop(self):
        ret, frame = self.cap.read()
        if not ret:
            self.get_logger().warn('Falha ao capturar frame da câmera.')
            return

        hand_up = self.detectar_mao(frame)

        # Dispara apenas na transição "sem gesto" → "gesto detectado"
        # E respeita o cooldown mínimo entre disparos
        if hand_up and not self.hand_was_up:
            agora = time.time()
            if agora - self.last_trigger_ts >= self.COOLDOWN_S:
                self.get_logger().info('Gesto (número 1) detectado! Disparando pedido de dose.')
                self.pub.publish(Empty())
                self.last_trigger_ts = agora

        self.hand_was_up = hand_up

    def destroy_node(self):
        self.cap.release()
        self.hands.close()
        super().destroy_node()


def main(args=None):
    rclpy.init(args=args)
    node = HandDetectorNode()
    try:
        rclpy.spin(node)
    except KeyboardInterrupt:
        pass
    finally:
        node.destroy_node()
        rclpy.shutdown()


if __name__ == '__main__':
    main()
