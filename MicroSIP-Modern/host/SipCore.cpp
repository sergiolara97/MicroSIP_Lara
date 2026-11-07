/*
 * MicroSIP Modern - nucleo SIP (pjsua) sin MFC.
 */

#include "SipCore.h"

#include "TeamsPresence.h"

#include <json/json.h>
#include <sqlite3.h>

#include <pjsua-lib/pjsua.h>
#include <pjmedia/echo.h>
#include <pjmedia/tonegen.h>

#include <shlobj.h>



#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <algorithm>
#include <fstream>
#include <vector>

namespace {

// Margen minimo entre dos comandos de estado de Teams (ms).
constexpr DWORD kTeamsMinGapMs = 2000;

std::string WideToUtf8(const std::wstring& value)
{
	if (value.empty()) {
		return std::string();
	}
	int size = WideCharToMultiByte(CP_UTF8, 0, value.c_str(), (int)value.size(), nullptr, 0, nullptr, nullptr);
	std::string result(size, '\0');
	WideCharToMultiByte(CP_UTF8, 0, value.c_str(), (int)value.size(), &result[0], size, nullptr, nullptr);
	return result;
}

std::wstring Utf8ToWide(const std::string& value)
{
	if (value.empty()) {
		return std::wstring();
	}
	int size = MultiByteToWideChar(CP_UTF8, 0, value.c_str(), (int)value.size(), nullptr, 0);
	std::wstring result(size, L'\0');
	MultiByteToWideChar(CP_UTF8, 0, value.c_str(), (int)value.size(), &result[0], size);
	return result;
}

const char* CallStateText(pjsip_inv_state state)
{
	switch (state) {
	case PJSIP_INV_STATE_NULL: return "null";
	case PJSIP_INV_STATE_CALLING: return "calling";
	case PJSIP_INV_STATE_INCOMING: return "incoming";
	case PJSIP_INV_STATE_EARLY: return "early";
	case PJSIP_INV_STATE_CONNECTING: return "connecting";
	case PJSIP_INV_STATE_CONFIRMED: return "confirmed";
	case PJSIP_INV_STATE_DISCONNECTED: return "disconnected";
	}
	return "unknown";
}

long long NowSeconds()
{
	return (long long)time(nullptr);
}

bool LooksEncryptedPassword(const wchar_t* value)
{
	size_t length = wcslen(value);
	if (length < 32 || (length % 2) != 0) {
		return false;
	}
	for (size_t i = 0; i < length; i++) {
		wchar_t ch = value[i];
		bool hex = (ch >= L'0' && ch <= L'9')
			|| (ch >= L'a' && ch <= L'f')
			|| (ch >= L'A' && ch <= L'F');
		if (!hex) {
			return false;
		}
	}
	return true;
}

} // namespace

void msip_on_call_state(pjsua_call_id call_id, pjsip_event* event);
void msip_on_incoming_call(pjsua_acc_id acc_id, pjsua_call_id call_id, pjsip_rx_data* rdata);
void msip_on_call_media_state(pjsua_call_id call_id);
void msip_on_reg_state(pjsua_acc_id acc_id);
pj_status_t msip_on_snd_dev_operation(int operation);
void msip_on_buddy_state(pjsua_buddy_id buddy_id);
void msip_on_mwi_info(pjsua_acc_id acc_id, pjsua_mwi_info* mwi_info);
void msip_on_pager2(pjsua_call_id call_id, const pj_str_t* from, const pj_str_t* to,
	const pj_str_t* contact, const pj_str_t* mime_type, const pj_str_t* body,
	pjsip_rx_data* rdata, pjsua_acc_id acc_id);

SipCore& SipCore::Instance()
{
	static SipCore instance;
	return instance;
}

bool SipCore::Init(HWND hwnd, const std::wstring& appDir)
{
	hwnd_ = hwnd;
	appDir_ = appDir;
	iniPath_ = appDir + L"\\microsip.ini";

	// Si esta instalacion aun no tiene cuentas, se importan las de MicroSIP
	// (%APPDATA%\MicroSIP\microsip.ini o una copia portable).
	if (!HasConfiguredAccounts(iniPath_)) {
		std::wstring source = FindOriginalConfig();
		if (!source.empty()) {
			ImportAccountsFrom(source);
		}
	}

	if (started_) {
		return true;
	}

	// Normaliza el .ini (UTF-8 con BOM -> UTF-16) y lee los ajustes globales
	// antes de configurar pjsua (STUN, DNS, nivel de log, audio...).
	EnsureIniExists();
	LoadSettings(iniPath_);

	pj_status_t status = pjsua_create();
	if (status != PJ_SUCCESS) {
		return false;
	}

	pjsua_config cfg;
	pjsua_config_default(&cfg);
	cfg.max_calls = 4;
	// Muchos PBX descartan REGISTER sin User-Agent.
	// Se usa el mismo que la app original para maxima compatibilidad.
	cfg.user_agent = pj_str((char*)"MicroSIP/3.22.22");
	cfg.cb.on_call_state = &msip_on_call_state;
	cfg.cb.on_incoming_call = &msip_on_incoming_call;
	cfg.cb.on_call_media_state = &msip_on_call_media_state;
	cfg.cb.on_reg_state = &msip_on_reg_state;
	cfg.cb.on_pager2 = &msip_on_pager2;
	cfg.cb.on_snd_dev_operation = &msip_on_snd_dev_operation;
	cfg.cb.on_buddy_state = &msip_on_buddy_state;
	cfg.cb.on_mwi_info = &msip_on_mwi_info;

	std::string stunServer;
	if (enableStun_ && !stun_.empty()) {
		stunServer = stun_;
		size_t sep = stunServer.find_first_of(" ,;");
		if (sep != std::string::npos) {
			stunServer = stunServer.substr(0, sep);
		}
		cfg.stun_srv[0] = pj_str((char*)stunServer.c_str());
		cfg.stun_srv_cnt = 1;
	}

	std::string nameServers[4];
	if (!dnsSrv_.empty()) {
		size_t start = 0;
		int count = 0;
		while (count < 4) {
			size_t sep = dnsSrv_.find_first_of(";,", start);
			std::string server = dnsSrv_.substr(start,
				sep == std::string::npos ? std::string::npos : sep - start);
			if (!server.empty()) {
				nameServers[count] = server;
				cfg.nameserver[count] = pj_str((char*)nameServers[count].c_str());
				count++;
			}
			if (sep == std::string::npos) {
				break;
			}
			start = sep + 1;
		}
		cfg.nameserver_count = count;
	}

	pjsua_logging_config log_cfg;
	pjsua_logging_config_default(&log_cfg);
	log_cfg.console_level = 0;
	log_cfg.level = logLevel_ > 0 ? logLevel_ : 3;
	log_cfg.log_filename = pj_str((char*)"microsip-modern.log");

	pjsua_media_config media_cfg;
	pjsua_media_config_default(&media_cfg);
	media_cfg.quality = 8;
	media_cfg.ec_tail_len = ec_ ? 200 : 0;
	media_cfg.no_vad = vad_ ? PJ_FALSE : PJ_TRUE;
	// Cierra el dispositivo de audio en cuanto no hay llamada (la app
	// original solo abre el microfono durante la llamada).
	media_cfg.snd_auto_close_time = 1;

	status = pjsua_init(&cfg, &log_cfg, &media_cfg);
	{
	}
	if (status != PJ_SUCCESS) {
		pjsua_destroy();
		return false;
	}

	// Puerto local (Settings/sourcePort). 0 = aleatorio; util para pruebas
	// con varias instancias en la misma maquina.
	wchar_t portText[32] = { 0 };
	GetPrivateProfileStringW(L"Settings", L"sourcePort", L"0", portText, 32, iniPath_.c_str());
	sourcePort_ = _wtoi(portText);

	pjsua_transport_config trans_cfg;
	pjsua_transport_config_default(&trans_cfg);
	trans_cfg.port = (unsigned)sourcePort_;
	status = pjsua_transport_create(PJSIP_TRANSPORT_UDP, &trans_cfg, &udpTransport_);
	PJ_LOG(2, ("SipCore", "udp transport=%d status=%d", (int)udpTransport_, status));

	pjsua_transport_config tcp_cfg;
	pjsua_transport_config_default(&tcp_cfg);
	tcp_cfg.port = 0;
	status = pjsua_transport_create(PJSIP_TRANSPORT_TCP, &tcp_cfg, &tcpTransport_);
	PJ_LOG(2, ("SipCore", "tcp transport=%d status=%d", (int)tcpTransport_, status));
	if (status != PJ_SUCCESS) {
		tcpTransport_ = -1;
	}

	status = pjsua_start();
	if (status != PJ_SUCCESS) {
		pjsua_destroy();
		return false;
	}

	// El dispositivo de audio NO se abre al arrancar: pjsua lo abre cuando
	// hay una llamada y lo cierra solo al quedar inactivo. Abrirlo aqui
	// mantendria el microfono capturando todo el tiempo.

	// Generador de tonos DTMF local: la app original lo reproduce en el
	// altavoz al pulsar las teclas del marcador (aunque no haya llamada).
	// Solo se conecta al puerto maestro mientras suena un tono; si se deja
	// conectado, el puente nunca queda inactivo y el micro no se cierra.
	tonePool_ = pjsua_pool_create("msip_tonegen", 512, 512);
	if (tonePool_) {
		if (pjmedia_tonegen_create(tonePool_, 8000, 1, 64, 16, 0, &toneGen_) == PJ_SUCCESS) {
			if (pjsua_conf_add_port(tonePool_, toneGen_, &toneSlot_) == PJ_SUCCESS) {
				PJ_LOG(3, ("SipCore", "dtmf tonegen slot=%d (desconectado en reposo)",
					(int)toneSlot_));
			}
		}
	}

	started_ = true;
	ApplyCodecs();

	if (!importedFrom_.empty()) {
		PJ_LOG(2, ("SipCore", "accounts imported from %s", WideToUtf8(importedFrom_).c_str()));
	}

	LoadHistory();
	LoadMessages();
	LoadSettings(iniPath_);
	LoadAccounts();
	LoadShortcuts();
	ApplyAccounts();
	return true;
}

void SipCore::Shutdown()
{
	if (!started_) {
		return;
	}
	// Si se cierra la app en mitad de una llamada, Teams no debe quedarse Ocupado.
	if (teamsBusy_ || teamsPending_) {
		teamsBusy_ = false;
		teamsPending_ = false;
		TeamsPresence::Restore();
	}
	started_ = false;
	pjsua_call_hangup_all();
	if (toneSlot_ != -1) {
		pjsua_conf_remove_port(toneSlot_);
		toneSlot_ = -1;
	}
	if (toneGen_) {
		pjmedia_port_destroy(toneGen_);
		toneGen_ = nullptr;
	}
	if (tonePool_) {
		pj_pool_release(tonePool_);
		tonePool_ = nullptr;
	}
	pjsua_destroy();
	SaveHistory();
}

void SipCore::LoadSettings(const std::wstring& iniPath)
{
	iniPath_ = iniPath;

	auto get = [&](const wchar_t* section, const wchar_t* key, const wchar_t* def) -> std::string {
		wchar_t buffer[2048] = { 0 };
		GetPrivateProfileStringW(section, key, def ? def : L"", buffer, 2048, iniPath_.c_str());
		return WideToUtf8(buffer);
	};

	std::string accountId = get(L"Settings", L"accountId", L"1");
	activeAccountIndex_ = atoi(accountId.c_str());
	if (activeAccountIndex_ < 0) {
		activeAccountIndex_ = 1;
	}

	volumeOut_ = atoi(get(L"Settings", L"volumeOutput", L"80").c_str());
	volumeIn_ = atoi(get(L"Settings", L"volumeInput", L"60").c_str());
	dnd_ = get(L"Settings", L"dnd", L"0") == "1";
	record_ = get(L"Settings", L"record", L"0") == "1";

	sourcePort_ = atoi(get(L"Settings", L"sourcePort", L"0").c_str());
	stun_ = get(L"Settings", L"STUN", L"");
	enableStun_ = get(L"Settings", L"enableSTUN", L"0") == "1";
	rtpPortMin_ = atoi(get(L"Settings", L"rtpPortMin", L"0").c_str());
	rtpPortMax_ = atoi(get(L"Settings", L"rtpPortMax", L"0").c_str());
	dnsSrv_ = get(L"Settings", L"dnsSrvNs", L"");
	audioCodecs_ = get(L"Settings", L"audioCodecs", L"");
	forceCodec_ = get(L"Settings", L"forceCodec", L"");
	dtmfMethod_ = get(L"Settings", L"DTMFMethod", L"rfc2833");
	forwarding_ = get(L"Settings", L"forwarding", L"none");
	forwardingNumber_ = get(L"Settings", L"forwardingNumber", L"");
	featureCodeCP_ = get(L"Settings", L"featureCodeCP", L"**");
	logLevel_ = atoi(get(L"Settings", L"logLevel", L"3").c_str());
	ec_ = get(L"Settings", L"ec", L"1") == "1";
	vad_ = get(L"Settings", L"vad", L"0") == "1";
	inputDev_ = atoi(get(L"Settings", L"inputDevice", L"-1").c_str());
	outputDev_ = atoi(get(L"Settings", L"outputDevice", L"-1").c_str());
	startMinimized_ = get(L"Settings", L"startMinimized", L"0") == "1";
	singleInstance_ = get(L"Settings", L"singleInstance", L"1") == "1";
	localDtmf_ = get(L"Settings", L"localDTMF", L"1") == "1";
	alwaysOnTop_ = get(L"Settings", L"alwaysOnTop", L"0") == "1";
	autoAnswer_ = get(L"Settings", L"autoAnswer", L"0") == "1";
	callWaiting_ = get(L"Settings", L"callWaiting", L"1") == "1";
}

void SipCore::LoadAccounts()
{
	accounts_.clear();

	auto get = [&](const std::wstring& section, const wchar_t* key, const wchar_t* def) -> std::string {
		wchar_t buffer[2048] = { 0 };
		GetPrivateProfileStringW(section.c_str(), key, def ? def : L"", buffer, 2048, iniPath_.c_str());
		return WideToUtf8(buffer);
	};

	// Account0 es la cuenta local de MicroSIP; las reales empiezan en Account1.
	// Se enumeran todas las secciones que tengan servidor o usuario.
	for (int index = 0; index <= 16; index++) {
		std::wstring section = L"Account" + Utf8ToWide(std::to_string(index));

		MsipAccount account;
		account.index = index;
		account.section = section;
		account.label = get(section, L"label", L"");
		account.server = get(section, L"server", L"");
		account.domain = get(section, L"domain", L"");
		account.username = get(section, L"username", L"");
		account.password = get(section, L"password", L"");
		account.authId = get(section, L"authID", L"");
		account.displayName = get(section, L"displayName", L"");
		account.transport = get(section, L"transport", L"udp");
		account.proxy = get(section, L"proxy", L"");
		account.voicemail = get(section, L"voicemailNumber", L"");
		account.dialingPrefix = get(section, L"dialingPrefix", L"");
		account.dialPlan = get(section, L"dialPlan", L"");
		account.hideCid = get(section, L"hideCID", L"");
		account.srtp = get(section, L"SRTP", L"");
		account.publicAddr = get(section, L"publicAddr", L"");
		account.registerRefresh = atoi(get(section, L"registerRefresh", L"300").c_str());
		account.keepAlive = atoi(get(section, L"keepAlive", L"15").c_str());
		account.publish = get(section, L"publish", L"0") == "1";
		account.ice = get(section, L"ICE", L"0") == "1";
		account.allowRewrite = get(section, L"allowRewrite", L"0") == "1";
		account.disableSessionTimer = get(section, L"disableSessionTimer", L"0") == "1";

		if (account.server.empty() && account.username.empty()) {
			// Cuenta local (call by IP): sin registro, escucha en el
			// transporte UDP y acepta llamadas a sip:local@<domain>.
			if (account.index == 0 && !account.label.empty()) {
				account.username = "local";
				account.domain = account.domain.empty() ? "127.0.0.1" : account.domain;
				account.server = account.domain;
				account.transport = "udp";
				account.registerRefresh = 0;
				account.keepAlive = 0;
			}
			else {
				continue;
			}
		}
		if (account.label.empty()) {
			account.label = account.displayName.empty() ? account.username : account.displayName;
		}
		// MicroSIP cifra el password con una clave privada que no esta en el
		// codigo publico: si no se pudo importar, hay que pedirlo una vez.
		account.needsPassword = account.index != 0
			&& !account.username.empty() && !account.server.empty()
			&& account.password.empty();
		accounts_.push_back(account);
	}

	// Si la cuenta activa desaparecio, usar la primera configurada.
	bool activeFound = false;
	for (const auto& account : accounts_) {
		if (account.index == activeAccountIndex_) {
			activeFound = true;
			break;
		}
	}
	if (!activeFound && !accounts_.empty()) {
		activeAccountIndex_ = accounts_.front().index;
	}

	PJ_LOG(2, ("SipCore", "accounts loaded: %d active=Account%d",
		(int)accounts_.size(), activeAccountIndex_));
}

MsipAccount* SipCore::FindAccount(int index)
{
	for (auto& account : accounts_) {
		if (account.index == index) {
			return &account;
		}
	}
	return nullptr;
}

const MsipAccount* SipCore::ActiveAccount() const
{
	for (const auto& account : accounts_) {
		if (account.index == activeAccountIndex_) {
			return &account;
		}
	}
	return accounts_.empty() ? nullptr : &accounts_.front();
}

std::string SipCore::ResolveUri(const std::string& target) const
{
	if (target.empty() || target.find("sip:") == 0 || target.find("sips:") == 0) {
		return target;
	}
	if (target.find('@') != std::string::npos) {
		return "sip:" + target;
	}
	const MsipAccount* account = ActiveAccount();
	std::string host;
	if (account) {
		// Enrutar por el servidor de registro: el dominio puede estar
		// detras de un proxy web que no habla SIP.
		host = account->server.empty() ? account->domain : account->server;
	}
	if (host.empty()) {
		return target;
	}
	std::string uri = "sip:" + target + "@" + host;
	if (account) {
		if (account->transport == "udp") {
			uri += ";transport=udp";
		}
		else if (account->transport == "tcp") {
			uri += ";transport=tcp";
		}
		else if (account->transport == "tls") {
			uri += ";transport=tls";
		}
	}
	return uri;
}

std::string SipCore::GetSetting(const char* key, const char* def)
{
	wchar_t buffer[512] = { 0 };
	GetPrivateProfileStringW(L"Settings", Utf8ToWide(key).c_str(), Utf8ToWide(def ? def : "").c_str(),
		buffer, 512, iniPath_.c_str());
	return WideToUtf8(buffer);
}

void SipCore::SetSetting(const char* key, const std::string& value)
{
	WritePrivateProfileStringW(L"Settings", Utf8ToWide(key).c_str(), Utf8ToWide(value).c_str(), iniPath_.c_str());
}

void SipCore::ApplyAccounts()
{
	activeAccId_ = -1;

	if (!started_) {
		return;
	}

	// Se recrean las cuentas al guardar cambios: primero se eliminan las
	// anteriores para no duplicar registros.
	for (auto& account : accounts_) {
		if (account.accId >= 0) {
			pjsua_acc_del((pjsua_acc_id)account.accId);
			account.accId = -1;
		}
	}

	for (auto& account : accounts_) {
		if (account.username.empty()) {
			continue;
		}

		std::string host = account.domain.empty() ? account.server : account.domain;
		if (host.empty()) {
			continue;
		}

		std::string idUri = "sip:" + account.username + "@" + host;
		bool localAccount = (account.index == 0);

		pjsua_acc_config acc_cfg;
		pjsua_acc_config_default(&acc_cfg);
		acc_cfg.id = pj_str((char*)idUri.c_str());
		if (localAccount) {
			// Cuenta local: sin registro ni credenciales; acepta INVITE
			// dirigidos a sip:local@<ip>:<sourcePort>.
			acc_cfg.reg_uri = pj_str((char*)"");
			acc_cfg.cred_count = 0;
			acc_cfg.register_on_acc_add = PJ_FALSE;
		}
		else {
			std::string regUri = "sip:" + account.server;
			acc_cfg.reg_uri = pj_str((char*)regUri.c_str());
			acc_cfg.cred_count = 1;
			acc_cfg.cred_info[0].realm = pj_str((char*)"*");
			acc_cfg.cred_info[0].scheme = pj_str((char*)"digest");
			acc_cfg.cred_info[0].username = pj_str((char*)(account.authId.empty() ? account.username : account.authId).c_str());
			acc_cfg.cred_info[0].data_type = PJSIP_CRED_DATA_PLAIN_PASSWD;
			acc_cfg.cred_info[0].data = pj_str((char*)account.password.c_str());
			acc_cfg.register_on_acc_add = PJ_TRUE;
		}

		acc_cfg.reg_timeout = account.registerRefresh > 0 ? account.registerRefresh : 300;
		acc_cfg.ka_interval = account.keepAlive;
		acc_cfg.publish_enabled = account.publish ? PJ_TRUE : PJ_FALSE;
		acc_cfg.ice_cfg_use = PJSUA_ICE_CONFIG_USE_CUSTOM;
		acc_cfg.ice_cfg.enable_ice = account.ice ? PJ_TRUE : PJ_FALSE;
		acc_cfg.allow_via_rewrite = account.allowRewrite ? PJ_TRUE : PJ_FALSE;
		acc_cfg.allow_sdp_nat_rewrite = acc_cfg.allow_via_rewrite;
		acc_cfg.allow_contact_rewrite = account.allowRewrite
			? (PJSUA_CONTACT_REWRITE_ALWAYS_UPDATE | PJSUA_CONTACT_REWRITE_UNREGISTER)
			: PJ_FALSE;
		if (account.disableSessionTimer) {
			acc_cfg.use_timer = PJSUA_SIP_TIMER_INACTIVE;
		}
		if (account.srtp == "optional") {
			acc_cfg.use_srtp = PJMEDIA_SRTP_OPTIONAL;
		}
		else if (account.srtp == "mandatory") {
			acc_cfg.use_srtp = PJMEDIA_SRTP_MANDATORY;
		}
		else {
			acc_cfg.use_srtp = PJMEDIA_SRTP_DISABLED;
		}
		if (!account.publicAddr.empty()) {
			acc_cfg.rtp_cfg.public_addr = pj_str((char*)account.publicAddr.c_str());
		}
		if (rtpPortMin_ > 0) {
			acc_cfg.rtp_cfg.port = rtpPortMin_;
			if (rtpPortMax_ > rtpPortMin_) {
				acc_cfg.rtp_cfg.port_range = rtpPortMax_ - rtpPortMin_;
			}
		}
		if (account.transport == "tcp" && tcpTransport_ >= 0) {
			acc_cfg.transport_id = tcpTransport_;
		}
		else if (account.transport == "tls" && tlsTransport_ >= 0) {
			acc_cfg.transport_id = tlsTransport_;
		}
		// Para UDP se deja el selector vacio: pjsua elige UDP por el
		// esquema/parametro del URI. Fijarlo aqui provoca
		// PJSIP_ETPNOTSUITABLE en el INVITE.
		PJ_LOG(2, ("SipCore", "acc %d transport=%s id=%d",
			account.index, account.transport.c_str(), (int)acc_cfg.transport_id));
		std::string proxyUri;
		if (!account.proxy.empty()) {
			proxyUri = account.proxy;
			size_t sep = proxyUri.find_first_of(" ,;");
			if (sep != std::string::npos) {
				proxyUri = proxyUri.substr(0, sep);
			}
			if (proxyUri.find("sip:") != 0 && proxyUri.find("sips:") != 0) {
				proxyUri = "sip:" + proxyUri;
			}
			acc_cfg.proxy_cnt = 1;
			acc_cfg.proxy[0] = pj_str((char*)proxyUri.c_str());
		}

		pjsua_acc_id acc_id = -1;
		pj_status_t addStatus = pjsua_acc_add(&acc_cfg, PJ_TRUE, &acc_id);
		PJ_LOG(2, ("SipCore", "acc_add Account%d id=%s server=%s status=%d",
			account.index, idUri.c_str(), account.server.c_str(), addStatus));
		if (addStatus == PJ_SUCCESS) {
			account.accId = acc_id;
		}
	}

	for (const auto& account : accounts_) {
		if (account.index == activeAccountIndex_ && account.accId >= 0) {
			activeAccId_ = account.accId;
		}
	}
	if (activeAccId_ < 0) {
		for (const auto& account : accounts_) {
			if (account.accId >= 0) {
				activeAccId_ = account.accId;
				activeAccountIndex_ = account.index;
				break;
			}
		}
	}

	PJ_LOG(2, ("SipCore", "active account=Account%d accId=%d",
		activeAccountIndex_, activeAccId_));
}

void SipCore::LoadContacts(const std::wstring& dbPath)
{
	contacts_.clear();
	UnsubscribePresence();
	contactsDbPath_ = dbPath;

	std::string path = WideToUtf8(dbPath);
	sqlite3* db = nullptr;
	if (sqlite3_open_v2(path.c_str(), &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) != SQLITE_OK) {
		if (db) {
			sqlite3_close(db);
		}
		return;
	}
	sqlite3_exec(db, "CREATE TABLE IF NOT EXISTS contacts ("
		"id INTEGER PRIMARY KEY AUTOINCREMENT,"
		"number TEXT NOT NULL DEFAULT '',"
		"name TEXT NOT NULL DEFAULT '',"
		"info TEXT NOT NULL DEFAULT '',"
		"presence INTEGER NOT NULL DEFAULT 0);", nullptr, nullptr, nullptr);
	// Bases creadas por versiones antiguas: anadir la columna si falta.
	sqlite3_exec(db, "ALTER TABLE contacts ADD COLUMN presence INTEGER NOT NULL DEFAULT 0;", nullptr, nullptr, nullptr);

	sqlite3_stmt* stmt = nullptr;
	const char* sql = "SELECT id, name, number, info, presence FROM contacts ORDER BY name COLLATE NOCASE";
	if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
		while (sqlite3_step(stmt) == SQLITE_ROW) {
			MsipContact contact;
			contact.id = sqlite3_column_int(stmt, 0);
			const unsigned char* name = sqlite3_column_text(stmt, 1);
			const unsigned char* number = sqlite3_column_text(stmt, 2);
			const unsigned char* info = sqlite3_column_text(stmt, 3);
			contact.name = name ? (const char*)name : "";
			contact.number = number ? (const char*)number : "";
			contact.info = info ? (const char*)info : "";
			contact.presence = sqlite3_column_int(stmt, 4) != 0;
			if (!contact.number.empty()) {
				contacts_.push_back(contact);
			}
		}
	}
	sqlite3_finalize(stmt);
	sqlite3_close(db);

	SubscribePresence();
}

void SipCore::UnsubscribePresence()
{
	if (!started_) {
		return;
	}
	for (auto& contact : contacts_) {
		if (contact.buddyId >= 0) {
			pjsua_buddy_del((pjsua_buddy_id)contact.buddyId);
			contact.buddyId = -1;
		}
	}
	presenceSubscribed_ = false;
}

void SipCore::SubscribePresence()
{
	if (!started_ || contacts_.empty()) {
		return;
	}
	const MsipAccount* account = ActiveAccount();
	if (!account) {
		return;
	}
	std::string host = account->domain.empty() ? account->server : account->domain;
	if (host.empty()) {
		return;
	}
	for (auto& contact : contacts_) {
		if (!contact.presence || contact.buddyId >= 0 || contact.number.empty()) {
			continue;
		}
		std::string uri = "sip:" + contact.number + "@" + host;
		pjsua_buddy_config cfg;
		pjsua_buddy_config_default(&cfg);
		cfg.uri = pj_str((char*)uri.c_str());
		cfg.subscribe = PJ_TRUE;
		pjsua_buddy_id id = PJSUA_INVALID_ID;
		if (pjsua_buddy_add(&cfg, &id) == PJ_SUCCESS) {
			contact.buddyId = (int)id;
			pjsua_buddy_subscribe_pres(id, PJ_TRUE);
			PJ_LOG(2, ("SipCore", "presence subscribe %s buddy=%d", uri.c_str(), (int)id));
		}
		else {
			PJ_LOG(2, ("SipCore", "presence subscribe failed %s", uri.c_str()));
		}
	}
	presenceSubscribed_ = true;
}

bool SipCore::SaveContact(int id, const std::string& name, const std::string& number,
	const std::string& info, bool presence)
{
	if (number.empty()) {
		return false;
	}
	if (contactsDbPath_.empty()) {
		contactsDbPath_ = appDir_ + L"\\contacts.db";
	}
	std::string path = WideToUtf8(contactsDbPath_);
	sqlite3* db = nullptr;
	if (sqlite3_open_v2(path.c_str(), &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) != SQLITE_OK) {
		if (db) sqlite3_close(db);
		return false;
	}
	sqlite3_exec(db, "CREATE TABLE IF NOT EXISTS contacts ("
		"id INTEGER PRIMARY KEY AUTOINCREMENT,"
		"number TEXT NOT NULL DEFAULT '',"
		"name TEXT NOT NULL DEFAULT '',"
		"info TEXT NOT NULL DEFAULT '',"
		"presence INTEGER NOT NULL DEFAULT 0);", nullptr, nullptr, nullptr);
	sqlite3_exec(db, "ALTER TABLE contacts ADD COLUMN presence INTEGER NOT NULL DEFAULT 0;", nullptr, nullptr, nullptr);

	sqlite3_stmt* stmt = nullptr;
	bool ok = false;
	if (id > 0) {
		if (sqlite3_prepare_v2(db,
			"UPDATE contacts SET name=?, number=?, info=?, presence=? WHERE id=?;",
			-1, &stmt, nullptr) == SQLITE_OK) {
			sqlite3_bind_text(stmt, 1, name.c_str(), -1, SQLITE_TRANSIENT);
			sqlite3_bind_text(stmt, 2, number.c_str(), -1, SQLITE_TRANSIENT);
			sqlite3_bind_text(stmt, 3, info.c_str(), -1, SQLITE_TRANSIENT);
			sqlite3_bind_int(stmt, 4, presence ? 1 : 0);
			sqlite3_bind_int(stmt, 5, id);
			ok = sqlite3_step(stmt) == SQLITE_DONE;
		}
	}
	else {
		if (sqlite3_prepare_v2(db,
			"INSERT INTO contacts (name, number, info, presence) VALUES (?, ?, ?, ?);",
			-1, &stmt, nullptr) == SQLITE_OK) {
			sqlite3_bind_text(stmt, 1, name.c_str(), -1, SQLITE_TRANSIENT);
			sqlite3_bind_text(stmt, 2, number.c_str(), -1, SQLITE_TRANSIENT);
			sqlite3_bind_text(stmt, 3, info.c_str(), -1, SQLITE_TRANSIENT);
			sqlite3_bind_int(stmt, 4, presence ? 1 : 0);
			ok = sqlite3_step(stmt) == SQLITE_DONE;
		}
	}
	sqlite3_finalize(stmt);
	sqlite3_close(db);

	if (ok) {
		LoadContacts(contactsDbPath_);
		Notify();
	}
	return ok;
}

bool SipCore::DeleteContact(int id)
{
	if (id <= 0) {
		return false;
	}
	if (contactsDbPath_.empty()) {
		contactsDbPath_ = appDir_ + L"\\contacts.db";
	}
	std::string path = WideToUtf8(contactsDbPath_);
	sqlite3* db = nullptr;
	if (sqlite3_open_v2(path.c_str(), &db, SQLITE_OPEN_READWRITE, nullptr) != SQLITE_OK) {
		if (db) sqlite3_close(db);
		return false;
	}
	bool ok = sqlite3_exec(db, ("DELETE FROM contacts WHERE id=" + std::to_string(id) + ";").c_str(), nullptr, nullptr, nullptr) == SQLITE_OK;
	sqlite3_close(db);
	if (ok) {
		LoadContacts(contactsDbPath_);
		Notify();
	}
	return ok;
}

void SipCore::Notify()
{
	if (hwnd_) {
		PostMessage(hwnd_, WM_APP + 1, 0, 0);
	}
}

std::string SipCore::BuildState()
{
	const MsipAccount* active = ActiveAccount();

	Json::Value root;
	root["registered"] = false;
	root["account"] = active ? active->username : "";
	root["server"] = active ? active->server : "";
	root["displayName"] = active ? active->displayName : "";
	root["transport"] = active ? active->transport : "udp";
	root["activeAccount"] = activeAccountIndex_;
	root["lastError"] = lastError_;
	root["accent"] = GetSetting("accentColor", "red");
	root["configPath"] = WideToUtf8(iniPath_);
	root["importedFrom"] = WideToUtf8(importedFrom_);
	root["dnd"] = dnd_;
	root["record"] = record_;
	root["muted"] = muted_;
	root["mutedOut"] = mutedOut_;
	root["volumeOut"] = volumeOut_;
	root["volumeIn"] = volumeIn_;
	root["conference"] = conference_;
	root["transferPending"] = attendedSource_ >= 0;
	root["voicemail"] = voicemail_;

	Json::Value accounts(Json::arrayValue);
	for (auto& account : accounts_) {
		bool registered = false;
		int status = 0;
		std::string reason;

		if (started_ && account.accId >= 0) {
			pjsua_acc_info info;
			if (pjsua_acc_get_info((pjsua_acc_id)account.accId, &info) == PJ_SUCCESS) {
				registered = (info.status == PJSIP_SC_OK);
				status = info.status;
				reason = pj_strbuf(&info.status_text) ? pj_strbuf(&info.status_text) : "";
			}
		}
		account.registered = registered;
		account.regStatus = status;
		account.regReason = reason;

		Json::Value item;
		item["index"] = account.index;
		item["label"] = account.label;
		item["username"] = account.username;
		item["server"] = account.server;
		item["domain"] = account.domain;
		item["displayName"] = account.displayName;
		item["transport"] = account.transport;
		item["proxy"] = account.proxy;
		item["voicemail"] = account.voicemail;
		item["dialingPrefix"] = account.dialingPrefix;
		item["dialPlan"] = account.dialPlan;
		item["hideCid"] = account.hideCid;
		item["srtp"] = account.srtp;
		item["publicAddr"] = account.publicAddr;
		item["registerRefresh"] = account.registerRefresh;
		item["keepAlive"] = account.keepAlive;
		item["publish"] = account.publish;
		item["ice"] = account.ice;
		item["allowRewrite"] = account.allowRewrite;
		item["disableSessionTimer"] = account.disableSessionTimer;
		item["registered"] = registered;
		item["status"] = status;
		item["reason"] = reason;
		item["needsPassword"] = account.needsPassword;
		item["active"] = (account.index == activeAccountIndex_);
		accounts.append(item);

		if (account.index == activeAccountIndex_) {
			root["registered"] = registered;
			root["regStatus"] = status;
			root["regReason"] = reason;
		}
	}
	root["accounts"] = accounts;

	Json::Value settings;
	settings["stun"] = stun_;
	settings["enableStun"] = enableStun_;
	settings["rtpPortMin"] = rtpPortMin_;
	settings["rtpPortMax"] = rtpPortMax_;
	settings["dnsSrv"] = dnsSrv_;
	settings["logLevel"] = logLevel_;
	settings["ec"] = ec_;
	settings["vad"] = vad_;
	settings["inputDevice"] = inputDev_;
	settings["outputDevice"] = outputDev_;
	settings["sourcePort"] = sourcePort_;
	settings["startMinimized"] = startMinimized_;
	settings["singleInstance"] = singleInstance_;
	settings["localDTMF"] = localDtmf_;
	settings["alwaysOnTop"] = alwaysOnTop_;
	settings["autoAnswer"] = autoAnswer_;
	settings["callWaiting"] = callWaiting_;
	settings["jabraIntegration"] = GetSetting("jabraIntegration", "1") == "1";
	settings["hidHeadset"] = GetSetting("hidHeadset", "1") == "1";
	settings["teamsPresence"] = GetSetting("teamsPresence", "1") == "1";
	settings["callToast"] = GetSetting("callToast", "1") == "1";
	settings["callToastPosition"] = GetSetting("callToastPosition", "topright");
	settings["audioCodecs"] = audioCodecs_;
	settings["forceCodec"] = forceCodec_;
	settings["dtmfMethod"] = dtmfMethod_;
	settings["forwarding"] = (forwarding_ == "all" || forwarding_ == "1");
	settings["forwardingNumber"] = forwardingNumber_;
	settings["featureCodeCP"] = featureCodeCP_;
	settings["volumeRing"] = atoi(GetSetting("volumeRing", "100").c_str());
	settings["language"] = GetSetting("language", "");
	root["settings"] = settings;

	Json::Value shortcuts(Json::arrayValue);
	for (const auto& shortcut : shortcuts_) {
		Json::Value item;
		item["label"] = shortcut.label;
		item["number"] = shortcut.number;
		shortcuts.append(item);
	}
	root["shortcuts"] = shortcuts;

	Json::Value devices(Json::arrayValue);
	if (started_) {
		pjmedia_aud_dev_info devs[32];
		unsigned devCount = 32;
		if (pjsua_enum_aud_devs(devs, &devCount) == PJ_SUCCESS) {
			for (unsigned i = 0; i < devCount; i++) {
				Json::Value dev;
				dev["id"] = (int)i;
				dev["name"] = devs[i].name;
				dev["inputs"] = (int)devs[i].input_count;
				dev["outputs"] = (int)devs[i].output_count;
				devices.append(dev);
			}
		}
	}
	root["audioDevices"] = devices;

	Json::Value codecs(Json::arrayValue);
	if (started_) {
		pjsua_codec_info infos[PJMEDIA_CODEC_MGR_MAX_CODECS];
		unsigned codecCount = PJMEDIA_CODEC_MGR_MAX_CODECS;
		if (pjsua_enum_codecs(infos, &codecCount) == PJ_SUCCESS) {
			for (unsigned i = 0; i < codecCount; i++) {
				Json::Value codec;
				codec["id"] = std::string(pj_strbuf(&infos[i].codec_id),
					(size_t)pj_strlen(&infos[i].codec_id));
				codec["name"] = pj_strbuf(&infos[i].desc) ? pj_strbuf(&infos[i].desc) : "";
				codec["priority"] = (int)infos[i].priority;
				codecs.append(codec);
			}
		}
	}
	root["codecs"] = codecs;

	Json::Value calls(Json::arrayValue);
	unsigned count = PJSUA_MAX_CALLS;
	pjsua_call_id ids[PJSUA_MAX_CALLS];
	if (started_ && pjsua_enum_calls(ids, &count) == PJ_SUCCESS) {
		for (unsigned i = 0; i < count; i++) {
			pjsua_call_info ci;
			if (pjsua_call_get_info(ids[i], &ci) != PJ_SUCCESS) {
				continue;
			}
			Json::Value call;
			call["id"] = (int)ci.id;
			call["state"] = CallStateText(ci.state);
			call["incoming"] = (ci.role == PJSIP_ROLE_UAS);
			call["held"] = (ci.media_status == PJSUA_CALL_MEDIA_REMOTE_HOLD) || (ci.media_status == PJSUA_CALL_MEDIA_LOCAL_HOLD);
			call["remote"] = pj_strbuf(&ci.remote_info) ? pj_strbuf(&ci.remote_info) : "";
			call["remoteNumber"] = pj_strbuf(&ci.remote_contact) ? pj_strbuf(&ci.remote_contact) : "";
			call["duration"] = (double)ci.connect_duration.sec;
			calls.append(call);
		}
	}
	root["calls"] = calls;
	return Json::FastWriter().write(root);
}

std::string SipCore::StateJson()
{
	return BuildState();
}

std::string SipCore::HistoryJson()
{
	Json::Value root(Json::arrayValue);
	for (auto it = history_.rbegin(); it != history_.rend(); ++it) {
		Json::Value item;
		item["index"] = (int)(history_.rend() - it - 1);
		item["time"] = (double)it->time;
		item["number"] = it->number;
		item["name"] = it->name;
		item["type"] = it->type;
		item["duration"] = it->duration;
		root.append(item);
	}
	return Json::FastWriter().write(root);
}

std::string SipCore::ContactsJson()
{
	Json::Value root(Json::arrayValue);
	for (const auto& contact : contacts_) {
		Json::Value item;
		item["id"] = contact.id;
		item["name"] = contact.name;
		item["number"] = contact.number;
		item["info"] = contact.info;
		item["presence"] = contact.presence;
		item["presenceStatus"] = contact.presenceStatus;
		root.append(item);
	}
	return Json::FastWriter().write(root);
}

std::string SipCore::MessagesJson()
{
	Json::Value root(Json::arrayValue);
	for (const auto& message : messages_) {
		Json::Value item;
		item["time"] = (double)message.time;
		item["peer"] = message.peer;
		item["from"] = message.from;
		item["body"] = message.body;
		item["outgoing"] = message.outgoing;
		item["status"] = message.status;
		root.append(item);
	}
	return Json::FastWriter().write(root);
}

// Nombre de la agenda para un numero. Compara tambien los ultimos 9 digitos,
// porque el PBX manda a veces el prefijo del pais o el 0 de marcacion.
std::string SipCore::ContactNameFor(const std::string& number) const
{
	if (number.empty()) {
		return std::string();
	}
	auto digits = [](const std::string& text) {
		std::string result;
		for (char c : text) {
			if (c >= '0' && c <= '9') {
				result.push_back(c);
			}
		}
		return result;
	};
	std::string wanted = digits(number);
	for (const auto& contact : contacts_) {
		if (contact.number == number) {
			return contact.name;
		}
		std::string other = digits(contact.number);
		if (wanted.empty() || other.empty()) {
			continue;
		}
		if (other == wanted) {
			return contact.name;
		}
		if (other.size() >= 9 && wanted.size() >= 9
			&& other.compare(other.size() - 9, 9, wanted, wanted.size() - 9, 9) == 0) {
			return contact.name;
		}
	}
	return std::string();
}

void SipCore::SaveAccount(const MsipAccount& account)
{
	int index = account.index > 0 ? account.index : activeAccountIndex_;
	if (index <= 0) {
		index = 1;
	}

	std::wstring ini = iniPath_;
	EnsureIniExists();
	std::wstring section = L"Account" + Utf8ToWide(std::to_string(index));

	auto write = [&](const wchar_t* key, const std::string& value) {
		WritePrivateProfileStringW(section.c_str(), key, Utf8ToWide(value).c_str(), ini.c_str());
	};

	write(L"label", account.label.empty()
		? (account.displayName.empty() ? account.username : account.displayName)
		: account.label);
	write(L"server", account.server);

	// El dominio SIP puede ser distinto del servidor.
	// Si el formulario no lo trae, se conserva el existente.
	if (!account.domain.empty()) {
		write(L"domain", account.domain);
	}
	else {
		wchar_t existingDomain[512] = { 0 };
		GetPrivateProfileStringW(section.c_str(), L"domain", L"", existingDomain, 512, ini.c_str());
		if (existingDomain[0] == L'\0') {
			write(L"domain", account.server);
		}
	}

	write(L"username", account.username);
	if (!account.password.empty()) {
		write(L"password", account.password);
	}
	write(L"authID", account.authId);
	write(L"displayName", account.displayName);
	write(L"proxy", account.proxy);
	write(L"voicemailNumber", account.voicemail);
	write(L"dialingPrefix", account.dialingPrefix);
	write(L"dialPlan", account.dialPlan);
	write(L"hideCID", account.hideCid);
	write(L"SRTP", account.srtp);
	write(L"transport", account.transport.empty() ? "udp" : account.transport);
	write(L"publicAddr", account.publicAddr);
	write(L"registerRefresh", std::to_string(account.registerRefresh > 0 ? account.registerRefresh : 300));
	write(L"keepAlive", std::to_string(account.keepAlive));
	write(L"publish", account.publish ? "1" : "0");
	write(L"ICE", account.ice ? "1" : "0");
	write(L"allowRewrite", account.allowRewrite ? "1" : "0");
	write(L"disableSessionTimer", account.disableSessionTimer ? "1" : "0");

	activeAccountIndex_ = index;
	WritePrivateProfileStringW(L"Settings", L"accountId",
		Utf8ToWide(std::to_string(index)).c_str(), ini.c_str());

	LoadSettings(ini);
	LoadAccounts();
	ApplyAccounts();
	Notify();
}

void SipCore::SetActiveAccount(int index)
{
	if (index < 0 || !FindAccount(index)) {
		return;
	}
	activeAccountIndex_ = index;
	activeAccId_ = -1;
	MsipAccount* account = FindAccount(index);
	if (account && account->accId >= 0) {
		activeAccId_ = account->accId;
	}
	SetSetting("accountId", std::to_string(index));
	PJ_LOG(2, ("SipCore", "active account switched to Account%d accId=%d", index, activeAccId_));
	Notify();
}

bool SipCore::HasConfiguredAccounts(const std::wstring& path) const
{
	for (int index = 0; index <= 16; index++) {
		std::wstring section = L"Account" + Utf8ToWide(std::to_string(index));
		wchar_t username[256] = { 0 };
		wchar_t server[512] = { 0 };
		GetPrivateProfileStringW(section.c_str(), L"username", L"", username, 256, path.c_str());
		GetPrivateProfileStringW(section.c_str(), L"server", L"", server, 512, path.c_str());
		if (username[0] != L'\0' && server[0] != L'\0') {
			return true;
		}
		// Cuenta local (call by IP): tiene label/domain pero no usuario.
		if (index == 0) {
			wchar_t label[256] = { 0 };
			GetPrivateProfileStringW(section.c_str(), L"label", L"", label, 256, path.c_str());
			if (label[0] != L'\0') {
				return true;
			}
		}
	}
	return false;
}

std::wstring SipCore::FindOriginalConfig() const
{
	std::vector<std::wstring> candidates;
	wchar_t folder[MAX_PATH] = { 0 };

	// Instalacion normal: %APPDATA%\MicroSIP\microsip.ini
	if (SHGetSpecialFolderPathW(nullptr, folder, CSIDL_APPDATA, FALSE)) {
		candidates.push_back(std::wstring(folder) + L"\\MicroSIP\\microsip.ini");
	}
	if (SHGetSpecialFolderPathW(nullptr, folder, CSIDL_LOCAL_APPDATA, FALSE)) {
		candidates.push_back(std::wstring(folder) + L"\\MicroSIP\\microsip.ini");
	}

	// Copia portable junto a la app moderna (dist\MicroSIP-Jabra).
	size_t slash = appDir_.find_last_of(L"\\/");
	if (slash != std::wstring::npos) {
		std::wstring parent = appDir_.substr(0, slash);
		candidates.push_back(parent + L"\\MicroSIP-Jabra\\microsip.ini");
		candidates.push_back(parent + L"\\microsip.ini");
	}

	for (const auto& candidate : candidates) {
		if (_wcsicmp(candidate.c_str(), iniPath_.c_str()) == 0) {
			continue;
		}
		if (GetFileAttributesW(candidate.c_str()) == INVALID_FILE_ATTRIBUTES) {
			continue;
		}
		if (HasConfiguredAccounts(candidate)) {
			return candidate;
		}
	}
	return std::wstring();
}

void SipCore::EnsureIniExists()
{
	FILE* fp = _wfopen(iniPath_.c_str(), L"rb");
	if (fp) {
		unsigned char head[3] = { 0 };
		size_t read = fread(head, 1, 3, fp);
		fseek(fp, 0, SEEK_END);
		long size = ftell(fp);
		fseek(fp, 0, SEEK_SET);
		std::string data(size > 0 ? (size_t)size : 0, '\0');
		if (size > 0) {
			fread(&data[0], 1, (size_t)size, fp);
		}
		fclose(fp);

		// Windows no entiende UTF-8 con BOM en los .ini: la seccion
		// [Settings] se pierde. Se reescribe como UTF-16 LE.
		if (read == 3 && head[0] == 0xEF && head[1] == 0xBB && head[2] == 0xBF) {
			std::string utf8 = data.size() > 3 ? data.substr(3) : std::string();
			int wideLength = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), (int)utf8.size(), nullptr, 0);
			std::wstring wide(wideLength > 0 ? (size_t)wideLength : 0, L'\0');
			if (wideLength > 0) {
				MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), (int)utf8.size(), &wide[0], wideLength);
			}
			FILE* out = _wfopen(iniPath_.c_str(), L"wb");
			if (out) {
				unsigned short bom = 0xFEFF;
				fwrite(&bom, sizeof(bom), 1, out);
				if (!wide.empty()) {
					fwrite(wide.data(), sizeof(wchar_t), wide.size(), out);
				}
				fclose(out);
			}
		}
		return;
	}

	// Archivo nuevo: UTF-16 LE con BOM.
	fp = _wfopen(iniPath_.c_str(), L"wb");
	if (!fp) {
		return;
	}
	unsigned short bom = 0xFEFF;
	fwrite(&bom, sizeof(bom), 1, fp);
	fclose(fp);
}

bool SipCore::ImportAccountsFrom(const std::wstring& path)
{
	if (path.empty()) {
		return false;
	}

	EnsureIniExists();

	wchar_t accountId[64] = { 0 };
	GetPrivateProfileStringW(L"Settings", L"accountId", L"1", accountId, 64, path.c_str());

	bool imported = false;
	for (int index = 0; index <= 16; index++) {
		std::wstring section = L"Account" + Utf8ToWide(std::to_string(index));

		wchar_t username[256] = { 0 };
		wchar_t server[512] = { 0 };
		GetPrivateProfileStringW(section.c_str(), L"username", L"", username, 256, path.c_str());
		GetPrivateProfileStringW(section.c_str(), L"server", L"", server, 512, path.c_str());
		if (username[0] == L'\0' && server[0] == L'\0') {
			continue;
		}

		std::vector<wchar_t> buffer(32768, 0);
		DWORD copied = GetPrivateProfileSectionW(section.c_str(), buffer.data(),
			(DWORD)buffer.size(), path.c_str());
		if (copied == 0) {
			continue;
		}

		WritePrivateProfileSectionW(section.c_str(), buffer.data(), iniPath_.c_str());

		wchar_t sourcePassword[2048] = { 0 };
		GetPrivateProfileStringW(section.c_str(), L"password", L"", sourcePassword, 2048, path.c_str());
		if (LooksEncryptedPassword(sourcePassword)) {
			// MicroSIP cifra el password con una clave privada que no esta en
			// el codigo publico. Se descarta: el usuario lo escribe una vez.
			WritePrivateProfileStringW(section.c_str(), L"password", nullptr, iniPath_.c_str());
		}
		imported = true;
	}

	if (imported) {
		WritePrivateProfileStringW(L"Settings", L"accountId", accountId, iniPath_.c_str());
		importedFrom_ = path;
	}
	return imported;
}

bool SipCore::ImportAccountsFromOriginal()
{
	std::wstring source = FindOriginalConfig();
	if (source.empty()) {
		lastError_ = "No se encontro la configuracion de MicroSIP";
		PJ_LOG(1, ("SipCore", "import: no source config found"));
		Notify();
		return false;
	}

	bool imported = ImportAccountsFrom(source);
	if (imported) {
		lastError_.clear();
		PJ_LOG(2, ("SipCore", "accounts imported from %s", WideToUtf8(source).c_str()));
		LoadSettings(iniPath_);
		LoadAccounts();
		ApplyAccounts();
	}
	else {
		lastError_ = "La configuracion de MicroSIP no tiene cuentas";
	}
	Notify();
	return imported;
}

void SipCore::ApplyGlobalSettings()
{
	LoadSettings(iniPath_);
	// No abrir el dispositivo de audio en reposo: solo se aplica durante
	// una llamada (si no, el microfono quedaria capturando siempre).
	if (started_ && (inputDev_ >= 0 || outputDev_ >= 0) && ActiveCallCount() > 0) {
		pjsua_set_snd_dev(inputDev_, outputDev_);
	}
	ApplyCodecs();
	Notify();
}

void SipCore::ApplyCodecs()
{
	if (!started_) {
		return;
	}
	pjsua_codec_info codecs[PJMEDIA_CODEC_MGR_MAX_CODECS];
	unsigned count = PJMEDIA_CODEC_MGR_MAX_CODECS;
	if (pjsua_enum_codecs(codecs, &count) != PJ_SUCCESS) {
		return;
	}
	std::string enabled = "," + audioCodecs_ + ",";
	for (unsigned i = 0; i < count; i++) {
		std::string id(pj_strbuf(&codecs[i].codec_id),
			(size_t)pj_strlen(&codecs[i].codec_id));
		unsigned prio = 128;
		if (!forceCodec_.empty()) {
			prio = (id == forceCodec_ || id.rfind(forceCodec_ + "/", 0) == 0) ? 255 : 0;
		}
		else if (!audioCodecs_.empty()) {
			prio = enabled.find("," + id + ",") != std::string::npos ? 128 : 0;
		}
		pjsua_codec_set_priority(&codecs[i].codec_id, (pj_uint8_t)prio);
	}
	PJ_LOG(2, ("SipCore", "codecs applied enabled='%s' force='%s'",
		audioCodecs_.c_str(), forceCodec_.c_str()));
}

void SipCore::AnswerIncoming()
{
	if (!started_) {
		return;
	}
	pjsua_call_id ids[PJSUA_MAX_CALLS];
	unsigned count = PJSUA_MAX_CALLS;
	if (pjsua_enum_calls(ids, &count) == PJ_SUCCESS) {
		for (unsigned i = 0; i < count; i++) {
			pjsua_call_info ci;
			if (pjsua_call_get_info(ids[i], &ci) == PJ_SUCCESS && ci.state == PJSIP_INV_STATE_INCOMING) {
				pjsua_call_answer(ids[i], 200, nullptr, nullptr);
			}
		}
	}
}

void SipCore::HangupIncoming()
{
	if (!started_) {
		return;
	}
	pjsua_call_id ids[PJSUA_MAX_CALLS];
	unsigned count = PJSUA_MAX_CALLS;
	if (pjsua_enum_calls(ids, &count) == PJ_SUCCESS) {
		for (unsigned i = 0; i < count; i++) {
			pjsua_call_info ci;
			if (pjsua_call_get_info(ids[i], &ci) == PJ_SUCCESS && ci.state == PJSIP_INV_STATE_INCOMING) {
				pjsua_call_hangup(ids[i], 486, nullptr, nullptr);
			}
		}
	}
}

void SipCore::ToggleMute()
{
	SetMute(!muted_);
}

void SipCore::Transfer(int id, const std::string& number)
{
	if (!started_ || number.empty()) {
		return;
	}
	std::string target = ResolveUri(number);
	pj_str_t uri = pj_str((char*)target.c_str());
	pj_status_t status = pjsua_call_xfer((pjsua_call_id)id, &uri, nullptr);
	PJ_LOG(2, ("SipCore", "transfer %s status=%d", target.c_str(), status));
}

void SipCore::StartRecordingIfNeeded()
{
	if (!started_ || !record_ || recId_ >= 0) {
		return;
	}

	pjsua_call_id ids[PJSUA_MAX_CALLS];
	unsigned count = PJSUA_MAX_CALLS;
	if (pjsua_enum_calls(ids, &count) != PJ_SUCCESS) {
		return;
	}

	for (unsigned i = 0; i < count; i++) {
		pjsua_call_info ci;
		if (pjsua_call_get_info(ids[i], &ci) != PJ_SUCCESS || ci.state != PJSIP_INV_STATE_CONFIRMED) {
			continue;
		}

		std::wstring dir = appDir_ + L"\\recordings";
		CreateDirectoryW(dir.c_str(), nullptr);

		time_t now = time(nullptr);
		struct tm local;
		localtime_s(&local, &now);
		wchar_t name[96];
		swprintf_s(name, L"\\call-%04d%02d%02d-%02d%02d%02d.wav",
			local.tm_year + 1900, local.tm_mon + 1, local.tm_mday,
			local.tm_hour, local.tm_min, local.tm_sec);

		std::string path = WideToUtf8(dir + name);
		pj_str_t filename = pj_str((char*)path.c_str());

		pjsua_recorder_id recorder = -1;
		if (pjsua_recorder_create(&filename, 0, nullptr, 0, 0, &recorder) == PJ_SUCCESS) {
			pjsua_conf_port_id slot = pjsua_recorder_get_conf_port(recorder);
			pjsua_conf_connect(ci.conf_slot, slot);
			pjsua_conf_connect(0, slot);
			recId_ = recorder;
		}
		break;
	}
}

void SipCore::StopRecording()
{
	if (recId_ >= 0) {
		pjsua_recorder_destroy(recId_);
		recId_ = -1;
	}
}

void SipCore::Dial(const std::string& number)
{
	if (!started_ || number.empty()) {
		return;
	}
	if (activeAccId_ < 0) {
		lastError_ = "No hay cuenta SIP activa";
		PJ_LOG(1, ("SipCore", "dial sin cuenta activa"));
		Notify();
		return;
	}
	std::string dialed = number;
	const MsipAccount* account = ActiveAccount();
	if (account && !account->dialingPrefix.empty()
		&& dialed.find("sip:") != 0 && dialed.find("sips:") != 0
		&& !dialed.empty() && dialed[0] != '+'
		&& dialed.find('@') == std::string::npos) {
		dialed = account->dialingPrefix + dialed;
	}
	std::string target = ResolveUri(dialed);
	pj_str_t uri = pj_str((char*)target.c_str());
	ApplySoundDevice();
	pjsua_call_id call_id = PJSUA_INVALID_ID;
	pj_status_t status = pjsua_call_make_call((pjsua_acc_id)activeAccId_, &uri, 0, nullptr, nullptr, &call_id);
	lastError_ = (status == PJ_SUCCESS) ? "" : "No se pudo iniciar la llamada";
	PJ_LOG(2, ("SipCore", "dial %s via Account%d status=%d",
		target.c_str(), activeAccountIndex_, status));
}

void SipCore::Answer(int id)
{
	if (!started_) {
		return;
	}
	// Llamada en espera: al contestar una segunda llamada se retiene la
	// que estuviera activa (como hace MicroSIP).
	ApplySoundDevice();
	pjsua_call_id ids[PJSUA_MAX_CALLS];
	unsigned count = PJSUA_MAX_CALLS;
	if (pjsua_enum_calls(ids, &count) == PJ_SUCCESS) {
		for (unsigned i = 0; i < count; i++) {
			if ((int)ids[i] == id) {
				continue;
			}
			pjsua_call_info ci;
			if (pjsua_call_get_info(ids[i], &ci) == PJ_SUCCESS
				&& ci.state == PJSIP_INV_STATE_CONFIRMED) {
				pjsua_call_set_hold(ids[i], nullptr);
			}
		}
	}
	pjsua_call_answer((pjsua_call_id)id, 200, nullptr, nullptr);
}

void SipCore::SwapTo(int id)
{
	if (!started_) {
		return;
	}
	pjsua_call_id ids[PJSUA_MAX_CALLS];
	unsigned count = PJSUA_MAX_CALLS;
	if (pjsua_enum_calls(ids, &count) != PJ_SUCCESS) {
		return;
	}
	for (unsigned i = 0; i < count; i++) {
		pjsua_call_info ci;
		if (pjsua_call_get_info(ids[i], &ci) != PJ_SUCCESS
			|| ci.state != PJSIP_INV_STATE_CONFIRMED) {
			continue;
		}
		if ((int)ids[i] == id) {
			pjsua_call_reinvite(ids[i], PJSUA_CALL_UNHOLD, nullptr);
		}
		else {
			pjsua_call_set_hold(ids[i], nullptr);
		}
	}
}

void SipCore::Hangup(int id)
{
	if (started_) {
		pjsua_call_hangup((pjsua_call_id)id, 0, nullptr, nullptr);
	}
}

void SipCore::HangupAll()
{
	if (started_) {
		pjsua_call_hangup_all();
	}
}

void SipCore::Hold(int id, bool on)
{
	if (!started_) {
		return;
	}
	pjsua_call_set_hold((pjsua_call_id)id, nullptr);
	if (!on) {
		pjsua_call_reinvite((pjsua_call_id)id, PJSUA_CALL_UNHOLD, nullptr);
	}
}

int SipCore::ActiveCallCount()
{
	if (!started_) {
		return 0;
	}
	int active = 0;
	pjsua_call_id ids[PJSUA_MAX_CALLS];
	unsigned count = PJSUA_MAX_CALLS;
	if (pjsua_enum_calls(ids, &count) == PJ_SUCCESS) {
		for (unsigned i = 0; i < count; i++) {
			pjsua_call_info ci;
			if (pjsua_call_get_info(ids[i], &ci) == PJ_SUCCESS
				&& (ci.state == PJSIP_INV_STATE_CONFIRMED || ci.state == PJSIP_INV_STATE_EARLY)) {
				active++;
			}
		}
	}
	return active;
}

// Hay alguna llamada viva (entrante, saliente o establecida).
bool SipCore::HasAnyCall()
{
	if (!started_) {
		return false;
	}
	pjsua_call_id ids[PJSUA_MAX_CALLS];
	unsigned count = PJSUA_MAX_CALLS;
	return pjsua_enum_calls(ids, &count) == PJ_SUCCESS && count > 0;
}

// Hay una llamada entrante sin contestar (timbre).
bool SipCore::HasIncomingCall()
{
	if (!started_) {
		return false;
	}
	pjsua_call_id ids[PJSUA_MAX_CALLS];
	unsigned count = PJSUA_MAX_CALLS;
	if (pjsua_enum_calls(ids, &count) != PJ_SUCCESS) {
		return false;
	}
	for (unsigned i = 0; i < count; i++) {
		pjsua_call_info ci;
		if (pjsua_call_get_info(ids[i], &ci) == PJ_SUCCESS
			&& ci.state == PJSIP_INV_STATE_INCOMING) {
			return true;
		}
	}
	return false;
}

// Sincroniza el estado de Microsoft Teams con la llamada en curso: Ocupado
// mientras hay una llamada establecida, Disponible al colgar. Se llama desde
// el callback de estado de llamada de pjsua.
void SipCore::UpdateTeamsPresence()
{
	if (GetSetting("teamsPresence", "1") != "1") {
		if (teamsBusy_ || teamsPending_) {
			teamsPending_ = false;
			ApplyTeamsState(false);
		}
		return;
	}

	bool connected = false;
	if (started_) {
		pjsua_call_id ids[PJSUA_MAX_CALLS];
		unsigned count = PJSUA_MAX_CALLS;
		if (pjsua_enum_calls(ids, &count) == PJ_SUCCESS) {
			for (unsigned i = 0; i < count; i++) {
				pjsua_call_info ci;
				if (pjsua_call_get_info(ids[i], &ci) == PJ_SUCCESS
					&& ci.state == PJSIP_INV_STATE_CONFIRMED) {
					connected = true;
					break;
				}
			}
		}
	}

	if (connected == teamsBusy_ && connected == teamsPending_) {
		return;
	}
	teamsPending_ = connected;

	// Dos ms-teams.exe lanzados casi a la vez se pisan y el segundo se pierde
	// (una llamada de un segundo se quedaba en Ocupado). Se deja pasar un
	// margen y se aplica solo el estado final.
	DWORD since = GetTickCount() - teamsCmdTick_;
	if (teamsCmdTick_ != 0 && since < kTeamsMinGapMs) {
		KillTimer(hwnd_, MSIP_TIMER_TEAMS);
		SetTimer(hwnd_, MSIP_TIMER_TEAMS, kTeamsMinGapMs - since, nullptr);
		return;
	}
	ApplyTeamsState(connected);
}

void SipCore::ApplyTeamsState(bool busy)
{
	teamsBusy_ = busy;
	teamsPending_ = busy;
	teamsCmdTick_ = GetTickCount();
	KillTimer(hwnd_, MSIP_TIMER_TEAMS);
	// Se aplica en el hilo de la ventana: leer el estado de Teams usa UI
	// Automation y no debe bloquear el hilo de pjsua.
	PostMessageW(hwnd_, MSIP_TEAMS_STATE_MSG, busy ? 1 : 0, 0);
}

void SipCore::ApplySoundDevice()
{
	if (!started_) {
		return;
	}
	if ((inputDev_ >= 0 || outputDev_ >= 0) && !pjsua_snd_is_active()) {
		pjsua_set_snd_dev(inputDev_, outputDev_);
	}
}

void SipCore::AddCall(const std::string& number)
{
	if (!started_ || number.empty()) {
		return;
	}
	if (ActiveCallCount() == 0) {
		Dial(number);
		return;
	}
	// Conferencia: se retiene la llamada actual y se marca la nueva; al
	// confirmarse, se unen los puertos de audio (MergeCalls).
	pendingConference_ = true;
	pjsua_call_id ids[PJSUA_MAX_CALLS];
	unsigned count = PJSUA_MAX_CALLS;
	if (pjsua_enum_calls(ids, &count) == PJ_SUCCESS) {
		for (unsigned i = 0; i < count; i++) {
			pjsua_call_info ci;
			if (pjsua_call_get_info(ids[i], &ci) == PJ_SUCCESS
				&& ci.state == PJSIP_INV_STATE_CONFIRMED) {
				pjsua_call_set_hold(ids[i], nullptr);
			}
		}
	}
	Dial(number);
}

void SipCore::MergeCalls()
{
	if (!started_) {
		return;
	}
	std::vector<int> ports;
	pjsua_call_id ids[PJSUA_MAX_CALLS];
	unsigned count = PJSUA_MAX_CALLS;
	if (pjsua_enum_calls(ids, &count) == PJ_SUCCESS) {
		for (unsigned i = 0; i < count; i++) {
			pjsua_call_info ci;
			if (pjsua_call_get_info(ids[i], &ci) != PJ_SUCCESS
				|| ci.state != PJSIP_INV_STATE_CONFIRMED) {
				continue;
			}
			// La llamada que se retuvo para marcar la segunda debe
			// recuperarse antes de unir el audio.
			if (ci.media_status == PJSUA_CALL_MEDIA_LOCAL_HOLD) {
				pjsua_call_reinvite(ids[i], PJSUA_CALL_UNHOLD, nullptr);
			}
			ports.push_back((int)ci.conf_slot);
		}
	}
	if (ports.size() < 2) {
		pendingConference_ = false;
		return;
	}
	// Todos con todos: cada participante oye al resto.
	for (size_t i = 0; i < ports.size(); i++) {
		for (size_t j = 0; j < ports.size(); j++) {
			if (i == j) {
				continue;
			}
			pjsua_conf_connect((pjsua_conf_port_id)ports[i], (pjsua_conf_port_id)ports[j]);
		}
	}
	conference_ = true;
	pendingConference_ = false;
	PJ_LOG(2, ("SipCore", "conference merged calls=%d", (int)ports.size()));
}

void SipCore::TransferAttended(int id, const std::string& number)
{
	if (!started_ || number.empty()) {
		return;
	}
	attendedSource_ = id;
	pjsua_call_set_hold((pjsua_call_id)id, nullptr);
	Dial(number);
	PJ_LOG(2, ("SipCore", "attended transfer: consulta %s sobre call=%d",
		number.c_str(), id));
}

void SipCore::CompleteAttendedTransfer()
{
	if (!started_ || attendedSource_ < 0) {
		return;
	}
	int consult = -1;
	pjsua_call_id ids[PJSUA_MAX_CALLS];
	unsigned count = PJSUA_MAX_CALLS;
	if (pjsua_enum_calls(ids, &count) == PJ_SUCCESS) {
		for (unsigned i = 0; i < count; i++) {
			pjsua_call_info ci;
			if (pjsua_call_get_info(ids[i], &ci) == PJ_SUCCESS
				&& ci.state == PJSIP_INV_STATE_CONFIRMED
				&& (int)ids[i] != attendedSource_) {
				consult = (int)ids[i];
				break;
			}
		}
	}
	if (consult >= 0) {
		pj_status_t status = pjsua_call_xfer_replaces(
			(pjsua_call_id)attendedSource_, (pjsua_call_id)consult, 0, nullptr);
		PJ_LOG(2, ("SipCore", "attended transfer complete src=%d consult=%d status=%d",
			attendedSource_, consult, status));
		pjsua_call_hangup((pjsua_call_id)consult, 0, nullptr, nullptr);
	}
	attendedSource_ = -1;
}

void SipCore::CancelAttendedTransfer()
{
	if (!started_ || attendedSource_ < 0) {
		return;
	}
	pjsua_call_id ids[PJSUA_MAX_CALLS];
	unsigned count = PJSUA_MAX_CALLS;
	if (pjsua_enum_calls(ids, &count) == PJ_SUCCESS) {
		for (unsigned i = 0; i < count; i++) {
			pjsua_call_info ci;
			if (pjsua_call_get_info(ids[i], &ci) != PJ_SUCCESS) {
				continue;
			}
			if ((int)ids[i] == attendedSource_) {
				pjsua_call_reinvite(ids[i], PJSUA_CALL_UNHOLD, nullptr);
			}
			else if (ci.state == PJSIP_INV_STATE_CONFIRMED || ci.state == PJSIP_INV_STATE_CALLING) {
				pjsua_call_hangup(ids[i], 0, nullptr, nullptr);
			}
		}
	}
	attendedSource_ = -1;
}

void SipCore::TransferHeldToActive()
{
	if (!started_) {
		return;
	}
	int held = -1;
	int active = -1;
	pjsua_call_id ids[PJSUA_MAX_CALLS];
	unsigned count = PJSUA_MAX_CALLS;
	if (pjsua_enum_calls(ids, &count) != PJ_SUCCESS) {
		return;
	}
	for (unsigned i = 0; i < count; i++) {
		pjsua_call_info ci;
		if (pjsua_call_get_info(ids[i], &ci) != PJ_SUCCESS
			|| ci.state != PJSIP_INV_STATE_CONFIRMED) {
			continue;
		}
		if (ci.media_status == PJSUA_CALL_MEDIA_LOCAL_HOLD
			|| ci.media_status == PJSUA_CALL_MEDIA_REMOTE_HOLD) {
			held = (int)ids[i];
		}
		else if (active < 0) {
			active = (int)ids[i];
		}
	}
	if (held < 0 || active < 0) {
		PJ_LOG(2, ("SipCore", "transferHeldToActive: no hay held+active"));
		return;
	}
	pj_status_t status = pjsua_call_xfer_replaces(
		(pjsua_call_id)held, (pjsua_call_id)active, 0, nullptr);
	PJ_LOG(2, ("SipCore", "xfer held=%d active=%d status=%d",
		held, active, status));
	pjsua_call_hangup((pjsua_call_id)active, 0, nullptr, nullptr);
}

void SipCore::SetAutoAnswer(bool on)
{
	autoAnswer_ = on;
	SetSetting("autoAnswer", on ? "1" : "0");
}

void SipCore::SetCallWaiting(bool on)
{
	callWaiting_ = on;
	SetSetting("callWaiting", on ? "1" : "0");
}

void SipCore::ForwardCall(int callId, const std::string& number)
{
	if (!started_ || number.empty()) {
		return;
	}
	std::string target = ResolveUri(number);
	pj_pool_t* pool = pjsua_pool_create("msip_fwd", 512, 512);
	if (!pool) {
		return;
	}
	pjsua_msg_data msg;
	pjsua_msg_data_init(&msg);
	pj_str_t hname = pj_str((char*)"Contact");
	pj_str_t hvalue = pj_str((char*)target.c_str());
	pjsip_generic_string_hdr* hdr = pjsip_generic_string_hdr_create(pool, &hname, &hvalue);
	if (hdr) {
		pj_list_push_back(&msg.hdr_list, hdr);
	}
	pj_status_t status = pjsua_call_answer((pjsua_call_id)callId, 302, nullptr, &msg);
	PJ_LOG(2, ("SipCore", "forward call=%d to=%s status=%d",
		callId, target.c_str(), status));
}

void SipCore::Pickup(const std::string& number)
{
	if (!started_ || number.empty()) {
		return;
	}
	Dial(featureCodeCP_ + number);
}

int SipCore::FirstActiveCallId()
{
	if (!started_) {
		return -1;
	}
	pjsua_call_id ids[PJSUA_MAX_CALLS];
	unsigned count = PJSUA_MAX_CALLS;
	if (pjsua_enum_calls(ids, &count) != PJ_SUCCESS) {
		return -1;
	}
	for (unsigned i = 0; i < count; i++) {
		pjsua_call_info ci;
		if (pjsua_call_get_info(ids[i], &ci) == PJ_SUCCESS
			&& ci.state == PJSIP_INV_STATE_CONFIRMED) {
			return (int)ids[i];
		}
	}
	return -1;
}

void SipCore::HoldActive()
{
	int id = FirstActiveCallId();
	if (id >= 0) {
		Hold(id, true);
	}
}

void SipCore::TransferActive(const std::string& number)
{
	int id = FirstActiveCallId();
	if (id >= 0) {
		Transfer(id, number);
	}
}

void SipCore::OnTimer(UINT_PTR id)
{
	if (id == MSIP_TIMER_DTMF) {
		PollDtmfTone();
	}
	else if (id == MSIP_TIMER_TEAMS) {
		KillTimer(hwnd_, MSIP_TIMER_TEAMS);
		if (teamsPending_ != teamsBusy_) {
			ApplyTeamsState(teamsPending_);
		}
	}
}

void SipCore::SetMute(bool on)
{
	muted_ = on;
	PJ_LOG(2, ("SipCore", "mute mic=%d", on ? 1 : 0));
	if (!started_) {
		return;
	}
	pjsua_conf_adjust_tx_level(0, on ? 0.0f : volumeIn_ / 100.0f);
}

void SipCore::SetMuteOutput(bool on)
{
	mutedOut_ = on;
	PJ_LOG(2, ("SipCore", "mute out=%d", on ? 1 : 0));
	if (!started_) {
		return;
	}
	pjsua_conf_adjust_rx_level(0, on ? 0.0f : volumeOut_ / 100.0f);
}

void SipCore::SendDtmf(const std::string& digit)
{
	if (!started_ || digit.empty()) {
		return;
	}

	// Enviar al interlocutor (RFC2833/inband segun la cuenta de pjsua).
	bool inCall = false;
	pjsua_call_id ids[PJSUA_MAX_CALLS];
	unsigned count = PJSUA_MAX_CALLS;
	if (pjsua_enum_calls(ids, &count) == PJ_SUCCESS) {
		for (unsigned i = 0; i < count; i++) {
			pjsua_call_info ci;
			if (pjsua_call_get_info(ids[i], &ci) != PJ_SUCCESS
				|| ci.media_status != PJSUA_CALL_MEDIA_ACTIVE) {
				continue;
			}
			pj_str_t d = pj_str((char*)digit.c_str());
			pjsua_call_send_dtmf_param param;
			pjsua_call_send_dtmf_param_default(&param);
			param.method = (_stricmp(dtmfMethod_.c_str(), "sipinfo") == 0 || _stricmp(dtmfMethod_.c_str(), "sip-info") == 0)
				? PJSUA_DTMF_METHOD_SIP_INFO : PJSUA_DTMF_METHOD_RFC2833;
			param.digits = d;
			pjsua_call_send_dtmf(ids[i], &param);
			inCall = true;
		}
	}

	// Tono local. En llamada siempre (igual que MicroSIP); sin llamada solo
	// si el ajuste "localDTMF" esta activo.
	if (inCall || localDtmf_) {
		PlayDtmfTone(digit);
	}
}

void SipCore::PlayDtmfTone(const std::string& digits)
{
	if (!toneGen_ || digits.empty()) {
		return;
	}

	pjmedia_tone_digit d[16];
	unsigned count = (unsigned)digits.size();
	if (count > PJ_ARRAY_SIZE(d)) {
		count = PJ_ARRAY_SIZE(d);
	}
	pj_bzero(d, sizeof(d));
	for (unsigned i = 0; i < count; i++) {
		d[i].digit = digits[i];
		d[i].on_msec = 80;
		d[i].off_msec = 0;
		d[i].volume = 1 + 25 * PJMEDIA_TONEGEN_VOLUME / 100;
	}

	// El tono solo se conecta al altavoz mientras suena: si se quedara
	// conectado, el puente de audio nunca quedaria inactivo y el
	// dispositivo (microfono incluido) no se cerraria al colgar.
	if (!dtmfConnected_) {
		if (pjsua_conf_connect(toneSlot_, 0) == PJ_SUCCESS) {
			dtmfConnected_ = true;
		}
	}
	pjmedia_tonegen_play_digits(toneGen_, count, d, 0);
	if (hwnd_) {
		SetTimer(hwnd_, MSIP_TIMER_DTMF, 40, nullptr);
	}
	PJ_LOG(3, ("SipCore", "dtmf tone '%s' busy=%d", digits.c_str(),
		(int)pjmedia_tonegen_is_busy(toneGen_)));
}

void SipCore::PollDtmfTone()
{
	if (!toneGen_) {
		return;
	}
	if (pjmedia_tonegen_is_busy(toneGen_)) {
		return;
	}
	StopDtmfTone();
}

void SipCore::StopDtmfTone()
{
	if (hwnd_) {
		KillTimer(hwnd_, MSIP_TIMER_DTMF);
	}
	if (dtmfConnected_) {
		pjsua_conf_disconnect(toneSlot_, 0);
		dtmfConnected_ = false;
		PJ_LOG(4, ("SipCore", "dtmf tonegen desconectado"));
	}
}

void SipCore::SetVolume(bool output, int value)
{
	value = std::max(0, std::min(100, value));
	if (output) {
		volumeOut_ = value;
		if (started_) {
			pjsua_conf_adjust_rx_level(0, mutedOut_ ? 0.0f : value / 100.0f);
		}
	}
	else {
		volumeIn_ = value;
		if (started_) {
			pjsua_conf_adjust_tx_level(0, muted_ ? 0.0f : value / 100.0f);
		}
	}
}

void SipCore::SetDnd(bool on)
{
	dnd_ = on;
	SetSetting("dnd", on ? "1" : "0");
}

void SipCore::SetRecord(bool on)
{
	record_ = on;
	SetSetting("record", on ? "1" : "0");
	if (on) {
		StartRecordingIfNeeded();
	}
	else {
		StopRecording();
	}
}

void SipCore::SetAlwaysOnTop(bool on)
{
	alwaysOnTop_ = on;
	SetSetting("alwaysOnTop", on ? "1" : "0");
}

void SipCore::UpdateBuddyStatus(int buddyId, const std::string& status)
{
	for (auto& contact : contacts_) {
		if (contact.buddyId == buddyId) {
			contact.presenceStatus = status;
			return;
		}
	}
}

void SipCore::SetVoicemailWaiting(bool on)
{
	voicemail_ = on;
	Notify();
}

void SipCore::DialVoicemail()
{
	const MsipAccount* account = ActiveAccount();
	if (account && !account->voicemail.empty()) {
		Dial(account->voicemail);
		return;
	}
	lastError_ = "La cuenta no tiene numero de buzon de voz";
	Notify();
}

void SipCore::DeleteHistory(int index)
{
	if (index < 0 || index >= (int)history_.size()) {
		return;
	}
	history_.erase(history_.begin() + index);
	SaveHistory();
	Notify();
}

void SipCore::ClearHistory()
{
	history_.clear();
	SaveHistory();
	Notify();
}

namespace {

std::string CsvField(const std::string& value)
{
	if (value.find_first_of(",\"\n\r") == std::string::npos) {
		return value;
	}
	std::string out = "\"";
	for (char ch : value) {
		if (ch == '"') {
			out += "\"\"";
		}
		else {
			out += ch;
		}
	}
	out += "\"";
	return out;
}

std::vector<std::string> CsvSplit(const std::string& line)
{
	std::vector<std::string> fields;
	std::string current;
	bool quoted = false;
	for (size_t i = 0; i < line.size(); i++) {
		char ch = line[i];
		if (quoted) {
			if (ch == '"') {
				if (i + 1 < line.size() && line[i + 1] == '"') {
					current += '"';
					i++;
				}
				else {
					quoted = false;
				}
			}
			else {
				current += ch;
			}
		}
		else if (ch == '"') {
			quoted = true;
		}
		else if (ch == ',') {
			fields.push_back(current);
			current.clear();
		}
		else if (ch != '\r') {
			current += ch;
		}
	}
	fields.push_back(current);
	return fields;
}

} // namespace

bool SipCore::ExportHistoryCsv(const std::wstring& path)
{
	std::ofstream out(path, std::ios::binary);
	if (!out) {
		return false;
	}
	out << "time,number,name,type,duration\n";
	for (const auto& item : history_) {
		out << item.time << ',' << CsvField(item.number) << ',' << CsvField(item.name)
			<< ',' << item.type << ',' << item.duration << '\n';
	}
	return true;
}

bool SipCore::ImportHistoryCsv(const std::wstring& path)
{
	std::ifstream in(path, std::ios::binary);
	if (!in) {
		return false;
	}
	std::string line;
	std::vector<MsipHistoryItem> imported;
	while (std::getline(in, line)) {
		if (line.empty() || line.rfind("time,", 0) == 0) {
			continue;
		}
		auto fields = CsvSplit(line);
		if (fields.size() < 5) {
			continue;
		}
		MsipHistoryItem item;
		item.time = atoll(fields[0].c_str());
		item.number = fields[1];
		item.name = fields[2];
		item.type = fields[3];
		item.duration = atoi(fields[4].c_str());
		imported.push_back(item);
	}
	history_.insert(history_.end(), imported.begin(), imported.end());
	SaveHistory();
	Notify();
	return true;
}

bool SipCore::ExportContactsCsv(const std::wstring& path)
{
	std::ofstream out(path, std::ios::binary);
	if (!out) {
		return false;
	}
	out << "name,number,info,presence\n";
	for (const auto& contact : contacts_) {
		out << CsvField(contact.name) << ',' << CsvField(contact.number) << ','
			<< CsvField(contact.info) << ',' << (contact.presence ? 1 : 0) << '\n';
	}
	return true;
}

bool SipCore::ImportContactsCsv(const std::wstring& path)
{
	std::ifstream in(path, std::ios::binary);
	if (!in) {
		return false;
	}
	std::string line;
	bool any = false;
	while (std::getline(in, line)) {
		if (line.empty() || line.rfind("name,", 0) == 0) {
			continue;
		}
		auto fields = CsvSplit(line);
		if (fields.size() < 2 || fields[1].empty()) {
			continue;
		}
		if (SaveContact(0, fields[0], fields[1],
			fields.size() > 2 ? fields[2] : "",
			fields.size() > 3 && fields[3] == "1")) {
			any = true;
		}
	}
	return any;
}

bool SipCore::ExportConfig(const std::wstring& path)
{
	if (path.empty()) {
		return false;
	}
	return CopyFileW(iniPath_.c_str(), path.c_str(), FALSE) != FALSE;
}

bool SipCore::ImportConfig(const std::wstring& path)
{
	if (path.empty() || GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) {
		return false;
	}
	wchar_t names[8192] = { 0 };
	GetPrivateProfileSectionNamesW(names, 8192, path.c_str());
	for (const wchar_t* section = names; *section; section += wcslen(section) + 1) {
		std::wstring name(section);
		if (name != L"Settings" && name != L"Shortcuts" && name != L"Global"
			&& name.rfind(L"Account", 0) != 0) {
			continue;
		}
		std::vector<wchar_t> buffer(16384, 0);
		DWORD size = GetPrivateProfileSectionW(section, buffer.data(), 16384, path.c_str());
		if (size == 0) {
			continue;
		}
		for (const wchar_t* entry = buffer.data(); *entry; entry += wcslen(entry) + 1) {
			std::wstring pair(entry);
			size_t eq = pair.find(L'=');
			if (eq == std::wstring::npos) {
				continue;
			}
			std::wstring key = pair.substr(0, eq);
			std::wstring value = pair.substr(eq + 1);
			WritePrivateProfileStringW(section, key.c_str(), value.c_str(), iniPath_.c_str());
		}
	}
	LoadSettings(iniPath_);
	LoadAccounts();
	LoadShortcuts();
	ApplyAccounts();
	Notify();
	return true;
}

bool SipCore::ImportShortcutsFrom(const std::wstring& path)
{
	std::vector<MsipShortcut> loaded;
	for (int i = 0; i < 32; i++) {
		wchar_t key[16] = { 0 };
		swprintf_s(key, L"%d", i);
		wchar_t buffer[1024] = { 0 };
		if (GetPrivateProfileStringW(L"Shortcuts", key, L"", buffer, 1024, path.c_str()) == 0) {
			break;
		}
		std::string value = WideToUtf8(buffer);
		size_t first = value.find(';');
		if (first == std::string::npos) {
			continue;
		}
		MsipShortcut shortcut;
		shortcut.label = value.substr(0, first);
		std::string rest = value.substr(first + 1);
		size_t second = rest.find(';');
		shortcut.number = second == std::string::npos ? rest : rest.substr(0, second);
		if (shortcut.number.empty()) {
			continue;
		}
		if (shortcut.label.empty()) {
			shortcut.label = shortcut.number;
		}
		loaded.push_back(shortcut);
	}
	if (loaded.empty()) {
		return false;
	}
	shortcuts_ = loaded;
	return true;
}

void SipCore::LoadShortcuts()
{
	shortcuts_.clear();
	if (!ImportShortcutsFrom(iniPath_) && GetSetting("shortcutsImported", "0") != "1") {
		// Primera vez: reutiliza los atajos de la app original si existen.
		std::wstring original = FindOriginalConfig();
		if (!original.empty() && ImportShortcutsFrom(original)) {
			SaveShortcuts();
			PJ_LOG(2, ("SipCore", "shortcuts imported from %s", WideToUtf8(original).c_str()));
		}
		SetSetting("shortcutsImported", "1");
	}
	PJ_LOG(2, ("SipCore", "shortcuts loaded: %d", (int)shortcuts_.size()));
}

void SipCore::SaveShortcuts()
{
	WritePrivateProfileSectionW(L"Shortcuts", nullptr, iniPath_.c_str());
	for (size_t i = 0; i < shortcuts_.size(); i++) {
		std::wstring key = Utf8ToWide(std::to_string(i));
		std::string value = shortcuts_[i].label + ";" + shortcuts_[i].number + ";dtmf;;0;";
		WritePrivateProfileStringW(L"Shortcuts", key.c_str(), Utf8ToWide(value).c_str(),
			iniPath_.c_str());
	}
}

void SipCore::SetShortcut(int index, const std::string& label, const std::string& number)
{
	if (number.empty()) {
		return;
	}
	MsipShortcut shortcut;
	shortcut.number = number;
	shortcut.label = label.empty() ? number : label;
	if (index >= 0 && index < (int)shortcuts_.size()) {
		shortcuts_[index] = shortcut;
	}
	else {
		shortcuts_.push_back(shortcut);
	}
	SaveShortcuts();
	PJ_LOG(2, ("SipCore", "shortcut saved: %s -> %s", shortcut.label.c_str(), shortcut.number.c_str()));
}

void SipCore::DeleteShortcut(int index)
{
	if (index < 0 || index >= (int)shortcuts_.size()) {
		return;
	}
	shortcuts_.erase(shortcuts_.begin() + index);
	SaveShortcuts();
	PJ_LOG(2, ("SipCore", "shortcut deleted, remaining=%d", (int)shortcuts_.size()));
}

void SipCore::SendMessage(const std::string& to, const std::string& body)
{
	if (!started_ || to.empty() || body.empty()) {
		return;
	}
	if (activeAccId_ < 0) {
		lastError_ = "No hay cuenta SIP activa para enviar el mensaje";
		PJ_LOG(1, ("SipCore", "im sin cuenta activa"));
		Notify();
		return;
	}

	std::string target = ResolveUri(to);
	pj_str_t uri = pj_str((char*)target.c_str());
	pj_str_t mime = pj_str((char*)"text/plain");
	pj_str_t content = pj_str((char*)body.c_str());

	pj_status_t status = pjsua_im_send((pjsua_acc_id)activeAccId_, &uri, &mime, &content, nullptr, nullptr);

	MsipChatMessage message;
	message.time = NowSeconds();
	message.peer = to;
	message.from = target;
	message.body = body;
	message.outgoing = true;
	message.status = (status == PJ_SUCCESS) ? "sent" : "failed";
	AddChatMessage(message);

	lastError_ = (status == PJ_SUCCESS) ? "" : "No se pudo enviar el mensaje";
	PJ_LOG(2, ("SipCore", "im_send to=%s via Account%d status=%d",
		target.c_str(), activeAccountIndex_, status));
}

void SipCore::AddChatMessage(const MsipChatMessage& message)
{
	messages_.push_back(message);
	if (messages_.size() > 500) {
		messages_.erase(messages_.begin());
	}
	SaveMessages();
	Notify();
}

void SipCore::LoadMessages()
{
	messages_.clear();
	std::wstring path = appDir_ + L"\\messages.tsv";
	FILE* fp = _wfopen(path.c_str(), L"rb");
	if (!fp) {
		return;
	}

	char line[4096];
	while (fgets(line, sizeof(line), fp)) {
		std::string text(line);
		while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) {
			text.pop_back();
		}

		std::vector<std::string> fields;
		size_t start = 0;
		for (int i = 0; i < 4; i++) {
			size_t tab = text.find('\t', start);
			if (tab == std::string::npos) {
				break;
			}
			fields.push_back(text.substr(start, tab - start));
			start = tab + 1;
		}
		if (fields.size() < 4) {
			continue;
		}
		fields.push_back(text.substr(start));

		MsipChatMessage message;
		message.time = atoll(fields[0].c_str());
		message.peer = fields[1];
		message.outgoing = fields[2] == "1";
		message.status = fields[3];
		message.body = fields[4];
		messages_.push_back(message);
	}
	fclose(fp);
}

void SipCore::SaveMessages()
{
	std::wstring path = appDir_ + L"\\messages.tsv";
	FILE* fp = _wfopen(path.c_str(), L"wb");
	if (!fp) {
		return;
	}

	auto sanitize = [](std::string value) {
		for (char& ch : value) {
			if (ch == '\t' || ch == '\n' || ch == '\r') {
				ch = ' ';
			}
		}
		return value;
	};

	for (const auto& message : messages_) {
		fprintf(fp, "%lld\t%s\t%d\t%s\t%s\n",
			message.time,
			sanitize(message.peer).c_str(),
			message.outgoing ? 1 : 0,
			sanitize(message.status).c_str(),
			sanitize(message.body).c_str());
	}
	fclose(fp);
}

void SipCore::AddHistory(const MsipHistoryItem& item)
{
	history_.push_back(item);
	if (history_.size() > 500) {
		history_.erase(history_.begin());
	}
	SaveHistory();
}

void SipCore::LoadHistory()
{
	history_.clear();
	std::wstring path = appDir_ + L"\\history.tsv";
	FILE* fp = _wfopen(path.c_str(), L"rb");
	if (!fp) {
		return;
	}
	char line[2048];
	while (fgets(line, sizeof(line), fp)) {
		std::string text(line);
		while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) {
			text.pop_back();
		}
		std::vector<std::string> fields;
		size_t start = 0;
		for (;;) {
			size_t tab = text.find('\t', start);
			if (tab == std::string::npos) {
				fields.push_back(text.substr(start));
				break;
			}
			fields.push_back(text.substr(start, tab - start));
			start = tab + 1;
		}
		if (fields.size() < 5) {
			continue;
		}
		MsipHistoryItem item;
		item.time = atoll(fields[0].c_str());
		item.number = fields[1];
		item.name = fields[2];
		item.type = fields[3];
		item.duration = atoi(fields[4].c_str());
		history_.push_back(item);
	}
	fclose(fp);
}

void SipCore::SaveHistory()
{
	std::wstring path = appDir_ + L"\\history.tsv";
	FILE* fp = _wfopen(path.c_str(), L"wb");
	if (!fp) {
		return;
	}
	for (const auto& item : history_) {
		fprintf(fp, "%lld\t%s\t%s\t%s\t%d\n", item.time, item.number.c_str(), item.name.c_str(),
			item.type.c_str(), item.duration);
	}
	fclose(fp);
}

/* ------------------------------------------------------------------ */
/* Callbacks de pjsua                                                  */
/* ------------------------------------------------------------------ */

void msip_on_reg_state(pjsua_acc_id acc_id)
{
	PJ_LOG(2, ("SipCore", "reg_state acc=%d", acc_id));
	SipCore::Instance().Notify();
}

void msip_on_pager2(pjsua_call_id call_id, const pj_str_t* from, const pj_str_t* to,
	const pj_str_t* contact, const pj_str_t* mime_type, const pj_str_t* body,
	pjsip_rx_data* rdata, pjsua_acc_id acc_id)
{
	std::string fromText;
	if (from) {
		fromText.assign(pj_strbuf(from), (size_t)pj_strlen(from));
	}
	std::string bodyText;
	if (body) {
		bodyText.assign(pj_strbuf(body), (size_t)pj_strlen(body));
	}
	if (bodyText.empty()) {
		return;
	}

	// Para responder se usa el Contact del remitente (incluye IP:puerto
	// reales); el From puede venir sin puerto. Si no hay Contact, From.
	std::string contactText;
	if (contact) {
		contactText.assign(pj_strbuf(contact), (size_t)pj_strlen(contact));
	}

	auto stripUri = [](std::string value) {
		if (!value.empty() && value.front() == '<') {
			value.erase(value.begin());
		}
		size_t close = value.find('>');
		if (close != std::string::npos) {
			value = value.substr(0, close);
		}
		if (value.find("sip:") == 0) {
			value = value.substr(4);
		}
		size_t params = value.find(';');
		if (params != std::string::npos) {
			value = value.substr(0, params);
		}
		return value;
	};

	// El usuario (extension) sale del From. La IP:puerto reales se toman del
	// paquete recibido: es la unica forma fiable de responder cuando el
	// From/Contact no llevan puerto.
	std::string user = stripUri(fromText);
	size_t atUser = user.find('@');
	if (atUser != std::string::npos) {
		user = user.substr(0, atUser);
	}

	std::string peer = stripUri(contactText.empty() ? fromText : contactText);
	if (!user.empty() && rdata && rdata->pkt_info.src_name[0] != '\0' && rdata->pkt_info.src_port > 0) {
		peer = user + "@" + rdata->pkt_info.src_name + ":" + std::to_string(rdata->pkt_info.src_port);
	}
	if (peer.empty()) {
		peer = fromText;
	}

	MsipChatMessage message;
	message.time = NowSeconds();
	message.peer = peer;
	message.from = fromText;
	message.body = bodyText;
	message.outgoing = false;
	message.status = "received";

	PJ_LOG(2, ("SipCore", "im_received from=%s contact=%s src=%s:%d peer=%s acc=%d",
		fromText.c_str(), contactText.c_str(),
		rdata ? rdata->pkt_info.src_name : "", rdata ? rdata->pkt_info.src_port : 0,
		peer.c_str(), acc_id));

	SipCore::Instance().AddChatMessage(message);
}

pj_status_t msip_on_snd_dev_operation(int operation)
{
	PJ_LOG(3, ("SipCore", "snd_dev %s", operation ? "open" : "close"));
	return PJ_SUCCESS;
}

void msip_on_buddy_state(pjsua_buddy_id buddy_id)
{
	pjsua_buddy_info info;
	if (pjsua_buddy_get_info(buddy_id, &info) != PJ_SUCCESS) {
		return;
	}
	std::string status = "unknown";
	if (info.status == PJSUA_BUDDY_STATUS_ONLINE) {
		status = "online";
	}
	else if (info.status == PJSUA_BUDDY_STATUS_OFFLINE) {
		status = "offline";
	}
	PJ_LOG(3, ("SipCore", "buddy %d state=%s", (int)buddy_id, status.c_str()));
	SipCore::Instance().UpdateBuddyStatus((int)buddy_id, status);
	SipCore::Instance().Notify();
}

void msip_on_mwi_info(pjsua_acc_id acc_id, pjsua_mwi_info* mwi_info)
{
	bool hasMail = false;
	if (mwi_info && mwi_info->rdata
		&& mwi_info->rdata->msg_info.msg
		&& mwi_info->rdata->msg_info.msg->body
		&& mwi_info->rdata->msg_info.msg->body->data) {
		std::string body((const char*)mwi_info->rdata->msg_info.msg->body->data,
			(size_t)mwi_info->rdata->msg_info.msg->body->len);
		hasMail = body.find("Messages-Waiting: yes") != std::string::npos
			|| body.find("messages-waiting: yes") != std::string::npos;
	}
	PJ_LOG(2, ("SipCore", "mwi acc=%d waiting=%d", (int)acc_id, hasMail ? 1 : 0));
	SipCore::Instance().SetVoicemailWaiting(hasMail);
}

void msip_on_incoming_call(pjsua_acc_id acc_id, pjsua_call_id call_id, pjsip_rx_data* rdata)
{
	SipCore& core = SipCore::Instance();

	// No molestar: se rechaza la llamada (antes solo cambiaba el icono).
	if (core.Dnd()) {
		pjsua_call_answer(call_id, 486, nullptr, nullptr);
		PJ_LOG(2, ("SipCore", "incoming %d rejected (DND)", (int)call_id));
		return;
	}

	// Sin llamada en espera solo se admite una llamada a la vez.
	if (!core.CallWaiting() && core.ActiveCallCount() > 0) {
		pjsua_call_answer(call_id, 486, nullptr, nullptr);
		PJ_LOG(2, ("SipCore", "incoming %d rejected (callWaiting=0)", (int)call_id));
		return;
	}

	// Desvio de llamadas (302 con Contact).
	if (core.Forwarding() && !core.ForwardingNumber().empty()) {
		core.ForwardCall(call_id, core.ForwardingNumber());
		return;
	}

	// Contestar automaticamente (si esta activado).
	if (core.AutoAnswer()) {
		PJ_LOG(2, ("SipCore", "incoming %d auto-answered", (int)call_id));
		core.Answer(call_id);
	}

	core.Notify();
}

void msip_on_call_media_state(pjsua_call_id call_id)
{
	pjsua_call_info ci;
	if (pjsua_call_get_info(call_id, &ci) == PJ_SUCCESS) {
		if (ci.media_status == PJSUA_CALL_MEDIA_ACTIVE) {
			pjsua_conf_connect(ci.conf_slot, 0);
			pjsua_conf_connect(0, ci.conf_slot);
			// Conferencia: cuando la segunda llamada ya tiene audio,
			// se unen los puertos de todas las llamadas.
			if (SipCore::Instance().pendingConference_) {
				SipCore::Instance().MergeCalls();
			}
		}
	}
	SipCore::Instance().Notify();
}

void msip_on_call_state(pjsua_call_id call_id, pjsip_event* event)
{
	pjsua_call_info ci;
	if (pjsua_call_get_info(call_id, &ci) != PJ_SUCCESS) {
		return;
	}

	SipCore& core = SipCore::Instance();

	if (ci.state == PJSIP_INV_STATE_DISCONNECTED) {
		MsipHistoryItem item;
		item.time = NowSeconds();
		item.number = pj_strbuf(&ci.remote_info) ? pj_strbuf(&ci.remote_info) : "";
		item.name = item.number;
		bool incoming = (ci.role == PJSIP_ROLE_UAS);
		bool connected = (ci.connect_duration.sec > 0 || ci.total_duration.sec > 0);
		item.type = connected ? (incoming ? "in" : "out") : (incoming ? "missed" : "out");
		item.duration = (int)ci.connect_duration.sec;
		core.AddHistory(item);
		core.StopRecording();
		// Sin llamadas: se deja que pjsua cierre el dispositivo de audio
		// (snd_auto_close_time) para que el microfono deje de capturar.
		if (core.ActiveCallCount() == 0) {
			core.conference_ = false;
			core.attendedSource_ = -1;
			core.pendingConference_ = false;
			PJ_LOG(3, ("SipCore", "sin llamadas: audio inactivo"));
		}
		else if (core.ActiveCallCount() == 1) {
			core.conference_ = false;
		}
	}
	else if (ci.state == PJSIP_INV_STATE_CONFIRMED) {
		core.StartRecordingIfNeeded();
		// Conferencia: la segunda llamada ya tiene audio; se unen los puertos.
		if (core.pendingConference_) {
			core.MergeCalls();
		}
	}

	core.UpdateTeamsPresence();
	core.Notify();
}
