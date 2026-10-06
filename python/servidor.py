import socket
import json

HOST = "127.0.0.1"
PORTA = 5005

servidor = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
servidor.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
servidor.bind((HOST, PORTA))
servidor.listen(1)
print(f"[Cerebro] A espera do corpo na porta {PORTA}...")

ligacao, endereco = servidor.accept()
print(f"[Cerebro] Liga com {endereco}")

buffer = ""
while True:
    dados = ligacao.recv(4096)
    if not dados:
        print("[Cerebro] Corpo desligado")
        break

    buffer += dados.decode("utf-8")

    while "\n" in buffer:
        linha, buffer = buffer.split("\n", 1)
        mensagem = json.loads(linha)
        print(f"[Cerebro] Recebido: {mensagem}")

ligacao.close()
servidor.close()