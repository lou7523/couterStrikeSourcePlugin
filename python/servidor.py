import socket
import json
import csv
import os

HOST = "127.0.0.1"
PORTA = 5005

NUM_RAIOS = 7
ALCANCE = 500.0
DIST_SALTAR = 48.0
ROTACAO_MAX = 5.0
VELOCIDADE_MAX = 150.0
FICHEIROS_RESULTADOS = "resultados.csv"
NOMES_FIM = {1: "SAIDA", 2: "TEMPO", 3: "INTERROMPIDA"} 


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

def guardar_resultado(linha):
    novo = not os.path.exists(FICHEIROS_RESULTADOS)
    with open(FICHEIROS_RESULTADOS, "a", newline="") as f:
        escritor = csv.writer(f)
        if novo:
            escritor.writerow(["episodio", "resultado", "tempo", "colisoes", "distancia_minima"])
        escritor.writerow(linha)

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
    episodio_atual = None
    colisoes = 0
    distancia_minima = None

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
            if mensagem["ep"] != episodio_atual:
                episodio_atual = mensagem["ep"]
                colisoes = 0
                distancia_minima = mensagem["dsaida"]

            colisoes += mensagem["bateu"]
            distancia_minima = min(distancia_minima, mensagem["dsaida"])

            rotacao, velocidade, saltar = decidir(mensagem["dist"], mensagem["salto"])

            resposta = {"rot": round(rotacao, 3), "vel": round(velocidade, 1), "saltar": int(saltar)}
            try:
                ligacao.sendall((json.dumps(resposta) + "\n").encode("utf-8"))
            except (ConnectionResetError, BrokenPipeError):
                break

            if mensagem["fim"] != 0:
                nome = NOMES_FIM.get(mensagem["fim"], "?")
                print(f"[Cerebro] Tentativa {episodio_atual}: {nome} em {mensagem['t']:.1f} s"
                      f"  (colisoes: {colisoes}, mais perto da saida: {distancia_minima:.0f})")
                guardar_resultado([episodio_atual, nome, round(mensagem["t"], 2), colisoes, distancia_minima])

            if contador % 66 == 0:
                print(f"[Cerebro] tentativa {mensagem['ep']}    t {mensagem['t']:5.1f}   "
                      f"saida a {mensagem['dsaida']:5.0f}   rot {rotacao:5.2f}  vel {velocidade:5.1f}")
    ligacao.close() 