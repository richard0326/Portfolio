#pragma once

enum {
	NETHEADER = 5,
};

struct st_SESSION;
class CSerializeBuffer; 
class CRingBuffer;
template<typename T> class CLockFreeQueue;
class CNetServer
{
protected:
	CNetServer();
	~CNetServer();

public:
	bool Init();
	void Release();
	// 오픈 IP / 포트 / 워커스레드 수(생성수, 러닝수) / 나글옵션 / 최대접속자 수
	bool Start(const wchar_t* ipWstr, int portNum, int workerCreateCnt, int workerRunningCnt,
		bool bNoDelayOpt, int serverMaxUser, bool bRSTOpt, bool bKeepAliveOpt, bool bOverlappedSend, int sendQsize, int RingBufferSize);
	void Stop();

	// accept 직후
	virtual bool OnConnectionRequest(const wchar_t* ipWstr, int portNum) = 0;

	// Accept 후 접속처리 완료 후 호출.
	virtual void OnClientJoin(unsigned long long SessionID) = 0;
	// Release 후 호출
	virtual void OnClientLeave(unsigned long long SessionID) = 0;

	// 패킷 수신 완료 후
	virtual int OnRecv(unsigned long long SessionID, CSerializeBuffer* pPacket) = 0;

	virtual void OnError(const wchar_t* ipWstr, int portNum, int errorCode, const wchar_t* errorMsg) = 0;

	virtual void GetMonitoringInfo(long long AcceptTps, long long RecvTps, long long SendTps,
		long long acceptCount, long long disconnectCount, int sessionCount,
		int chunkCount, int chunkNodeCount,
		long long sendBytePerSec, long long recvBytePerSec) = 0;

private:
	static unsigned __stdcall monitoringThread(void* pParameter);
	static unsigned __stdcall netAcceptThread(void* pParameter);
	static unsigned __stdcall netIOThread(void* pParameter);

	int netWSARecv(st_SESSION* pSession);
	int netWSARecvPost(st_SESSION* pSession);
protected:
	int netWSASendPacket(unsigned long long SessionID, CSerializeBuffer* pPacket);
	int netWSASendEnq(unsigned long long SessionID, CSerializeBuffer* pPacket);
private:
	int netWSASendPost(st_SESSION* pSession);
	// 세션 생성
	st_SESSION* CreateSession(SOCKET clientSocket);

protected:
	bool GetIP_Port(unsigned long long SessionID, wchar_t* outIpStr, unsigned short* outPortNum);
	// 세션 연결 종료 처리
	void DisconnectSession(unsigned long long SessionID);
private:
#ifdef MY_DEBUG
	void DisconnectSession(st_SESSION* pSession);

	void DecreaseIOCount(st_SESSION* pSession);
	void PrintError(st_SESSION* pSession, const wchar_t* errStr, int errorCode);

	st_SESSION* AcquireLock(unsigned long long SessionID);
	void ReleaseLock(st_SESSION* pSession);
#else
	__forceinline void DisconnectSession(st_SESSION* pSession);

	__forceinline void DecreaseIOCount(st_SESSION* pSession);
	__forceinline void PrintError(st_SESSION* pSession, const wchar_t* errStr, int errorCode);

	__forceinline st_SESSION* AcquireLock(unsigned long long SessionID);
	__forceinline void ReleaseLock(st_SESSION* pSession);
#endif	

private:
	SOCKET		m_ListenSocket;
	sockaddr_in m_ListenInfo;
	HANDLE		m_hIOCP;
	HANDLE		m_hAcceptThread;
	HANDLE		m_hMonitoringThread;
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

	st_SESSION* m_SessionArray;
	CLockFreeStack<int>* m_idxStack;
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

	__declspec(align(64)) int m_SessionCnt = 0;
#ifdef MY_DEBUG
	__declspec(align(64)) long long m_DisconnectCnt = 0;
#endif
	__declspec(align(64)) unsigned long long m_SessionID = 1;
};