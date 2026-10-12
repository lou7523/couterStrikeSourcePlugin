import socket
import json

HOST = "127.0.0.1"
PORTA = 5005

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

            if contador % 33 == 0:
                print(f"[Cerebro] tick {mensagem['tick']}  dist {mensagem['dist']}  salto {mensagem['salto']}")

    ligacao.close()