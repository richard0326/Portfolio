#include "stdafx.h"
#include "ChattingServer.h"

#include <mmsystem.h>
#pragma comment(lib, "Winmm.lib")

#ifdef MY_DEBUG
// 메모리 로그 enum 문
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
	// Interlock으로 인한 cpu lock에 걸리지 않도록 
	// 64 캐시라인 크기 만큼 2개 변수를 벌려놓음.
	alignas(64) bool UseFlag;	// 현재 플레이러를 사용 중인지에 대한 변수
	alignas(64) short IOCount;	// 참조카운트 - 현재 플레이어를 참조하고 있는 개수
	
	//
	unsigned long long SessionID; // 네트워크 라이브러리에서 받은 Session ID
	INT64 AccountNo; // 유저 번호
	wchar_t ID[20]; // 유저 id
	wchar_t Nickname[20]; // 유저 닉네임
	char SessionKey[64]; // 유저 세션 키

	WORD SectorX; // 유저 섹터 위치 x, y
	WORD SectorY;

	DWORD timeout; // timeOut 변수
	wchar_t IpStr[16]; // IP, Port 변수
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

// 서버를 시작할때
bool CChattingServer::Start(const wchar_t* ipWstr, int portNum, int workerCreateCnt, int workerRunningCnt,
	bool bNoDelayOpt, int serverMaxUser, bool bRSTOpt, bool bKeepAliveOpt, bool bOverlappedSend, int SendQSize)
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

	m_shutdown = false;

	// 동기화 객체 초기화
	InitializeCriticalSection(&m_loginCS);
	InitializeCriticalSection(&m_playerCS);

	for(int i = 0; i < dfSECTOR_X_MAX; ++i)
		for(int j = 0; j < dfSECTOR_Y_MAX; ++j)
			InitializeCriticalSection(&m_sectorArr[i][j].sectorCS);	

	// 기본 초기화
	m_pPlayerPool = new CLockFreeTlsPoolA<st_PLAYER>(PLAYERPOOL_CHUNK, PLAYERPOOL_NODE);
	
	// timeout 스레드 생성
	m_hMoritoring = (HANDLE)_beginthreadex(nullptr, 0, CChattingServer::TimeoutThread, this, 0, nullptr);

	return CNetServer::Start(ipWstr, portNum, workerCreateCnt, workerRunningCnt, bNoDelayOpt, serverMaxUser, bRSTOpt, bKeepAliveOpt, bOverlappedSend, SendQSize);
}

// 서버를 정지할때
void CChattingServer::Stop()
{
	DeleteCriticalSection(&m_loginCS);
	DeleteCriticalSection(&m_playerCS);

	m_shutdown = true;
	CloseHandle(m_hMoritoring);

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

	// 1번 모델에서는 모든 플레이어 저장하는 것이 비효율적이여서 제거함.
	// IP, Port 받아오는 함수
	GetIP_Port(pPlayer->SessionID, pPlayer->IpStr, &pPlayer->usPort);

	// 위에서 초기 정보를 세팅하고, 접속중인 유저 목록에 추가한다.
	EnterCriticalSection(&m_loginCS);
	m_umLoginList[pPlayer->SessionID] = pPlayer;
	LeaveCriticalSection(&m_loginCS);
}

// Release 후 호출
void CChattingServer::OnClientLeave(unsigned long long SessionID)
{
	// PlayerMap에서 해당 SessionID를 갖고 있는 플레이어를 가져온다
	st_PLAYER* pPlayer = AcquirePlayerLock(SessionID);
	if (pPlayer == nullptr)
	{
		// LoginMap에서 플레이어를 가져온다.
		pPlayer = AcquireLoginLock(SessionID);
		if (pPlayer == nullptr)
			return;  // 실패하면 이미 제거된 것임.
	}

	// 연결 끊기 요청하고 참조카운트를 1개 내려준다.
	DisconnectPlayer(SessionID);
	IODecreasePlayer(pPlayer);
}

// 패킷 수신 완료 후
int CChattingServer::OnRecv(unsigned long long SessionID, CSerializeBuffer* pPacket)
{
	// CSerializeBuffer는 네트워크 라이브러리의 헤더를 제외한 부분만 온다.

	WORD Type;
	CSerializeBuffer* pRecvPacket = pPacket;

	try {
		(*pRecvPacket) >> Type;

		switch (Type)
		{
			// 로그인 요청 패킷
		case en_PACKET_CS_CHAT_REQ_LOGIN:
		{
			st_PLAYER* pPlayer = AcquireLoginLock(SessionID);
			if(pPlayer == nullptr)
			{
				return 0;
			}

			pPlayer->timeout = timeGetTime();

			// 패킷 언마샬링
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

			// 로그인 응답 마샬링 - 1은 성공
			mpChatResLogin(pSendPacket, 1, pPlayer->AccountNo);

			DEBUG(DEBUG_UPDATE_LOCATION::JOB_ONRECV_REQ_LOGIN_BEFORE_SEND, pPlayer, pSendPacket);
			// 패킷 송신
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

		// 섹터 이동
		case en_PACKET_CS_CHAT_REQ_SECTOR_MOVE:
		{
			// 접속에 성공한 플레이어 찾기
			st_PLAYER* pPlayer = AcquirePlayerLock(SessionID);
			bool first = false;

			if(pPlayer == nullptr)
			{
				// 접속에 진행중인 플레이어 찾기
				pPlayer = AcquireLoginLock(SessionID);
				if(pPlayer == nullptr)
				{
					return 0;
				}

				first = true;

				DEBUG(DEBUG_UPDATE_LOCATION::JOB_ONRECV_REQ_SECTOR_MOVEPLAYER, pPlayer, nullptr);
				
				// 접속 중인 맵에서 제거해준다.
				EnterCriticalSection(&m_loginCS);
				m_umLoginList.erase(SessionID);
				LeaveCriticalSection(&m_loginCS);

				// 접속에 성공한 플레이어 맵으로 옮긴다.
				EnterCriticalSection(&m_playerCS);
				m_umPlayerList[SessionID] = pPlayer;
				LeaveCriticalSection(&m_playerCS);
			}

			INT64 AccountNo;
			WORD	SectorX;
			WORD	SectorY;
			// 패킷 언마샬링
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
				// 처음이 아닌 경우. 이전에 다른 섹터에서 존재하기 때문에 다른 섹터에서 지워준다.
				DoubleLock(SectorX, SectorY, prevSectorX, prevSectorY); // 2개 섹터를 동시에 접근하는 경우 데드락이 발생할 수 있음으로 데드락 예방함수를 사용함.
				DEBUG(DEBUG_UPDATE_LOCATION::JOB_ONRECV_REQ_SECTOR_REMOVE_SECTOR, pPlayer, nullptr);
				m_sectorArr[prevSectorX][prevSectorY].playerList.remove(pPlayer);
				LeaveCriticalSection(&m_sectorArr[prevSectorX][prevSectorY].sectorCS);
			}
			else
				EnterCriticalSection(&m_sectorArr[SectorX][SectorY].sectorCS);

			pPlayer->SectorX = SectorX;
			pPlayer->SectorY = SectorY;

			// 다른 섹터로 이동시켜준다.
			DEBUG(DEBUG_UPDATE_LOCATION::JOB_ONRECV_REQ_SECTOR_REPLACE_SECTOR, pPlayer, nullptr);
			m_sectorArr[SectorX][SectorY].playerList.push_back(pPlayer);

			LeaveCriticalSection(&m_sectorArr[SectorX][SectorY].sectorCS);

			CSerializeBuffer* pSendPacket = CSerializeBuffer::Alloc(NETHEADER, 14);
			if (pSendPacket == nullptr)
				CCrashDump::Crash();

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

		// 채팅 메시지 요청
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
			// 패킷 언마샬링
			if (ppChatReqMessage(pRecvPacket, &AccountNo, &MessageLen, Message) == false)
			{
				LOG(L"ChattingMsg", en_LOG_LEVEL::LEVEL_DEBUG, L"[ IP %s, Port %d ] Wrong AccountNo", pPlayer->IpStr, pPlayer->usPort);
				DisconnectPlayer(SessionID);
				IODecreasePlayer(pPlayer);
				return 0;
			}

			pPlayer->timeout = timeGetTime();
			// AccountNo 비교
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
			// 메시지 마샬링
			mpChatResMessage(pSendPacket, AccountNo, pPlayer->ID, pPlayer->Nickname, MessageLen, Message);

			DEBUG(DEBUG_UPDATE_LOCATION::JOB_ONRECV_REQ_CHAT_MSG_BEFORE_SEND, pPlayer, pSendPacket);
			// 섹터 브로드캐스팅 - 자신을 포함한 9방향 섹터에게 채팅 메시지 보내기
			ChatBroadcastSector(pSendPacket, pPlayer->SectorX, pPlayer->SectorY);
			
#ifdef MY_DEBUG
			pSendPacket->SetLog(6, pPlayer->SessionID);
#endif
			pSendPacket->DecreaseRefCount();
			IODecreasePlayer(pPlayer);
		}
		break;

		// 하트비트 처리
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

		// 예상하지 못한 타입의 패킷이 온 경우. 잘못된 클라로 판단하고 연결을 끊는다.
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

	return 1; // 정상 종료
}

void CChattingServer::OnError(const wchar_t* ipWstr, int portNum, int errorCode, const wchar_t* errorMsg)
{
	LOG(L"Error", en_LOG_LEVEL::LEVEL_ERROR, L"[ IP %s, Port %d ] errMsg  %s , errCode %d ", ipWstr, portNum, errorMsg, errorCode);
}

// 네트워크 라이브러리로 부터 모니터링 정보 획득
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

	// 모니터링 정보 출력...
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

// timeOut 스레드
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
		// timeOut은 내부 구현하지 않음.
		pChattingServer->CheckTimeOut();

		Sleep(10000);
	}

	return 0;
}

void CChattingServer::CheckTimeOut()
{
	// timeOut은 구현하지 않음.
	// 성능 테스트는 1번 모델 한정으로 했기 때문에 timeOut은 구현하지 않음.
	// 최적화도 1번 모델만 진행함.
}

// 9방향 섹터 브로드캐스팅
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

// 연결 끊기 또는 끊겨진 유저 정리하기
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

// 정당하게 접속중인 플레이어 획득하기
st_PLAYER* CChattingServer::AcquireLoginLock(unsigned long long SessionID)
{
	// 락을 걸고 loginList에 플레이어가 있는 경우
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
		// 참조카운트를 증가시키고 획득에 성공한다.
		InterlockedIncrement16(&pPlayer->IOCount);
	}
	LeaveCriticalSection(&m_loginCS);

	return pPlayer;
}

// 정당하게 접속에 성공한 플레이어 획득하기
st_PLAYER* CChattingServer::AcquirePlayerLock(unsigned long long SessionID)
{
	// 락을 걸고 PlayerList에 플레이어가 있는 경우
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
		// 참조카운트를 증가시키고 획득에 성공한다.
		InterlockedIncrement16(&pPlayer->IOCount);
	}
	LeaveCriticalSection(&m_playerCS);

	return pPlayer;
}

// 참조카운트 감소
void CChattingServer::IODecreasePlayer(st_PLAYER* pPlayer)
{
	// 일단 감소 시키고
	int IOCount = InterlockedDecrement16(&pPlayer->IOCount);
	if (IOCount < 0)
	{
		int* crash = nullptr;
		*crash = 0;
	}

	// 0인 경우 - 모든 스레드에서 이 플레이어를 사용하지 않는 상태가 됨
	if (IOCount == 0)
	{
		unsigned long long SessionID = pPlayer->SessionID;
		EnterCriticalSection(&m_playerCS);
		if (m_umPlayerList.find(SessionID) != m_umPlayerList.end())
		{
			// PlayerList 안에 있는 경우
			DEBUG(DEBUG_UPDATE_LOCATION::DISCONNECT_PLAYER_ERASE_PLAYER, pPlayer, nullptr);

			// 지워준다.
			m_umPlayerList.erase(SessionID);
			LeaveCriticalSection(&m_playerCS);

			// 섹터에서도 제거해준다.
			EnterCriticalSection(&m_sectorArr[pPlayer->SectorX][pPlayer->SectorY].sectorCS);
			m_sectorArr[pPlayer->SectorX][pPlayer->SectorY].playerList.remove(pPlayer);
			LeaveCriticalSection(&m_sectorArr[pPlayer->SectorX][pPlayer->SectorY].sectorCS);

			// 메모리풀에 반납한다.
			m_pPlayerPool->Free(pPlayer);
			return;
		}
		LeaveCriticalSection(&m_playerCS);

		EnterCriticalSection(&m_loginCS);
		if (m_umLoginList.find(SessionID) != m_umLoginList.end())
		{
			DEBUG(DEBUG_UPDATE_LOCATION::DISCONNECT_PLAYER_ERASE_LOGIN, pPlayer, nullptr);

			// 접속중인 경우 섹터에는 없기 떄문에 리스트에서만 제거
			m_umLoginList.erase(SessionID);
			LeaveCriticalSection(&m_loginCS);

			// 메모리풀에 반납한다.
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
		// 2개의 객체에 대해서 락을 시도한다.
		// 하나라도 실패하는 경우에는 Unlock을 하고 처음부터 다시 시도한다.
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