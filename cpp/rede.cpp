#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#include <string.h>
#include "rede.h"

#pragma comment(lib, "Ws2_32.lib")

const int DESLIGADO = 0;
const int A_LIGAR = 1;
const int LIGADO = 2;

static SOCKET g_socket = INVALID_SOCKET;
static int g_estado = DESLIGADO;
static bool g_iniciado = false;

static void Fechar() {
    if (g_socket != INVALID_SOCKET) {
        closesocket(g_socket);
        g_socket = INVALID_SOCKET;
    }
    g_estado = DESLIGADO;
}

bool RedeIniciar() {
    WSADATA wsa;
    g_iniciado = (WSAStartup(MAKEWORD(2, 2), &wsa) == 0);
    return g_iniciado;
}

void RedeTerminar() {
    Fechar();
    if (g_iniciado)
        WSACleanup();
    g_iniciado = false;
}

void RedeTentarLigar(int porta) {
    if (!g_iniciado || g_estado != DESLIGADO)
    return;

    g_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (g_socket == INVALID_SOCKET)
    return;

    u_long naoBloqueante = 1;
    ioctlsocket(g_socket, FIONBIO, &naoBloqueante);

    int sim = 1;
    setsockopt(g_socket, IPPROTO_TCP, TCP_NODELAY, (const char*)&sim, sizeof(sim));

    sockaddr_in endereco;
    memset(&endereco, 0, sizeof(endereco));
    endereco.sin_family = AF_INET;
    endereco.sin_port = htons((u_short)porta);
    endereco.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    int r = connect(g_socket, (sockaddr*)&endereco, sizeof(endereco));
    if (r == 0)
        g_estado = LIGADO;
    else if (WSAGetLastError() == WSAEWOULDBLOCK)
        g_estado = A_LIGAR;
    else
        Fechar();
}

bool RedeLigado(){
    if (g_estado == A_LIGAR) {
        fd_set escrita, erros;
        FD_ZERO(&escrita);
        FD_ZERO(&erros);
        FD_SET(g_socket, &escrita);
        FD_SET(g_socket, &erros);
        timeval zero = {0, 0};

        int r = select(0, NULL, &escrita, &erros, &zero);
            if (r < 0 || FD_ISSET(g_socket, &erros))
        Fechar();
            else if (r > 0 && FD_ISSET(g_socket, &escrita))
            g_estado = LIGADO;
    }

    return g_estado == LIGADO;
}

bool RedeEnviar(const char* texto){
    if (!RedeLigado())
    return false;

    int r = send(g_socket, texto, (int)strlen(texto), 0);
    if (r == SOCKET_ERROR) {
        if (WSAGetLastError() != WSAEWOULDBLOCK)
            Fechar();
        return false;
    }
    return true;
}