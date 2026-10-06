import socket
import json
import time

cliente = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
cliente.connect(("127.0.0.1", 5005))
print("[Teste] Ligacao ao cerebro. ")

for tick in range(10):
    mensagem = {"tick": tick, "distancias": [500, 320, 210, 150, 90, 60, 45]}
    texto = json.dumps(mensagem) + "\n"
    cliente.sendall(texto.encode("utf-8"))
    print("[Teste] Enviei o tick", tick)
    time.sleep(0.5)

cliente.close()