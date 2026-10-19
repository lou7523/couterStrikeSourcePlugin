import socket
import json

HOST = "127.0.0.1"
PORTA = 5005

NUM_RAIOS = 7
ALCANCE = 500.0
DIST_SALTAR = 48.0
ROTACAO_MAX = 5.0
VELOCIDADE_MAX = 150.0


def decidir(dist, salto):
    metade = NUM_RAIOS // 2

    direita = sum(dist[0:metade]) / metade
    esquerda = sum(dist[metade + 1:NUM_RAIOS]) / metade
    frente = dist[metade]

    rotacao = ROTACAO_MAX * (esquerda - direita) / ALCANCE
    if frente < 80:
        if esquerda >= direita:
            rotacao += ROTACAO_MAX
        else:
            rotacao -= ROTACAO_MAX

    velocidade = VELOCIDADE_MAX * (frente / ALCANCE)

    saltar = False
    for i in range(metade - 1, metade + 2):
        if salto[i] < DIST_SALTAR:
            saltar = True

    return rotacao, velocidade, saltar


servidor = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
servidor.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
servidor.bind((HOST, PORTA))
servidor.listen(1)

while True:
    print(f"[Cerebro] A espera do corpo na porta {PORTA}...")
    ligacao, endereco = servidor.accept()
    print(f"[Cerebro] Ligado com {endereco}")

    buffer = ""
    contador = 0
    while True:
        try:
            dados = ligacao.recv(4096)
        except ConnectionResetError:
            dados = b""

        if not dados:
            print("[Cerebro] Corpo desligado")
            break

        buffer += dados.decode("utf-8")

        while "\n" in buffer:
            linha, buffer = buffer.split("\n", 1)
            mensagem = json.loads(linha)
            contador += 1

            rotacao, velocidade, saltar = decidir(mensagem["dist"], mensagem["salto"])

            resposta = {"rot": round(rotacao, 3), "vel": round(velocidade, 1), "saltar": int(saltar)}
            try:
                ligacao.sendall((json.dumps(resposta) + "\n").encode("utf-8"))
            except (ConnectionResetError, BrokenPipeError):
                break

            if contador % 33 == 0:
                print(f"[Cerebro] dist {mensagem['dist']}  ->  rot {rotacao:5.2f}  vel {velocidade:5.1f}  saltar {saltar}")

    ligacao.close() 