#include "stdafx.h"
#include "ChattingServer.h"

#include <mmsystem.h>
#pragma comment(lib, "Winmm.lib")

#ifdef MY_DEBUG
// �޸� �α� enum ��
enum DEBUG_UPDATE_LOCATION
{
	JOB_JOIN,

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
	// Interlock���� ���� cpu lock�� �ɸ��� �ʵ��� 
	// 64 ĳ�ö��� ũ�� ��ŭ 2�� ������ ��������.
	alignas(64) bool UseFlag;	// ���� �÷��̷��� ��� �������� ���� ����
	alignas(64) short IOCount;	// ����ī��Ʈ - ���� �÷��̾ �����ϰ� �ִ� ����
	
	//
	unsigned long long SessionID; // ��Ʈ��ũ ���̺귯������ ���� Session ID
	INT64 AccountNo; // ���� ��ȣ
	wchar_t ID[20]; // ���� id
	wchar_t Nickname[20]; // ���� �г���
	char SessionKey[64]; // ���� ���� Ű

	WORD SectorX; // ���� ���� ��ġ x, y
	WORD SectorY;

	DWORD timeout; // timeOut ����
	wchar_t IpStr[16]; // IP, Port ����
	u_short usPort;

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
	CloseHandle(m_hMoritoring);
}

// ������ �����Ҷ�
bool CChattingServer::Start(const wchar_t* ipWstr, int portNum, int workerCreateCnt, int workerRunningCnt,
	bool bNoDelayOpt, int serverMaxUser, bool bRSTOpt, bool bKeepAliveOpt, bool bOverlappedSend, int SendQSize)
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

	m_shutdown = false;

	// ����ȭ ��ü �ʱ�ȭ
	InitializeCriticalSection(&m_loginCS);
	InitializeCriticalSection(&m_playerCS);

	for(int i = 0; i < dfSECTOR_X_MAX; ++i)
		for(int j = 0; j < dfSECTOR_Y_MAX; ++j)
			InitializeCriticalSection(&m_sectorArr[i][j].sectorCS);	

	// �⺻ �ʱ�ȭ
	m_pPlayerPool = new CLockFreeTlsPoolA<st_PLAYER>(PLAYERPOOL_CHUNK, PLAYERPOOL_NODE);
	
	// timeout ������ ����
	m_hMoritoring = (HANDLE)_beginthreadex(nullptr, 0, CChattingServer::TimeoutThread, this, 0, nullptr);

	return CNetServer::Start(ipWstr, portNum, workerCreateCnt, workerRunningCnt, bNoDelayOpt, serverMaxUser, bRSTOpt, bKeepAliveOpt, bOverlappedSend, SendQSize);
}

// ������ �����Ҷ�
void CChattingServer::Stop()
{
	DeleteCriticalSection(&m_loginCS);
	DeleteCriticalSection(&m_playerCS);

	m_shutdown = true;
	CloseHandle(m_hMoritoring);

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
	st_PLAYER* pPlayer = m_pPlayerPool->Alloc();
	if (pPlayer == nullptr)
		CCrashDump::Crash();

#ifdef MY_DEBUG
	int idx = InterlockedIncrement(&g_tempCount) % 100;
	g_playerTemp[idx] = pPlayer;
#endif
	pPlayer->UseFlag = true;
	pPlayer->IOCount = 1;

	DEBUG(DEBUG_UPDATE_LOCATION::JOB_JOIN, pPlayer, nullptr);
	pPlayer->timeout = timeGetTime();
	pPlayer->SessionID = SessionID;

	// 1�� �𵨿����� ��� �÷��̾� �����ϴ� ���� ��ȿ�����̿��� ������.
	// IP, Port �޾ƿ��� �Լ�
	GetIP_Port(pPlayer->SessionID, pPlayer->IpStr, &pPlayer->usPort);

	// ������ �ʱ� ������ �����ϰ�, �������� ���� ��Ͽ� �߰��Ѵ�.
	EnterCriticalSection(&m_loginCS);
	m_umLoginList[pPlayer->SessionID] = pPlayer;
	LeaveCriticalSection(&m_loginCS);
}

// Release �� ȣ��
void CChattingServer::OnClientLeave(unsigned long long SessionID)
{
	// PlayerMap���� �ش� SessionID�� ���� �ִ� �÷��̾ �����´�
	st_PLAYER* pPlayer = AcquirePlayerLock(SessionID);
	if (pPlayer == nullptr)
	{
		// LoginMap���� �÷��̾ �����´�.
		pPlayer = AcquireLoginLock(SessionID);
		if (pPlayer == nullptr)
			return;  // �����ϸ� �̹� ���ŵ� ����.
	}

	// ���� ���� ��û�ϰ� ����ī��Ʈ�� 1�� �����ش�.
	DisconnectPlayer(SessionID);
	IODecreasePlayer(pPlayer);
}

// ��Ŷ ���� �Ϸ� ��
int CChattingServer::OnRecv(unsigned long long SessionID, CSerializeBuffer* pPacket)
{
	// CSerializeBuffer�� ��Ʈ��ũ ���̺귯���� ����� ������ �κи� �´�.

	WORD Type;
	CSerializeBuffer* pRecvPacket = pPacket;

	try {
		(*pRecvPacket) >> Type;

		switch (Type)
		{
			// �α��� ��û ��Ŷ
		case en_PACKET_CS_CHAT_REQ_LOGIN:
		{
			st_PLAYER* pPlayer = AcquireLoginLock(SessionID);
			if(pPlayer == nullptr)
			{
				return 0;
			}

			pPlayer->timeout = timeGetTime();

			// ��Ŷ �𸶼���
			if (ppChatReqLogin(pRecvPacket, &pPlayer->AccountNo, pPlayer->ID, pPlayer->Nickname, pPlayer->SessionKey) == false)
			{
				LOG(L"ChattingLogin", en_LOG_LEVEL::LEVEL_DEBUG, L"[ IP %s, Port %d ] ppChatReqLogin Fail", pPlayer->IpStr, pPlayer->usPort);
				DisconnectPlayer(SessionID);
				IODecreasePlayer(pPlayer);
				return 0;
			}

			CSerializeBuffer* pSendPacket = CSerializeBuffer::Alloc(NETHEADER, 11);
			if (pSendPacket == nullptr)
				CCrashDump::Crash();

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
				IODecreasePlayer(pPlayer);
				pSendPacket->DecreaseRefCount();
				return 0;
			}

#ifdef MY_DEBUG
			pSendPacket->SetLog(2, pPlayer->SessionID);
#endif
			pSendPacket->DecreaseRefCount();
			IODecreasePlayer(pPlayer);
		}
		break;

		// ���� �̵�
		case en_PACKET_CS_CHAT_REQ_SECTOR_MOVE:
		{
			// ���ӿ� ������ �÷��̾� ã��
			st_PLAYER* pPlayer = AcquirePlayerLock(SessionID);
			bool first = false;

			if(pPlayer == nullptr)
			{
				// ���ӿ� �������� �÷��̾� ã��
				pPlayer = AcquireLoginLock(SessionID);
				if(pPlayer == nullptr)
				{
					return 0;
				}

				first = true;

				DEBUG(DEBUG_UPDATE_LOCATION::JOB_ONRECV_REQ_SECTOR_MOVEPLAYER, pPlayer, nullptr);
				
				// ���� ���� �ʿ��� �������ش�.
				EnterCriticalSection(&m_loginCS);
				m_umLoginList.erase(SessionID);
				LeaveCriticalSection(&m_loginCS);

				// ���ӿ� ������ �÷��̾� ������ �ű��.
				EnterCriticalSection(&m_playerCS);
				m_umPlayerList[SessionID] = pPlayer;
				LeaveCriticalSection(&m_playerCS);
			}

			INT64 AccountNo;
			WORD	SectorX;
			WORD	SectorY;
			// ��Ŷ �𸶼���
			if (ppChatReqSectorMove(pRecvPacket, &AccountNo, &SectorX, &SectorY) == false)
			{
				LOG(L"ChattingSector", en_LOG_LEVEL::LEVEL_DEBUG, L"[ IP %s, Port %d ] ppChatReqSectorMove Fail", pPlayer->IpStr, pPlayer->usPort);
				DisconnectPlayer(SessionID);
				IODecreasePlayer(pPlayer);
				return 0;
			}

			pPlayer->timeout = timeGetTime();
			if (pPlayer->AccountNo != AccountNo)
				CCrashDump::Crash();
			
			int prevSectorX = pPlayer->SectorX;
			int prevSectorY = pPlayer->SectorY;
			
			if (!first)
			{
				// ó���� �ƴ� ���. ������ �ٸ� ���Ϳ��� �����ϱ� ������ �ٸ� ���Ϳ��� �����ش�.
				DoubleLock(SectorX, SectorY, prevSectorX, prevSectorY); // 2�� ���͸� ���ÿ� �����ϴ� ��� ������� �߻��� �� �������� ����� �����Լ��� �����.
				DEBUG(DEBUG_UPDATE_LOCATION::JOB_ONRECV_REQ_SECTOR_REMOVE_SECTOR, pPlayer, nullptr);
				m_sectorArr[prevSectorX][prevSectorY].playerList.remove(pPlayer);
				LeaveCriticalSection(&m_sectorArr[prevSectorX][prevSectorY].sectorCS);
			}
			else
				EnterCriticalSection(&m_sectorArr[SectorX][SectorY].sectorCS);

			pPlayer->SectorX = SectorX;
			pPlayer->SectorY = SectorY;

			// �ٸ� ���ͷ� �̵������ش�.
			DEBUG(DEBUG_UPDATE_LOCATION::JOB_ONRECV_REQ_SECTOR_REPLACE_SECTOR, pPlayer, nullptr);
			m_sectorArr[SectorX][SectorY].playerList.push_back(pPlayer);

			LeaveCriticalSection(&m_sectorArr[SectorX][SectorY].sectorCS);

			CSerializeBuffer* pSendPacket = CSerializeBuffer::Alloc(NETHEADER, 14);
			if (pSendPacket == nullptr)
				CCrashDump::Crash();

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
				IODecreasePlayer(pPlayer);
				return 0;
			}

#ifdef MY_DEBUG
			pSendPacket->SetLog(4, pPlayer->SessionID);
#endif
			pSendPacket->DecreaseRefCount();
			IODecreasePlayer(pPlayer);
		}
		break;

		// ä�� �޽��� ��û
		case en_PACKET_CS_CHAT_REQ_MESSAGE:
		{
			st_PLAYER* pPlayer = AcquirePlayerLock(SessionID);
			if (pPlayer == nullptr)
			{
				return 0;
			}

			INT64 AccountNo;
			WORD MessageLen;
			wchar_t Message[260];
			// ��Ŷ �𸶼���
			if (ppChatReqMessage(pRecvPacket, &AccountNo, &MessageLen, Message) == false)
			{
				LOG(L"ChattingMsg", en_LOG_LEVEL::LEVEL_DEBUG, L"[ IP %s, Port %d ] Wrong AccountNo", pPlayer->IpStr, pPlayer->usPort);
				DisconnectPlayer(SessionID);
				IODecreasePlayer(pPlayer);
				return 0;
			}

			pPlayer->timeout = timeGetTime();
			// AccountNo ��
			if (pPlayer->AccountNo != AccountNo)
			{
				LOG(L"ChattingMsg", en_LOG_LEVEL::LEVEL_DEBUG, L"[ IP %s, Port %d ] Wrong AccountNo", pPlayer->IpStr, pPlayer->usPort);
				DisconnectPlayer(SessionID);
				IODecreasePlayer(pPlayer);
				return 0;
			}

			CSerializeBuffer* pSendPacket = CSerializeBuffer::Alloc(NETHEADER, 92 + MessageLen);
			if (pSendPacket == nullptr)
				CCrashDump::Crash();

#ifdef MY_DEBUG
			pSendPacket->SetLog(5, pPlayer->SessionID);
#endif
			// �޽��� ������
			mpChatResMessage(pSendPacket, AccountNo, pPlayer->ID, pPlayer->Nickname, MessageLen, Message);

			DEBUG(DEBUG_UPDATE_LOCATION::JOB_ONRECV_REQ_CHAT_MSG_BEFORE_SEND, pPlayer, pSendPacket);
			// ���� ��ε�ĳ���� - �ڽ��� ������ 9���� ���Ϳ��� ä�� �޽��� ������
			ChatBroadcastSector(pSendPacket, pPlayer->SectorX, pPlayer->SectorY);
			
#ifdef MY_DEBUG
			pSendPacket->SetLog(6, pPlayer->SessionID);
#endif
			pSendPacket->DecreaseRefCount();
			IODecreasePlayer(pPlayer);
		}
		break;

		// ��Ʈ��Ʈ ó��
		case en_PACKET_CS_CHAT_REQ_HEARTBEAT:
		{
			st_PLAYER* pPlayer = AcquirePlayerLock(SessionID);
			if (pPlayer == nullptr)
			{
				pPlayer = AcquireLoginLock(SessionID);
				if (pPlayer == nullptr)
				{
					return 0;
				}
			}

			DEBUG(DEBUG_UPDATE_LOCATION::JOB_ONRECV_REQ_HEARTBEAT, pPlayer, nullptr);
			pPlayer->timeout = timeGetTime();
			IODecreasePlayer(pPlayer);
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
			return 0;
		}
	}
	catch (CSerializeBufException e)
	{
		wprintf(L"%s", (const wchar_t*)e.what());
		int* crash = nullptr;
		*crash = 0;
	}

	return 1; // ���� ����
}

void CChattingServer::OnError(const wchar_t* ipWstr, int portNum, int errorCode, const wchar_t* errorMsg)
{
	LOG(L"Error", en_LOG_LEVEL::LEVEL_ERROR, L"[ IP %s, Port %d ] errMsg  %s , errCode %d ", ipWstr, portNum, errorMsg, errorCode);
}

// ��Ʈ��ũ ���̺귯���� ���� ����͸� ���� ȹ��
void CChattingServer::GetMonitoringInfo(long long AcceptTps, long long RecvTps, long long SendTps,
	long long acceptCount, long long disconnectCount, int sessionCount,
	int chunkCount, int chunkNodeCount,
	int workerCreatCnt, int workerRunningCnt,
	long long sendBytePerSec, long long recvBytePerSec)
{
#ifdef MY_DEBUG
	//temp = m_broadcastCount;
	//long long broadcastCnt = temp - m_broadcastPrevCount;
	//m_broadcastPrevCount = temp;
	long long sum = 0;
	for (int i = 0; i < dfSECTOR_X_MAX; ++i)
		for (int j = 0; j < dfSECTOR_Y_MAX; ++j)
			sum += (long long)m_sectorArr[i][j].playerList.size();
#endif

	// ����͸� ���� ���...
	printf("=========================================== \n");
	printf(" - Server Library -\n");
	printf(" Key Info                   : U / L ( Unlock / Lock ) \n");
	printf(" Created / Worker Thread    : %d / %d \n", workerCreatCnt, workerRunningCnt);
	printf(" Send / Recv / Accept TPS   : %lld / %lld / %d\n", SendTps, RecvTps, AcceptTps);
	printf(" Accept / Disconnect Count  : %lld / %lld \n", acceptCount, disconnectCount);
	printf(" Session Count              : %d \n", sessionCount);
	printf(" Packet Chunk / Node Count  : %d / %d \n", chunkCount, chunkNodeCount);
#ifdef MY_DEBUG
	printf(" Send / Recv BytePerSec     : %lld / %lld \n", sendBytePerSec, recvBytePerSec);
#endif
	printf("\n - Chatting Server -\n");
	printf(" Login / Player Size        : %d / %d \n", m_umLoginList.size(), m_umPlayerList.size());
	printf(" Player Chunk / Node Count  : %d / %d \n", m_pPlayerPool->GetChunkSize(), m_pPlayerPool->GetNodeSize());
#ifdef MY_DEBUG
	printf(" Sector Total Size          : %d \n", sum);
	//printf(" Timeout Count              : %d \n", m_countTimeOut);
	//printf(" Broadcast Count / TpsDiff  : %lld / %lld \n", broadcastCnt, SendTps - RecvTps);
#endif
	printf("=========================================== \n");
}

// timeOut ������
unsigned int __stdcall CChattingServer::TimeoutThread(void* pParameter)
{
	CChattingServer* pChattingServer = (CChattingServer*)pParameter;

	int acceptTps = 0;
	long long recvTps = 0;
	long long sendTps = 0;
	int acceptCount = 0;
	int disconnectCount = 0;
	int sessionCount = 0;
	int chunkCount = 0;
	int chunkNodeCount = 0;
	int workerCreatCnt = 0;
	int workerRunningCnt = 0;

	int eventWakeTps = 0;
	int updateTps = 0;

	unordered_map<int, st_PLAYER*>* pLoginList = &pChattingServer->m_umLoginList;
	unordered_map<int, st_PLAYER*>* pPlayerList = &pChattingServer->m_umPlayerList;
	CLockFreeTlsPoolA<st_PLAYER>* pPlayerPool = pChattingServer->m_pPlayerPool;
	bool* pShutdown = &pChattingServer->m_shutdown;

	int timeOutSec = pChattingServer->m_playerTimeOut / 1000;
	while (!(*pShutdown))
	{
		// timeOut�� ���� �������� ����.
		pChattingServer->CheckTimeOut();

		Sleep(10000);
	}

	return 0;
}

void CChattingServer::CheckTimeOut()
{
	// timeOut�� �������� ����.
	// ���� �׽�Ʈ�� 1�� �� �������� �߱� ������ timeOut�� �������� ����.
	// ����ȭ�� 1�� �𵨸� ������.
}

// 9���� ���� ��ε�ĳ����
void CChattingServer::ChatBroadcastSector(CSerializeBuffer* pSendPacket, WORD SectorX, WORD SectorY)
{
	for (int i = SectorX - 1; i <= SectorX + 1; ++i)
	{
		if (i < 0 || dfSECTOR_X_MAX <= i)
			continue;

		for (int j = SectorY - 1; j <= SectorY + 1; ++j)
		{
			if (j < 0 || dfSECTOR_Y_MAX <= j)
				continue;

			CList<unsigned long long> erasePlayer;

			EnterCriticalSection(&m_sectorArr[i][j].sectorCS);
			auto iter = m_sectorArr[i][j].playerList.begin();
			auto end = m_sectorArr[i][j].playerList.end();
			
			for (;iter != end; )
			{
				st_PLAYER* pPlayer = *iter;
				if (pPlayer == nullptr)
					CCrashDump::Crash();

				if (netWSASendPacket(pPlayer->SessionID, pSendPacket) == 0)
				{
					DEBUG(DEBUG_UPDATE_LOCATION::BROADCAST_MSG_SENDFAIL, pPlayer, pSendPacket);
					erasePlayer.push_back(pPlayer->SessionID);
					iter = m_sectorArr[i][j].playerList.erase(iter);
				}
				else
					++iter;
			}

			LeaveCriticalSection(&m_sectorArr[i][j].sectorCS);

			auto eraseIter = erasePlayer.begin();
			auto eraseEnd = erasePlayer.end();

			for (; eraseIter != eraseEnd; ++eraseIter)
			{
				DisconnectPlayer(*eraseIter);
			}
		}
	}
}

// ���� ���� �Ǵ� ������ ���� �����ϱ�
void CChattingServer::DisconnectPlayer(unsigned long long SessionID)
{
	st_PLAYER* pPlayer = AcquirePlayerLock(SessionID);
	if (pPlayer == nullptr)
	{
		pPlayer = AcquireLoginLock(SessionID);
		if (pPlayer == nullptr)
		{
			return;
		}
	}
	
	if (true == InterlockedExchange8((char*)&pPlayer->UseFlag, false))
	{
		DisconnectSession(SessionID);
		IODecreasePlayer(pPlayer);
	}

	IODecreasePlayer(pPlayer);
}

// �����ϰ� �������� �÷��̾� ȹ���ϱ�
st_PLAYER* CChattingServer::AcquireLoginLock(unsigned long long SessionID)
{
	// ���� �ɰ� loginList�� �÷��̾ �ִ� ���
	st_PLAYER* pPlayer = nullptr;
	EnterCriticalSection(&m_loginCS);
	if (m_umLoginList.find(SessionID) != m_umLoginList.end())
	{
		pPlayer = m_umLoginList[SessionID];
		if (pPlayer->UseFlag == false)
		{
			LeaveCriticalSection(&m_loginCS);
			return nullptr;
		}
		// ����ī��Ʈ�� ������Ű�� ȹ�濡 �����Ѵ�.
		InterlockedIncrement16(&pPlayer->IOCount);
	}
	LeaveCriticalSection(&m_loginCS);

	return pPlayer;
}

// �����ϰ� ���ӿ� ������ �÷��̾� ȹ���ϱ�
st_PLAYER* CChattingServer::AcquirePlayerLock(unsigned long long SessionID)
{
	// ���� �ɰ� PlayerList�� �÷��̾ �ִ� ���
	st_PLAYER* pPlayer = nullptr;
	EnterCriticalSection(&m_playerCS);
	if (m_umPlayerList.find(SessionID) != m_umPlayerList.end())
	{
		pPlayer = m_umPlayerList[SessionID];
		if (pPlayer->UseFlag == false)
		{
			LeaveCriticalSection(&m_playerCS);
			return nullptr;
		}
		// ����ī��Ʈ�� ������Ű�� ȹ�濡 �����Ѵ�.
		InterlockedIncrement16(&pPlayer->IOCount);
	}
	LeaveCriticalSection(&m_playerCS);

	return pPlayer;
}

// ����ī��Ʈ ����
void CChattingServer::IODecreasePlayer(st_PLAYER* pPlayer)
{
	// �ϴ� ���� ��Ű��
	int IOCount = InterlockedDecrement16(&pPlayer->IOCount);
	if (IOCount < 0)
	{
		int* crash = nullptr;
		*crash = 0;
	}

	// 0�� ��� - ��� �����忡�� �� �÷��̾ ������� �ʴ� ���°� ��
	if (IOCount == 0)
	{
		unsigned long long SessionID = pPlayer->SessionID;
		EnterCriticalSection(&m_playerCS);
		if (m_umPlayerList.find(SessionID) != m_umPlayerList.end())
		{
			// PlayerList �ȿ� �ִ� ���
			DEBUG(DEBUG_UPDATE_LOCATION::DISCONNECT_PLAYER_ERASE_PLAYER, pPlayer, nullptr);

			// �����ش�.
			m_umPlayerList.erase(SessionID);
			LeaveCriticalSection(&m_playerCS);

			// ���Ϳ����� �������ش�.
			EnterCriticalSection(&m_sectorArr[pPlayer->SectorX][pPlayer->SectorY].sectorCS);
			m_sectorArr[pPlayer->SectorX][pPlayer->SectorY].playerList.remove(pPlayer);
			LeaveCriticalSection(&m_sectorArr[pPlayer->SectorX][pPlayer->SectorY].sectorCS);

			// �޸�Ǯ�� �ݳ��Ѵ�.
			m_pPlayerPool->Free(pPlayer);
			return;
		}
		LeaveCriticalSection(&m_playerCS);

		EnterCriticalSection(&m_loginCS);
		if (m_umLoginList.find(SessionID) != m_umLoginList.end())
		{
			DEBUG(DEBUG_UPDATE_LOCATION::DISCONNECT_PLAYER_ERASE_LOGIN, pPlayer, nullptr);

			// �������� ��� ���Ϳ��� ���� ������ ����Ʈ������ ����
			m_umLoginList.erase(SessionID);
			LeaveCriticalSection(&m_loginCS);

			// �޸�Ǯ�� �ݳ��Ѵ�.
			m_pPlayerPool->Free(pPlayer);
			return;
		}
		LeaveCriticalSection(&m_loginCS);
	}
}

void CChattingServer::DoubleLock(WORD SectorX1, WORD SectorY1, WORD SectorX2, WORD SectorY2)
{
	for (;;)
	{
		// 2���� ��ü�� ���ؼ� ���� �õ��Ѵ�.
		// �ϳ��� �����ϴ� ��쿡�� Unlock�� �ϰ� ó������ �ٽ� �õ��Ѵ�.
		if (TryEnterCriticalSection(&m_sectorArr[SectorX1][SectorY1].sectorCS) != 0)
		{
			if (TryEnterCriticalSection(&m_sectorArr[SectorX2][SectorY2].sectorCS) == 0)
			{
				LeaveCriticalSection(&m_sectorArr[SectorX1][SectorY1].sectorCS);
				continue;
			}
			break;
		}
	}
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