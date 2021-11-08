#pragma once

// ��1�� �ٸ��� ���͸��� ����ȭ�� �ɾ�����ϱ� ������ st_SECTOR ����ü�� �ʿ���. 
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

	// ���� �����Ҷ�
	bool Start(const wchar_t* ipWstr, int portNum, int workerCreateCnt, int workerRunningCnt,
		bool bNoDelayOpt, int serverMaxUser, bool bRSTOpt, bool bKeepAliveOpt, bool bOverlappedSend, int SendQSize);
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
		int workerCreatCnt, int workerRunningCnt,
		long long sendBytePerSec, long long recvBytePerSec);

	// timeout üũ ������
	static unsigned int __stdcall TimeoutThread(void* pParameter);

private:
	// ��ü ������ ���� timeOut �Լ�
	void CheckTimeOut();
	// ä�� ��ε�ĳ����
	void ChatBroadcastSector(CSerializeBuffer* pSendPacket, WORD SectorX, WORD SectorY);
	// �÷��̾� �α׾ƿ�
	void DisconnectPlayer(unsigned long long SessionID);

	// ���� ���� �÷��̾� �����ϰ� ��������
	st_PLAYER* AcquireLoginLock(unsigned long long SessionID);
	// ���ӿ� ������ �÷��̾� �����ϰ� ��������
	st_PLAYER* AcquirePlayerLock(unsigned long long SessionID);

	// �÷��̾� ����ī��Ʈ ����
	void IODecreasePlayer(st_PLAYER* pPlayer);
	// ���Ͱ��� �̵��Ҷ� ����� �ɸ��� �ʰ� �� �ɱ�
	void DoubleLock(WORD SectorX1, WORD SectorY1, WORD SectorX2, WORD SectorY2);

private:
	// �ڵ�
	HANDLE m_hMoritoring;
	
	CLockFreeTlsPoolA<st_PLAYER>* m_pPlayerPool;

	// stl map ���
	unordered_map<int, st_PLAYER*> m_umLoginList;	
	unordered_map<int, st_PLAYER*> m_umPlayerList;

	CRITICAL_SECTION m_loginCS;			// ���� ���� ������ ���� ����ȭ ��ü
	CRITICAL_SECTION m_playerCS;		// ���ӿ� ������ ������ ���� ����ȭ ��ü
	st_SECTOR m_sectorArr[dfSECTOR_X_MAX][dfSECTOR_Y_MAX];	// ���� ����

	int m_playerTimeOut = 0;	// ���� �õ��ϴ� ������ ���� timeOut
	int m_loginTimeOut = 0;		// ���� ������ ������ ���� timeOut
	bool m_shutdown;			// ���� ���� ����
};