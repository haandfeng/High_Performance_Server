cmake_minimum_required(VERSION 3.27)
project(High_Performance_Server)

set(CMAKE_CXX_STANDARD 17)

add_executable(High_Performance_Server main.cpp)

add_executable(ReadHttp
        C8/ReadHttp.cpp)

add_executable(ReceiveTypeDataBySelect
        C9/ReceiveUrgDataBySelect.cpp
        C9/ReceiveUrgDataBySelect.cpp)
add_executable(LTvsET C9/LTvsET.cpp
        C9/LTvsET.cpp)
add_executable(UnblockedConnect C9/UnblockedConnect.cpp
        C9/UnblockedConnect.cpp)
add_executable(ChatRoom_Client C9/ChatRoom_Client.cpp
        C9/ChatRoom_Client.cpp
        C11/LST_Timer.h)
add_executable(ChatRoom_Server C9/ChatRoom_Server.cpp
        C9/ChatRoom_Server.cpp)
add_executable(SimultaneousTCPandUDP C9/SimultaneousTCPandUDP.cpp
        C9/SimultaneousTCPandUDP.cpp)
add_executable(UnifiedEventSource C10/UnifiedEventSource.cpp
        C10/UnifiedEventSource.cpp)
add_executable(ReceiveUrgDataBySig C10/ReceiveUrgDataBySig.cpp
        C10/ReceiveUrgDataBySig.cpp)
add_executable(ConnectTimeOut C11/ConnectTimeOut.cpp
        C11/ConnectTimeOut.cpp)
add_executable(CloseUnactiveConnect C11/CloseUnactiveConnect.cpp
        C11/CloseUnactiveConnect.cpp
        C11/LST_Timer.h)

add_executable(ShareMemoryChatRoomServer C13/ShareMemoryChatRoomServer.cpp
        C13/ShareMemoryChatRoomServer.cpp)
add_executable(PassFD C13/PassFD.cpp
        C13/PassFD.cpp)