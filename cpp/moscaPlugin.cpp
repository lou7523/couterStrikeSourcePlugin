//Os includes dizem ao compilador que partes do SDK vamos usar
//Os mais importantes são:

#include <stdio.h>
#include <string.h>
#include "interface.h"
#include "filesystem.h"
#include "engine/iserverplugin.h"		//define a forma como o plugin vai se comunicar com a engine	
#include "eiface.h"						//tem as funções gerais do servidor
#include "convar.h"						//permite criar comandos e variáveis de consola
#include "engine/IEngineTrace.h"		//ajuda a criar colisões e traços (olhos da mosca)
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

//Estado do Bot
/*
	Aqui guardamos tudo oq precisamos de saber sobre o bot entre ticks, para não termos de ir buscar à engine
	cada vez que precisamos de alguma coisa.

*/
static edict_t* g_pBotEdict = NULL;							//é a ficha "técnica" do bot
static IBotController* g_pBot = NULL;						//ficha do bot, para saber se ele está vivo, se está a jogar...
static IPlayerInfo* g_pBotInfo = NULL;
static float           g_flYaw = 0.0f;						//para saber para onde o bot está a olhar em graus
static float           g_flProximaTentativaEquipa = 0.0f;

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
ConVar mosca_rotacao("mosca_rotacao", "1.5", 0, "Graus que o bot roda em cada tick");

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

//Tick
/*
	Um servidor de cs:source funciona por ticks, ou seja, este atualiza o mundo 66 vezes por segundo
	Esta função ocorre em cada tick e faz 3 coisas:
		- Verifica se o bot ainda existe, se não existir limpa os ponteiros dele.

		- Põe o bot numa equipa: No cs:source existem 3 equipas, 0 = nenhuma, 1 = espectador, 2 = terrorista e 3 = countra-terrorista.
								 Verificamos isto uma vez por segundo, para não sobrecarregar o servidor com comandos de equipa.

		- Mexe o bot: O CBotCmd é equivalente ao teclado e rato do bot naquele tick, fowardmove é igual a carregar no W e
					  viewangles é para onde o rato aponta. O yaw é o ângulo horizontal (soma-se um pouco a cada tick para o bot andar em
					  circulos). E o runPlayerMove entrega estes comandos à engine

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

	if (!g_pBotInfo->IsDead())
	{
		g_flYaw += mosca_rotacao.GetFloat();
		if (g_flYaw > 180.0f)  g_flYaw -= 360.0f;
		if (g_flYaw < -180.0f) g_flYaw += 360.0f;

		cmd.forwardmove = mosca_velocidade.GetFloat();
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

	A ultima linha é o que faz o código ser reconhecido como um plugin pela engine. (Exporta o código como DDL)

*/
class CMoscaPlugin : public IServerPluginCallbacks
{
public:
	virtual bool Load(CreateInterfaceFn interfaceFactory, CreateInterfaceFn gameServerFactory);
	virtual void Unload(void);
	virtual void Pause(void) {}
	virtual void UnPause(void) {}
	virtual const char* GetPluginDescription(void) { return "SimuladorPap - Mosca v0.1"; }
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

	Msg("[Mosca] ---- A carregar o plugin ----\n");
	Msg("[Mosca] engine:            %s\n", engine ? "OK" : "FALHOU");
	Msg("[Mosca] helpers:           %s\n", helpers ? "OK" : "FALHOU");
	Msg("[Mosca] enginetrace:       %s\n", enginetrace ? "OK" : "FALHOU");
	Msg("[Mosca] playerinfomanager: %s\n", playerinfomanager ? "OK" : "FALHOU");
	Msg("[Mosca] botmanager:        %s\n", botmanager ? "OK" : "FALHOU");

	if (!engine || !helpers || !playerinfomanager || !botmanager)
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