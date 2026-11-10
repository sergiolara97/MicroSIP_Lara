/*
 * MicroSIP Modern - nucleo SIP (pjsua) sin MFC.
 */

#pragma once

#include <windows.h>
#include <string>
#include <vector>
#include <mutex>
#include <map>

#include <pjsua-lib/pjsua.h>

// Timers del host (WM_TIMER).
constexpr UINT MSIP_TIMER_DTMF = 1;
constexpr UINT MSIP_TIMER_AUTOANSWER = 2;

struct MsipCall {
	int id = -1;
	int state = 0;
	bool incoming = false;
	bool held = false;
	bool connected = false;
	std::string number;
	std::string name;
	std::string stateText;
	long long connectedAt = 0;
};

struct MsipHistoryItem {
	long long time = 0;
	std::string number;
	std::string name;
	std::string type; // in | out | missed
	int duration = 0;
};

struct MsipContact {
	int id = 0;
	std::string name;
	std::string number;
	std::string info;
	bool presence = false;
	std::string presenceStatus = "unknown";
	int buddyId = -1;
};

struct MsipAccount {
	int index = 0;              // N de [AccountN]
	std::wstring section;       // L"AccountN"
	std::string label;
	std::string server;
	std::string domain;
	std::string username;
	std::string password;
	std::string authId;
	std::string displayName;
	std::string transport;
	std::string proxy;
	std::string voicemail;
	std::string dialingPrefix;
	std::string dialPlan;
	std::string hideCid;
	std::string srtp;
	std::string publicAddr;
	int registerRefresh = 300;
	int keepAlive = 15;
	bool publish = false;
	bool ice = false;
	bool allowRewrite = false;
	bool disableSessionTimer = false;
	int accId = -1;             // pjsua_acc_id
	bool needsPassword = false; // importada de MicroSIP: password cifrado
	bool registered = false;
	int regStatus = 0;
	std::string regReason;
};

struct MsipChatMessage {
	long long time = 0;
	std::string peer;           // numero/ext del interlocutor
	std::string from;           // URI original
	std::string body;
	bool outgoing = false;
	std::string status;         // sent | failed
};

struct MsipShortcut {
	std::string label;
	std::string number;
};

class SipCore
{
public:
	static SipCore& Instance();

	bool Init(HWND hwnd, const std::wstring& appDir);
	void Shutdown();
	bool IsRunning() const { return started_; }

	void LoadSettings(const std::wstring& iniPath);
	void LoadAccounts();
	void LoadContacts(const std::wstring& dbPath);

	std::string StateJson();
	std::string HistoryJson();
	std::string ContactsJson();
	std::string MessagesJson();

	// Nombre de la agenda para un numero (vacio si no esta guardado).
	std::string ContactNameFor(const std::string& number) const;

	void Dial(const std::string& number);
	void AnswerIncoming();
	void HangupIncoming();
	void ToggleMute();
	bool Muted() const { return muted_; }
	void Transfer(int id, const std::string& number);
	void StartRecordingIfNeeded();
	void StopRecording();
	void SaveAccount(const MsipAccount& account);
	bool ImportAccountsFromOriginal();
	void ApplyGlobalSettings();
	void SetActiveAccount(int index);
	void SendMessage(const std::string& to, const std::string& body);
	void AddChatMessage(const MsipChatMessage& message);
	bool SaveContact(int id, const std::string& name, const std::string& number,
		const std::string& info, bool presence);
	bool DeleteContact(int id);
	void DeleteHistory(int index);
	void ClearHistory();
	bool ExportHistoryCsv(const std::wstring& path);
	bool ImportHistoryCsv(const std::wstring& path);
	bool ExportContactsCsv(const std::wstring& path);
	bool ImportContactsCsv(const std::wstring& path);
	bool ExportConfig(const std::wstring& path);
	bool ImportConfig(const std::wstring& path);
	bool VoicemailWaiting() const { return voicemail_; }
	void DialVoicemail();
	void SetVoicemailWaiting(bool on);
	void UpdateBuddyStatus(int buddyId, const std::string& status);

	void Answer(int id);
	void Hangup(int id);
	void HangupAll();
	void Hold(int id, bool on);
	void SwapTo(int id);
	void SetMute(bool on);
	void SetMuteOutput(bool on);
	void LogConfLevels();
	void SendDtmf(const std::string& digit);
	void PlayDtmfTone(const std::string& digits);
	void StopDtmfTone();
	void PollDtmfTone();
	void SetVolume(bool output, int value);
	void SetDnd(bool on);
	void SetRecord(bool on);
	void AddCall(const std::string& number);
	void MergeCalls();
	void TransferAttended(int id, const std::string& number);
	void CompleteAttendedTransfer();
	void CancelAttendedTransfer();
	void TransferHeldToActive();
	void SetAutoAnswer(bool on);
	void SetCallWaiting(bool on);
	void ForwardCall(int callId, const std::string& number);
	void Pickup(const std::string& number);
	int FirstActiveCallId();
	void HoldActive();
	void TransferActive(const std::string& number);
	void ApplyCodecs();
	int ActiveCallCount();
	bool HasAnyCall();
	bool HasIncomingCall();
	// Pone el estado de Teams en Ocupado mientras hay llamada establecida.
	void UpdateTeamsPresence();
	void ApplySoundDevice();
	bool Dnd() const { return dnd_; }
	bool AutoAnswer() const { return autoAnswer_; }
	bool CallWaiting() const { return callWaiting_; }
	bool Forwarding() const { return forwarding_ == "all" || forwarding_ == "1"; }
	const std::string& ForwardingNumber() const { return forwardingNumber_; }
	void OnTimer(UINT_PTR id);

	void SetAlwaysOnTop(bool on);
	bool AlwaysOnTop() const { return alwaysOnTop_; }
	void LoadShortcuts();
	void SetShortcut(int index, const std::string& label, const std::string& number);
	void DeleteShortcut(int index);

	void Notify();
	std::string GetSetting(const char* key, const char* def);
	void SetSetting(const char* key, const std::string& value);

private:
	SipCore() = default;
	void ApplyAccounts();
	bool HasConfiguredAccounts(const std::wstring& path) const;
	std::wstring FindOriginalConfig() const;
	bool ImportAccountsFrom(const std::wstring& path);
	void EnsureIniExists();
	MsipAccount* FindAccount(int index);
	const MsipAccount* ActiveAccount() const;
	std::string ResolveUri(const std::string& target) const;
	void AddHistory(const MsipHistoryItem& item);
	void LoadHistory();
	void SaveHistory();
	void LoadMessages();
	void SaveMessages();
	void SubscribePresence();
	void UnsubscribePresence();
	void SaveShortcuts();
	bool ImportShortcutsFrom(const std::wstring& path);
	std::string BuildState();

	HWND hwnd_ = nullptr;
	std::wstring appDir_;
	std::wstring iniPath_;
	std::wstring importedFrom_;
	std::wstring contactsDbPath_;
	bool started_ = false;
	bool dnd_ = false;
	bool record_ = false;
	bool muted_ = false;
	bool mutedOut_ = false;
	bool autoAnswer_ = false;
	bool callWaiting_ = true;
	bool conference_ = false;
	bool pendingConference_ = false;
	bool ringing_ = false;
	bool dtmfConnected_ = false;
	bool voicemail_ = false;
	bool presenceSubscribed_ = false;
	int attendedSource_ = -1;
	int volumeOut_ = 80;
	int volumeIn_ = 60;
	int activeAccountIndex_ = 1;
	int activeAccId_ = -1;
	int sourcePort_ = 0;
	pjsua_recorder_id recId_ = -1;
	std::string lastError_;

	// Ajustes globales (red, audio, general)
	std::string stun_;
	bool enableStun_ = false;
	int rtpPortMin_ = 0;
	int rtpPortMax_ = 0;
	std::string dnsSrv_;
	std::string audioCodecs_;
	std::string forceCodec_;
	std::string dtmfMethod_ = "rfc2833";
	std::string forwarding_ = "none";
	std::string forwardingNumber_;
	std::string featureCodeCP_ = "**";
	int logLevel_ = 3;
	bool ec_ = true;
	bool vad_ = false;
	int inputDev_ = -1;
	int outputDev_ = -1;
	bool startMinimized_ = false;
	bool singleInstance_ = true;
	bool localDtmf_ = true;
	bool alwaysOnTop_ = false;
	pjsua_transport_id udpTransport_ = -1;
	pjsua_transport_id tcpTransport_ = -1;
	pjsua_transport_id tlsTransport_ = -1;

	// Generador de tonos DTMF local (se oye al marcar, como en MicroSIP).
	pj_pool_t* tonePool_ = nullptr;
	pjmedia_port* toneGen_ = nullptr;
	pjsua_conf_port_id toneSlot_ = -1;

	std::mutex mutex_;
	std::vector<MsipAccount> accounts_;
	std::vector<MsipCall> calls_;
	std::vector<MsipHistoryItem> history_;
	std::vector<MsipContact> contacts_;
	std::vector<MsipChatMessage> messages_;
	std::vector<MsipShortcut> shortcuts_;

	// Identidad preferida por llamada (P-Asserted-Identity / Remote-Party-ID).
	struct CallIdentity {
		std::string remote;
		std::string number;
	};
	std::map<int, CallIdentity> callIdentity_;
	mutable std::mutex identityMutex_;

	friend void msip_on_call_state(pjsua_call_id, pjsip_event*);
	friend void msip_on_incoming_call(pjsua_acc_id, pjsua_call_id, pjsip_rx_data*);
	friend void msip_on_call_media_state(pjsua_call_id);
	friend void msip_on_reg_state(pjsua_acc_id);
	friend void msip_on_pager2(pjsua_call_id, const pj_str_t*, const pj_str_t*,
		const pj_str_t*, const pj_str_t*, const pj_str_t*, pjsip_rx_data*, pjsua_acc_id);
};
