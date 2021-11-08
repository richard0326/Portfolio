#include "stdafx.h"
#include "ChattingServer.h"
#include "MakePacket.h"
#include "PacketProc.h"

#include <mmsystem.h>
#pragma comment(lib, "Winmm.lib")

enum en_JOB
{
	JOB_CLIENT_JOIN,
	JOB_CLIENT_LEAVE,
	JOB_ONRECV,
	JOB_TIMEOUT,
};

struct st_JOB
{
	en_JOB jobType;
	unsigned long long SessionID;
	CSerializeBuffer* pPacket;
};

#ifdef MY_DEBUG
// �޸� �α� enum ��
enum DEBUG_UPDATE_LOCATION
{
	JOB_JOIN = 1,

	JOB_ONRECV_REQ_LOGIN,
	JOB_ONRECV_REQ_LOGIN_BEFORE_SEND,
	JOB_ONRECV_REQ_LOGIN_SENDFAIL,

	JOB_ONRECV_REQ_SECTOR_MOVEPLAYER,
	JOB_ONRECV_REQ_SECTOR_REMOVE_SECTOR,
	JOB_ONRECV_REQ_SECTOR_REPLACE_SECTOR,
	JOB_ONRECV_REQ_SECTOR_BEFORE_SEND,
	JOB_ONRECV_REQ_SECTOR_SENDFAIL,

	BROADCAST_MSG_SENDFAIL,
	JOB_ONRECV_REQ_CHAT_MSG_BEFORE_SEND,

	JOB_ONRECV_REQ_HEARTBEAT,

	JOB_TIMEOUT_ERASE_LOGIN,
	JOB_TIMEOUT_ERASE_PLAYER,

	DISCONNECT_PLAYER_ERASE_LOGIN,
	DISCONNECT_PLAYER_ERASE_PLAYER,
};

#endif

struct st_PLAYER
{
#ifdef MY_DEBUG
	bool UseFlag; // ��� ���� �÷��� - �޸�Ǯ���� Ȯ���ϱ� ���� �뵵
#endif

	DWORD timeout; // timeOut ����
	WORD SectorX; // ���� ���� ��ġ x, y
	WORD SectorY;

	
	char SessionKey[64]; // ���� ���� Ű
	unsigned long long SessionID; // ��Ʈ��ũ ���̺귯������ ���� Session ID
	INT64 AccountNo; // ���� ��ȣ
	wchar_t ID[20];  // ���� id
	wchar_t Nickname[20]; // ���� �г���

#ifdef MY_DEBUG
	struct st_ForDebug
	{
		unsigned int debugID;
		DEBUG_UPDATE_LOCATION location;
		unsigned long long SessionID;
		//INT64 AccountNo;
		//WORD SectorX;
		//WORD SectorY;
#if defined(MY_DEBUG) && defined(MY_PACKET_DEBUG)
		CSerializeBuffer copyPacket;
#endif
	};

	__declspec(align(64)) unsigned int debugID = 0;
	st_ForDebug debugUpdateArr[100];
#endif
};

#ifdef MY_DEBUG
void DebugCheck(DEBUG_UPDATE_LOCATION location, st_PLAYER* pPlayer, CSerializeBuffer* pPacket);
#define DEBUG(location, pPlayer, pPacket) DebugCheck((DEBUG_UPDATE_LOCATION)location, pPlayer, pPacket)
#else
#define DEBUG(location, pSession) ;
#endif

#ifdef MY_DEBUG
st_PLAYER* g_playerTemp[100];
LONG	g_tempCount;
#endif

CChattingServer::CChattingServer()
{
}

CChattingServer::~CChattingServer()
{
	CloseHandle(m_hTimeoutThread);
	CloseHandle(m_hUpdateEvent);
	CloseHandle(m_hUpdateThread);
}

// ������ �����Ҷ�
bool CChattingServer::Start(const wchar_t* ipWstr, int portNum, int workerCreateCnt, int workerRunningCnt,
	bool bNoDelayOpt, int serverMaxUser, bool bRSTOpt, bool bKeepAliveOpt, bool bOverlappedSend, int SendQSize, int RingBufferSize)
{
	// chattingInfo.txt ���Ϸ� ���� ������ �о�´�.
	CParser chattingParser;
	
	wchar_t* errMsg = nullptr;
	if (chattingParser.ReadBuffer(L"chattingInfo.txt", &errMsg) == false)
	{
		LOG(L"System", en_LOG_LEVEL::LEVEL_SYSTEM, L"chattingParser Err : %s", errMsg);
		return false;
	}

	int JOBQUEUE_SIZE = 0;
	if (chattingParser.GetValue(L"Chatting", L"JOBQUEUE_SIZE", &JOBQUEUE_SIZE) == false)
		return false;

	int JOBPOOL_CHUNK = 0;
	if (chattingParser.GetValue(L"Chatting", L"JOBPOOL_CHUNK", &JOBPOOL_CHUNK) == false)
		return false;

	int JOBPOOL_NODE = 0;
	if (chattingParser.GetValue(L"Chatting", L"JOBPOOL_NODE", &JOBPOOL_NODE) == false)
		return false;

	int PLAYERPOOL_CHUNK = 0;
	if (chattingParser.GetValue(L"Chatting", L"PLAYERPOOL_CHUNK", &PLAYERPOOL_CHUNK) == false)
		return false;

	int PLAYERPOOL_NODE = 0;
	if (chattingParser.GetValue(L"Chatting", L"PLAYERPOOL_NODE", &PLAYERPOOL_NODE) == false)
		return false;
	
	if (chattingParser.GetValue(L"Chatting", L"TIMEOUT_LOGIN", &m_loginTimeOut) == false)
		return false;

	if (chattingParser.GetValue(L"Chatting", L"TIMEOUT_PLAYER", &m_playerTimeOut) == false)
		return false;

	LOG(L"System", en_LOG_LEVEL::LEVEL_SYSTEM, L"[ Chatting System ] Timeout Login : %d, Player : %d", m_loginTimeOut, m_playerTimeOut);
	m_shutdown = false;

	// �̸� �����Ҵ��ϱ�
	m_mLoginMap.allocFreeNode(serverMaxUser);
	m_mPlayerMap.allocFreeNode(serverMaxUser);

	// �̸� �����Ҵ��ϱ�
	for(int i = 0; i < dfSECTOR_X_MAX; ++i)
		for (int j = 0; j < dfSECTOR_Y_MAX; ++j)
			m_sectorArr[i][j].allocFreeNode(100);

	// �⺻ �ʱ�ȭ
	m_pJobQueue = new CLockFreeQueue<st_JOB*>(JOBQUEUE_SIZE);
	LOG(L"System", en_LOG_LEVEL::LEVEL_SYSTEM, L"[ Chatting System ] jobQ size : %d", JOBQUEUE_SIZE);
	m_pJobPool = new CLockFreeTlsPoolA<st_JOB>(JOBPOOL_CHUNK, JOBPOOL_NODE);
	LOG(L"System", en_LOG_LEVEL::LEVEL_SYSTEM, L"[ Chatting System ] jobPool Chunk size : %d, Node Size : %d", JOBPOOL_CHUNK, JOBPOOL_NODE);
	m_pPlayerPool = new CLockFreeTlsPoolA<st_PLAYER>(PLAYERPOOL_CHUNK, PLAYERPOOL_NODE);
	LOG(L"System", en_LOG_LEVEL::LEVEL_SYSTEM, L"[ Chatting System ] playerPool Chunk size : %d, Node Size : %d", PLAYERPOOL_CHUNK, PLAYERPOOL_NODE);
	
	// event ��ü �ʱ�ȭ
	m_hUpdateEvent = CreateEvent(nullptr, false, false, nullptr);
	
	// Update, Timeout ������ ����
	m_hUpdateThread = (HANDLE)_beginthreadex(nullptr, 0, CChattingServer::UpdateThead, this, 0, nullptr);
	m_hTimeoutThread = (HANDLE)_beginthreadex(nullptr, 0, CChattingServer::TimeoutThread, this, 0, nullptr);

	// ���̺귯�� �ʱ�ȭ
	return CNetServer::Start(ipWstr, portNum, workerCreateCnt, workerRunningCnt, bNoDelayOpt, serverMaxUser, bRSTOpt, bKeepAliveOpt, bOverlappedSend, SendQSize, RingBufferSize);
}

// ������ �����Ҷ�
void CChattingServer::Stop()
{
	// Jobť ����ֱ�
	while (true)
	{
		if (m_pJobQueue->GetSize() == 0)
			break;

		Sleep(1000);
	}

	m_shutdown = true;
	CloseHandle(m_hTimeoutThread);
	CloseHandle(m_hUpdateEvent);
	CloseHandle(m_hUpdateThread);

	CNetServer::Stop();
}

// accept ����
bool CChattingServer::OnConnectionRequest(const wchar_t* ipWstr, int portNum)
{
	// �ź��� ������ ������ �� �ִ�.
	// ���� �ð�. White IP. Black IP

	return true;
}

// Accept �� ����ó�� �Ϸ� �� ȣ��.
void CChattingServer::OnClientJoin(unsigned long long SessionID)
{
	st_JOB* pJob = m_pJobPool->Alloc();
	if (pJob == nullptr)
	{
		LOG(L"stJOBAlloc", en_LOG_LEVEL::LEVEL_ERROR, L"[ IP %s, Port %d ] OnClientJoin Alloc Fail");
		CCrashDump::Crash();
	}
	pJob->jobType = JOB_CLIENT_JOIN;
	pJob->SessionID = SessionID;
	pJob->pPacket = nullptr;

	// ������ �õ��ϴ� JOB_CLIENT_JOIN �־��ش�.
	if (m_pJobQueue->Enqueue(pJob) == false)
		CCrashDump::Crash();

	SetEvent(m_hUpdateEvent);
}

// Release �� ȣ��
void CChattingServer::OnClientLeave(unsigned long long SessionID)
{
	st_JOB* pJob = m_pJobPool->Alloc();
	if (pJob == nullptr)
	{
		LOG(L"stJOBAlloc", en_LOG_LEVEL::LEVEL_ERROR, L"[ IP %s, Port %d ] OnClientLeave Alloc Fail");
		CCrashDump::Crash();
	}
	pJob->jobType = JOB_CLIENT_LEAVE;
	pJob->SessionID = SessionID;
	pJob->pPacket = nullptr;

	// ������ �����ϴ� JOB_CLIENT_LEAVE �־��ش�.
	if (m_pJobQueue->Enqueue(pJob) == false)
		CCrashDump::Crash();

	SetEvent(m_hUpdateEvent);
}

// ��Ŷ ���� �Ϸ� ��
int CChattingServer::OnRecv(unsigned long long SessionID, CSerializeBuffer* pPacket)
{
	// CSerializeBuffer�� ��Ʈ��ũ ���̺귯���� ����� ������ �κи� �´�.

	st_JOB* pJob = m_pJobPool->Alloc();
	if (pJob == nullptr)
	{
		LOG(L"stJOBAlloc", en_LOG_LEVEL::LEVEL_ERROR, L"[ IP %s, Port %d ] OnRecv Alloc Fail");
		CCrashDump::Crash();
	}
	pJob->jobType = JOB_ONRECV;
	pJob->SessionID = SessionID;
	pJob->pPacket = pPacket;

	// ���� �Ϸ��������� ���� ��Ŷ�� Update ������� �Ѱ��ش�.
	pPacket->IncreaseRefCount();
	if (m_pJobQueue->Enqueue(pJob) == false)
		CCrashDump::Crash();

	SetEvent(m_hUpdateEvent);

	return 1; // ���� ����
}

void CChattingServer::OnError(const wchar_t* ipWstr, int portNum, int errorCode, const wchar_t* errorMsg)
{
	LOG(L"OnError", en_LOG_LEVEL::LEVEL_ERROR, L"[ IP %s, Port %d ] errMsg  %s , errCode %d ", ipWstr, portNum, errorMsg, errorCode);
}

// ��Ʈ��ũ ���̺귯���� ���� ����͸� ���� ȹ��
void CChattingServer::GetMonitoringInfo(long long AcceptTps, long long RecvTps, long long SendTps,
	long long acceptCount, long long disconnectCount, int sessionCount,
	int chunkCount, int chunkNodeCount,
	long long sendBytePerSec, long long recvBytePerSec)
{
	// event ����ȭ ������ �ƿ� Ƚ��
	long long temp = m_eventWakeCount;
	long long eventWakeTps = temp - m_eventWakePrevCount;
	m_eventWakePrevCount = temp;

	// update loop �� �ƿ� Ƚ��
	temp = m_updateCount;
	long long updateTps = temp - m_updatePrevCount;
	m_updatePrevCount = temp;

#ifdef MY_DEBUG
	// ��ε�ĳ������ Ƚ��
	temp = m_broadcastCount;
	long long broadcastCnt = temp - m_broadcastPrevCount;
	m_broadcastPrevCount = temp;

	// ���Ϳ� �����ִ� ��� player ���� Ȯ��	long long sum = 0;
	long long freeSum = 0;
	for (int i = 0; i < dfSECTOR_X_MAX; ++i)
		for (int j = 0; j < dfSECTOR_Y_MAX; ++j)
		{
			sum += (long long)m_sectorArr[i][j].size();
			freeSum += (long long)m_sectorArr[i][j].freesize();
		}
#endif

	// ����͸� ���� ���...
	printf("=========================================== \n");
	printf(" - Server Library -\n");
	printf(" Key Info                   : U / L ( Unlock / Lock ) \n");
	printf(" Send / Recv / Accept TPS   : %lld / %lld / %d\n", SendTps, RecvTps, AcceptTps);
	printf(" Session Count              : %d \n", sessionCount);
#ifdef MY_DEBUG
	printf(" Accept / Disconnect Count  : %lld / %lld \n", acceptCount, disconnectCount);
	printf(" Packet Chunk / Node Count  : %d / %d \n", chunkCount, chunkNodeCount);
#else
	printf(" Accept Count               : %lld \n", acceptCount);
	printf(" Packet Chunk Count         : %d \n", chunkCount);
#endif
#ifdef MY_DEBUG
	printf(" Send / Recv BytePerSec     : %lld / %lld \n", sendBytePerSec, recvBytePerSec);
#endif
	printf("\n - Chatting Server -\n");
	printf(" Job Queue Size             : %d \n", m_pJobQueue->GetSize());

#ifdef MY_DEBUG
	printf(" Login Size / Free          : %d / %d \n", m_mLoginMap.size(), m_mLoginMap.GetFreeSize());
	printf(" Player Size / Free         : %d / %d \n", m_mPlayerMap.size(), m_mPlayerMap.GetFreeSize());
#else
	printf(" Login / Player Size        : %d / %d \n", m_mLoginMap.size(), m_mPlayerMap.size());
#endif

#ifdef MY_DEBUG
	printf(" Job Chunk / Node Count     : %d / %d \n", m_pJobPool->GetChunkSize(), m_pJobPool->GetNodeSize());
	printf(" Player Chunk / Node Count  : %d / %d \n", m_pPlayerPool->GetChunkSize(), m_pPlayerPool->GetNodeSize());
#else
	printf(" Job Chunk Count            : %d \n", m_pJobPool->GetChunkSize());
	printf(" Player Chunk Count         : %d \n", m_pPlayerPool->GetChunkSize());
#endif
	printf(" Event Wake / Update TPS    : %lld / %lld \n", eventWakeTps, updateTps);
#ifdef MY_DEBUG
	printf(" Timeout Count              : %d \n", m_countTimeOut);
	printf(" Broadcast Count / TpsDiff  : %lld / %lld \n", broadcastCnt, SendTps - RecvTps);
	printf(" Sector Total / Free Cnt    : %lld / %lld \n", sum, freeSum);
#endif
	printf("=========================================== \n");
}

// Update ������
unsigned int __stdcall CChattingServer::UpdateThead(void* pParameter)
{
	CChattingServer* pChattingServer = (CChattingServer*)pParameter;

	CLockFreeTlsPoolA<st_JOB>* pJobPool = pChattingServer->m_pJobPool;

	for (;;)
	{
#ifdef MY_DEBUG
		PRO_BEGIN(L"Wait_Update");
#endif // MY_DEBUG

		// event ��ü�� �����带 �����.
		WaitForSingleObject(pChattingServer->m_hUpdateEvent, INFINITE);

#ifdef MY_DEBUG
		PRO_END(L"Wait_Update");
#endif // MY_DEBUG

		DWORD dwTime = timeGetTime();			// ��Ŷ timeOut �����ϱ� ���� �ð�
		pChattingServer->m_eventWakeCount++;
		long long prevUpdateCount = pChattingServer->m_updateCount;
#ifdef MY_DEBUG
		PRO_BEGIN(L"Wake_Update");
#endif // MY_DEBUG
		bool* pShutdown = &pChattingServer->m_shutdown;
		
		// ���� ����
		while (!(*pShutdown))
		{
			// �� Deq �ؼ� ó���ϱ�
			st_JOB* pJob = nullptr;
			if (pChattingServer->m_pJobQueue->Dequeue(&pJob) == false)
				break;

			++pChattingServer->m_updateCount;

			switch (pJob->jobType)
			{
			case en_JOB::JOB_CLIENT_JOIN:
				pChattingServer->JobClientJoin(pJob, dwTime);
				break;
			case en_JOB::JOB_CLIENT_LEAVE:
				pChattingServer->JobClientLeave(pJob, dwTime);
				break;
			case en_JOB::JOB_ONRECV:
				pChattingServer->JobClientOnRecv(pJob, dwTime);
				break;
			case en_JOB::JOB_TIMEOUT:
				pChattingServer->JobClientTimeOut(dwTime);
				break;
			}

			pJobPool->Free(pJob);

			// Update�� ���⸸ �ϴ� ��츦 ���� ���� ��ġ
			if (pChattingServer->m_updateCount - prevUpdateCount > 100000)
			{
				prevUpdateCount = pChattingServer->m_updateCount;
				dwTime = timeGetTime();
			}
		}

		ResetEvent(pChattingServer->m_hUpdateEvent);

#ifdef MY_DEBUG
		PRO_END(L"Wake_Update");
#endif // MY_DEBUG
	}
}

// timeout ������
unsigned int __stdcall CChattingServer::TimeoutThread(void* pParameter)
{
	CChattingServer* pChattingServer = (CChattingServer*)pParameter;

	bool* pShutdown = &pChattingServer->m_shutdown;
#ifdef  MY_DEBUG
	int* pTimeOutCnt = &pChattingServer->m_countTimeOut;
#endif //  MY_DEBUG

	while (!(*pShutdown))
	{
		st_JOB* pJob = pChattingServer->m_pJobPool->Alloc();
		if (pJob == nullptr)
		{
			LOG(L"stJOBAlloc", en_LOG_LEVEL::LEVEL_ERROR, L"[ IP %s, Port %d ] TimeoutThread Alloc Fail");
			CCrashDump::Crash();
		}
		pJob->jobType = en_JOB::JOB_TIMEOUT;
		pChattingServer->m_pJobQueue->Enqueue(pJob);	// timeOut �� Enq
		SetEvent(pChattingServer->m_hUpdateEvent);
#ifdef  MY_DEBUG
		++(*pTimeOutCnt);
#endif //  MY_DEBUG
		Sleep(pChattingServer->m_playerTimeOut);
	}

	return 0;
}

// ���� ���
void CChattingServer::JobClientJoin(st_JOB* pJob, DWORD dwTime)
{
	st_PLAYER* pPlayer = m_pPlayerPool->Alloc();
	if (pPlayer == nullptr)
	{
		LOG(L"stPLAYERAlloc", en_LOG_LEVEL::LEVEL_ERROR, L"[ IP %s, Port %d ] JobClientJoin Alloc Fail");
		CCrashDump::Crash();
	}
#ifdef MY_DEBUG
	pPlayer->UseFlag = true;
	
	// ������
	int idx = InterlockedIncrement(&g_tempCount) % 100;
	g_playerTemp[idx] = pPlayer;
#endif

	// �ʱ�ȭ
	pPlayer->timeout = dwTime;
	pPlayer->SessionID = pJob->SessionID;
	pPlayer->SectorX = pPlayer->SectorY = 0;

	DEBUG(DEBUG_UPDATE_LOCATION::JOB_JOIN, pPlayer, nullptr);

	// �α��� Map�� �߰��Ѵ�
	if (m_mLoginMap.insert(pPlayer->SessionID, pPlayer) == false)
	{
		wchar_t IpStr[16];
		u_short usPort;
		GetIP_Port(pPlayer->SessionID, IpStr, &usPort);
		LOG(L"MapInsertFail", en_LOG_LEVEL::LEVEL_DEBUG, L"[ IP %s, Port %d ] JobClientJoin insert Fail", IpStr, usPort);
		DisconnectPlayer(pPlayer->SessionID);
	}
}

void CChattingServer::JobClientLeave(st_JOB* pJob, DWORD dwTime)
{
	// �÷��̾ ä�� �������� �����Ѵ�.
	DisconnectPlayer(pJob->SessionID);
}

void CChattingServer::JobClientOnRecv(st_JOB* pJob, DWORD dwTime)
{
	WORD Type;
	unsigned long long SessionID = pJob->SessionID;
	CSerializeBuffer* pRecvPacket = pJob->pPacket;

#ifdef MY_DEBUG
	try {
#endif
		(*pRecvPacket) >> Type;

		switch (Type)
		{
			// �α��� ��û ��Ŷ
		case en_PACKET_CS_CHAT_REQ_LOGIN:
		{
			st_PLAYER* pPlayer = nullptr;
			if (m_mLoginMap.at(SessionID, &pPlayer) == false)
			{
				pRecvPacket->DecreaseRefCount();
				return;
			}
			pPlayer->timeout = dwTime;

			// ��Ŷ �𸶼���
			if(ppChatReqLogin(pRecvPacket, &pPlayer->AccountNo, pPlayer->ID, pPlayer->Nickname, pPlayer->SessionKey) == false)
			{
				wchar_t IpStr[16];
				u_short usPort;
				GetIP_Port(pPlayer->SessionID, IpStr, &usPort);
				LOG(L"ChattingLogin", en_LOG_LEVEL::LEVEL_DEBUG, L"[ IP %s, Port %d ] ppChatReqLogin Fail", IpStr, usPort);
				DisconnectPlayer(SessionID);
				pRecvPacket->DecreaseRefCount();
				return;
			}

			CSerializeBuffer* pSendPacket = CSerializeBuffer::Alloc(NETHEADER, 11);
			if (pSendPacket == nullptr)
			{
				LOG(L"CSerializeBuffer", en_LOG_LEVEL::LEVEL_ERROR, L"[ IP %s, Port %d ] REQ_LOGIN Alloc Fail");
				CCrashDump::Crash();
			}
#ifdef MY_DEBUG
			pSendPacket->SetLog(1, pPlayer->SessionID);
#endif
			DEBUG(DEBUG_UPDATE_LOCATION::JOB_ONRECV_REQ_LOGIN, pPlayer, pSendPacket);

			// �α��� ���� ������ - 1�� ����
			mpChatResLogin(pSendPacket, 1, pPlayer->AccountNo);

			DEBUG(DEBUG_UPDATE_LOCATION::JOB_ONRECV_REQ_LOGIN_BEFORE_SEND, pPlayer, pSendPacket);
			// ��Ŷ �۽�
			if (netWSASendPacket(SessionID, pSendPacket) == 0)
			{
				DEBUG(DEBUG_UPDATE_LOCATION::JOB_ONRECV_REQ_LOGIN_SENDFAIL, pPlayer, pSendPacket);
				DisconnectPlayer(SessionID);
				pRecvPacket->DecreaseRefCount();
				pSendPacket->DecreaseRefCount();
				return;
			}

#ifdef MY_DEBUG
			pSendPacket->SetLog(2, pPlayer->SessionID);
#endif
			pSendPacket->DecreaseRefCount();
		}
		break;
		
		// ���� �̵�
		case en_PACKET_CS_CHAT_REQ_SECTOR_MOVE:
		{
			st_PLAYER* pPlayer = nullptr;
			bool first = false;

			// �÷��̾� ã��
			if (m_mPlayerMap.at(SessionID, &pPlayer) == false)
			{
				// ó�� ������ ��� LoginMap���� PlayerMap���� �ű�� ���� �۾�
				if (m_mLoginMap.at(SessionID, &pPlayer) == false)
				{
					// ������ ��𿡵� ���� ���� �ʴ� ���...
					pRecvPacket->DecreaseRefCount();
					return;
				}

				// ó�� ��Ŷ �̵��� ���� ���
				first = true;

				DEBUG(DEBUG_UPDATE_LOCATION::JOB_ONRECV_REQ_SECTOR_MOVEPLAYER, pPlayer, nullptr);
				m_mLoginMap.erase(SessionID);

				// PlayerMap�� �߰����ش�.
				if (m_mPlayerMap.insert(SessionID, pPlayer) == false)
				{
					wchar_t IpStr[16];
					u_short usPort;
					GetIP_Port(pPlayer->SessionID, IpStr, &usPort);
					LOG(L"MapInsertFail", en_LOG_LEVEL::LEVEL_DEBUG, L"[ IP %s, Port %d ] SectorMove insert Fail", IpStr, usPort);
					pRecvPacket->DecreaseRefCount();
					return;
				}
			}

			INT64 AccountNo;
			WORD	SectorX;
			WORD	SectorY;
			// ��Ŷ �𸶼���
			if (ppChatReqSectorMove(pRecvPacket, &AccountNo, &SectorX, &SectorY) == false)
			{
				wchar_t IpStr[16];
				u_short usPort;
				GetIP_Port(pPlayer->SessionID, IpStr, &usPort);
				LOG(L"ChattingSector", en_LOG_LEVEL::LEVEL_DEBUG, L"[ IP %s, Port %d ] ppChatReqSectorMove Fail", IpStr, usPort);
				DisconnectPlayer(SessionID);
				pRecvPacket->DecreaseRefCount();
				return;
			}

			pPlayer->timeout = dwTime;
			if (pPlayer->AccountNo != AccountNo)
				CCrashDump::Crash();

			if (!first)
			{
				// ó���� �ƴ� ���. ������ �ٸ� ���Ϳ��� �����ϱ� ������ �ٸ� ���Ϳ��� �����ش�.
				DEBUG(DEBUG_UPDATE_LOCATION::JOB_ONRECV_REQ_SECTOR_REMOVE_SECTOR, pPlayer, nullptr);
				m_sectorArr[pPlayer->SectorX][pPlayer->SectorY].remove_one(pPlayer);
			}

			pPlayer->SectorX = SectorX;
			pPlayer->SectorY = SectorY;

			// �ٸ� ���ͷ� �̵������ش�.
			DEBUG(DEBUG_UPDATE_LOCATION::JOB_ONRECV_REQ_SECTOR_REPLACE_SECTOR, pPlayer, nullptr);
			m_sectorArr[SectorX][SectorY].push_back(pPlayer);

			CSerializeBuffer* pSendPacket = CSerializeBuffer::Alloc(NETHEADER, 14);
			if (pSendPacket == nullptr)
			{
				LOG(L"CSerializeBuffer", en_LOG_LEVEL::LEVEL_ERROR, L"[ IP %s, Port %d ] REQ_SECTOR Alloc Fail");
				CCrashDump::Crash();
			}
#ifdef MY_DEBUG
			pSendPacket->SetLog(3, pPlayer->SessionID);
#endif
			// ���� ��Ŷ ������
			mpChatResSectorMove(pSendPacket, AccountNo, SectorX, SectorY);

			DEBUG(DEBUG_UPDATE_LOCATION::JOB_ONRECV_REQ_SECTOR_BEFORE_SEND, pPlayer, pSendPacket);
			if (netWSASendPacket(SessionID, pSendPacket) == 0)
			{
				DEBUG(DEBUG_UPDATE_LOCATION::JOB_ONRECV_REQ_SECTOR_SENDFAIL, pPlayer, pSendPacket);
				DisconnectPlayer(SessionID);
				pSendPacket->DecreaseRefCount();
				pRecvPacket->DecreaseRefCount();
				return;
			}

#ifdef MY_DEBUG
			pSendPacket->SetLog(4, pPlayer->SessionID);
#endif
			pSendPacket->DecreaseRefCount();
		}
		break;

		// ä�� �޽��� ��û
		case en_PACKET_CS_CHAT_REQ_MESSAGE:
		{
			st_PLAYER* pPlayer = nullptr;
			if (m_mPlayerMap.at(SessionID, &pPlayer) == false)
			{
				pRecvPacket->DecreaseRefCount();
				return;
			}		

			INT64 AccountNo;
			WORD MessageLen;
			wchar_t Message[260];
			// ��Ŷ �𸶼���
			if (ppChatReqMessage(pRecvPacket, &AccountNo, &MessageLen, Message) == false)
			{
				wchar_t IpStr[16];
				u_short usPort;
				GetIP_Port(pPlayer->SessionID, IpStr, &usPort);
				LOG(L"ChattingMsg", en_LOG_LEVEL::LEVEL_DEBUG, L"[ IP %s, Port %d ] ppChatReqMessage", IpStr, usPort);
				DisconnectPlayer(SessionID);
				pRecvPacket->DecreaseRefCount();
				return;
			}

			pPlayer->timeout = dwTime;
			// AccountNo ��
			if(pPlayer->AccountNo != AccountNo)
			{
				wchar_t IpStr[16];
				u_short usPort;
				GetIP_Port(pPlayer->SessionID, IpStr, &usPort);
				LOG(L"ChattingMsg", en_LOG_LEVEL::LEVEL_DEBUG, L"[ IP %s, Port %d ] Wrong AccountNo", IpStr, usPort);
				DisconnectPlayer(SessionID);
				pRecvPacket->DecreaseRefCount();
				return;
			}

			CSerializeBuffer* pSendPacket = CSerializeBuffer::Alloc(NETHEADER, 92 + MessageLen);
			if (pSendPacket == nullptr)
			{
				LOG(L"CSerializeBuffer", en_LOG_LEVEL::LEVEL_ERROR, L"[ IP %s, Port %d ] REQ_MESSAGE Alloc Fail");
				CCrashDump::Crash();
			}
#ifdef MY_DEBUG
			pSendPacket->SetLog(5, pPlayer->SessionID);
#endif
			// �޽��� ������
			mpChatResMessage(pSendPacket, AccountNo, pPlayer->ID, pPlayer->Nickname, MessageLen, Message);

			DEBUG(DEBUG_UPDATE_LOCATION::JOB_ONRECV_REQ_CHAT_MSG_BEFORE_SEND, pPlayer, pSendPacket);
			
			// �ڽſ��Ե� �����ֱ�
			if (netWSASendPacket(SessionID, pSendPacket) == 0)
			{
				DEBUG(DEBUG_UPDATE_LOCATION::JOB_ONRECV_REQ_SECTOR_SENDFAIL, pPlayer, pSendPacket);
				DisconnectPlayer(SessionID);
				pSendPacket->DecreaseRefCount();
				pRecvPacket->DecreaseRefCount();
				return;
			}
			// ���� ��ε�ĳ���� - �ڽ��� ������ 9���� ���Ϳ� ä�� �޽��� ������
			ChatBroadcastSector(pPlayer, pSendPacket, pPlayer->SectorX, pPlayer->SectorY);

#ifdef MY_DEBUG
			pSendPacket->SetLog(6, pPlayer->SessionID);
#endif
			pSendPacket->DecreaseRefCount();
		}
			break;

		// ��Ʈ��Ʈ ó��
		case en_PACKET_CS_CHAT_REQ_HEARTBEAT:
		{
			st_PLAYER* pPlayer = nullptr;
			if (m_mPlayerMap.at(SessionID, &pPlayer) == false)
			{
				if (m_mLoginMap.at(SessionID, &pPlayer) == false)
				{
					pRecvPacket->DecreaseRefCount();
					return;
				}				
			}		
			DEBUG(DEBUG_UPDATE_LOCATION::JOB_ONRECV_REQ_HEARTBEAT, pPlayer, nullptr);
			pPlayer->timeout = dwTime; // timeOut ����
		}
			break;

			// �������� ���� Ÿ���� ��Ŷ�� �� ���. �߸��� Ŭ��� �Ǵ��ϰ� ������ ���´�.
		case en_PACKET_CS_CHAT_SERVER:
		case en_PACKET_CS_CHAT_RES_LOGIN:
		case en_PACKET_CS_CHAT_RES_SECTOR_MOVE:
		case en_PACKET_CS_CHAT_RES_MESSAGE:
		default:
			LOG(L"ChattingType", en_LOG_LEVEL::LEVEL_DEBUG, L"Wrong PayloadType : %d", Type);
			DisconnectPlayer(SessionID);
			break;
		}
#ifdef MY_DEBUG
	}
	catch (CSerializeBufException e)
	{
		wprintf(L"%s", (const wchar_t*)e.what());
		int* crash = nullptr;
		*crash = 0;
	}
#endif // MY_DEBUG

	pRecvPacket->DecreaseRefCount();
}

// TimeOut ó��
void CChattingServer::JobClientTimeOut(DWORD dwTime)
{
	DWORD nowTime = dwTime;
	wchar_t IpStr[16];
	u_short usPort;

	auto loginIter = m_mLoginMap.begin();
	auto loginEnd = m_mLoginMap.end();
	for (; loginIter != loginEnd; )
	{
		// ���� ���� �������� ��� �ð��� �� ��� ������ ���������.
		st_PLAYER* pPlayer = loginIter.second();
		if (nowTime - pPlayer->timeout > m_loginTimeOut)
		{
			DEBUG(DEBUG_UPDATE_LOCATION::JOB_TIMEOUT_ERASE_LOGIN, pPlayer, nullptr);
			loginIter = m_mLoginMap.erase(loginIter);
#ifdef MY_DEBUG
			GetIP_Port(pPlayer->SessionID, IpStr, &usPort);
			LOG(L"ChattingTimeout", en_LOG_LEVEL::LEVEL_DEBUG, L"[ IP %s, Port %d ] Login Timeout", IpStr, usPort);
			pPlayer->UseFlag = false;
#endif
			m_pPlayerPool->Free(pPlayer);
			DisconnectSession(pPlayer->SessionID);
		}
		else
			++loginIter;
	}

	auto iter = m_mPlayerMap.begin();
	auto end = m_mPlayerMap.end();
	for (; iter != end; )
	{
		// ���� ���� ���� �߿� �������� ��Ŷ�� ������ �ʾҴٸ� ������ ���´�.
		st_PLAYER* pPlayer = iter.second();
		if (nowTime - pPlayer->timeout > m_playerTimeOut)
		{
			unsigned long long SessionID = pPlayer->SessionID;
			DEBUG(DEBUG_UPDATE_LOCATION::JOB_TIMEOUT_ERASE_PLAYER, pPlayer, nullptr);
			m_sectorArr[pPlayer->SectorX][pPlayer->SectorY].remove_one(pPlayer);
			iter = m_mPlayerMap.erase(iter);
#ifdef MY_DEBUG
			pPlayer->UseFlag = false;
			GetIP_Port(pPlayer->SessionID, IpStr, &usPort);
			LOG(L"ChattingTimeout", en_LOG_LEVEL::LEVEL_DEBUG, L"[ IP %s, Port %d ] Player Timeout", IpStr, usPort);
#endif
			m_pPlayerPool->Free(pPlayer);
			DisconnectSession(SessionID);
		}
		else
			++iter;
	}
}

// 9���� ������ ��� �������� SendPacket�� ���ش�.
void CChattingServer::ChatBroadcastSector(st_PLAYER* pSendPlayer, CSerializeBuffer* pSendPacket, WORD SectorX, WORD SectorY)
{
	for (int i = SectorX - 1; i <= SectorX + 1; ++i)
	{
		if (i < 0 || dfSECTOR_X_MAX <= i)
			continue;

		for (int j = SectorY - 1; j <= SectorY + 1; ++j)
		{
			if (j < 0 || dfSECTOR_Y_MAX <= j)
				continue;

			auto iter = m_sectorArr[i][j].begin();
			auto end = m_sectorArr[i][j].end();
			
			unsigned long long SessionID;
			for (;iter != end; )
			{
				st_PLAYER* pPlayer = *iter;
				if (pPlayer == nullptr)
					CCrashDump::Crash();

				if (pPlayer == pSendPlayer)
				{
					++iter;
					continue;
				}

				SessionID = pPlayer->SessionID;
				// Enq�� ���ְ� ������ Send�ǵ��� ������ �ִ��� ��.
				if (netWSASendEnq(SessionID, pSendPacket) == 0) 
				{
					DEBUG(DEBUG_UPDATE_LOCATION::BROADCAST_MSG_SENDFAIL, pPlayer, pSendPacket);
					//DisconnectPlayer(SessionID);

					iter = m_sectorArr[i][j].erase(iter);
					m_mPlayerMap.erase(SessionID);
#ifdef MY_DEBUG
					pPlayer->UseFlag = false;
#endif
					m_pPlayerPool->Free(pPlayer);

					DisconnectSession(SessionID);
				}
				else
				{
					++iter;
#ifdef MY_DEBUG
					++m_broadcastCount;
#endif
				}
			}
		}
	}
#ifdef MY_DEBUG
	m_broadcastCount--;
#endif
}

// ���� ���� �Ǵ� ������ ���� �����ϱ�
void CChattingServer::DisconnectPlayer(unsigned long long SessionID)
{
	st_PLAYER* pPlayer = nullptr;
	if (m_mPlayerMap.at(SessionID, &pPlayer) == false)
	{
		if (m_mLoginMap.at(SessionID, &pPlayer) == false)
			return;

		DEBUG(DEBUG_UPDATE_LOCATION::DISCONNECT_PLAYER_ERASE_LOGIN, pPlayer, nullptr);
		m_sectorArr[pPlayer->SectorX][pPlayer->SectorY].remove_one(pPlayer);
		m_mLoginMap.erase(SessionID);
	}
	else
	{
		DEBUG(DEBUG_UPDATE_LOCATION::DISCONNECT_PLAYER_ERASE_PLAYER, pPlayer, nullptr);
		m_sectorArr[pPlayer->SectorX][pPlayer->SectorY].remove_one(pPlayer);
		m_mPlayerMap.erase(SessionID);
	}
	
#ifdef MY_DEBUG
	pPlayer->UseFlag = false;
#endif
	m_pPlayerPool->Free(pPlayer);

	DisconnectSession(SessionID);
}

#ifdef MY_DEBUG
void DebugCheck(DEBUG_UPDATE_LOCATION location, st_PLAYER* pPlayer, CSerializeBuffer* pPacket)
{
	unsigned int debugID = InterlockedIncrement(&pPlayer->debugID);
	unsigned int index = debugID % 100;

	pPlayer->debugUpdateArr[index].debugID = debugID;
	pPlayer->debugUpdateArr[index].SessionID = pPlayer->SessionID;
	pPlayer->debugUpdateArr[index].location = location;
	//pPlayer->debugUpdateArr[index].AccountNo = pPlayer->AccountNo;
	//pPlayer->debugUpdateArr[index].SectorX = pPlayer->SectorX;
	//pPlayer->debugUpdateArr[index].SectorY = pPlayer->SectorY;
#if defined(MY_DEBUG) && defined(MY_PACKET_DEBUG)
	if(pPacket != nullptr)
		pPlayer->debugUpdateArr[index].copyPacket = *pPacket;
#endif
}
#endif