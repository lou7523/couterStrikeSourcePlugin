//Os includes dizem ao compilador que partes do SDK vamos usar
//Os mais importantes são:

#include <stdio.h>
#include <string.h>
#include <math.h>						//para usar funções matemáticas como sin, cos
#include "interface.h"
#include "filesystem.h"
#include "engine/iserverplugin.h"		//define a forma como o plugin vai se comunicar com a engine	
#include "eiface.h"						//tem as funções gerais do servidor
#include "convar.h"						//permite criar comandos e variáveis de consola
#include "engine/IEngineTrace.h"		//ajuda a criar colisões e traços (olhos da mosca)
#include "engine/ivdebugoverlay.h"		//permite desenhar linhas no ecrã, para vermos os raios no jogo
#include "tier2/tier2.h"
#include "game/server/iplayerinfo.h"	//toda a informação sobre os jogadores e bots

#include "tier0/memdbgon.h"				//tem de ser sempre o último include, 
										//porque troca o sistema de memória pelo do motor.

//Interfaces do motor
/*
	O motor do jogo é como uma caixa de ferramentas, e as interfaces são as ferramentas que podemos usar.
	Aqui vamos criar vários ponteiros NULL para cada menu, e mais para a frente no Load, vamos pedir à engine para nos dar a ferramenta que queremos,
	e vamos guardar o ponteiro dela na variável correspondente.
*/
 
IVEngineServer* engine = NULL;
IPlayerInfoManager* playerinfomanager = NULL;
IBotManager* botmanager = NULL;					//Serve para criar bots
IServerPluginHelpers* helpers = NULL;			//Para enviar comandos em nome do bot
IEngineTrace* enginetrace = NULL;				//para as colisoes e traços (olhos da mosca)
CGlobalVars* gpGlobals = NULL;					//para saber o tempo, o número de jogadores...
IVDebugOverlay* debugoverlay = NULL;			//para desenhar linhas no ecrã, para vermos os raios no jogo

//Estado do Bot
/*
	Aqui guardamos tudo oq precisamos de saber sobre o bot entre ticks, para não termos de ir buscar à engine
	cada vez que precisamos de alguma coisa.

*/
static edict_t* g_pBotEdict = NULL;							//é a ficha "técnica" do bot
static IBotController* g_pBot = NULL;						
static IPlayerInfo* g_pBotInfo = NULL;						//ficha do bot, para saber se ele está vivo, se está a jogar...
static float g_flYaw = 0.0f;						        //para saber para onde o bot está a olhar em graus
static float g_flProximaTentativaEquipa = 0.0f;

/*
	7 raios espalhados por 120º, cada um com um alcance de 500 unidades (cerca de 9,5m)
	Saem a 40 unidades de altura, à cintura, para detetarem também caixas baixas e não so paredes.
  */

const int NUM_RAIOS = 7;									//quantos raios vamos disparar para ver se há paredes à frente do bot
const float ANGULO_ABERTURA = 120.0f;						//qual é o ângulo de abertura dos raios, em graus
const float ALCANCE_RAIO = 500.0f;							//qual é o alcance dos raios, em unidades do jogo
const float ALTURA_DEGRAU = 18.0f;							//o bot já consegue subir degraus sozinho ate 18 unidade
const float AVANCO_MINIMO = 10.0f;							//se depois de subir avanca menos que isto, e uma parede
const int MAX_SUBIDAS = 24;									//quantos degraus um raio pode subir no maximo

static float g_flDistancias[NUM_RAIOS];						//este array vai guardar a distancia de cada raio 
static float g_flProximoPrint = 0.0f;

static void LimparBot()										//esquece o bot quando ele desaparece, para nunca usarmos 
//um ponteiro para algo que já não existe(isso crasharia o jogo).
{
	g_pBotEdict = NULL;
	g_pBot = NULL;
	g_pBotInfo = NULL;
}

//Variáveis da consola
/*Uma ConVar é uma variável de consola que pode ser alterada sem recompilar o jogo*/

//Os argumentos são: nome da variável, valor por defeito, flags (0 = normal), descrição (útil para testar)
ConVar mosca_velocidade("mosca_velocidade", "150", 0, "Velocidade para a frente do bot (0 a 250)");			
ConVar mosca_rotacao("mosca_rotacao", "5", 0, "Graus que o bot roda em cada tick");						//maximo que e preciso rodar para desviar
ConVar mosca_debug("mosca_debug", "1", 0, "1 = desenha os raios e mostra as distancias na consola");	//liga e desliga os desenhos dos raios

//Comando para criar o bot
/*
	Esta função ocorre quando queremos spawnar a moscar na consola.

	Primeiro o código verifica se temos acesso ao botmanager / playerinfomanager / se o bot já existe,
	estas informações são necessárias para criar o bot.
*/
void MoscaSpawn_f()
{
	if (!botmanager || !playerinfomanager)
	{
		Warning("[Mosca] Nao tenho acesso ao botmanager, nao consigo criar o bot.\n");
		return;
	}
	if (g_pBotEdict)
	{
		Msg("[Mosca] O bot ja existe.\n");
		return;
	}

	g_pBotEdict = botmanager->CreateBot("Mosca");								//se tudo correr bem cria o bot e gurada o ponteiro para a ficha técnica dele na variável g_pBotEdict
	if (!g_pBotEdict)
	{
		Warning("[Mosca] Falha ao criar o bot (o servidor esta cheio?).\n");
		LimparBot();
		return;
	}

	g_pBot = botmanager->GetBotController(g_pBotEdict);
	g_pBotInfo = playerinfomanager->GetPlayerInfo(g_pBotEdict);
	g_flYaw = 0.0f;
	g_flProximaTentativaEquipa = 0.0f;

	Msg("[Mosca] Bot criado!\n");
}
ConCommand cc_mosca_spawn("mosca_spawn", MoscaSpawn_f, "Cria o bot Mosca");		//Este vai ligar o texto mosca_spawn da consola à função MoscaSpawn_f

//Comando para remover o bot
/*
	Esta função ocorre quando queremos remover a moscar na consola.
	Primeiro o código verifica se temos acesso ao botmanager / playerinfomanager / se o bot já existe,
	estas informações são necessárias para criar o bot.

	Usamos o kickid para remover o bot, e depois chamamos a função LimparBot() para limpar os ponteiros do bot.

	Construimos o texto do comando com Q_snprintf, que é uma versão segura de sprintf, para evitar buffer overflow
	(Sprintf é inseguro porque não verifica o tamanho do buffer, e pode escrever mais do que o buffer suporta, causando crashes ou exploits).
*/

void MoscaRemover_f()
{
	if (!g_pBotEdict)
	{
		Msg("[Mosca] Nao ha nenhum bot para remover.\n");
		return;
	}

	char comando[64];
	Q_snprintf(comando, sizeof(comando), "kickid %d\n", engine->GetPlayerUserId(g_pBotEdict));
	engine->ServerCommand(comando);
	LimparBot();
	Msg("[Mosca] Bot removido.\n");
}
ConCommand cc_mosca_remover("mosca_remover", MoscaRemover_f, "Remove o bot Mosca");

/*
	Calculamos a posição dos olhos do bot, a posição do bot e a subida 40 unidades.

	Para cada raio, calculamos o desvio do angulo, o raio 0 fica a -60º (direita), o 3 fica a 0º (frente) e o 6 fica a +60º (esquerda).
	(No CS:S os angulos positivos vao para a esquerda e os negativos para a direita)

	Convertemos para radianos e calculamos o ponto final do raio com trignometria: X = cos(angulo) * alcance, Y = sin(angulo) * alcance

	TraceRay é o raycast: o motor percorre a linha e diz onde bateu. O filtro faz o raio ignorar jogadores e só ver paredes e objetos.

	O fraction vai de 0 (bateu) 1 (não bateu), multiplicamos pelo alcance para saber a distância real do raio.

	Com o debug ligado, desenhamos o raio: verde se está livre, vermelho se há parede perto.
*/

static float MedirRaio(const Vector& pes, float anguloRad) {
	float dx = cosf(anguloRad);
	float dy = sinf(anguloRad);

	Vector pos(pes.x, pes.y, pes.z + ALTURA_DEGRAU);
	float percorrido = 0.0f;
	bool livre = false;

	Vector segInicio[MAX_SUBIDAS];
	Vector segFim[MAX_SUBIDAS];
	int numSeg = 0;

	int s;
	for (s = 0; s < MAX_SUBIDAS; s++) {
		float resta = ALCANCE_RAIO - percorrido;
		Vector fim(pos.x + dx * resta, pos.y + dy * resta, pos.z);

		Ray_t raio;
		raio.Init(pos, fim);
		CTraceFilterWorldAndPropsOnly filtro;
		trace_t tr;
		enginetrace->TraceRay(raio, MASK_PLAYERSOLID, &filtro, &tr);

		if (tr.startsolid)
			break;

		segInicio[numSeg] = pos;
		segFim[numSeg] = tr.endpos;
		numSeg++;

		float avanco = tr.fraction * resta;
		percorrido += avanco;

		if (tr.fraction >= 1.0f) {
			livre = true;
			break;
		}

		if (s > 0 && avanco < AVANCO_MINIMO)
			break;

		pos = Vector(tr.endpos.x - dx, tr.endpos.y - dy, tr.endpos.z + ALTURA_DEGRAU);
		percorrido -= 1.0f;
    }
	
	if (s == MAX_SUBIDAS)
		livre = true;

	if (percorrido < 0.0f)
		percorrido = 0.0f;

	float distancia = livre ? ALCANCE_RAIO : percorrido;

	if (mosca_debug.GetBool() && debugoverlay) {
		float fracao = distancia / ALCANCE_RAIO;
		int vermelho = (int)(255 * (1.0f - fracao));
		int verde = (int)(255 * fracao);
		for (int k = 0; k < numSeg; k++)
			debugoverlay->AddLineOverlay(segInicio[k], segFim[k], vermelho, verde, 0, true, 0.05f);

	}

	return distancia;
}

static void MoscaVer() {
	Vector pes = g_pBot->GetLocalOrigin();

	for (int i = 0; i < NUM_RAIOS; i++) {
		float desvio = -ANGULO_ABERTURA / 2.0f + i * (ANGULO_ABERTURA / (NUM_RAIOS - 1));
		float anguloRad = (g_flYaw + desvio) * 3.14159265f / 180.0f;

		g_flDistancias[i] = MedirRaio(pes, anguloRad);
	}
}
/*
	Faz a média dos 3 raios da direita e dos 3 da esquerda, olha para o do meio, e aplica três regras: 
	vira para o lado com mais espaço, vira com força se tem parede mesmo à frente (menos de 80 unidades), 
	e abranda quando há parede à frente. Na defesa, isto chama-se um veículo de Braitenberg
*/

static void MoscaDecidir(float& rotacao, float& velocidade) {
	int metade = NUM_RAIOS / 2;

	float direita = 0.0f;
	for (int i = 0; i < metade; i++) 
		direita += g_flDistancias[i];
	direita /= metade;

	float esquerda = 0.0f;
	for (int i = metade + 1; i < NUM_RAIOS; i++)
		esquerda += g_flDistancias[i];
	esquerda /= metade;

	float frente = g_flDistancias[metade];

	rotacao = mosca_rotacao.GetFloat() * (esquerda - direita) / ALCANCE_RAIO;

	if (frente < 80.0f)
		rotacao += (esquerda >= direita) ? mosca_rotacao.GetFloat() : -mosca_rotacao.GetFloat();

	velocidade = mosca_velocidade.GetFloat() * (frente / ALCANCE_RAIO);
}

//Tick
/*
	Um servidor de cs:source funciona por ticks, ou seja, este atualiza o mundo 66 vezes por segundo
	Esta função ocorre em cada tick e faz 3 coisas:
		- Verifica se o bot ainda existe, se não existir limpa os ponteiros dele.

		- Põe o bot numa equipa: No cs:source existem 3 equipas, 0 = nenhuma, 1 = espectador, 2 = terrorista e 3 = countra-terrorista.
								 Verificamos isto uma vez por segundo, para não sobrecarregar o servidor com comandos de equipa.

		- Mexe o bot: O CBotCmd é equivalente ao teclado e rato do bot naquele tick, fowardmove é igual a carregar no W e
					  viewangles é para onde o rato aponta. O yaw é o ângulo horizontal (o yaw muda conforme a rotação calculada
					  pelo MoscaDecidir). E o runPlayerMove entrega estes comandos à engine

*/
static void MoscaTick()
{
	if (!g_pBotEdict || !gpGlobals)
		return;

	if (g_pBotEdict->IsFree() || !g_pBotInfo || !g_pBotInfo->IsConnected())
	{
		LimparBot();
		return;
	}

	if (g_pBotInfo->GetTeamIndex() < 2 && gpGlobals->curtime >= g_flProximaTentativaEquipa)
	{
		helpers->ClientCommand(g_pBotEdict, "jointeam 3");
		helpers->ClientCommand(g_pBotEdict, "joinclass 0");
		g_flProximaTentativaEquipa = gpGlobals->curtime + 1.0f;
	}

	CBotCmd cmd;
	memset(&cmd, 0, sizeof(cmd));

	/*
	
	O ciclo completo: sentir (raios), decidir (rotação e velocidade), agir (aplicar ao bot). 
	Duas vezes por segundo, escreve as 7 distâncias na consola, da esquerda para a direita.

	*/

	if (!g_pBotInfo->IsDead())
	{
		MoscaVer();

		float rotacao, velocidade;
		MoscaDecidir(rotacao, velocidade);

		g_flYaw += rotacao;
		if (g_flYaw > 180.0f) g_flYaw -= 360.0f;
		if (g_flYaw < -180.0f) g_flYaw += 360.0f;
		cmd.forwardmove = velocidade;

		if (mosca_debug.GetBool() && gpGlobals->curtime >= g_flProximoPrint) {
			Msg("[Mosca] Esq %4.0f %4.0f %4.0f | %4.0f | %4.0f %4.0f %4.0f Dir  rot %5.2f  vel %5.1f\n",
				g_flDistancias[6], g_flDistancias[5], g_flDistancias[4], g_flDistancias[3],
				g_flDistancias[2], g_flDistancias[1], g_flDistancias[0], rotacao, velocidade);
			g_flProximoPrint = gpGlobals->curtime + 0.5f;
		}
	}

	cmd.viewangles = QAngle(0.0f, g_flYaw, 0.0f);
	g_pBot->SetLocalAngles(cmd.viewangles);
	g_pBot->RunPlayerMove(&cmd);
}

//A classe do plugin
/*
	A Valve tem uma classe abstrata, IServerPluginCallbacks, com uma lista de métodos que o motor chama em momentos especificos como:
	quando uma plugin carrega, a cada tick (GameFrame), quando o mapa acaba(LevelShutdown), quando um jogador entra ou sai do servidor(ClientDisconnect)...

	Como é abstrata, temos de implementar todos os métodos dela, mesmo que não façamos nada neles, esses ficam com {}.
	Os que devolvem PLUGIN_RESULT respondem com PLUGIN_CONTINUE, que significa que a engine deve continuar a processar o evento normalmente.

	Os que usamos normalmente são:
		GameFrame; MoscaTick; LevelShutdown; ClientDisconnect;

	A ultima linha é o que faz o código ser reconhecido como um plugin pela engine. (Exporta o código como DLL)

*/
class CMoscaPlugin : public IServerPluginCallbacks
{
public:
	virtual bool Load(CreateInterfaceFn interfaceFactory, CreateInterfaceFn gameServerFactory);
	virtual void Unload(void);
	virtual void Pause(void) {}
	virtual void UnPause(void) {}
	virtual const char* GetPluginDescription(void) { return "SimuladorPap - Mosca v0.2"; }
	virtual void LevelInit(char const* pMapName) {}
	virtual void ServerActivate(edict_t* pEdictList, int edictCount, int clientMax) {}
	virtual void GameFrame(bool simulating) { if (simulating) MoscaTick(); }
	virtual void LevelShutdown(void) { LimparBot(); }
	virtual void ClientActive(edict_t* pEntity) {}
	virtual void ClientDisconnect(edict_t* pEntity) { if (pEntity == g_pBotEdict) LimparBot(); }
	virtual void ClientPutInServer(edict_t* pEntity, char const* playername) {}
	virtual void SetCommandClient(int index) {}
	virtual void ClientSettingsChanged(edict_t* pEdict) {}
	virtual PLUGIN_RESULT ClientConnect(bool* bAllowConnect, edict_t* pEntity, const char* pszName, const char* pszAddress, char* reject, int maxrejectlen) { return PLUGIN_CONTINUE; }
	virtual PLUGIN_RESULT ClientCommand(edict_t* pEntity, const CCommand& args) { return PLUGIN_CONTINUE; }
	virtual PLUGIN_RESULT NetworkIDValidated(const char* pszUserName, const char* pszNetworkID) { return PLUGIN_CONTINUE; }
	virtual void OnQueryCvarValueFinished(QueryCvarCookie_t iCookie, edict_t* pPlayerEntity, EQueryCvarValueStatus eStatus, const char* pCvarName, const char* pCvarValue) {}
	virtual void OnEdictAllocated(edict_t* edict) {}
	virtual void OnEdictFreed(const edict_t* edict) {}
};

CMoscaPlugin g_MoscaPlugin;
EXPOSE_SINGLE_INTERFACE_GLOBALVAR(CMoscaPlugin, IServerPluginCallbacks, INTERFACEVERSION_ISERVERPLUGINCALLBACKS, g_MoscaPlugin);

//Load e Unload
/*
	O Load é o primeiro método que o motor chama. Este recebe duas fábricas:
		- Motor: interfaceFactory
		- Jogo:  gameServerFactory

	Pedimos a cada uma das interfaces o que precisamos, o nome e a versão (Se uma versão não existir, recebemos um NULL).
	Por isso escrevemos na consola se recebemos NULL ou não, para sabermos se o plugin vai funcionar.
	Se algo faltar , devolvemos false e a plugin não carrega.

	O ConVar_Register() é o que faz aparecer os nosso comandos e variáveis na consola.
	O Unload faz o contrário de tudo, limpa o bot, remove os comandos e desconecta as interfaces.
*/

bool CMoscaPlugin::Load(CreateInterfaceFn interfaceFactory, CreateInterfaceFn gameServerFactory)
{
	ConnectTier1Libraries(&interfaceFactory, 1);
	ConnectTier2Libraries(&interfaceFactory, 1);

	engine = (IVEngineServer*)interfaceFactory(INTERFACEVERSION_VENGINESERVER, NULL);
	helpers = (IServerPluginHelpers*)interfaceFactory(INTERFACEVERSION_ISERVERPLUGINHELPERS, NULL);
	enginetrace = (IEngineTrace*)interfaceFactory(INTERFACEVERSION_ENGINETRACE_SERVER, NULL);
	playerinfomanager = (IPlayerInfoManager*)gameServerFactory(INTERFACEVERSION_PLAYERINFOMANAGER, NULL);
	botmanager = (IBotManager*)gameServerFactory(INTERFACEVERSION_PLAYERBOTMANAGER, NULL);
	debugoverlay = (IVDebugOverlay*)interfaceFactory(VDEBUG_OVERLAY_INTERFACE_VERSION, NULL);

	Msg("[Mosca] ---- A carregar o plugin ----\n");
	Msg("[Mosca] engine:            %s\n", engine ? "OK" : "FALHOU");
	Msg("[Mosca] helpers:           %s\n", helpers ? "OK" : "FALHOU");
	Msg("[Mosca] enginetrace:       %s\n", enginetrace ? "OK" : "FALHOU");
	Msg("[Mosca] playerinfomanager: %s\n", playerinfomanager ? "OK" : "FALHOU");
	Msg("[Mosca] botmanager:        %s\n", botmanager ? "OK" : "FALHOU");
	Msg("[Mosca] debugoverlay:      %s\n", debugoverlay ? "OK" : "FALHOU (nao se ve os raios)");

	if (!engine || !helpers || !enginetrace || !playerinfomanager || !botmanager)
	{
		Warning("[Mosca] Falta uma interface essencial, o plugin nao vai carregar.\n");
		return false;
	}

	gpGlobals = playerinfomanager->GetGlobalVars();

	ConVar_Register(0);
	Msg("[Mosca] Plugin carregado. Escreve mosca_spawn para criar o bot.\n");
	return true;
}

void CMoscaPlugin::Unload(void)
{
	LimparBot();
	ConVar_Unregister();
	DisconnectTier2Libraries();
	DisconnectTier1Libraries();
}