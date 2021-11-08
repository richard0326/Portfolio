#pragma once

struct st_JOB;
struct st_PLAYER;
class CSerializeBuffer;
class CChattingServer : public CNetServer
{
public:
	CChattingServer();
	~CChattingServer();

	// 서버 시작할때
	bool Start(const wchar_t* ipWstr, int portNum, int workerCreateCnt, int workerRunningCnt,
		bool bNoDelayOpt, int serverMaxUser, bool bRSTOpt, bool bKeepAliveOpt, bool bOverlappedSend, int SendQSize, int RingBufferSize);
	
	// 서버 멈출 때
	void Stop();

	// accept 직후
	virtual bool OnConnectionRequest(const wchar_t* ipWstr, int portNum);

	// Accept 후 접속처리 완료 후 호출.
	virtual void OnClientJoin(unsigned long long SessionID);
	// Release 후 호출
	virtual void OnClientLeave(unsigned long long SessionID);

	// 패킷/메시지 수신 완료 후
	virtual int OnRecv(unsigned long long SessionID, CSerializeBuffer* pPacket);
	// 에러일 때 호출되는 함수
	virtual void OnError(const wchar_t* ipWstr, int portNum, int errorCode, const wchar_t* errorMsg);

	// 네트워크 라이브러리에서 보내주는 모니터링 정보
	void GetMonitoringInfo(long long AcceptTps, long long RecvTps, long long SendTps,
		long long acceptCount, long long disconnectCount, int sessionCount,
		int chunkCount, int chunkNodeCount,
		long long sendBytePerSec, long long recvBytePerSec);

	// 모든 처리를 할 Update 스레드
	static unsigned int __stdcall UpdateThead(void* pParameter);
	// timeout 체크 스레드
	static unsigned int __stdcall TimeoutThread(void* pParameter);

private:

	// 들어온 경우
	void JobClientJoin(st_JOB* pJob, DWORD dwTime);
	// 나간 경우
	void JobClientLeave(st_JOB* pJob, DWORD dwTime);
	// 읽기 완료 통지 처리
	void JobClientOnRecv(st_JOB* pJob, DWORD dwTime);
	// 주기적으로 TimeOut 스레드에서 보내는 TimeOut Job
	void JobClientTimeOut(DWORD dwTime);
	// 채팅 브로드캐스팅
	void ChatBroadcastSector(st_PLAYER* pSendPlayer, CSerializeBuffer* pSendPacket, WORD SectorX, WORD SectorY);
	// 플레이어 로그아웃
	void DisconnectPlayer(unsigned long long SessionID);

private:

	// 핸들
	HANDLE m_hUpdateThread;
	HANDLE m_hUpdateEvent;
	HANDLE m_hTimeoutThread;
	
	CLockFreeQueue<st_JOB*>* m_pJobQueue;  
	CLockFreeTlsPoolA<st_JOB>* m_pJobPool; 
	CLockFreeTlsPoolA<st_PLAYER>* m_pPlayerPool;

	long long m_eventWakeCount = 0;		// event 객체를 깨운 횟수
	long long m_eventWakePrevCount = 0;
	long long m_updateCount = 0;		// update loop 돈 횟수
	long long m_updatePrevCount = 0;

#ifdef MY_DEBUG
	long long m_broadcastCount = 0;		// 브로드캐스팅 횟수
	long long m_broadcastPrevCount = 0;
#endif

	COrderedmap<unsigned long long, st_PLAYER*> m_mLoginMap;		// 접속을 시도하는 유저
	COrderedmap<unsigned long long, st_PLAYER*> m_mPlayerMap;		// 접속에 성공한 유저

	CList<st_PLAYER*> m_sectorArr[dfSECTOR_X_MAX][dfSECTOR_Y_MAX];	// 섹터 영역

#ifdef  MY_DEBUG
	int m_countTimeOut = 0;
#endif
	int m_playerTimeOut = 0;		// 접속 시도하는 유저에 대한 timeOut
	int m_loginTimeOut = 0;			// 접속 성공한 유저에 대한 timeOut
	bool m_shutdown;				// 서버 정지 여부
};