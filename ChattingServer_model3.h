#pragma once

// 모델1과 다르게 섹터마다 동기화를 걸어줘야하기 때문에 st_SECTOR 구조체가 필요함. 
struct st_PLAYER;
struct st_SECTOR
{
	CRITICAL_SECTION sectorCS;
	CList<st_PLAYER*> playerList;
};

class CSerializeBuffer;
class CChattingServer : public CNetServer
{
public:
	CChattingServer();
	~CChattingServer();

	// 서버 시작할때
	bool Start(const wchar_t* ipWstr, int portNum, int workerCreateCnt, int workerRunningCnt,
		bool bNoDelayOpt, int serverMaxUser, bool bRSTOpt, bool bKeepAliveOpt, bool bOverlappedSend, int SendQSize);
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
		int workerCreatCnt, int workerRunningCnt,
		long long sendBytePerSec, long long recvBytePerSec);

	// timeout 체크 스레드
	static unsigned int __stdcall TimeoutThread(void* pParameter);

private:
	// 전체 유저에 대한 timeOut 함수
	void CheckTimeOut();
	// 채팅 브로드캐스팅
	void ChatBroadcastSector(CSerializeBuffer* pSendPacket, WORD SectorX, WORD SectorY);
	// 플레이어 로그아웃
	void DisconnectPlayer(unsigned long long SessionID);

	// 접속 중인 플레이어 정당하게 가져오기
	st_PLAYER* AcquireLoginLock(unsigned long long SessionID);
	// 접속에 성공한 플레이어 정당하게 가져오기
	st_PLAYER* AcquirePlayerLock(unsigned long long SessionID);

	// 플레이어 참조카운트 감소
	void IODecreasePlayer(st_PLAYER* pPlayer);
	// 섹터간에 이동할때 데드락 걸리지 않고 락 걸기
	void DoubleLock(WORD SectorX1, WORD SectorY1, WORD SectorX2, WORD SectorY2);

private:
	// 핸들
	HANDLE m_hMoritoring;
	
	CLockFreeTlsPoolA<st_PLAYER>* m_pPlayerPool;

	// stl map 사용
	unordered_map<int, st_PLAYER*> m_umLoginList;	
	unordered_map<int, st_PLAYER*> m_umPlayerList;

	CRITICAL_SECTION m_loginCS;			// 접속 중인 유저에 대한 동기화 객체
	CRITICAL_SECTION m_playerCS;		// 접속에 성공한 유저에 대한 동기화 객체
	st_SECTOR m_sectorArr[dfSECTOR_X_MAX][dfSECTOR_Y_MAX];	// 섹터 영역

	int m_playerTimeOut = 0;	// 접속 시도하는 유저에 대한 timeOut
	int m_loginTimeOut = 0;		// 접속 성공한 유저에 대한 timeOut
	bool m_shutdown;			// 서버 정지 여부
};