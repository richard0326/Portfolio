#pragma once

enum {
	NETHEADER = 5,
};

class CSerializeBuffer;
class CRingBuffer;
template<typename T> class CLockFreeQueue;
class CMMOServer
{
	enum class en_SESSION_STATE
	{
		MODE_NONE = 0,
		MODE_AUTH, 
		MODE_AUTH_TO_GAME, 
		MODE_GAME, 
		MODE_AUTH_TO_RELEASE,
		MODE_GAME_TO_RELEASE,
		MODE_RELEASE
	};

	enum {
		WSABUF_SIZE = 200,
		RET_SOCKET_ERROR = 0,
		RET_SUCCESS = 1,
	};

	struct OVERLAPPEDEX : public OVERLAPPED
	{
		bool recvFlags;
	};
	enum class DEBUG_LOCATION
	{
		IO_TRANS_SEND_ZERO = 1,
		IO_TRANS_RECV_ZERO,
		IO_RECV_COMPLETE,
		IO_RECV_RECVPACKET,
		IO_SEND_COMPLETE,

		AUTH_CREATESESSION,
		AUTH_RECVPOST,
		AUTH_DISCONNECT,
		AUTH_AUTH2GAME,

		GAME_AUTH2GAME,
		GAME_AUTH2REL,
		GAME_GAME2REL,
		GAME_DISCONNECT,
		GAME_RELEASESESSION_BEGIN,
		GAME_RELEASESESSION_END,

		SEND_BEF_SENDPOST,
		SEND_SENDPOST,
		CALL_DISCONNECT,

		DISCONNECT_IO_COUNT,
		DISCONNECT_SENDPACKET_FAIL,
		DISCONNECT_SENDPOST_FAIL,
		DISCONNECT_CREATE_RECVPOST_FAIL,
		DISCONNECT_IO_RECVPOST_FAIL,
	};

protected:
	struct st_SESSION
	{
		friend class CMMOServer;
	public:
		st_SESSION();
		virtual ~st_SESSION();

		void SendPacket(CSerializeBuffer* pSendPacket);
		void DecreaseIOCount(void);
		void Disconnect(void);
		void SetMode_Game();

		virtual void OnAuth_ClientJoin(void) = 0;
		virtual void OnAuth_ClientLeave(void) = 0;
		virtual bool OnAuth_Packet(CSerializeBuffer* pRecvPacket) = 0;
		virtual void OnGame_ClientJoin(void) = 0;
		virtual void OnGame_ClientLeave(void) = 0;
		virtual bool OnGame_Packet(CSerializeBuffer* pRecvPacket) = 0;
		virtual void OnGame_Release(void) = 0;
	private:
		// 읽기 전용 데이터
		CLockFreeQueue<CSerializeBuffer*>* SendQ; // 송신 큐.
		CQueue<CSerializeBuffer*>* CompleteRecvQ; // 완료송신 큐.
		CRingBuffer* RecvQ; // 수신 큐.

		unsigned long long SessionID = 0; // 접속자의 고유 세션 ID.
		SOCKET Socket = 0; // 현 접속의 TCP 소켓.
		wchar_t IpStr[16];
		u_short usPort;
		bool isShutdown;

		int PacketArrLen;
		CSerializeBuffer* pPacketArr[WSABUF_SIZE];

		OVERLAPPEDEX RecvOverlappedEx;	// 수신 오버랩드 구조체
		OVERLAPPEDEX SendOverlappedEx;	// 송신 오버랩드 구조체

		bool IOSend = false;
		bool ModeToGameFlag = false;
		bool DisconnectFlag = false;
		short IOCount = 0;
		short Index = 0;
		en_SESSION_STATE State;

#ifdef MY_DEBUG
		struct st_ForDebug
		{
			DWORD threadID = 0;
			unsigned int debugID = 0;
			DEBUG_LOCATION location;
			short IOCount = 0;
			bool IOSend = 0; 
			bool ModeToGameFlag = false;
			bool DisconnectFlag = false;
			bool isShutdown = 0;
			int RecvQSize = 0;
			int SendQSize = 0;
			int CompQSize = 0;
			SOCKET workSocket = 0;
			DWORD workID = 0;
			en_SESSION_STATE State;
		};

		__declspec(align(64)) unsigned int debugID = 0;
		st_ForDebug debugSendArr[100];
		st_ForDebug debugRecvArr[100];
		st_ForDebug debugNonIOArr[100];
		DWORD tlsIndex;
		__declspec(align(64)) unsigned int debugSendCount = 0;
		__declspec(align(64)) unsigned int debugRecvCount = 0;
		__declspec(align(64)) unsigned int debugNonIOCount = 0;
#endif

	public:
		void DebugCheck(DEBUG_LOCATION location);
	};

protected:
	CMMOServer();
	~CMMOServer();

public:
	bool Init();
	void Release();
	// 오픈 IP / 포트 / 워커스레드 수(생성수, 러닝수) / 나글옵션 / 최대접속자 수
	bool Start(const wchar_t* ipWstr, int portNum, int workerCreateCnt, int workerRunningCnt,
		bool bNoDelayOpt, int serverMaxUser, bool bRSTOpt, bool bKeepAliveOpt, bool bOverlappedSend, 
		int sendQsize, int RingBufferSize, int CompleteRecvQSize, int SocketQSize);
	void Stop();

	// accept 직후
	virtual bool OnConnectionRequest(const wchar_t* ipWstr, int portNum) = 0;

	virtual void OnError(const wchar_t* ipWstr, int portNum, int errorCode, const wchar_t* errorMsg) = 0;

	virtual void GetMonitoringInfo(long long AcceptTps, long long RecvTps, long long SendTps,
		long long AuthTps, long long GameTps,
		long long acceptCount, long long disconnectCount, int sessionCount,
		int chunkCount, int chunkNodeCount,
		long long sendBytePerSec, long long recvBytePerSec) = 0;

private:
	static unsigned __stdcall monitoringThread(void* pParameter);
	static unsigned __stdcall netAcceptThread(void* pParameter);
	static unsigned __stdcall netIOThread(void* pParameter);
	static unsigned __stdcall netAuthThread(void* pParameter);
	static unsigned __stdcall netGameThread(void* pParameter);
	static unsigned __stdcall netSendThread(void* pParameter);

	// 세션 생성
	__forceinline CMMOServer::st_SESSION* CreateSession(SOCKET clientSocket);
	__forceinline void ReleaseSession(st_SESSION* pSession);
	int netWSARecv(st_SESSION* pSession);
	int netWSARecvPost(st_SESSION* pSession);
	int netWSASendPost(st_SESSION* pSession);
protected:
	// 외부에서 SessionArray를 초기화해줘야한다.
	st_SESSION** GetSessionPPArr(int maxUser) { 
		m_ppSessionArray = new st_SESSION * [maxUser];
		return m_ppSessionArray; 
	}
private:
	__forceinline void PrintError(st_SESSION* pSession, const wchar_t* errStr, int errorCode);

private:
	SOCKET		m_ListenSocket;
	sockaddr_in m_ListenInfo;
	HANDLE		m_hIOCP;
	HANDLE		m_hAcceptThread;
	HANDLE		m_hMonitoringThread;
	HANDLE		m_hAuthThread;
	HANDLE		m_hGameThread;
	HANDLE		m_hSendThread;
	HANDLE*		m_hIOThread;

	wchar_t m_ipWstr[20];
	int m_portNum;
	bool m_bNoDelayOpt;
	bool m_bRSTOpt;
	bool m_bKeepAliveOpt;
	bool m_bOverlappedSend;
	int m_MaxSession;
	int m_workerCreateCnt;
	int m_workerRunningCnt;

	st_SESSION** m_ppSessionArray = nullptr;
	CLockFreeStack<int>* m_idxStack;
	CLockFreeQueue<SOCKET>* m_pSocketQueue;
	CLockFreeTlsPoolA<CSerializeBuffer>* m_packetTlsPool;
	DWORD m_tlsIndex = 0;
#ifdef MY_DEBUG
	DWORD m_tlsDebugIndex = 0;
#endif

	// Tps
	DWORD m_tlsSendTpsIndex = 0;
	DWORD m_tlsRecvTpsIndex = 0;
	long long* m_pTlsIndexTpsArr;
	long long* m_pTlsSendTpsArr;
	long long* m_pSaveSendTpsArr;
	long long* m_pTlsRecvTpsArr;
	long long* m_pSaveRecvTpsArr;

#ifdef MY_DEBUG
	// 네트워크 트래픽
	DWORD m_tlsSendBPSIndex = 0;
	DWORD m_tlsRecvBPSIndex = 0;
	long long* m_pSendBytePerSecArr;
	long long* m_pSaveSendBytePerSecArr;
	long long* m_pRecvBytePerSecArr;
	long long* m_pSaveRecvBytePerSecArr;
#endif
	// 값이 지속적으로 변하는 변수들
	int m_tlsCount = 0;
	long long m_acceptCount = 0;
	long long m_acceptPrevCount = 0;

	long long m_authCount = 0;
	long long m_authPrevCount = 0;

	long long m_gameCount = 0;
	long long m_gamePrevCount = 0;

	__declspec(align(64)) int m_SessionCnt = 0;
#ifdef MY_DEBUG
	__declspec(align(64)) long long m_DisconnectCnt = 0;
#endif
	__declspec(align(64)) unsigned long long m_SessionID = 1;
};