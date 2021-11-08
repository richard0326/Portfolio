#pragma once

struct st_JOB;
struct st_PLAYER;
class CSerializeBuffer;
class CChattingServer : public CNetServer
{
public:
	CChattingServer();
	~CChattingServer();

	// ���� �����Ҷ�
	bool Start(const wchar_t* ipWstr, int portNum, int workerCreateCnt, int workerRunningCnt,
		bool bNoDelayOpt, int serverMaxUser, bool bRSTOpt, bool bKeepAliveOpt, bool bOverlappedSend, int SendQSize, int RingBufferSize);
	
	// ���� ���� ��
	void Stop();

	// accept ����
	virtual bool OnConnectionRequest(const wchar_t* ipWstr, int portNum);

	// Accept �� ����ó�� �Ϸ� �� ȣ��.
	virtual void OnClientJoin(unsigned long long SessionID);
	// Release �� ȣ��
	virtual void OnClientLeave(unsigned long long SessionID);

	// ��Ŷ/�޽��� ���� �Ϸ� ��
	virtual int OnRecv(unsigned long long SessionID, CSerializeBuffer* pPacket);
	// ������ �� ȣ��Ǵ� �Լ�
	virtual void OnError(const wchar_t* ipWstr, int portNum, int errorCode, const wchar_t* errorMsg);

	// ��Ʈ��ũ ���̺귯������ �����ִ� ����͸� ����
	void GetMonitoringInfo(long long AcceptTps, long long RecvTps, long long SendTps,
		long long acceptCount, long long disconnectCount, int sessionCount,
		int chunkCount, int chunkNodeCount,
		long long sendBytePerSec, long long recvBytePerSec);

	// ��� ó���� �� Update ������
	static unsigned int __stdcall UpdateThead(void* pParameter);
	// timeout üũ ������
	static unsigned int __stdcall TimeoutThread(void* pParameter);

private:

	// ���� ���
	void JobClientJoin(st_JOB* pJob, DWORD dwTime);
	// ���� ���
	void JobClientLeave(st_JOB* pJob, DWORD dwTime);
	// �б� �Ϸ� ���� ó��
	void JobClientOnRecv(st_JOB* pJob, DWORD dwTime);
	// �ֱ������� TimeOut �����忡�� ������ TimeOut Job
	void JobClientTimeOut(DWORD dwTime);
	// ä�� ��ε�ĳ����
	void ChatBroadcastSector(st_PLAYER* pSendPlayer, CSerializeBuffer* pSendPacket, WORD SectorX, WORD SectorY);
	// �÷��̾� �α׾ƿ�
	void DisconnectPlayer(unsigned long long SessionID);

private:

	// �ڵ�
	HANDLE m_hUpdateThread;
	HANDLE m_hUpdateEvent;
	HANDLE m_hTimeoutThread;
	
	CLockFreeQueue<st_JOB*>* m_pJobQueue;  
	CLockFreeTlsPoolA<st_JOB>* m_pJobPool; 
	CLockFreeTlsPoolA<st_PLAYER>* m_pPlayerPool;

	long long m_eventWakeCount = 0;		// event ��ü�� ���� Ƚ��
	long long m_eventWakePrevCount = 0;
	long long m_updateCount = 0;		// update loop �� Ƚ��
	long long m_updatePrevCount = 0;

#ifdef MY_DEBUG
	long long m_broadcastCount = 0;		// ��ε�ĳ���� Ƚ��
	long long m_broadcastPrevCount = 0;
#endif

	COrderedmap<unsigned long long, st_PLAYER*> m_mLoginMap;		// ������ �õ��ϴ� ����
	COrderedmap<unsigned long long, st_PLAYER*> m_mPlayerMap;		// ���ӿ� ������ ����

	CList<st_PLAYER*> m_sectorArr[dfSECTOR_X_MAX][dfSECTOR_Y_MAX];	// ���� ����

#ifdef  MY_DEBUG
	int m_countTimeOut = 0;
#endif
	int m_playerTimeOut = 0;		// ���� �õ��ϴ� ������ ���� timeOut
	int m_loginTimeOut = 0;			// ���� ������ ������ ���� timeOut
	bool m_shutdown;				// ���� ���� ����
};