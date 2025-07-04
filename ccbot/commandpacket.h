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

#ifndef COMMANDPACKET_H
#define COMMANDPACKET_H

//
// CCommandPacket
//

class CCommandPacket
{
private:
	unsigned char m_PacketType;
	int m_ID;
	BYTEARRAY m_Data;

public:
	CCommandPacket( unsigned char nPacketType, int nID, const BYTEARRAY &nData );
	~CCommandPacket( );

	unsigned char GetPacketType( )	{ return m_PacketType; }
	int GetID( )			{ return m_ID; }
	BYTEARRAY GetData( )		{ return m_Data; }
};

#endif
