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
// 메모리 로그 enum 문
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
	bool UseFlag; // 사용 여부 플래그 - 메모리풀에서 확인하기 위한 용도
#endif

	DWORD timeout; // timeOut 변수
	WORD SectorX; // 유저 섹터 위치 x, y
	WORD SectorY;

	
	char SessionKey[64]; // 유저 세션 키
	unsigned long long SessionID; // 네트워크 라이브러리에서 받은 Session ID
	INT64 AccountNo; // 유저 번호
	wchar_t ID[20];  // 유저 id
	wchar_t Nickname[20]; // 유저 닉네임

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

// 서버를 시작할때
bool CChattingServer::Start(const wchar_t* ipWstr, int portNum, int workerCreateCnt, int workerRunningCnt,
	bool bNoDelayOpt, int serverMaxUser, bool bRSTOpt, bool bKeepAliveOpt, bool bOverlappedSend, int SendQSize, int RingBufferSize)
{
	// chattingInfo.txt 파일로 부터 정보를 읽어온다.
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

	// 미리 동적할당하기
	m_mLoginMap.allocFreeNode(serverMaxUser);
	m_mPlayerMap.allocFreeNode(serverMaxUser);

	// 미리 동적할당하기
	for(int i = 0; i < dfSECTOR_X_MAX; ++i)
		for (int j = 0; j < dfSECTOR_Y_MAX; ++j)
			m_sectorArr[i][j].allocFreeNode(100);

	// 기본 초기화
	m_pJobQueue = new CLockFreeQueue<st_JOB*>(JOBQUEUE_SIZE);
	LOG(L"System", en_LOG_LEVEL::LEVEL_SYSTEM, L"[ Chatting System ] jobQ size : %d", JOBQUEUE_SIZE);
	m_pJobPool = new CLockFreeTlsPoolA<st_JOB>(JOBPOOL_CHUNK, JOBPOOL_NODE);
	LOG(L"System", en_LOG_LEVEL::LEVEL_SYSTEM, L"[ Chatting System ] jobPool Chunk size : %d, Node Size : %d", JOBPOOL_CHUNK, JOBPOOL_NODE);
	m_pPlayerPool = new CLockFreeTlsPoolA<st_PLAYER>(PLAYERPOOL_CHUNK, PLAYERPOOL_NODE);
	LOG(L"System", en_LOG_LEVEL::LEVEL_SYSTEM, L"[ Chatting System ] playerPool Chunk size : %d, Node Size : %d", PLAYERPOOL_CHUNK, PLAYERPOOL_NODE);
	
	// event 객체 초기화
	m_hUpdateEvent = CreateEvent(nullptr, false, false, nullptr);
	
	// Update, Timeout 스레드 생성
	m_hUpdateThread = (HANDLE)_beginthreadex(nullptr, 0, CChattingServer::UpdateThead, this, 0, nullptr);
	m_hTimeoutThread = (HANDLE)_beginthreadex(nullptr, 0, CChattingServer::TimeoutThread, this, 0, nullptr);

	// 라이브러리 초기화
	return CNetServer::Start(ipWstr, portNum, workerCreateCnt, workerRunningCnt, bNoDelayOpt, serverMaxUser, bRSTOpt, bKeepAliveOpt, bOverlappedSend, SendQSize, RingBufferSize);
}

// 서버를 정지할때
void CChattingServer::Stop()
{
	// Job큐 비워주기
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

// accept 직후
bool CChattingServer::OnConnectionRequest(const wchar_t* ipWstr, int portNum)
{
	// 거부할 유저를 선택할 수 있다.
	// 점검 시간. White IP. Black IP

	return true;
}

// Accept 후 접속처리 완료 후 호출.
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

	// 접속을 시도하는 JOB_CLIENT_JOIN 넣어준다.
	if (m_pJobQueue->Enqueue(pJob) == false)
		CCrashDump::Crash();

	SetEvent(m_hUpdateEvent);
}

// Release 후 호출
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

	// 접속을 종료하는 JOB_CLIENT_LEAVE 넣어준다.
	if (m_pJobQueue->Enqueue(pJob) == false)
		CCrashDump::Crash();

	SetEvent(m_hUpdateEvent);
}

// 패킷 수신 완료 후
int CChattingServer::OnRecv(unsigned long long SessionID, CSerializeBuffer* pPacket)
{
	// CSerializeBuffer는 네트워크 라이브러리의 헤더를 제외한 부분만 온다.

	st_JOB* pJob = m_pJobPool->Alloc();
	if (pJob == nullptr)
	{
		LOG(L"stJOBAlloc", en_LOG_LEVEL::LEVEL_ERROR, L"[ IP %s, Port %d ] OnRecv Alloc Fail");
		CCrashDump::Crash();
	}
	pJob->jobType = JOB_ONRECV;
	pJob->SessionID = SessionID;
	pJob->pPacket = pPacket;

	// 수신 완료통지에서 받은 패킷을 Update 스레드로 넘겨준다.
	pPacket->IncreaseRefCount();
	if (m_pJobQueue->Enqueue(pJob) == false)
		CCrashDump::Crash();

	SetEvent(m_hUpdateEvent);

	return 1; // 정상 종료
}

void CChattingServer::OnError(const wchar_t* ipWstr, int portNum, int errorCode, const wchar_t* errorMsg)
{
	LOG(L"OnError", en_LOG_LEVEL::LEVEL_ERROR, L"[ IP %s, Port %d ] errMsg  %s , errCode %d ", ipWstr, portNum, errorMsg, errorCode);
}

// 네트워크 라이브러리로 부터 모니터링 정보 획득
void CChattingServer::GetMonitoringInfo(long long AcceptTps, long long RecvTps, long long SendTps,
	long long acceptCount, long long disconnectCount, int sessionCount,
	int chunkCount, int chunkNodeCount,
	long long sendBytePerSec, long long recvBytePerSec)
{
	// event 동기화 변수를 꺠운 횟수
	long long temp = m_eventWakeCount;
	long long eventWakeTps = temp - m_eventWakePrevCount;
	m_eventWakePrevCount = temp;

	// update loop 를 꺠운 횟수
	temp = m_updateCount;
	long long updateTps = temp - m_updatePrevCount;
	m_updatePrevCount = temp;

#ifdef MY_DEBUG
	// 브로드캐스팅한 횟수
	temp = m_broadcastCount;
	long long broadcastCnt = temp - m_broadcastPrevCount;
	m_broadcastPrevCount = temp;

	// 섹터에 남아있는 모든 player 개수 확인	long long sum = 0;
	long long freeSum = 0;
	for (int i = 0; i < dfSECTOR_X_MAX; ++i)
		for (int j = 0; j < dfSECTOR_Y_MAX; ++j)
		{
			sum += (long long)m_sectorArr[i][j].size();
			freeSum += (long long)m_sectorArr[i][j].freesize();
		}
#endif

	// 모니터링 정보 출력...
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

// Update 스레드
unsigned int __stdcall CChattingServer::UpdateThead(void* pParameter)
{
	CChattingServer* pChattingServer = (CChattingServer*)pParameter;

	CLockFreeTlsPoolA<st_JOB>* pJobPool = pChattingServer->m_pJobPool;

	for (;;)
	{
#ifdef MY_DEBUG
		PRO_BEGIN(L"Wait_Update");
#endif // MY_DEBUG

		// event 객체로 스레드를 깨운다.
		WaitForSingleObject(pChattingServer->m_hUpdateEvent, INFINITE);

#ifdef MY_DEBUG
		PRO_END(L"Wait_Update");
#endif // MY_DEBUG

		DWORD dwTime = timeGetTime();			// 패킷 timeOut 갱신하기 위한 시간
		pChattingServer->m_eventWakeCount++;
		long long prevUpdateCount = pChattingServer->m_updateCount;
#ifdef MY_DEBUG
		PRO_BEGIN(L"Wake_Update");
#endif // MY_DEBUG
		bool* pShutdown = &pChattingServer->m_shutdown;
		
		// 내부 루프
		while (!(*pShutdown))
		{
			// 잡 Deq 해서 처리하기
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

			// Update만 돌기만 하는 경우를 위한 안전 장치
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

// timeout 스레드
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
		pChattingServer->m_pJobQueue->Enqueue(pJob);	// timeOut 잡 Enq
		SetEvent(pChattingServer->m_hUpdateEvent);
#ifdef  MY_DEBUG
		++(*pTimeOutCnt);
#endif //  MY_DEBUG
		Sleep(pChattingServer->m_playerTimeOut);
	}

	return 0;
}

// 들어온 경우
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
	
	// 디버깅용
	int idx = InterlockedIncrement(&g_tempCount) % 100;
	g_playerTemp[idx] = pPlayer;
#endif

	// 초기화
	pPlayer->timeout = dwTime;
	pPlayer->SessionID = pJob->SessionID;
	pPlayer->SectorX = pPlayer->SectorY = 0;

	DEBUG(DEBUG_UPDATE_LOCATION::JOB_JOIN, pPlayer, nullptr);

	// 로그인 Map에 추가한다
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
	// 플레이어를 채팅 서버에서 제거한다.
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
			// 로그인 요청 패킷
		case en_PACKET_CS_CHAT_REQ_LOGIN:
		{
			st_PLAYER* pPlayer = nullptr;
			if (m_mLoginMap.at(SessionID, &pPlayer) == false)
			{
				pRecvPacket->DecreaseRefCount();
				return;
			}
			pPlayer->timeout = dwTime;

			// 패킷 언마샬링
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

			// 로그인 응답 마샬링 - 1은 성공
			mpChatResLogin(pSendPacket, 1, pPlayer->AccountNo);

			DEBUG(DEBUG_UPDATE_LOCATION::JOB_ONRECV_REQ_LOGIN_BEFORE_SEND, pPlayer, pSendPacket);
			// 패킷 송신
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
		
		// 섹터 이동
		case en_PACKET_CS_CHAT_REQ_SECTOR_MOVE:
		{
			st_PLAYER* pPlayer = nullptr;
			bool first = false;

			// 플레이어 찾기
			if (m_mPlayerMap.at(SessionID, &pPlayer) == false)
			{
				// 처음 진입한 경우 LoginMap에서 PlayerMap으로 옮기기 위한 작업
				if (m_mLoginMap.at(SessionID, &pPlayer) == false)
				{
					// 유저가 어디에도 존재 하지 않는 경우...
					pRecvPacket->DecreaseRefCount();
					return;
				}

				// 처음 패킷 이동을 보낸 경우
				first = true;

				DEBUG(DEBUG_UPDATE_LOCATION::JOB_ONRECV_REQ_SECTOR_MOVEPLAYER, pPlayer, nullptr);
				m_mLoginMap.erase(SessionID);

				// PlayerMap의 추가해준다.
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
			// 패킷 언마샬링
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
				// 처음이 아닌 경우. 이전에 다른 섹터에서 존재하기 때문에 다른 섹터에서 지워준다.
				DEBUG(DEBUG_UPDATE_LOCATION::JOB_ONRECV_REQ_SECTOR_REMOVE_SECTOR, pPlayer, nullptr);
				m_sectorArr[pPlayer->SectorX][pPlayer->SectorY].remove_one(pPlayer);
			}

			pPlayer->SectorX = SectorX;
			pPlayer->SectorY = SectorY;

			// 다른 섹터로 이동시켜준다.
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
			// 응답 패킷 마샬링
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

		// 채팅 메시지 요청
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
			// 패킷 언마샬링
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
			// AccountNo 비교
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
			// 메시지 마샬링
			mpChatResMessage(pSendPacket, AccountNo, pPlayer->ID, pPlayer->Nickname, MessageLen, Message);

			DEBUG(DEBUG_UPDATE_LOCATION::JOB_ONRECV_REQ_CHAT_MSG_BEFORE_SEND, pPlayer, pSendPacket);
			
			// 자신에게도 보내주기
			if (netWSASendPacket(SessionID, pSendPacket) == 0)
			{
				DEBUG(DEBUG_UPDATE_LOCATION::JOB_ONRECV_REQ_SECTOR_SENDFAIL, pPlayer, pSendPacket);
				DisconnectPlayer(SessionID);
				pSendPacket->DecreaseRefCount();
				pRecvPacket->DecreaseRefCount();
				return;
			}
			// 섹터 브로드캐스팅 - 자신을 제외한 9방향 섹터에 채팅 메시지 보내기
			ChatBroadcastSector(pPlayer, pSendPacket, pPlayer->SectorX, pPlayer->SectorY);

#ifdef MY_DEBUG
			pSendPacket->SetLog(6, pPlayer->SessionID);
#endif
			pSendPacket->DecreaseRefCount();
		}
			break;

		// 하트비트 처리
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
			pPlayer->timeout = dwTime; // timeOut 갱신
		}
			break;

			// 예상하지 못한 타입의 패킷이 온 경우. 잘못된 클라로 판단하고 연결을 끊는다.
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

// TimeOut 처리
void CChattingServer::JobClientTimeOut(DWORD dwTime)
{
	DWORD nowTime = dwTime;
	wchar_t IpStr[16];
	u_short usPort;

	auto loginIter = m_mLoginMap.begin();
	auto loginEnd = m_mLoginMap.end();
	for (; loginIter != loginEnd; )
	{
		// 아직 접속 중이지만 대기 시간이 긴 경우 연결을 끊어버린다.
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
		// 연결 중인 유저 중에 오랫동안 패킷을 보내지 않았다면 연결을 끊는다.
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

// 9방향 섹터의 모든 유저에게 SendPacket을 해준다.
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
				// Enq만 해주고 스스로 Send되도록 유도를 최대한 함.
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

// 연결 끊기 또는 끊겨진 유저 정리하기
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