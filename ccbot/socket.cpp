/*

   Copyright [2009] [Joško Nikolić]

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.

   HEAVILY MODIFIED PROJECT BASED ON GHOST++: http://forum.codelain.com
   GHOST++ CODE IS PORTED FROM THE ORIGINAL GHOST PROJECT: http://ghost.pwner.org/

*/

#include "ccbot.h"
#include "util.h"
#include "socket.h"

#include <string.h>

#ifndef WIN32
 int GetLastError( ) { return errno; }
#endif

//
// CSocket
//

CSocket :: CSocket( ) : m_Socket( INVALID_SOCKET ), m_HasError( false ), m_Error( 0 )
{
	memset( &m_SIN, 0, sizeof( m_SIN ) );
}

CSocket :: CSocket( SOCKET nSocket, struct sockaddr_in nSIN ) : m_Socket( nSocket ), m_SIN( nSIN ), m_HasError( false ), m_Error( 0 )
{

}

CSocket :: ~CSocket( )
{
	if( m_Socket != INVALID_SOCKET )
		closesocket( m_Socket );
}

BYTEARRAY CSocket :: GetPort( )
{
	return UTIL_CreateByteArray( m_SIN.sin_port, false );
}

BYTEARRAY CSocket :: GetIP( )
{
	return UTIL_CreateByteArray( (uint32_t)m_SIN.sin_addr.s_addr, false );
}

string CSocket :: GetIPString( )
{
	return inet_ntoa( m_SIN.sin_addr );
}

string CSocket :: GetErrorString( )
{
	if( !m_HasError )
		return "NO ERROR";

	switch( m_Error )
	{
		case EWOULDBLOCK: 	return "EWOULDBLOCK";
		case EINPROGRESS:	return "EINPROGRESS";
		case EALREADY: 		return "EALREADY";
		case ENOTSOCK: 		return "ENOTSOCK";
		case EDESTADDRREQ: 	return "EDESTADDRREQ";
		case EMSGSIZE: 		return "EMSGSIZE";
		case EPROTOTYPE: 	return "EPROTOTYPE";
		case ENOPROTOOPT: 	return "ENOPROTOOPT";
		case EPROTONOSUPPORT: 	return "EPROTONOSUPPORT";
		case ESOCKTNOSUPPORT:	return "ESOCKTNOSUPPORT";
		case EOPNOTSUPP: 	return "EOPNOTSUPP";
		case EPFNOSUPPORT: 	return "EPFNOSUPPORT";
		case EAFNOSUPPORT:	return "EAFNOSUPPORT";
		case EADDRINUSE: 	return "EADDRINUSE";
		case EADDRNOTAVAIL: 	return "EADDRNOTAVAIL";
		case ENETDOWN: 		return "ENETDOWN";
		case ENETUNREACH: 	return "ENETUNREACH";
		case ENETRESET: 	return "ENETRESET";
		case ECONNABORTED: 	return "ECONNABORTED";
		case ECONNRESET: 	return "ECONNRESET";
		case ENOBUFS: 		return "ENOBUFS";
		case EISCONN: 		return "EISCONN";
		case ENOTCONN: 		return "ENOTCONN";
		case ESHUTDOWN: 	return "ESHUTDOWN";
		case ETOOMANYREFS: 	return "ETOOMANYREFS";
		case ETIMEDOUT: 	return "ETIMEDOUT";
		case ECONNREFUSED: 	return "ECONNREFUSED";
		case ELOOP: 		return "ELOOP";
		case ENAMETOOLONG: 	return "ENAMETOOLONG";
		case EHOSTDOWN: 	return "EHOSTDOWN";
		case EHOSTUNREACH: 	return "EHOSTUNREACH";
		case ENOTEMPTY: 	return "ENOTEMPTY";
		case EUSERS: 		return "EUSERS";
		case EDQUOT: 		return "EDQUOT";
		case ESTALE: 		return "ESTALE";
		case EREMOTE: 		return "EREMOTE";
	}

	return "UNKNOWN ERROR";
}

void CSocket :: SetFD( fd_set *fd, fd_set *send_fd, int *nfds )
{
	if( m_Socket == INVALID_SOCKET )
		return;

	FD_SET( m_Socket, fd );

#ifndef WIN32
	if( m_Socket > *nfds )
		*nfds = m_Socket;
#endif
}

void CSocket :: Allocate( int type )
{
	m_Socket = socket( AF_INET, type, 0 );

	if( m_Socket == INVALID_SOCKET )
	{
		m_HasError = true;
		m_Error = GetLastError( );
		CONSOLE_Print( "[SOCKET] error (socket) - " + GetErrorString( ) );
		return;
	}
}

void CSocket :: Reset( )
{
	if( m_Socket != INVALID_SOCKET )
		closesocket( m_Socket );

	m_Socket = INVALID_SOCKET;
	memset( &m_SIN, 0, sizeof( m_SIN ) );
	m_HasError = false;
	m_Error = 0;
}

//
// CTCPSocket
//

CTCPSocket :: CTCPSocket( ) : CSocket( ), m_Connected( false )
{
	Allocate( SOCK_STREAM );
	m_LastRecv = m_LastSend = GetTime( );

	// make socket non blocking

#ifdef WIN32
	int iMode = 1;
	ioctlsocket( m_Socket, FIONBIO, (u_long FAR *)&iMode );
#else
	fcntl( m_Socket, F_SETFL, fcntl( m_Socket, F_GETFL ) | O_NONBLOCK );
#endif

	// disable Nagle's algorithm for better performance
	
	int OptVal = 1;
	setsockopt( m_Socket, IPPROTO_TCP, TCP_NODELAY, (const char *)&OptVal, sizeof( int ) );	
}

void CTCPSocket :: SetFD( fd_set *fd, fd_set *send_fd, int *nfds )
{
	if( m_Socket == INVALID_SOCKET )
		return;

	FD_SET( m_Socket, fd );

	if( !m_SendBuffer.empty() ) 
		FD_SET( m_Socket, send_fd );

#ifndef WIN32
	if( m_Socket > *nfds )
		*nfds = m_Socket;
#endif
}

CTCPSocket :: CTCPSocket( SOCKET nSocket, struct sockaddr_in nSIN ) : CSocket( nSocket, nSIN ), m_Connected( true )
{
	m_LastRecv = m_LastSend = GetTime( );

	// make socket non blocking

#ifdef WIN32
	int iMode = 1;
	ioctlsocket( m_Socket, FIONBIO, (u_long FAR *)&iMode );
#else
	fcntl( m_Socket, F_SETFL, fcntl( m_Socket, F_GETFL ) | O_NONBLOCK );
#endif

	// disable Nagle's algorithm for better performance

	int OptVal = 1;
	setsockopt( m_Socket, IPPROTO_TCP, TCP_NODELAY, (const char *)&OptVal, sizeof( int ) );
}

CTCPSocket :: ~CTCPSocket( )
{

}

void CTCPSocket :: Reset( )
{
	CSocket :: Reset( );

	Allocate( SOCK_STREAM );
	m_Connected = false;
	m_RecvBuffer.clear( );
	m_SendBuffer.clear( );
	m_LastRecv = m_LastSend = GetTime( );

	// make socket non blocking

#ifdef WIN32
	int iMode = 1;
	ioctlsocket( m_Socket, FIONBIO, (u_long FAR *)&iMode );
#else
	fcntl( m_Socket, F_SETFL, fcntl( m_Socket, F_GETFL ) | O_NONBLOCK );
#endif

	// disable Nagle's algorithm for better performance
	
	int OptVal = 1;
	setsockopt( m_Socket, IPPROTO_TCP, TCP_NODELAY, (const char *)&OptVal, sizeof( int ) );
}

void CTCPSocket :: PutBytes( const string &bytes )
{
	m_SendBuffer += bytes;
}

void CTCPSocket :: PutBytes( const BYTEARRAY &bytes )
{
	m_SendBuffer += string( bytes.begin( ), bytes.end( ) );
}

void CTCPSocket :: DoRecv( fd_set *fd )
{
	if( m_Socket == INVALID_SOCKET || m_HasError || !m_Connected )
		return;

	if( FD_ISSET( m_Socket, fd ) )
	{
		// data is waiting, receive it

		char buffer[1024];
		int c = recv( m_Socket, buffer, 1024, 0 );
		
		if( c > 0 )
		{
			// success! add the received data to the buffer

			m_RecvBuffer += string( buffer, c );
			m_LastRecv = GetTime( );
		}
		else if( c == SOCKET_ERROR && GetLastError( ) != EWOULDBLOCK )
		{
			// receive error

			m_HasError = true;
			m_Error = GetLastError( );
			CONSOLE_Print( "[TCPSOCKET] error (recv) - " + GetErrorString( ) );
			return;
		}
		else if( c == 0 )
		{
			// the other end closed the connection

			CONSOLE_Print( "[TCPSOCKET] closed by remote host" );
			m_Connected = false;
		}
	}
}

void CTCPSocket :: DoSend(  fd_set *fd )
{
	if( m_Socket == INVALID_SOCKET || m_HasError || !m_Connected || m_SendBuffer.empty( ) )
		return;

	if( FD_ISSET( m_Socket, fd ) )
	{
		// socket is ready, send it

		int s = send( m_Socket, m_SendBuffer.c_str( ), (int)m_SendBuffer.size( ), MSG_NOSIGNAL );

		if( s > 0 )
		{
			// success! only some of the data may have been sent, remove it from the buffer

			m_SendBuffer = m_SendBuffer.substr( s );
			m_LastSend = GetTime( );
		}
		else if( s == SOCKET_ERROR && GetLastError( ) != EWOULDBLOCK )
		{
			// send error

			m_HasError = true;
			m_Error = GetLastError( );
			CONSOLE_Print( "[TCPSOCKET] error (send) - " + GetErrorString( ) );
			return;
		}
	}
}

void CTCPSocket :: Disconnect( )
{
	if( m_Socket != INVALID_SOCKET )
		shutdown( m_Socket, SHUT_RDWR );

	m_Connected = false;
}

//
// CTCPClient
//

CTCPClient :: CTCPClient( ) : CTCPSocket( ), m_Connecting( false )
{

}

CTCPClient :: ~CTCPClient( )
{

}

void CTCPClient :: Reset( )
{
	CTCPSocket :: Reset( );
	m_Connecting = false;
}

void CTCPClient :: Disconnect( )
{
	CTCPSocket :: Disconnect( );
	m_Connecting = false;
}

void CTCPClient :: Connect( const string &localaddress, const string &address, uint16_t port )
{
	if( m_Socket == INVALID_SOCKET || m_HasError || m_Connecting || m_Connected )
                return;

        if( !localaddress.empty( ) )
        {
                struct sockaddr_in LocalSIN;
                memset( &LocalSIN, 0, sizeof( LocalSIN ) );
                LocalSIN.sin_family = AF_INET;

                if( ( LocalSIN.sin_addr.s_addr = inet_addr( localaddress.c_str( ) ) ) == INADDR_NONE )
                        LocalSIN.sin_addr.s_addr = INADDR_ANY;

                LocalSIN.sin_port = htons( 0 );
        }

        // get IP address

        struct hostent *HostInfo;
        uint32_t HostAddress;
        HostInfo = gethostbyname( address.c_str( ) );

        if( !HostInfo )
        {
                m_HasError = true;
                // m_Error = h_error;
                CONSOLE_Print( "[TCPCLIENT] error (gethostbyname)" );
                return;
        }

        memcpy( &HostAddress, HostInfo->h_addr, HostInfo->h_length );

        // connect

        m_SIN.sin_family = AF_INET;
        m_SIN.sin_addr.s_addr = HostAddress;
        m_SIN.sin_port = htons( port );

        if( connect( m_Socket, (struct sockaddr *)&m_SIN, sizeof( m_SIN ) ) == SOCKET_ERROR )
        {
                if( GetLastError( ) != EINPROGRESS && GetLastError( ) != EWOULDBLOCK )
                {
                        // connect error

                        m_HasError = true;
                        m_Error = GetLastError( );
                        CONSOLE_Print( "[TCPCLIENT] error (connect) - " + GetErrorString( ) );
                        return;
                }
        }


        m_Connecting = true;
}

bool CTCPClient :: CheckConnect( )
{
	if( m_Socket == INVALID_SOCKET || m_HasError || !m_Connecting )
		return false;

	fd_set fd;
	FD_ZERO( &fd );
	FD_SET( m_Socket, &fd );

	struct timeval tv;
	tv.tv_sec = 0;
	tv.tv_usec = 0;

	// check if the socket is connected

#ifdef WIN32
	if( select( 1, NULL, &fd, NULL, &tv ) == SOCKET_ERROR )
#else
	if( select( m_Socket + 1, NULL, &fd, NULL, &tv ) == SOCKET_ERROR )
#endif
	{
		m_HasError = true;
		m_Error = GetLastError( );
		return false;
	}

	if( FD_ISSET( m_Socket, &fd ) )
	{
		m_Connecting = false;
		m_Connected = true;
		return true;
	}

	return false;
}


