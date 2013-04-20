//------------------------------------------------------------------------------------------------
// File:    ngsserver.cpp
//
// Desc:    Entry point for the server
//
//  Copyright 2006-2010 Karl Gluck. All rights reserved.
//
//  Redistribution and use in source and binary forms, with or without modification, are
//  permitted provided that the following conditions are met:
//
//     1. Redistributions of source code must retain the above copyright notice, this list of
//        conditions and the following disclaimer.
//
//     2. Redistributions in binary form must reproduce the above copyright notice, this list
//        of conditions and the following disclaimer in the documentation and/or other materials
//        provided with the distribution.
//
//  THIS SOFTWARE IS PROVIDED BY KARL GLUCK ``AS IS'' AND ANY EXPRESS OR IMPLIED
//  WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND
//  FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL KARL GLUCK OR
//  CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
//  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
//  SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
//  ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
//  NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
//  ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//
//  The views and conclusions contained in the software and documentation are those of the
//  authors and should not be interpreted as representing official policies, either expressed
//  or implied, of Karl Gluck.
//------------------------------------------------------------------------------------------------
#include <winsock2.h>
#include <stdio.h>
#include <conio.h>
#include "user.h"

// Settings that define how the server operates
#define WINSOCK_VERSION     MAKEWORD(2,2)
#define SERVER_COMM_PORT    27192
#define MAX_PACKET_SIZE     1024
#define MAX_USERS           16


// Global variables used in the server program.  These variables are global because they are used
// by the server thread and initialized in the main thread.  It would be inefficient and
// duplicative to find an object-oriented workaround.
HANDLE g_hExitEvent;    // Set when the server shuts down
HANDLE g_hCommThread;   // Thread processing object
WSAEVENT g_hRecvEvent;  // Event triggered when data is received
SOCKET g_sSocket;       // Socket to send and receive on

// Global variables for client management
//HANDLE g_hUserSemaphore;    // Allows only a certain number of users to process simultaneously
User g_Users[MAX_USERS];    // List of all of the users

enum Message
{
    MSG_LOGON,
    MSG_LOGOFF,
    MSG_UPDATEPLAYER,
    MSG_CONFIRMLOGON,
    MSG_PLAYERLOGGEDOFF,
};

struct MessageHeader
{
    Message MsgID;
};

struct LogOnMessage
{
    MessageHeader   Header;

    LogOnMessage() { Header.MsgID = MSG_LOGON; }
};

struct LogOffMessage
{
    MessageHeader   Header;

    LogOffMessage() { Header.MsgID = MSG_LOGOFF; }
};

struct UpdatePlayerMessage
{
    MessageHeader   Header;
    DWORD           dwPlayerID;
    FLOAT           fVelocity[3];       // Expressed in meters / second
    FLOAT           fPosition[3];
    DWORD           dwState;
    FLOAT           fYaw;

    UpdatePlayerMessage() { Header.MsgID = MSG_UPDATEPLAYER; }
};

struct ConfirmLogOnMessage
{
    MessageHeader   Header;

    ConfirmLogOnMessage() { Header.MsgID = MSG_CONFIRMLOGON; }
};

struct PlayerLoggedOff
{
    MessageHeader   Header;
    DWORD           dwPlayerID;

    PlayerLoggedOff() { Header.MsgID = MSG_PLAYERLOGGEDOFF; }
};


BOOL WaitForPackets()
{
    WSAEVENT hEvents[] = { g_hRecvEvent, g_hExitEvent };
    return WAIT_OBJECT_0 == WSAWaitForMultipleEvents( 2, hEvents, FALSE, INFINITE, FALSE );
}


int RecvPacket( char * pBuffer, int length, SOCKADDR_IN * pAddress  )
{
    // CHEAP TRICK!!  Force the receive event to reset.  Find out why this doesn't usually work!!
    ResetEvent( g_hRecvEvent );

    // Size of the address structure
    int iFromLen = sizeof(SOCKADDR_IN);

    // Perform the operation
    return recvfrom( g_sSocket, pBuffer, length, 0, (LPSOCKADDR)pAddress, &iFromLen );
}


int SendPacket( const LPSOCKADDR_IN pAddress, const CHAR * pBuffer, int length )
{
    return sendto( g_sSocket, pBuffer, length, 0, (LPSOCKADDR)&pAddress, sizeof(SOCKADDR_IN) );
}

HRESULT ProcessUserPacket( User * pUser, const CHAR * pBuffer, DWORD dwSize )
{
    // Get the message header so we can determine the type of the packet
    MessageHeader * mh = (MessageHeader*)pBuffer;

    // Process the message
    switch( mh->MsgID )
    {
        case MSG_LOGOFF:
            {
                // Get this user's ID number
                DWORD dwId = pUser->GetId();

                // Build a disconnect message
                PlayerLoggedOff packet;
                packet.dwPlayerID = dwId;

                // Tell all of the other users that this player disconnected
                for( DWORD i = 0; i < MAX_USERS; ++i )
                {
                    // Send to all connected users except the source
                    if( (i != dwId) && (g_Users[i].IsConnected()) )
                        g_Users[i].SendPacket( (CHAR*)&packet, sizeof(packet) );
                }

                // Disconnect the user
                pUser->Disconnect();

                // Return a success message, but the recieve loop can't continue
                return S_FALSE;
            }

        case MSG_UPDATEPLAYER:
            {
                // Get this user's ID number
                DWORD dwId = pUser->GetId();

                // Set the message's ID component
                UpdatePlayerMessage upm;
                memcpy( &upm, pBuffer, dwSize );
                upm.dwPlayerID = dwId;

                // Re-broadcast this message
                for( DWORD i = 0; i < MAX_USERS; ++i )
                {
                    // Send to all connected users except the source
                    if( (i != dwId) && (g_Users[i].IsConnected()) )
                        g_Users[i].SendPacket( (CHAR*)&upm, dwSize );
                }

            } break;

        default:
            {
                // This message type couldn't be processed
                return E_FAIL;
            }
    }

    // Success
    return S_OK;
}

DWORD WINAPI UserProcessor( LPVOID pParam )
{
    // Get a pointer to the user structure
    User * pUser = (User*)pParam;

    // Send a message to the user telling them that they have successfully logged on
    {
        // Build a packet
        ConfirmLogOnMessage packet;

        // Send the packet
        pUser->SendPacket( (CHAR*)&packet, sizeof(packet) );
    }

    // Activity flag
    BOOL bUserActive = TRUE;

    // Enter the processing loop
    while( bUserActive )
    {
        // Wait for something to happen or for 5 seconds to expire
        switch( pUser->WaitForPackets( 5000 ) )
        {
            // When a packet is recieved, process it
            case EWR_RECVPACKET:
                {
                    OutputDebugString( "." );

                    // Buffers used to recieve data
                    CHAR buffer[MAX_PACKET_SIZE];
                    int size;

                    // Get the packet
                    while( SOCKET_ERROR != (size = pUser->RecvPacket( buffer, sizeof(buffer) )) )
                    {
                        // Process information from the packet
                        if( S_FALSE == ProcessUserPacket( pUser, buffer, size ) )
                            break;
                    }

                } break;

            // If the user hasn't sent a message in a while, determine if the program wants to
            // exit.  If it does, break the loop.
            case EWR_TIMEOUT:
                {
                    // Output message
                    printf( "\n[%u] lagged out", pUser->GetId() );

                    // If the global termination event is set, exit
                    if( WAIT_OBJECT_0 == WaitForSingleObject( g_hExitEvent, 0 ) )
                        break;

                    // Log this player out
                    //pUser->Disconnect();

                } break;

            // This user has been forcibly disconnected by the server, usually by shutting down.
            case EWR_DISCONNECT:
                {
                    // Output message
                    printf( "\n[%u] disconnected", pUser->GetId() );

                    // No longer active
                    bUserActive = FALSE;

                } break;
        }
    }

    pUser->ThreadFinished();

    // Success
    return S_OK;
}


HRESULT LogOnNewPlayer( const SOCKADDR_IN * pAddr )
{
    // Look through the list
    for( int i = 0; i < MAX_USERS; ++i )
    {
        // If this user structure isn't already handling someone, set up the connection
        if( g_Users[i].IsConnected() == FALSE )
        {
            printf( "\nLogged on user %i", i );
            return g_Users[i].Connect( pAddr, UserProcessor );
        }
    }

    // Couldn't find a place for the player
    return E_FAIL;
}


DWORD WINAPI CommThread( LPVOID pParam )
{
    // Wait for things to happen
    while( WaitForPackets() )
    {
        // Holds incoming data values
        char buffer[MAX_PACKET_SIZE];
        SOCKADDR_IN address;
        int len;
    
        ZeroMemory( buffer, sizeof(buffer) );

        // Get data until the operation would block
        while( SOCKET_ERROR != (len = RecvPacket( buffer, sizeof(buffer), &address )) )
        {
            MessageHeader * pMh = (MessageHeader*)buffer;
            if( pMh->MsgID == MSG_LOGON )
                LogOnNewPlayer( &address );
        }
    }

    // Success
    return S_OK;
}


int main()
{
    // Start up Winsock
    {
        // Stores information about Winsock
        WSADATA wsaData;

        // Call the DLL initialization function
        if( SOCKET_ERROR == WSAStartup( WINSOCK_VERSION, &wsaData ) ||
                    wsaData.wVersion != WINSOCK_VERSION )
            return -1;
    }

    // Tell the user the local IP address and host information
    {
        CHAR strHostName[80];
        if( SOCKET_ERROR == gethostname( strHostName, sizeof(strHostName) ) )
            return -1;

        // Get the entry in the host table
        LPHOSTENT pHostEnt = gethostbyname(strHostName);
        if( !pHostEnt )
            return -1;

        // Get the primary address from the host entry table
        in_addr addr;
        memcpy(&addr, pHostEnt->h_addr_list[0], sizeof(addr));

        // Tell the user information about the server
        printf( "Server '%s' is operating at %s\n", strHostName, inet_ntoa(addr) );
    }

    // Set up server data
    {
        // Initialize the exit event
        g_hExitEvent = CreateEvent( NULL, TRUE, FALSE, NULL );

        // Set up the main socket to accept and send UDP packets
        g_sSocket = socket( AF_INET, SOCK_DGRAM, IPPROTO_UDP );

        // Generate the address to bind to
        SOCKADDR_IN addr;
        addr.sin_family = AF_INET;
        addr.sin_port = htons( SERVER_COMM_PORT );
        addr.sin_addr.S_un.S_addr = INADDR_ANY;

        // Bind it to a port
        if( SOCKET_ERROR == bind( g_sSocket, (LPSOCKADDR)&addr, sizeof(SOCKADDR_IN) ) )
            return -1;

        // Create the event
        g_hRecvEvent = WSACreateEvent();
        if( SOCKET_ERROR == WSAEventSelect( g_sSocket, g_hRecvEvent, FD_READ ) )
            return -1;

        // Create the processor thread
        if( NULL == (g_hCommThread = CreateThread( NULL, 0, CommThread, NULL, 0, NULL )) )
            return -1;
    }

    // Initialize all of the clients
    {
        WORD wBasePort = SERVER_COMM_PORT + 1;
        for( int i = 0; i < MAX_USERS; ++i )
            while( !g_Users[i].Create( i, wBasePort + i ) ) { wBasePort++; }
    }

    // Tell the user that the server has been initialized
    printf( "Server successfully initialized.  Press any key to exit..." );

    // Repeat until the termination event is set
    while( WAIT_OBJECT_0 != WaitForSingleObject( g_hExitEvent, 0 ) )
    {
        // Wait for 10 seconds to exit
        _getch();
        SetEvent( g_hExitEvent );
    }

    // Wait for the main thread to terminate
    WaitForSingleObject( g_hCommThread, INFINITE );

    // Shut down all of the clients
    {
        for( int i = 0; i < MAX_USERS; ++i )
            g_Users[i].Destroy();
    }

    // Delete the receive event
    WSACloseEvent( g_hRecvEvent );

    // Close the socket
    closesocket( g_sSocket );

    // Shut down Winsock
    WSACleanup();

    // Success
    return 0;
}