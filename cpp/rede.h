bool RedeReceberLinha(char* destino, int tamanho);

#ifndef REDE_H
#define REDE_H

bool RedeIniciar();                 //prepara o Winsock (chamar uma vez, no Load)
void RedeTerminar();                //fecha tudo
void RedeTentarLigar(int porta);    //tenta ligar ao cerebro neste PC, sem travar o jogo
bool RedeLigado();                  //devolve true se a ligacao estiver pronta
bool RedeEnviar(const char* texto); //envia texto (sem bloquear)

#endif