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

   HEAVILY MODIFIED PROJECT BASED ON GHost++: http://forum.codelain.com
   GHOST++ CODE IS PORTED FROM THE ORIGINAL GHOST PROJECT: http://ghost.pwner.org/

*/

#include "ccbot.h"
#include "util.h"
#include "config.h"
#include "language.h"
#include "socket.h"
#include "commandpacket.h"
#include "ccbotdb.h"
#include "bncsutilinterface.h"
#include "bnetprotocol.h"
#include "bnet.h"
#include <stdio.h>
#include <stdlib.h>

/*
#ifdef WIN32
	#include <time.h>
#endif
*/

//
// CBNET
//

CBNET :: CBNET( CCCBot *nCCBot, string nServer, string nCDKeyROC, string nCDKeyTFT, string nCountryAbbrev, string nCountry, string nUserName, string nUserPassword, string nFirstChannel, string nRootAdmin, char nCommandTrigger, unsigned char nWar3Version, BYTEARRAY nEXEVersion, BYTEARRAY nEXEVersionHash, string nPasswordHashType, unsigned char nMaxMessageLength, string nClanTag, bool nGreetUsers, bool nSwearingKick, bool nAnnounceGames, bool nSelfJoin, bool nBanChat, unsigned char nClanDefaultAccess, string nHostbotName, bool nAntiSpam ): m_CCBot( nCCBot ), m_Exiting( false ), m_Server( nServer ), m_ServerAlias( m_Server ), m_CDKeyROC( nCDKeyROC ), m_CDKeyTFT( nCDKeyTFT ), m_CountryAbbrev( nCountryAbbrev ), m_Country( nCountry ), m_UserName( nUserName ), m_UserPassword( nUserPassword ), m_FirstChannel( nFirstChannel ), m_CurrentChannel( nFirstChannel ), m_RootAdmin( nRootAdmin ), m_HostbotName( nHostbotName ), m_War3Version( nWar3Version ), m_EXEVersion( nEXEVersion ), m_EXEVersionHash( nEXEVersionHash ), m_PasswordHashType( nPasswordHashType ), m_Delay( 3001 ),  m_ClanDefaultAccess( nClanDefaultAccess ), m_MaxMessageLength( nMaxMessageLength ), m_NextConnectTime( GetTime( ) ), m_LastNullTime( GetTime( ) ), m_LastGetClanTime( 0 ), m_LastRejoinTime( 0 ), m_RejoinInterval( 15 ), m_LastAnnounceTime( 0 ), m_LastInvitationTime( 0 ), m_LastChatCommandTicks( 0 ), m_LastOutPacketTicks( 0 ), m_LastSpamCacheCleaning( 0 ), m_WaitingToConnect( true ), m_LoggedIn( false ), m_InChat( false ), m_AntiSpam( nAntiSpam ), m_Announce( false ), m_IsLockdown( false ), m_ActiveInvitation( false ), m_ActiveCreation( false ), m_AnnounceGames( nAnnounceGames ), m_BanChat( nBanChat ), m_SwearingKick( nSwearingKick ), m_SelfJoin( nSelfJoin ), m_GreetUsers( nGreetUsers ), m_ClanTag( "Clan " + nClanTag )
{
	// todotodo: append path seperator to Warcraft3Path if needed

	m_Socket = new CTCPClient( );
	m_Protocol = new CBNETProtocol( );
	m_BNCSUtil = new CBNCSUtilInterface( nUserName, nUserPassword );	
 	transform( m_ServerAlias.begin( ), m_ServerAlias.end( ), m_ServerAlias.begin( ), (int(*)(int))tolower );

	if( m_ServerAlias == "server.eurobattle.net" )
		m_ServerAlias = "eurobattle.net";

	// needed only on BNET

	if( m_CDKeyROC.size( ) != 26 )
		CONSOLE_Print( "[BNET: " + m_ServerAlias + "] warning - your ROC CD key is not 26 characters long and is probably invalid" );

	if( !m_CDKeyTFT.empty( ) && m_CDKeyTFT.size( ) != 26 )
		CONSOLE_Print( "[BNET: " + m_ServerAlias + "] warning - your TFT CD key is not 26 characters long and is probably invalid" );

	transform( m_CDKeyROC.begin( ), m_CDKeyROC.end( ), m_CDKeyROC.begin( ), (int(*)(int))toupper );
	transform( m_CDKeyTFT.begin( ), m_CDKeyTFT.end( ), m_CDKeyTFT.begin( ), (int(*)(int))toupper );
	transform( m_RootAdmin.begin( ), m_RootAdmin.end( ), m_RootAdmin.begin( ), (int(*)(int))tolower );
	m_CCBot->m_DB->AccessSet( m_Server, m_RootAdmin, 10 );
	
	m_CommandTrigger = nCommandTrigger;
}

CBNET :: ~CBNET( )
{
	delete m_Socket;
	delete m_Protocol;

	while( !m_Packets.empty( ) )
	{
		delete m_Packets.front( );
		m_Packets.pop( );
	}

	delete m_BNCSUtil;

	for( map<string, CUser *> :: iterator i = m_Channel.begin( ); i != m_Channel.end( ); ++i )
		delete i->second;
}

unsigned int CBNET :: SetFD( void *fd, void *send_fd, int *nfds )
{
	if( !m_Socket->HasError( ) && m_Socket->GetConnected( ) )
	{
		m_Socket->SetFD( (fd_set *)fd, (fd_set *)send_fd, nfds );
		return 1;
	}

	return 0;
}
bool CBNET :: Update( void *fd, void *send_fd )
{
	// we return at the end of each if statement so we don't have to deal with errors related to the order of the if statements
	// that means it might take a few ms longer to complete a task involving multiple steps (in this case, reconnecting) due to blocking or sleeping
	// but it's not a big deal at all, maybe 100ms in the worst possible case (based on a 50ms blocking time)
	
	uint64_t Time = GetTime( ), Ticks = GetTicks( );

	if( m_Socket->GetConnected( ) )
	{
		// the socket is connected and everything appears to be working properly

		m_Socket->DoRecv( (fd_set *)fd );
		ExtractPackets( );
		ProcessPackets( );	
		
		if( !m_ChatCommands.empty( ) && Ticks >= m_LastChatCommandTicks + m_Delay )
		{			
			string ChatCommand = m_ChatCommands.front( );
			m_ChatCommands.pop( );
			m_Delay = 1100 + ChatCommand.length( ) * 40;
			SendChatCommand( ChatCommand, BNET );
			m_LastChatCommandTicks = Ticks;
			m_LastOutPacketTicks = Ticks;
		}
	
		// send a null packet to detect disconnects
		
		if( Time >= m_LastNullTime + 60 )
		{
			m_Socket->PutBytes( m_Protocol->SEND_SID_NULL( ) );
			m_LastNullTime = Time;
		}
		
		if( Time >= m_LastSpamCacheCleaning + 5 && m_AntiSpam )
		{
			m_SpamCache.clear( );
			m_LastSpamCacheCleaning = Time;
		}

		// refresh the clan vector so it gets updated every 70 seconds
		
		if( Time >= m_LastGetClanTime + 70 && m_LoggedIn )
		{
			SendGetClanList( );
			m_LastGetClanTime = Time;
		}

		// part of !Announce
		
		if( ( Time >= m_LastAnnounceTime + m_AnnounceInterval ) && m_Announce  )
		{
			QueueChatCommand( m_AnnounceMsg, BNET );
			m_LastAnnounceTime = Time;
		}

		// rejoining the channel when not in the set channel
		
		if( Time >= m_LastRejoinTime + m_RejoinInterval && m_LoggedIn && m_Rejoin )
		{
			SendChatCommandHidden( "/join " + m_CurrentChannel, BNET );
			m_LastRejoinTime = Time;
		}

		// if we didn't accept a clan invitation send a decline after 29 seconds (to be on the safe side it doesn't cross over 30 seconds )
		
		if( Time >= ( m_LastInvitationTime + 29 ) && m_ActiveInvitation && m_LoggedIn )
		{
			m_Socket->PutBytes( m_Protocol->SEND_SID_CLANINVITATIONRESPONSE( m_Protocol->GetClanTag( ), m_Protocol->GetInviter( ), false ) );
			m_ActiveInvitation = false;
		}

		// wait 5 seconds before accepting the invitation otherwise the clan can get created but you would get an false error
		
		if( Time >= ( m_LastInvitationTime + 5 ) && m_ActiveCreation && m_LoggedIn )
		{
			m_Socket->PutBytes( m_Protocol->SEND_SID_CLANCREATIONINVITATION( m_Protocol->GetClanCreationTag( ), m_Protocol->GetClanCreator( ) ) );
			m_ActiveCreation = false;
		}

		m_Socket->DoSend( (fd_set*)send_fd );
		return m_Exiting;
	}

	if( m_Socket->HasError( ) )
	{
		// the socket has an error

		CONSOLE_Print( "[BNET: " + m_ServerAlias + "] disconnected from battle.net due to socket error" );
		m_BNCSUtil->Reset( m_UserName, m_UserPassword );
		m_Socket->Reset( );
		m_NextConnectTime = Time + 11;
		m_LoggedIn = false;
		m_InChat = false;
		m_WaitingToConnect = true;
		return m_Exiting;
	}

	if( !m_Socket->GetConnecting( ) && !m_Socket->GetConnected( ) && !m_WaitingToConnect )
	{
		// the socket was disconnected

		CONSOLE_Print( "[BNET: " + m_ServerAlias + "] disconnected from battle.net due to socket not connected" );
		m_BNCSUtil->Reset( m_UserName, m_UserPassword );
		m_Socket->Reset( );
		m_NextConnectTime = Time + 11;
		m_LoggedIn = false;
		m_InChat = false;
		m_WaitingToConnect = true;
		return m_Exiting;
	}	

	if( m_Socket->GetConnecting( ) )
	{
		// we are currently attempting to connect to battle.net

		if( m_Socket->CheckConnect( ) )
		{
			// the connection attempt completed

			CONSOLE_Print( "[BNET: " + m_ServerAlias + "] connected" );
			m_Socket->PutBytes( m_Protocol->SEND_PROTOCOL_INITIALIZE_SELECTOR( ) );
			m_Socket->PutBytes( m_Protocol->SEND_SID_AUTH_INFO( m_War3Version, !m_CDKeyTFT.empty( ), m_CountryAbbrev, m_Country ) );
			m_Socket->DoSend( (fd_set*)send_fd );
			m_LastNullTime = Time;
			m_LastChatCommandTicks = Ticks;

			while( !m_OutPackets.empty( ) )
			m_OutPackets.pop( );

			return m_Exiting;
		}
		else if( Time >= m_NextConnectTime + 15 )
		{
			// the connection attempt timed out (11 seconds)

			CONSOLE_Print( "[BNET: " + m_ServerAlias + "] connect timed out" );
			m_Socket->Reset( );
			m_NextConnectTime = Time + 11;
			m_WaitingToConnect = true;
			return m_Exiting;
		}
	}

	if( !m_Socket->GetConnecting( ) && !m_Socket->GetConnected( ) && Time >= m_NextConnectTime )
	{
		// attempt to connect to battle.net

		CONSOLE_Print( "[BNET: " + m_ServerAlias + "] connecting to server [" + m_Server + "] on port 6112" );

		if( m_ServerIP.empty( ) )
		{
			m_Socket->Connect( string( ), m_Server, 6112 );
			
			if( !m_Socket->HasError( ) )
			{
				m_ServerIP = m_Socket->GetIPString( );
				CONSOLE_Print( "[BNET: " + m_ServerAlias + "] resolved and cached server IP address " + m_ServerIP );
			}
        }
		else
        {
			// use cached server IP address since resolving takes time and is blocking

			CONSOLE_Print( "[BNET: " + m_Server + "] using cached server IP address " + m_ServerIP );
			m_Socket->Connect( string( ), m_ServerIP, 6112 );
        }

		m_WaitingToConnect = false;
		return m_Exiting;
	}

	return m_Exiting;
}

inline void CBNET :: ExtractPackets( )
{
	// extract as many packets as possible from the socket's receive buffer and put them in the m_Packets queue

        string *RecvBuffer = m_Socket->GetBytes( );
        BYTEARRAY Bytes = UTIL_CreateByteArray( (unsigned char *)RecvBuffer->c_str( ), RecvBuffer->size( ) );

        // a packet is at least 4 bytes so loop as long as the buffer contains 4 bytes

        while( Bytes.size( ) >= 4 )
        {
                // byte 0 is always 255

                if( Bytes[0] == BNET_HEADER_CONSTANT )
                {
                        // bytes 2 and 3 contain the length of the packet

                        uint16_t Length = UTIL_ByteArrayToUInt16( Bytes, false, 2 );

                        if( Length >= 4 )
                        {
                                if( Bytes.size( ) >= Length )
                                {
                                        m_Packets.push( new CCommandPacket( BNET_HEADER_CONSTANT, Bytes[1], BYTEARRAY( Bytes.begin( ), Bytes.begin( ) + Length ) ) );
                                        *RecvBuffer = RecvBuffer->substr( Length );
                                        Bytes = BYTEARRAY( Bytes.begin( ) + Length, Bytes.end( ) );
                                }
                                else
                                        return;
                        }
                        else
                        {
                                CONSOLE_Print( "[BNET: " + m_ServerAlias + "] error - received invalid packet from battle.net (bad length), disconnecting" );
                                m_Socket->Disconnect( );
                                return;
                        }
                }
                else
                {
                        CONSOLE_Print( "[BNET: " + m_ServerAlias + "] error - received invalid packet from battle.net (bad header constant), disconnecting" );
                        m_Socket->Disconnect( );
                        return;
                }
        }
}

inline void CBNET :: ProcessPackets( )
{
	CIncomingChatEvent *ChatEvent = NULL;

	// process all the received packets in the m_Packets queue
	// this normally means sending some kind of response

	while( !m_Packets.empty( ) )
	{
		CCommandPacket *Packet = m_Packets.front( );
		m_Packets.pop( );

		if( Packet->GetPacketType( ) == BNET_HEADER_CONSTANT )
		{
			switch( Packet->GetID( ) )
			{
			case CBNETProtocol :: SID_NULL:

				// warning: we do not respond to NULL packets with a NULL packet of our own
				// this is because PVPGN servers are programmed to respond to NULL packets so it will create a vicious cycle of useless traffic
				// official battle.net servers do not respond to NULL packets

				m_Protocol->RECEIVE_SID_NULL( Packet->GetData( ) );
				break;

			case CBNETProtocol :: SID_ENTERCHAT:
				if( m_Protocol->RECEIVE_SID_ENTERCHAT( Packet->GetData( ) ) )
				{
					CONSOLE_Print( "[BNET: " + m_ServerAlias + "] joining channel [" + m_FirstChannel + "]" );
					m_InChat = true;
					m_Socket->PutBytes( m_Protocol->SEND_SID_JOINCHANNEL( m_FirstChannel ) );
				}

				break;

			case CBNETProtocol :: SID_CHATEVENT:
				ChatEvent = m_Protocol->RECEIVE_SID_CHATEVENT( Packet->GetData( ) );

				if( ChatEvent )
					ProcessChatEvent( ChatEvent );

				delete ChatEvent;
				ChatEvent = NULL;
				break;
	
			case CBNETProtocol :: SID_FLOODDETECTED:
				if( m_Protocol->RECEIVE_SID_FLOODDETECTED( Packet->GetData( ) ) )
				{
					// we're disced for flooding, exit so we don't prolong the temp IP ban
					m_Exiting = true;
				}
				break;
			
			case CBNETProtocol :: SID_PING:
				m_Socket->PutBytes( m_Protocol->SEND_SID_PING( m_Protocol->RECEIVE_SID_PING( Packet->GetData( ) ) ) );
				break;

			case CBNETProtocol :: SID_AUTH_INFO:
				if( m_Protocol->RECEIVE_SID_AUTH_INFO( Packet->GetData( ) ) )
				{
					if( m_BNCSUtil->HELP_SID_AUTH_CHECK( m_CCBot->m_Warcraft3Path, m_CDKeyROC, m_CDKeyTFT, m_Protocol->GetValueStringFormulaString( ), m_Protocol->GetIX86VerFileNameString( ), m_Protocol->GetClientToken( ), m_Protocol->GetServerToken( ) ) )
					{

						if( m_CDKeyTFT.empty( ) )
							CONSOLE_Print( "[BNET: " + m_ServerAlias + "] attempting to auth as Warcraft III: Reign of Chaos" );
						else
							CONSOLE_Print( "[BNET: " + m_ServerAlias + "] attempting to auth as Warcraft III: The Frozen Throne" );

						// override the exe information generated by bncsutil if specified in the config file
						// apparently this is useful for pvpgn users

						if( m_EXEVersion.size( ) == 4 )
						{
							CONSOLE_Print( "[BNET: " + m_ServerAlias + "] using custom exe version bnet_custom_exeversion = " + UTIL_ToString( m_EXEVersion[0] ) + " " + UTIL_ToString( m_EXEVersion[1] ) + " " + UTIL_ToString( m_EXEVersion[2] ) + " " + UTIL_ToString( m_EXEVersion[3] ) );
							m_BNCSUtil->SetEXEVersion( m_EXEVersion );
						}

						if( m_EXEVersionHash.size( ) == 4 )
						{
							CONSOLE_Print( "[BNET: " + m_ServerAlias + "] using custom exe version hash bnet_custom_exeversionhash = " + UTIL_ToString( m_EXEVersionHash[0] ) + " " + UTIL_ToString( m_EXEVersionHash[1] ) + " " + UTIL_ToString( m_EXEVersionHash[2] ) + " " + UTIL_ToString( m_EXEVersionHash[3] ) );
							m_BNCSUtil->SetEXEVersionHash( m_EXEVersionHash );
						}

						m_Socket->PutBytes( m_Protocol->SEND_SID_AUTH_CHECK( m_Protocol->GetClientToken( ), m_BNCSUtil->GetEXEVersion( ), m_BNCSUtil->GetEXEVersionHash( ), m_BNCSUtil->GetKeyInfoROC( ), m_BNCSUtil->GetKeyInfoTFT( ), m_BNCSUtil->GetEXEInfo( ), "CCBot" ) );
					}
					else
					{
						CONSOLE_Print( "[BNET: " + m_ServerAlias + "] logon failed - bncsutil key hash failed (check your Warcraft 3 path and cd keys), disconnecting" );
						m_Socket->Disconnect( );
						delete Packet;
						return;
					}
				}
				break;
	
			case CBNETProtocol :: SID_AUTH_CHECK:
				if( m_Protocol->RECEIVE_SID_AUTH_CHECK( Packet->GetData( ) ) )
				{
					// cd keys accepted

					m_BNCSUtil->HELP_SID_AUTH_ACCOUNTLOGON( );
					m_Socket->PutBytes( m_Protocol->SEND_SID_AUTH_ACCOUNTLOGON( m_BNCSUtil->GetClientKey( ), m_UserName ) );
				}
				else
				{	
					// cd keys not accepted
					switch( UTIL_ByteArrayToUInt32( m_Protocol->m_KeyState, false ) )
					{
						case 	CBNETProtocol :: KR_OLD_GAME_VERSION:
							CONSOLE_Print( "[BNET: " + m_ServerAlias + "] Old game version - update your hashes and/or ccbot.cfg" );
							break;

						case 	CBNETProtocol :: KR_INVALID_VERSION:
							CONSOLE_Print( "[BNET: " + m_ServerAlias + "] Invalid game version - update your hashes and/or ccbot.cfg" );
							break;

						case 	CBNETProtocol :: KR_INVALID_ROC_KEY:
							CONSOLE_Print( "[BNET: " + m_ServerAlias + "] Invalid RoC key" );
							break;

						case 	CBNETProtocol :: KR_ROC_KEY_IN_USE:
							CONSOLE_Print( "[BNET: " + m_ServerAlias + "] RoC key is being used by someone else" );
							break;
							
						case 	CBNETProtocol :: KR_ROC_KEY_BANNED:
							CONSOLE_Print( "[BNET: " + m_ServerAlias + "] Banned RoC key" );
							break;

						case 	CBNETProtocol :: KR_WRONG_PRODUCT:
							CONSOLE_Print( "[BNET: " + m_ServerAlias + "] Wrong product key" );
							break;

						case 	CBNETProtocol :: KR_INVALID_TFT_KEY:
							CONSOLE_Print( "[BNET: " + m_ServerAlias + "] Invalid TFT key" );
							break;

						case 	CBNETProtocol :: KR_TFT_KEY_BANNED:
							CONSOLE_Print( "[BNET: " + m_ServerAlias + "] Banned TFT key" );
							break;

						case 	CBNETProtocol :: KR_TFT_KEY_IN_USE:
							CONSOLE_Print( "[BNET: " + m_ServerAlias + "] TFT key is being used by someone else" );
							break;
					}				

					m_Socket->Disconnect( );
					delete Packet;
					return;
				}

				break;

			case CBNETProtocol :: SID_AUTH_ACCOUNTLOGON:
				if( m_Protocol->RECEIVE_SID_AUTH_ACCOUNTLOGON( Packet->GetData( ) ) )
				{
					// pvpgn logon
					m_BNCSUtil->HELP_PvPGNPasswordHash( m_UserPassword );
					m_Socket->PutBytes( m_Protocol->SEND_SID_AUTH_ACCOUNTLOGONPROOF( m_BNCSUtil->GetPvPGNPasswordHash( ) ) );					
				}
				else
				{
					CONSOLE_Print( "[BNET: " + m_ServerAlias + "] logon failed - invalid username, disconnecting" );
					m_Socket->Disconnect( );
					delete Packet;
					return;
				}

				break;

			case CBNETProtocol :: SID_AUTH_ACCOUNTLOGONPROOF:
				if( m_Protocol->RECEIVE_SID_AUTH_ACCOUNTLOGONPROOF( Packet->GetData( ) ) )
				{
					// logon successful

					CONSOLE_Print( "[BNET: " + m_ServerAlias + "] logon successful" );
					m_LoggedIn = true;
					m_Socket->PutBytes( m_Protocol->SEND_SID_ENTERCHAT( ) );
					m_Socket->PutBytes( m_Protocol->SEND_SID_CLANMEMBERLIST( ) );

					/* for( vector<CIncomingChannelEvent *> :: iterator i = m_Event.begin( ); i != m_Event.end( ); ++i )
					delete *i; */
					
       				}
				else
				{
					CONSOLE_Print( "[BNET: " + m_ServerAlias + "] logon failed - invalid password, disconnecting" );
					m_Socket->Disconnect( );
					delete Packet;
					return;
				}

				break;

			case CBNETProtocol :: SID_CLANINVITATION:
				switch( m_Protocol->RECEIVE_SID_CLANINVITATION( Packet->GetData( ) ) )
				{
					case 9:
						QueueChatCommand( m_ClanTag + " is currently full, please contact a shaman/chieftain.", BNET );
						break;
								
					case 5:
						QueueChatCommand( "Error: " + m_LastKnown + " is using a Chat client or already in clan.", BNET );
						break;

					case 0:					
						QueueChatCommand( m_LastKnown + " has successfully joined " + m_ClanTag + ".", BNET );
						SendGetClanList( );					
						break;

					case 4:
						QueueChatCommand( m_LastKnown + " has rejected a clan invitation for " + m_ClanTag + ".", BNET );
						break;

					case 8:
						QueueChatCommand( "Error: " + m_LastKnown + " is using a Chat client or already in clan.", BNET );
						break;

					default:
						CONSOLE_Print( "[CLAN: " + m_ServerAlias + "] received unknown SID_CLANINVITATION value [" + UTIL_ToString( m_Protocol->RECEIVE_SID_CLANINVITATION( Packet->GetData( ) ) ) + "]" );
						break;
				}				

			case CBNETProtocol :: SID_CLANINVITATIONRESPONSE:
				if( m_Protocol->RECEIVE_SID_CLANINVITATIONRESPONSE( Packet->GetData( ) ) )
				{
					QueueChatCommand( "Clan invitation received for [" + m_Protocol->GetClanName( ) + "] from [" + m_Protocol->GetInviterStr( ) + "].", BNET );
					QueueChatCommand( "Type " + m_CommandTrigger + "accept to accept the clan invitation.", BNET );

					m_ActiveInvitation = true;
					m_LastInvitationTime = GetTime( );										
				}
				break;

			case CBNETProtocol :: SID_CLANCREATIONINVITATION:
				if( m_Protocol->RECEIVE_SID_CLANCREATIONINVITATION( Packet->GetData( ) ) )
				{
					QueueChatCommand( "Clan creation of [" + m_Protocol->GetClanCreationName( ) + "] by [" + m_Protocol->GetClanCreatorStr( ) + "] received - accepting...", BNET );

					m_ActiveCreation = true;
					m_LastInvitationTime = GetTime( );
				}
				break;

			case CBNETProtocol :: SID_CLANMAKECHIEFTAIN:
				switch( m_Protocol->RECEIVE_SID_CLANMAKECHIEFTAIN( Packet->GetData( ) ) )
				{
					case 0:
						QueueChatCommand( m_LastKnown + " successfuly promoted to chieftain.", BNET );
						SendGetClanList( );
						break;

					default:
						QueueChatCommand( "Error setting user to Chieftain.", BNET );					
				}
				break;

			case CBNETProtocol :: SID_CLANREMOVEMEMBER:
				switch( m_Protocol->RECEIVE_SID_CLANREMOVEMEMBER( Packet->GetData( ) ) )
				{
					case 1:
						QueueChatCommand( "Error: " + m_Removed + " - removal failed by " + m_UsedRemove + ".", BNET );
						m_Removed = "Unknown User";
						break;
			
					case 2:
						QueueChatCommand( "Error: " + m_Removed + " - can't be removed yet from " + m_ClanTag + ".", BNET );
						m_Removed = "Unknown User";
						break;

					case 0:
						QueueChatCommand( m_Removed + " has been kicked from " + m_ClanTag + " by " + m_UsedRemove + ".", BNET );
						m_Removed = "Unknown User";
						SendGetClanList( );
						break;

					case 7:
						QueueChatCommand( "Error: " + m_Removed + " - can't be removed, bot not authorised to remove.", BNET );
						m_Removed = "Unknown User";
						break;

					case 8:
						QueueChatCommand( "Error: " + m_Removed + " - can't be removed, not allowed.", BNET );
						m_Removed = "Unknown User";
                                		break;

					default:
						CONSOLE_Print( "[CLAN: " + m_ServerAlias + "] received unknown SID_CLANREMOVEMEMBER value [" + UTIL_ToString( m_Protocol->RECEIVE_SID_CLANREMOVEMEMBER( Packet->GetData( ) ) ) + "]" );
						break;
				}

			case CBNETProtocol :: SID_CLANMEMBERLIST:
				{
					vector<CIncomingClanList *> Clans = m_Protocol->RECEIVE_SID_CLANMEMBERLIST( Packet->GetData( ) );

					for( vector<CIncomingClanList *> :: iterator i = m_Clans.begin( ); i != m_Clans.end( ); ++i )
						delete *i;

					m_Clans = Clans;
				}			
				break;				
			}
		}

		delete Packet;
	}
}

void CBNET :: SendChatCommand( const string &chatCommand, unsigned char destination )
{
	// don't call this function directly, use QueueChatCommand instead to prevent getting kicked for flooding
	if( m_LoggedIn )
	{

		if( destination == BNET )
		{
			if( chatCommand.size( ) > 200 )					
				m_Socket->PutBytes( m_Protocol->SEND_SID_CHATCOMMAND( chatCommand.substr( 0, 200 ) ) );
			else
				m_Socket->PutBytes( m_Protocol->SEND_SID_CHATCOMMAND( chatCommand ) );

			if( chatCommand.substr( 0, 3 ) == "/w " )
				{
					string chatCommandWhisper = chatCommand.substr( 3, chatCommand.size( ) -3 );
					CONSOLE_Print( "[WHISPER: " + m_ServerAlias + "] to " + chatCommandWhisper.substr( 0, chatCommandWhisper.find_first_of(" ") ) +": " + chatCommandWhisper.substr( chatCommandWhisper.find_first_of(" ")+1 , chatCommandWhisper.size( ) - chatCommandWhisper.find_first_of(" ") - 1 ) );
				}
			else
				CONSOLE_Print( "[LOCAL: " + m_ServerAlias + "] " + chatCommand );
		}
		else if( destination == CONSOLE )
		{
			if( chatCommand.substr( 0, 3 ) == "/w " )
				{
					string chatCommandWhisper = chatCommand.substr( 3, chatCommand.size( ) -3 );
					CONSOLE_Print( "[WHISPER: " + m_ServerAlias + "] to " + chatCommandWhisper.substr( 0, chatCommandWhisper.find_first_of(" ") ) +": " + chatCommandWhisper.substr( chatCommandWhisper.find_first_of(" ")+1 , chatCommandWhisper.size( ) - chatCommandWhisper.find_first_of(" ") - 1 ) );
				}
			else
				CONSOLE_Print( "[LOCAL: " + m_ServerAlias + "] " + chatCommand );
		}
	}

}

void CBNET :: SendChatCommandHidden( const string &chatCommand, unsigned char destination )
{
	// don't call this function directly, use QueueChatCommand instead to prevent getting kicked for flooding
	if( m_LoggedIn )
	{
		if( destination == BNET )
		{
			if( chatCommand.size( ) > 200 )
				m_Socket->PutBytes( m_Protocol->SEND_SID_CHATCOMMAND( chatCommand.substr( 0, 200 ) ) );
			else				
				m_Socket->PutBytes( m_Protocol->SEND_SID_CHATCOMMAND( chatCommand ) );
		}
	}
}

void CBNET :: QueueChatCommand( const string &chatCommand, unsigned char destination )
{
	if( chatCommand.empty( ) )
		return;
	
	if( destination == BNET )
		m_ChatCommands.push( chatCommand );
}

void CBNET :: QueueChatCommand( const string &chatCommand, string user, bool whisper, unsigned char destination )
{
	if( chatCommand.empty( ) )
		return;

	// if whisper is true send the chat command as a whisper to user, otherwise just queue the chat command

	if( whisper && destination == BNET )
		QueueChatCommand( "/w " + user + " " + chatCommand, destination );
	else if( destination == BNET )
		QueueChatCommand( chatCommand, destination );
	else if ( whisper && destination == CONSOLE )
	{
		string chatCommandWhisper = chatCommand.substr( 3, chatCommand.size( ) -3 );
		CONSOLE_Print( "[WHISPER: " + m_ServerAlias + "] to " + chatCommandWhisper.substr( 0, chatCommandWhisper.find_first_of(" ") ) +": " + chatCommandWhisper.substr( chatCommandWhisper.find_first_of(" ")+1 , chatCommandWhisper.size( ) - chatCommandWhisper.find_first_of(" ") - 1 ) );
	}
	else if ( destination == CONSOLE )
		CONSOLE_Print( "[LOCAL: " + m_ServerAlias + "] " + chatCommand );
}

void CBNET :: QueueWhisperCommand( const string &chatCommand, string user, unsigned char destination )
{
	if( chatCommand.empty( ) )
		return;

	if( destination == BNET )
		m_ChatCommands.push( "/w " + user + " " + chatCommand );
	else if( destination == CONSOLE )
	{
		string chatCommandWhisper = chatCommand.substr( 3, chatCommand.size( ) -3 );
		CONSOLE_Print( "[WHISPER: " + m_ServerAlias + "] to " + chatCommandWhisper.substr( 0, chatCommandWhisper.find_first_of(" ") ) +": " + chatCommandWhisper.substr( chatCommandWhisper.find_first_of(" ")+1 , chatCommandWhisper.size( ) - chatCommandWhisper.find_first_of(" ") - 1 ) );
	}
}

void CBNET :: ImmediateChatCommand( const string &chatCommand, unsigned char destination )
{
	if( chatCommand.empty( ) )
		return;

	if( GetTicks( ) >= m_LastChatCommandTicks + 1000 )
	{
		SendChatCommand( chatCommand, destination );
		m_LastChatCommandTicks = GetTicks( );
	}	
}

void CBNET :: ImmediateChatCommand( const string &chatCommand, string user, bool whisper, unsigned char destination )
{
	if( chatCommand.empty( ) )
		return;

	if( whisper && destination == BNET )
		ImmediateChatCommand( "/w " + user + " " + chatCommand, destination );
	else if( destination == BNET )
		ImmediateChatCommand( chatCommand, destination );
	else if ( whisper && destination == CONSOLE )
	{
		string chatCommandWhisper = chatCommand.substr( 3, chatCommand.size( ) -3 );
		CONSOLE_Print( "[WHISPER: " + m_ServerAlias + "] to " + chatCommandWhisper.substr( 0, chatCommandWhisper.find_first_of(" ") ) +": " + chatCommandWhisper.substr( chatCommandWhisper.find_first_of(" ")+1 , chatCommandWhisper.size( ) - chatCommandWhisper.find_first_of(" ") - 1 ) );
	}
	else if ( destination == CONSOLE )
		CONSOLE_Print( "[LOCAL: " + m_ServerAlias + "] " + chatCommand );
}

bool CBNET :: IsRootAdmin( string name )
{
	// m_RootAdmin was already transformed to lower case in the constructor

	transform( name.begin( ), name.end( ), name.begin( ), (int(*)(int))tolower );
	return name == m_RootAdmin;
}

bool CBNET :: IsClanPeon( string name )
{
	for( vector<CIncomingClanList *> :: iterator i = m_Clans.begin( ); i != m_Clans.end( ); ++i )
		if( Match( (*i)->GetName( ), name ) && ( (*i)->GetRank( ) == "Peon" || (*i)->GetRank( ) == "Recruit" ) )
			return true;

	return false;
}

bool CBNET :: IsClanGrunt( string name )
{
	for( vector<CIncomingClanList *> :: iterator i = m_Clans.begin( ); i != m_Clans.end( ); ++i )
		if( Match( (*i)->GetName( ), name ) && (*i)->GetRank( ) == "Grunt" )
			return true;

	return false;
}

bool CBNET :: IsClanShaman( string name )
{
	for( vector<CIncomingClanList *> :: iterator i = m_Clans.begin( ); i != m_Clans.end( ); ++i )
		if( Match( (*i)->GetName( ), name ) && (*i)->GetRank( ) == "Shaman" )
			return true;

	return false;
}

bool CBNET :: IsClanChieftain( string name )
{

	for( vector<CIncomingClanList *> :: iterator i = m_Clans.begin( ); i != m_Clans.end( ); ++i )
		if( Match( (*i)->GetName( ), name ) && (*i)->GetRank( ) == "Chieftain" )
			return true;

	return false;
}

bool CBNET :: IsClanMember( string name )
{
	for( vector<CIncomingClanList *> :: iterator i = m_Clans.begin( ); i != m_Clans.end( ); ++i )
		if( Match( (*i)->GetName( ), name ) )
			return true;
		
	return false;
}

vector<string> :: iterator CBNET :: GetSquelched( string name )
{
	for( vector<string> :: iterator i = m_Squelched.begin( ); i != m_Squelched.end( ); ++i )	
		if( Match( name, *i ) )
			return i;

	return m_Squelched.end( );
}

bool CBNET :: IsInChannel( string name )
{
	transform( name.begin( ), name.end( ), name.begin( ), (int(*)(int))tolower );

	for( map<string,  CUser *> :: iterator i = m_Channel.begin( ); i != m_Channel.end( ); ++i )
		if( (*i).first == name )
			return true;

	return false;
}

vector<string> :: iterator CBNET :: GetLockdown( string name )
{
	for( vector<string> :: iterator i = m_Lockdown.begin( ); i != m_Lockdown.end( ); ++i )
		if( Match( name, (*i) ) )	
			return i;

	return m_Lockdown.end( );	
}

void CBNET :: SendClanChangeRank( string accountName, CBNETProtocol :: RankCode rank )
{
	if( m_LoggedIn )
		m_Socket->PutBytes( m_Protocol->SEND_SID_CLANCHANGERANK( accountName, rank ) );
}

void CBNET :: SendGetClanList( )
{
	if( m_LoggedIn )
		m_Socket->PutBytes( m_Protocol->SEND_SID_CLANMEMBERLIST( ) );
}

string CBNET :: GetUserFromNamePartial( string name )
{
	int Matches = 0;
	string User;
	transform( name.begin( ), name.end( ), name.begin( ), (int(*)(int))tolower );	

	// try to match each username with the passed string (e.g. "Varlock" would be matched with "lock")

	for( map<string,  CUser *> :: iterator i = m_Channel.begin( ); i != m_Channel.end( ); ++i )
	{
		if( (*i).first.find( name ) != string :: npos )
		{
			++Matches;
			User = (*i).first;
		
			if( User == name )
				return User;
		}
	}

	if( Matches == 1 )
		return User;

	return string( );
}


//
// CUser
//

CUser :: CUser( const string &nUser, int nPing, uint32_t nUserflags ) : m_User ( nUser ), m_Ping( nPing ), m_UserFlags( nUserflags )
{

}

CUser :: ~CUser( )
{

}

CUser* CBNET :: GetUserByName( string name )
{
	transform( name.begin( ), name.end( ), name.begin( ), (int(*)(int))tolower );

	for( map<string, CUser *> :: iterator i = m_Channel.begin( ); i != m_Channel.end( ); ++i )
	{
		if( name == (*i).first )
			return i->second;
	}

	return NULL;
}
