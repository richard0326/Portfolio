#pragma once

#include <Windows.h>
#include <memoryapi.h>
#include <iostream>

template <typename T>
class CLockFreePool
{
	template <typename T> friend class CLockFreeTlsPoolA;

	struct st_Node
	{
		T data;
#ifdef MY_DEBUG
		int iGuard;
		bool	bDealloced;
#endif
		st_Node* pNext;
	};

	struct st_Top
	{
		st_Node* node;
		long long nodeID;
	};

public:
	CLockFreePool(int poolSize, bool placementNew = false, bool placementOnce = false, int aligned = 0, bool pageLock = false)
	{
		//m_pHead = (st_Top*)_aligned_malloc(sizeof(st_Top), 16);
		m_pHead.node = nullptr;
		m_pHead.nodeID = 1;

		m_bPlacementNew = placementNew;
		m_bPlacementOnce = placementOnce;
		m_aligned = aligned;
#ifdef MY_DEBUG
		static int st_iGuard = 0;
		m_iGuard = InterlockedIncrement((LONG*)&st_iGuard);
#endif

		if (SetPoolSize(poolSize, pageLock) == false)
		{
			int* crash = nullptr;
			*crash = 0;
		}
	}

	~CLockFreePool()
	{
		if (m_bPlacementOnce)
		{
			int padding = 0;
			if(m_aligned != 0)
				padding = m_aligned - (sizeof(st_Node) % m_aligned);
			
			int paddingNodeSize = sizeof(st_Node) + padding;

			T* pInitNext = (T*)m_pPoolPtr;

			for (int i = 0; i < m_maxPoolSize; ++i)
			{
				pInitNext->~T();

				pInitNext = (T*)((char*)pInitNext + paddingNodeSize);
			}
		}

		//_aligned_free(m_pHead);

		VirtualFree(m_pPoolPtr, 0, MEM_RELEASE);
	}

	__forceinline T* Alloc()
	{
		if (InterlockedIncrement(&m_allocCnt) > m_maxPoolSize)
		{
			InterlockedDecrement(&m_allocCnt);
			return nullptr;
		}

		st_Top top;
		st_Node* outputNode = nullptr;

		while (1)
		{
			top.node = m_pHead.node;
			top.nodeID = m_pHead.nodeID;

			//if (top.nodeID == m_pHead->nodeID)
			{
				if (1 == InterlockedCompareExchange128((long long*)&m_pHead,
					top.nodeID + 1, (long long)top.node->pNext,
					(long long*)&top))
				{
					outputNode = top.node;
#ifdef MY_DEBUG
					outputNode->bDealloced = false;
#endif
					outputNode->pNext = nullptr;

					if (m_bPlacementNew)
						new (outputNode) T;

					break;
				}
			}

			YieldProcessor();
		}

		return (T*)outputNode;
	}

	__forceinline bool Free(T* pDealloc)
	{
		st_Node* pNode = (st_Node*)pDealloc;
#ifdef MY_DEBUG
		if (pDealloc == nullptr)
		{
			// nullptr 반환시
			int* _crash = nullptr;
			*_crash = 0;
			return false;
		}	
		// stNode로 변환
		if (pNode->iGuard != m_iGuard)
		{
			// 가드 침범시...
			int* _crash = nullptr;
			*_crash = 1;
			return false;
		}

		if (pNode->bDealloced == true)
		{
			// 중복 반환 시
			int* _crash = nullptr;
			*_crash = 1;
			return false;
		}
#endif	

		st_Top top;

		while (true)
		{
			top.node = m_pHead.node;
			top.nodeID = m_pHead.nodeID;

			pNode->pNext = top.node;
#ifdef MY_DEBUG
			pNode->bDealloced = true;
#endif
			//if (top.nodeID == m_pHead->nodeID)
			{
				if (1 == InterlockedCompareExchange128((long long*)&m_pHead,
					top.nodeID + 1, (long long)pNode,
					(long long*)&top))
				{
					if (m_bPlacementNew)
						pDealloc->~T();

					InterlockedDecrement(&m_allocCnt);
					break;
				}
			}

			YieldProcessor();
		}

		return true;
	}

	__forceinline int GetAllocSize()
	{
		return m_allocCnt;
	}

	__forceinline int GetCapacitySize()
	{
		return m_maxPoolSize;
	}

private:
	bool SetPoolSize(int poolSize, bool pageLock)
	{
		// 이미 할당이 된 경우에는 pool을 변경할 수 없게 변경
		if (m_pPoolPtr != nullptr)
		{
			return false;
		}

		int paddingNodeSize = sizeof(st_Node);
		if (m_aligned != 0)
		{
			// 줄 맞춰서 생성하기
			if (m_aligned % 2 == 0)
			{
				int padding = m_aligned - (sizeof(st_Node) % m_aligned);
				paddingNodeSize = sizeof(st_Node) + padding;
			}
			else
				m_aligned = 0;
		}

		m_pPoolPtr = (char*)VirtualAlloc(NULL, paddingNodeSize * poolSize, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
		if (m_pPoolPtr == nullptr)
			return false;

		if (pageLock)
		{
			// page lock - 비효율적인 것 같아서 주석으로 처리함.
			if (VirtualLock(m_pPoolPtr, paddingNodeSize * poolSize) == 0)
			{
				printf("Virtual Lock Error : %d\n", GetLastError());
				VirtualFree(m_pPoolPtr, 0, MEM_RELEASE);
				return false;
			}
		}

		st_Node* pInitNext = (st_Node*)m_pPoolPtr;
		m_pHead.node = pInitNext;
		m_allocCnt = 0;
		m_maxPoolSize = poolSize;

		for (int i = 0; i < poolSize; ++i)
		{
#ifdef MY_DEBUG
			// 반환 확인용 변수 초기화 - 반환되어 있는 상태
			pInitNext->bDealloced = true;
			pInitNext->iGuard = m_iGuard;
#endif
			if (m_bPlacementOnce)
				new (pInitNext) T;

			// 다음 노드 설정, next 설정
			if (i != poolSize - 1) {
				pInitNext = pInitNext->pNext = (st_Node*)((char*)pInitNext + paddingNodeSize); // 포인터 연산
			}
			else
			{
				pInitNext->pNext = nullptr;
			}
		}

		return true;
	}

private:
	long			m_maxPoolSize = 0;
	int				m_aligned;
	char*			m_pPoolPtr = nullptr;
#ifdef MY_DEBUG
	int				m_iGuard;
#endif
	bool			m_bPlacementNew;
	bool			m_bPlacementOnce;
	__declspec(align(64)) long		m_allocCnt = 0;
	__declspec(align(64)) st_Top m_pHead;
};