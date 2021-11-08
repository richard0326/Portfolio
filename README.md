2021.11.03   
===========  
면접 후 포트폴리오에 대한 부족함을 느끼고 내용을 추가하기 위해서 만든 저장소임.  
빠른 시일 내에 더 추가하도록 하겠음.  
    
    
    
    
    
2021.11.06     
=========== 
면접에서 포트폴리오에 대한 설명을 더 요청함.   
면접에서 컨텐츠 제작 경험 부족을 지적 받음. (나중에 컨텐츠도 만들어서 추가하도록 하겠음.)   
시험에서 틀렸던 부분들은 대체로 그래픽스, DB에 대한 부분들이였음.   
      
    
    
    
2021.11.08  
===========
- 포트폴리오 추가 설명  
  
- (조금 부족한 내용들을 글, 사진 추가함.)   
  
[pdf에 있던 안정화 테스트 영상 링크](https://www.youtube.com/watch?v=Y7Du3PCgPkg)  
  
- 서버 테스트 환경 경험에서 테스트 환경  
  
<details>
    <summary>접기</summary>
    <div markdown="1">
![안정화 테스트 환경 이미지](https://github.com/richard0326/Portfolio/blob/main/2.PNG)  
    </div>
</details>

- 각 서버의 시퀀스 다이어그램  
  
![채팅서버 다이어그램](https://github.com/richard0326/Portfolio/blob/main/%EC%B1%84%ED%8C%85%EC%84%9C%EB%B2%84%20%EB%8B%A4%EC%9D%B4%EC%96%B4%EA%B7%B8%EB%9E%A8.PNG)  
![로그인서버 다이어그램](https://github.com/richard0326/Portfolio/blob/main/%EB%A1%9C%EA%B7%B8%EC%9D%B8%EC%84%9C%EB%B2%84%20%EB%8B%A4%EC%9D%B4%EC%96%B4%EA%B7%B8%EB%9E%A8.PNG)  
![MMORPG 에코 서버 다이어그램](https://github.com/richard0326/Portfolio/blob/main/MMORPG%EC%84%9C%EB%B2%84%20%EB%8B%A4%EC%9D%B4%EC%96%B4%EA%B7%B8%EB%9E%A8.PNG)  
  
  
- 안정화 테스트 설명 및 사진들  
  
![pdh 설명](https://github.com/richard0326/Portfolio/blob/main/pdh%EC%84%A4%EB%AA%85.PNG)  
![채팅서버 설명](https://github.com/richard0326/Portfolio/blob/main/%EC%B1%84%ED%8C%85%EC%84%A4%EB%AA%85.PNG)  
![로그인서버 설명](https://github.com/richard0326/Portfolio/blob/main/%EB%A1%9C%EA%B7%B8%EC%9D%B8%EC%84%A4%EB%AA%85.PNG)  
![MMORPG 에코 서버 설명](https://github.com/richard0326/Portfolio/blob/main/MMORPG%EC%84%A4%EB%AA%85.PNG)  
  
4~5일 안정화 테스트에서 이상 없이 잘 돌아갔으며,  
학원에서 졸업 기준치를 상회하는 서버를 만든 인증 사진임.  
기준치는 C#으로 만든 서버보다 빠른 서버 정도의 느낌이다.(학원 기준.)   
  
![채팅서버 안정화 테스트 결과 이미지](https://github.com/richard0326/Portfolio/blob/main/2020903%EC%9D%BC%EA%B9%8C%EC%A7%803.PNG)  
![로그인서버 안정화 테스트 결과 이미지](https://github.com/richard0326/Portfolio/blob/main/%EB%94%94%EC%9A%B4%ED%81%B4%EB%9D%BC%EB%B0%9C%EC%83%9D_%EC%9B%90%EC%9D%B8%EB%B6%88%EB%AA%85%206%EC%9D%BC.PNG)  
![MMORPG 에코 서버 안정화 테스트 결과 이미지](https://github.com/richard0326/Portfolio/blob/main/5%EC%9D%BC.PNG)  
  
- NetServer 설명  
   
[NetServer.h](https://github.com/richard0326/Portfolio/blob/main/NetServer.h)  
  
  
포트폴리오에서 설명했던 모델 1, 3이 NetServer를 상속 받아 만듦.  

모델 1.  
컨텐츠 부분에서 스레드를 1개 만들어서 Queueing 하는 방식으로 패킷을 처리하는 방식  
  
[ChattingServer_model1.cpp](https://github.com/richard0326/Portfolio/blob/main/ChattingServer_model1.cpp)  
[ChattingServer_model1.h](https://github.com/richard0326/Portfolio/blob/main/ChattingServer_model1.h)  
  
모델 3.   
각각의 IOThread에 OnRecv 가상 함수를 통해서 컨텐츠 부분에 패킷을 전달함.  
컨텐츠에서는 큐잉하지 않고 바로 처리하는 방식으로 진행함.  
멀티스레드를 고려해서 코딩해야함.  

모델3은 수업에서 언급만 하셨던 것인데, 제가 시간을 따로 만들어서 만들고 테스트를 진행해본 모델임.  
  
[ChattingServer_model3.cpp](https://github.com/richard0326/Portfolio/blob/main/ChattingServer_model3.cpp)  
[ChattingServer_model3.h](https://github.com/richard0326/Portfolio/blob/main/ChattingServer_model3.h)  
  
  
- MMORPGServer 설명  
  
[MMORPGServer.h](https://github.com/richard0326/Portfolio/blob/main/MMOServer.h)   
  
  
메모리 로그 관련 : DEBUG_LOCATION, st_ForDebug   
일부 모니터링 정보는 디버그 모드에서만 출력되도록 진행함.  
   
중요한 정보는 en_SESSION_STATE 부분에서 AUTH 부분과 GAME 부분으로 나눠지는데  
이것은 DB에서 데이터를 로드하기 전에는 AUTH 부분에서 싱글 스레드 방식으로 처리하고,  
이후에 플레이어에 대한 데이터를 모두 로드했다면, GAME 스레드 부분으로 넘겨주어서  
DB에 의해서 성능적으로 느려지는 부분을 막는다.  
   
헤더 파일에 이것 저것 많이 들어간 이유는  
초기화 할때, st_SESSION을 상속 받아서 컨텐츠에서 st_PLAYER를 동적할당해서 MMORPGServer로 넘겨주도록 만들었기 때문이다.  
MMORPGServer 내부 스레드에서 폴링하면서 st_Session을 직접 접근하기 때문에 그렇게 됨.  
  
가상함수를 스레드 별로 두었는데, 이는 외부 컨텐츠를 짤때, 스레드를 고려한 코딩이 필요하기 때문에 그렇다.  
예) OnAuth_XXXX, OnGame_XXXX  
  
  
- 로그인 서버 구조  
  
DB 활용에 대해서 많이 물어보셔서 솔직하게 잘하진 못하지만, 객체화시키기고 사용할 정도로는 쓸 줄 알고 있음.  
![로그인 서버 구조](https://github.com/richard0326/Portfolio/blob/main/%EB%A1%9C%EA%B7%B8%EC%9D%B8%20%EC%84%9C%EB%B2%84%20%EA%B5%AC%EC%A1%B0.PNG)  
  
[DBConnector.h](https://github.com/richard0326/Portfolio/blob/main/DBConnector.h)
[DBConnector.cpp](https://github.com/richard0326/Portfolio/blob/main/DBConnector.cpp)
  
[RedisConnector.h](https://github.com/richard0326/Portfolio/blob/main/RedisConnector.h)
[RedisConnector.cpp](https://github.com/richard0326/Portfolio/blob/main/RedisConnector.cpp)
  
  
- 락프리 메모리풀  
  
내부적으로 락프리스택을 사용함.  
메모리 관리를 Windows API 인 Virtual Alloc을 활용하여 해서, 유연하게 늘어나지는 않지만, 
메모리 누수 추적에 용이함.
스레드 간의 경함을 줄이기 위해서 만든 tls 활용한 스레드풀.  
  
[LockFreePool.h](https://github.com/richard0326/Portfolio/blob/main/LockFreePool.h)  
[LockFreeTlsPoolA.h](https://github.com/richard0326/Portfolio/blob/main/LockFreeTlsPoolA.h)
  
  
시간을 할애해서 컨텐츠 부분도 추가하여 올려보도록 노력하겠음.  
