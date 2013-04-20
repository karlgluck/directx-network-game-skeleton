//------------------------------------------------------------------------------------------------
// File:    user.h
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
#ifndef __USER_H__
#define __USER_H__


// Include files required to compile this header
#include <winsock2.h>

enum EndWaitResult
{
    EWR_RECVPACKET,
    EWR_DISCONNECT,
    EWR_TIMEOUT,
};

class User
{
    public:

        User();
        ~User();
        HRESULT Create( DWORD dwId, WORD wPort );
        VOID Destroy();

        DWORD GetId();

        HRESULT Connect( const SOCKADDR_IN * pAddress, LPTHREAD_START_ROUTINE lpProcAddress );
        VOID Disconnect();
        BOOL IsConnected();
        VOID ThreadFinished();

        EndWaitResult WaitForPackets( DWORD dwTimeout );
        int SendPacket( const CHAR * pBuffer, int length );
        int RecvPacket( char * pBuffer, int length );

    protected:

        BOOL m_bConnected;
        DWORD m_dwId;
        HANDLE m_hThread;
        WSAEVENT m_hRecvEvent;
        SOCKET m_sSocket;
        HANDLE m_hDisconnectEvent;
};

#endif // __USER_H__