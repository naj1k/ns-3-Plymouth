/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright (c) 2005, 2009 INRIA
 * Copyright (c) 2009 MIRKO BANCHI
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation;
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * Authors: Mathieu Lacage <mathieu.lacage@sophia.inria.fr>
 *          Mirko Banchi <mk.banchi@gmail.com>
 */

#include "ns3/simulator.h"
#include "ns3/packet.h"
#include "ns3/uinteger.h"
#include "wifi-mac-queue.h"
#include "qos-blocked-destinations.h"

//Added by Najwan
#include "ns3/udp-header.h"		//To be used in GetViContents
#include "ns3/seq-ts-header.h"	//To be used in GetViContents
#include "ns3/frame-type-tag.h"	//To be used in GetViContents
#include <fstream> 				//To use with std::ofstream
#include "ns3/ipv4-header.h"	//needed for identifying source and destination IP in CheckPacketInfo()
#include "ns3/seq-ts-header.h"  //needed for identifying packet sequence in CheckPacketInfo()
#include "ns3/udp-header.h"		//needed for identifying source port in CheckPacketInfo()
#include "ns3/evalvid-server.h" //to use in conjunction with EvalvidServer pointer
#include "qos-tag.h"			//needed for identifying qos tag in CheckPacketInfo()
#include "ns3/node-id-tag.h"	//needed for identifying node id tag in CheckPacketInfo()
#include "ns3/frame-type-tag.h" //needed for identifying frame type tag in CheckPacketInfo()
#include "ns3/app-id-tag.h"		//needed for identifying application id tag in CheckPacketInfo()
#include "ns3/dev-id-tag.h"		//needed for identifying device id tag in CheckPacketInfo()
	
//#include "ns3/node-list.h"		//to use in conjuction with the NodeList
#include <math.h>
namespace ns3 {

NS_OBJECT_ENSURE_REGISTERED (WifiMacQueue);

WifiMacQueue::Item::Item (Ptr<const Packet> packet,
                          const WifiMacHeader &hdr,
                          Time tstamp)
  : packet (packet),
    hdr (hdr),
    tstamp (tstamp)
{
}

TypeId
WifiMacQueue::GetTypeId (void)
{
  static TypeId tid = TypeId ("ns3::WifiMacQueue")
    .SetParent<Object> ()
    .SetGroupName ("Wifi")
    .AddConstructor<WifiMacQueue> ()
    .AddAttribute ("MaxPacketNumber", "If a packet arrives when there are already this number of packets, it is dropped.",
                   UintegerValue (400),
                   MakeUintegerAccessor (&WifiMacQueue::m_maxSize),
                   MakeUintegerChecker<uint32_t> ())
    .AddAttribute ("MaxDelay", "If a packet stays longer than this delay in the queue, it is dropped.",
                   TimeValue (MilliSeconds (500.0)),
                   MakeTimeAccessor (&WifiMacQueue::m_maxDelay),
                   MakeTimeChecker ())
   //--------------------------------------------------------------------------------------------------------
   // Added by Rafael Araujo in Dec 2015 to monitor when MAC Queue is Full and Drop
   //--------------------------------------------------------------------------------------------------------
    .AddTraceSource ("FullMacQueueDrop",
					"Trace source indicating a packet "
					"was refused in MAC Queue because max size was reached",
					MakeTraceSourceAccessor (&WifiMacQueue::m_fullQueueTrace),
					"ns3::Packet::TracedCallback")
    .AddTraceSource ("PacketTimeoutDrop",
					"Trace source indicating a packet "
					"was removed from MAC Queue because timeout was reached",
					MakeTraceSourceAccessor (&WifiMacQueue::m_packetTimeoutTrace),
					"ns3::Packet::TracedCallback")
	//--------------------------------------------------------------------------------------------------------
  ;
  return tid;
}

WifiMacQueue::WifiMacQueue ()
  : m_size (0)
{
}

WifiMacQueue::~WifiMacQueue ()
{
  Flush ();
}

void
WifiMacQueue::SetMaxSize (uint32_t maxSize)
{
  m_maxSize = maxSize;
}

void
WifiMacQueue::SetMaxDelay (Time delay)
{
  m_maxDelay = delay;
}

uint32_t
WifiMacQueue::GetMaxSize (void) const
{
  return m_maxSize;
}

Time
WifiMacQueue::GetMaxDelay (void) const
{
  return m_maxDelay;
}

void
WifiMacQueue::Enqueue (Ptr<const Packet> packet, const WifiMacHeader &hdr)
{      
	
  Cleanup ();
  
  //// ===============================================================================
  //// This section is to test the algorithm by dropping a sequence of packet manually
  //// ===============================================================================
  
  //Ptr<Packet> q = packet->Copy();
  //q->EnablePrinting();
	
  //CheckPacketInfo (q);
  	
  //uint32_t ApplicationId = GetAppId();
  //std::string FrameTypeString = GetFrameTypeString();
  
  //double testTime = Simulator::Now().GetSeconds();
  
  //if ((ApplicationId==0) && (testTime >25) && (testTime<35) && ((FrameTypeString=="I") || (FrameTypeString=="P") || (FrameTypeString=="B")) ){
	  //std::cout << "testTime: " << testTime << "\tAppId: " << ApplicationId << std::endl;
	  //Log_ManualDrop(packet);
	  //return;
  //}
  
  //// =======================================================================
  
  
  if (m_size == m_maxSize) {
	  //NK: Log the info of the dropped packet due to "queue is full" in a text file
	  //Log_QueueFull(packet);
	  Log_PacketDrop(packet, "QF");
  
	  //--------------------------------------------------------------------------------------------------------
      // Added by Rafael Araujo in Dec 2015 to monitor when MAC Queue is Full and Drop
      //--------------------------------------------------------------------------------------------------------
      m_fullQueueTrace(packet);
      //--------------------------------------------------------------------------------------------------------
      		
      return;
  }
  
  Time now = Simulator::Now ();
  m_queue.push_back (Item (packet, hdr, now));
  m_size++;
}

void
WifiMacQueue::Cleanup (void) {
	if (m_queue.empty ()){
		return;
    }

	Time now = Simulator::Now ();
	uint32_t n = 0;
	
	for (PacketQueueI i = m_queue.begin (); i != m_queue.end (); ) {
		
		if (i->tstamp + m_maxDelay > now) {
			i++;
        } else {
			//--------------------------------------------------------------------------------------------------------
			// Added by Rafael Araujo in Dec 2015 to monitor when MAC Queue is Full and Drop
			//--------------------------------------------------------------------------------------------------------
			Ptr<const Packet> packet = i->packet;
			m_packetTimeoutTrace(packet);
			std::cout << "wifi-mac-queue: TimeOut Reached" << std::endl;
			std::cout << "tstamp: " << i->tstamp << "\tmaxDelay: " << + m_maxDelay << "\tNow: " << now << std::endl;
			//--------------------------------------------------------------------------------------------------------		
			
			//NK: Log the info of the dropped packet due to "time out reached" is full in a text file
			//Log_TimeOutReached(packet);
			Log_PacketDrop(packet, "TO");
			
			i = m_queue.erase (i);
			n++;
        }
    }
  m_size -= n;
}

Ptr<const Packet>
WifiMacQueue::Dequeue (WifiMacHeader *hdr){
  Cleanup ();
  if (!m_queue.empty ())
    {
      Item i = m_queue.front ();
      m_queue.pop_front ();
      m_size--;
      *hdr = i.hdr;
      return i.packet;
    }
  return 0;
}

Ptr<const Packet>
WifiMacQueue::Peek (WifiMacHeader *hdr){
  Cleanup ();
  if (!m_queue.empty ())
    {
      Item i = m_queue.front ();
      *hdr = i.hdr;
      return i.packet;
    }
  return 0;
}

Ptr<const Packet>
WifiMacQueue::DequeueByTidAndAddress (WifiMacHeader *hdr, uint8_t tid, WifiMacHeader::AddressType type, Mac48Address dest){
  Cleanup ();
  Ptr<const Packet> packet = 0;
  if (!m_queue.empty ())
    {
      PacketQueueI it;
      for (it = m_queue.begin (); it != m_queue.end (); ++it)
        {
          if (it->hdr.IsQosData ())
            {
              if (GetAddressForPacket (type, it) == dest
                  && it->hdr.GetQosTid () == tid)
                {
                  packet = it->packet;
                  *hdr = it->hdr;
                  m_queue.erase (it);
                  m_size--;
                  break;
                }
            }
        }
    }
  return packet;
}

Ptr<const Packet>
WifiMacQueue::PeekByTidAndAddress (WifiMacHeader *hdr, uint8_t tid, WifiMacHeader::AddressType type, Mac48Address dest, Time *timestamp){
  Cleanup ();
  if (!m_queue.empty ())
    {
      PacketQueueI it;
      for (it = m_queue.begin (); it != m_queue.end (); ++it)
        {
          if (it->hdr.IsQosData ())
            {
              if (GetAddressForPacket (type, it) == dest
                  && it->hdr.GetQosTid () == tid)
                {
                  *hdr = it->hdr;
                  *timestamp = it->tstamp;
                  return it->packet;
                }
            }
        }
    }
  return 0;
}

bool
WifiMacQueue::IsEmpty (void){
  Cleanup ();
  return m_queue.empty ();
}

uint32_t
WifiMacQueue::GetSize (void){
  Cleanup ();
  return m_size;
}

void
WifiMacQueue::Flush (void){
  m_queue.erase (m_queue.begin (), m_queue.end ());
  m_size = 0;
}

Mac48Address
WifiMacQueue::GetAddressForPacket (enum WifiMacHeader::AddressType type, PacketQueueI it) {
  if (type == WifiMacHeader::ADDR1)
    {
      return it->hdr.GetAddr1 ();
    }
  if (type == WifiMacHeader::ADDR2)
    {
      return it->hdr.GetAddr2 ();
    }
  if (type == WifiMacHeader::ADDR3)
    {
      return it->hdr.GetAddr3 ();
    }
  return 0;
}

bool
WifiMacQueue::Remove (Ptr<const Packet> packet)
{
  PacketQueueI it = m_queue.begin ();
  for (; it != m_queue.end (); it++)
    {
      if (it->packet == packet)
        {
          m_queue.erase (it);
          m_size--;
          return true;
        }
    }
  return false;
}

void
WifiMacQueue::PushFront (Ptr<const Packet> packet, const WifiMacHeader &hdr)
{
  Cleanup ();
  if (m_size == m_maxSize)
    {
      return;
    }
  Time now = Simulator::Now ();
  m_queue.push_front (Item (packet, hdr, now));
  m_size++;
}

uint32_t
WifiMacQueue::GetNPacketsByTidAndAddress (uint8_t tid, WifiMacHeader::AddressType type, Mac48Address addr) {
  Cleanup ();
  uint32_t nPackets = 0;
  if (!m_queue.empty ())
    {
      PacketQueueI it;
      for (it = m_queue.begin (); it != m_queue.end (); it++)
        {
          if (GetAddressForPacket (type, it) == addr)
            {
              if (it->hdr.IsQosData () && it->hdr.GetQosTid () == tid)
                {
                  nPackets++;
                }
            }
        }
    }
  return nPackets;
}

Ptr<const Packet>
WifiMacQueue::DequeueFirstAvailable (WifiMacHeader *hdr, Time &timestamp, const QosBlockedDestinations *blockedPackets) {
  Cleanup ();
  Ptr<const Packet> packet = 0;
  for (PacketQueueI it = m_queue.begin (); it != m_queue.end (); it++)
    {
      if (!it->hdr.IsQosData ()
          || !blockedPackets->IsBlocked (it->hdr.GetAddr1 (), it->hdr.GetQosTid ()))
        {
          *hdr = it->hdr;
          timestamp = it->tstamp;
          packet = it->packet;
          m_queue.erase (it);
          m_size--;
          return packet;
        }
    }
  return packet;
}

Ptr<const Packet>
WifiMacQueue::PeekFirstAvailable (WifiMacHeader *hdr, Time &timestamp, const QosBlockedDestinations *blockedPackets) {
  Cleanup ();
  for (PacketQueueI it = m_queue.begin (); it != m_queue.end (); it++)
    {
      if (!it->hdr.IsQosData ()
          || !blockedPackets->IsBlocked (it->hdr.GetAddr1 (), it->hdr.GetQosTid ()))
        {
          *hdr = it->hdr;
          timestamp = it->tstamp;
          return it->packet;
        }
    }
  return 0;
}

//Added by Najwan [-START-]

bool
//WifiMacQueue::IBoost (Ptr<const Packet> packet, const WifiMacHeader &hdr)
WifiMacQueue::IBoost ()
{
	Cleanup ();
  
	if (!m_queue.empty()){
		
		PacketQueueI it = m_queue.begin ();
		
		if (it == m_queue.end ()){
			std::cout << "it == m_queue.end ()" << std::endl;
			return false;
		}
	
		for (; it != m_queue.end (); it++){
						
			Time now = Simulator::Now ();
			
			Ptr<Packet> q = it->packet->Copy();		
			q->EnablePrinting();
			
			CheckPacketInfo(q);
			
			std::string FrameType = GetFrameTypeString();
			uint32_t SeqId = GetSeqId();
			
			//if (FrameType == "I"){
			if ((FrameType == "I") && ((it->tstamp + m_maxDelay - now <=200000000))){
							
				std::cout << FrameType << ": IBoost!" << std::endl;
				
				Ptr<const Packet> packet = it->packet;
				WifiMacHeader wmhdr = it->hdr;
				//Time now = Simulator::Now ();
				
				m_queue.erase (it);
				m_queue.push_front (Item (packet, wmhdr, now));
				
				std::cout << "Boosted: |" << FrameType << ":" << SeqId << "|" << std::endl;
				
				return true;
	
			}
		}
	} return false;
}


bool
//WifiMacQueue::IBoost (Ptr<const Packet> packet, const WifiMacHeader &hdr)
WifiMacQueue::IBoostByAppId (uint32_t SrcAppId)
{
	Cleanup ();
  
	if (!m_queue.empty()){
		
		PacketQueueI it = m_queue.begin ();
		
		if (it == m_queue.end ()){
			std::cout << "it == m_queue.end ()" << std::endl;
			return false;
		}
	
		for (; it != m_queue.end (); it++){
						
			Time now = Simulator::Now ();
			
			Ptr<Packet> q = it->packet->Copy();		
			q->EnablePrinting();
			
			CheckPacketInfo(q);
			
			std::string FrameType = GetFrameTypeString();
			uint32_t SeqId = GetSeqId();
			uint32_t AppId = GetAppId();
			
			//if (FrameType == "I"){
			if ((FrameType == "I") && (AppId==SrcAppId) && ((it->tstamp + m_maxDelay - now <=200000000))){
							
				std::cout << FrameType << ": IBoost!" << std::endl;
				
				Ptr<const Packet> packet = it->packet;
				WifiMacHeader wmhdr = it->hdr;
				//Time now = Simulator::Now ();
				
				m_queue.erase (it);
				m_queue.push_front (Item (packet, wmhdr, now));
				
				std::cout << "Boosted: |" << FrameType << ":" << SeqId << "|" << std::endl;
				
				return true;
	
			}
		}
	} return false;
}







std::string
WifiMacQueue::GetViContents (uint8_t tid, WifiMacHeader::AddressType type, Mac48Address addr) { 
  //std::cout << Simulator::Now().GetSeconds()  << "\tCurrently in queue:\t";
  Cleanup ();
  uint32_t nPackets = 0, SeqId, SourcePort;
  std::string content = "";
  uint32_t AppId;
  std::string StrSourcePort = "";
  std::string StrPacketSeqId = "";
  std::string StrAppId = "";
  
  if (!m_queue.empty ()){
	  
      PacketQueueI it;
      
      for (it = m_queue.begin (); it != m_queue.end (); it++){
          
		if (it->hdr.IsQosData () && it->hdr.GetQosTid () == tid){
			
			//Get packet info (FrameType, SeqId and AppId)
			Ptr<Packet> q = it->packet->Copy();
			q->EnablePrinting();
		
			CheckPacketInfo (q);
	
			SeqId = GetSeqId();
			SourcePort = GetSourcePort();
			AppId = GetAppId();
			std::string FrameType = GetFrameTypeString();

			//Send SeqId to ConvertInt() to convert it to ::string
			StrPacketSeqId	= WifiMacQueue::ConvertInt(SeqId);
			StrSourcePort	= WifiMacQueue::ConvertInt(SourcePort);
			StrAppId		= WifiMacQueue::ConvertInt(AppId);
			
			content = "|" + FrameType + ":" + StrPacketSeqId + ":" + StrAppId + content;
 			
            nPackets++;
        }
          
      }
  }
  content = "--> " + content + "| -->";
  return content;
}

std::string WifiMacQueue::ConvertInt(uint64_t number)
{
   std::stringstream ss;	//create a stringstream
   ss << number;			//add number to the stream
   return ss.str();			//return a string with the contents of the stream
}

bool
WifiMacQueue::RemoveBFrameByPort(uint32_t port, uint32_t seqid){
	
	std::cout << "WifiMacQueue::RemoveBFrameByPort" << std::endl;
	uint32_t SourcePort, SeqId;
	
	PacketQueueI it = m_queue.begin ();
	
	for (; it != m_queue.end (); it++){
		
		//Get packet port in queue
		//========================
		Ptr<Packet> q = it->packet->Copy();
		q->EnablePrinting();
		
		PacketMetadata::ItemIterator metadataIterator = q->BeginItem();
		PacketMetadata::Item item;			
		
		while (metadataIterator.HasNext()){
		  
			item = metadataIterator.Next();
		
			//Check UdpHeader for source port
			//===============================
			if(item.tid.GetName()=="ns3::UdpHeader"){
			
				Callback<ObjectBase *> constr = item.tid.GetConstructor();
				NS_ASSERT(!constr.IsNull());
				
				// Ptr<> and DynamicCast<> won't work here as all headers are from ObjectBase, not Object
				ObjectBase *instance = constr();
				NS_ASSERT(instance != 0);
				
				UdpHeader* udpHeader = dynamic_cast<UdpHeader*> (instance);
				NS_ASSERT(udpHeader != 0);
				
				udpHeader->Deserialize(item.current);
				
				// The source port can now obtain the source of the packet
				SourcePort = udpHeader->GetSourcePort();
				//std::cout << "SourcePort: " << SourcePort << std::endl;
							
				delete udpHeader;

			}
			
			//Check SeqTsHeader for SeqId
			//===========================
			if(item.tid.GetName()=="ns3::SeqTsHeader"){
			
				Callback<ObjectBase *> constr = item.tid.GetConstructor();
				NS_ASSERT(!constr.IsNull());
				
				// Ptr<> and DynamicCast<> won't work here as all headers are from ObjectBase, not Object
				ObjectBase *instance = constr();
				NS_ASSERT(instance != 0);
				
				SeqTsHeader* seqTsHeader = dynamic_cast<SeqTsHeader*> (instance);
				NS_ASSERT(seqTsHeader != 0);
				
				seqTsHeader->Deserialize(item.current);
				
				// The tcp sequence can now obtain the source of the packet
				SeqId = seqTsHeader->GetSeq();
				//std::cout << "SeqId: " << SeqId << std::endl;
				
				delete seqTsHeader;
				
				break;
			}
		}
		
		//Check Frametype Tag for frametype
		//=================================
		FrameTypeTag frameTag;
		uint32_t FrameTypeValue;
		
		if (it->packet->PeekPacketTag(frameTag)){
			
			FrameTypeValue = frameTag.GetFrameType();
			//std::cout << "FrameTypeValue: " << FrameTypeValue << std::endl;
		}
		
		//Remove that packet if frametypevalue = 3 && SourcePort = port
		if (FrameTypeValue == 3 && SourcePort==port){	//If frametypevalue==3 (B-Frame)
			
			m_queue.erase (it);
			m_size--;
			
			std::cout << "Removed |B:" << SeqId << ":" << SourcePort << "|" << std::endl;
			
			std::ofstream RemovedBFrameFromQueue;
			RemovedBFrameFromQueue.open ("RemovedBFrameFromQueue.out",std::ios_base::app);
			RemovedBFrameFromQueue << Simulator::Now().GetSeconds() << "Removed |B:" << SeqId << ":" << SourcePort << "|" << "\tReason: RemoveOwnBFrameForOwnIFrameIfViQueueFull" << "\n";
			RemovedBFrameFromQueue.close();
			
			return true;
			
		} //std::cout << "Ola!" << std::endl;
	}
	return false;
}


bool
WifiMacQueue::RemoveBFrameByAppId(uint32_t AppId){
	
	std::cout << "WifiMacQueue::RemoveBFrameByAppId" << std::endl;
		
	PacketQueueI it = m_queue.begin ();
	
	if (it == m_queue.end ()){
		std::cout << "it == m_queue.end ()" << std::endl;
		return false;
	}
	
	for (; it != m_queue.end (); it++){
		
		Ptr<Packet> q = it->packet->Copy();
		q->EnablePrinting();
	
		CheckPacketInfo (q);

		uint32_t SeqId = GetSeqId();
		int FrameTypeValue = GetFrameTypeValue();
		uint32_t ApplicationId = GetAppId();
				
		//Remove that packet if frametypevalue = 3 && AppId = ApplicationId
		if (FrameTypeValue == 3 && AppId==ApplicationId){	//If frametypevalue==3 (B-Frame)
			
			//Log Deleted packet
			//Log_ROPB(it->packet);
			Log_PacketDrop(it->packet, "ROPB");
			
			m_queue.erase (it);
			m_size--;
			
			std::cout << "Removed |B:" << SeqId << ":" << ApplicationId << "|" << std::endl;
			
			std::ofstream ROPB;
			ROPB.open ("ROPB.out",std::ios_base::app);
			ROPB << Simulator::Now().GetSeconds() << "Removed |B:" << SeqId << ":" << ApplicationId << "|" << "\tReason: ROPB" << "\n";
			ROPB.close();
						
			return true;
		}
	}
	return false;
}

bool
WifiMacQueue::RemoveAnyBFrame(){
	
	std::cout << "WifiMacQueue::RemoveAnyBFrame" << std::endl;
	
	PacketQueueI it = m_queue.begin ();
	
	if (it == m_queue.end ()){
		std::cout << "it == m_queue.end ()" << std::endl;
		return false;
	}
	
	for (; it != m_queue.end (); it++){
		
		Ptr<Packet> q = it->packet->Copy();
		q->EnablePrinting();
	
		//Get packet SeqId, FrameType and AppId
		CheckPacketInfo (q);
	
		uint32_t SeqId = GetSeqId();
		int FrameTypeValue = GetFrameTypeValue();
		uint32_t ApplicationId = GetAppId();	

		if (FrameTypeValue == 3){	//If frametypevalue==3 (B-Frame)
			
			//Log Deleted packet
			//Log_RAPB(it->packet);
			Log_PacketDrop(it->packet, "RAPB");
			
			m_queue.erase (it);
			m_size--;
			
			std::cout << "Removed |B:" << SeqId << ":" << ApplicationId << "|" << std::endl;
			
			std::ofstream RAPB;
			RAPB.open ("RAPB.out",std::ios_base::app);
			RAPB << Simulator::Now().GetSeconds() << "\tRemoved |B:" << SeqId << ":" << ApplicationId << "|" << "\tReason: RAPB" << "\n";
			RAPB.close();
			
			return true;
	    } 
	}
	return false;
}

//This function is to log all packets that had been dropped and the reasons, and as the main reference for PSNR prediction to check the number of packets that are dropped.
void
WifiMacQueue::Log_PacketDrop(Ptr<const Packet> packet, std::string reason){
	std::cout << "Log_PacketDrop" << std::endl;
	
	Ptr<Packet> packet_copy = packet->Copy();	//Copy packet to be sent to CheckPacketInfo()
	CheckPacketInfo(packet_copy);				//Send packet_copy to CheckPacketInfo()
	
	Ipv4Address SourceIP = GetSourceIP();
	Ipv4Address DestinationIP = GetDestinationIP();
	uint32_t SourcePort = GetSourcePort();
	uint32_t SeqId = GetSeqId();
	std::string FrameTypeString = GetFrameTypeString();
	uint8_t Tid = GetQosTid();
	uint32_t AppId = GetAppId();
	
	// All packets that has been dropped is recorded in og_PacketDrop.out
	// ==================================================================
	// Format of the file is as follows:
	// Column 1:  Time
	// Column 2:  SourceIP
	// Column 3:  DestinationIP
	// Column 4:  SourcePort
	// Column 5:  AppId
	// Column 6:  FrameType
	// Column 7:  Tid
	// Column 8:  SeqId
	// Column 9:  Reason of being dropped (QF - Queue Full, TO - Timeout, RAPB, ROPB)
	
	std::ofstream Log_PacketDrop;
	Log_PacketDrop.open ("Log_PacketDrop.out",std::ios_base::app);
	Log_PacketDrop << Simulator::Now().GetSeconds() << "\t"
				   << SourceIP << "\t"
				   << DestinationIP << "\t"
				   << SourcePort << "\t"
				   << AppId     << "\t"
				   << FrameTypeString << "\t"
				   << (int)Tid << "\t"
				   << SeqId  << "\t"
				   << reason << "\n";
	Log_PacketDrop.close();
}


void
WifiMacQueue::CheckPacketInfo (Ptr<Packet> packet_copy){
	/* Check packet info based on metadata
	 * 1. SourceIP		(IPv4 Header)
	 * 2. DestinationIP	(IPv4 Header)
	 * 3. Source Port	(UDP Header)
	 * 4. SeqId			(SeqTs Header)
	 */
	
	Ipv4Address SourceIP, DestinationIP;
	uint32_t SourcePort = -1, SeqId = -1;
	
	packet_copy->EnablePrinting();
	
	PacketMetadata::ItemIterator metadataIterator = packet_copy->BeginItem();
	PacketMetadata::Item item;
	
	while (metadataIterator.HasNext()){
		
		item = metadataIterator.Next();
		
		//CheckIpv4Header
		//===============
		if(item.tid.GetName()=="ns3::Ipv4Header"){
			//std::cout << "ns3::Ipv4Header" << std::endl;
			Callback<ObjectBase *> constr = item.tid.GetConstructor();
			NS_ASSERT(!constr.IsNull());
			
			// Ptr<> and DynamicCast<> won't work here as all headers are from ObjectBase, not Object
			ObjectBase *instance = constr();
			NS_ASSERT(instance != 0);
			
			Ipv4Header* ipv4Header = dynamic_cast<Ipv4Header*> (instance);
			NS_ASSERT(ipv4Header != 0);
			
			ipv4Header->Deserialize(item.current);
			
			// The tcp sequence can now obtain the source of the packet
			SourceIP		= ipv4Header->GetSource();
			DestinationIP	= ipv4Header->GetDestination();
			
			//Set Source / Destination Address***
			SetSourceIP(SourceIP);
			SetDestinationIP(DestinationIP);
			
			delete ipv4Header;
			//break;
		}
		
		//CheckUdpHeader
		//==============
		if(item.tid.GetName()=="ns3::UdpHeader"){
			//std::cout << "ns3::UdpHeader" << std::endl;
			Callback<ObjectBase *> constr = item.tid.GetConstructor();
			NS_ASSERT(!constr.IsNull());
			
			// Ptr<> and DynamicCast<> won't work here as all headers are from ObjectBase, not Object
			ObjectBase *instance = constr();
			NS_ASSERT(instance != 0);
			
			UdpHeader* udpHeader = dynamic_cast<UdpHeader*> (instance);
			NS_ASSERT(udpHeader != 0);
			
			udpHeader->Deserialize(item.current);
			
			// The source port can now obtain the source of the packet
			SourcePort = udpHeader->GetSourcePort();
			
			//Set Source Port Number***
			SetSourcePort(SourcePort);
						
			delete udpHeader;
			//break;
		}
		
		//Check SeqTsHeader
		//=================
		if(item.tid.GetName()=="ns3::SeqTsHeader"){
			//std::cout << "ns3::SeqTsHeader" << std::endl;
			Callback<ObjectBase *> constr = item.tid.GetConstructor();
			NS_ASSERT(!constr.IsNull());
			
			// Ptr<> and DynamicCast<> won't work here as all headers are from ObjectBase, not Object
			ObjectBase *instance = constr();
			NS_ASSERT(instance != 0);
			
			SeqTsHeader* seqTsHeader = dynamic_cast<SeqTsHeader*> (instance);
			NS_ASSERT(seqTsHeader != 0);
			
			seqTsHeader->Deserialize(item.current);
			
			// The tcp sequence can now obtain the source of the packet
			//std::cout << "SeqTsHeader: " << seqTsHeader->GetSeq() << std::endl;
			SeqId = seqTsHeader->GetSeq();
			
			//Set SeqId***
			SetSeqId(SeqId);
			
			delete seqTsHeader;
			break;
		}

	}	
	
	/* Check packet info based on frame tag
	 * 1. Frame Tag Value	(Frame Type Tag)
	 * 2. Frame Type		(Frame Type Tag)
	 * 3. Qos Tid			(QoS Tag)
	 */
	
	//Check Frame Type Tag
	//====================
	FrameTypeTag frameTag;
	uint32_t FrameTypeValue;
	std::string FrameTypeString = "";
	
	if (packet_copy->PeekPacketTag(frameTag)){
		
		FrameTypeValue = frameTag.GetFrameType();
		SetFrameTypeValue(FrameTypeValue);
		
		if (FrameTypeValue == 1){
			FrameTypeString = "I";
			SetFrameTypeString(FrameTypeString);
			
		} else if (FrameTypeValue == 2){
			FrameTypeString = "P";
			SetFrameTypeString(FrameTypeString);
			
		} else if (FrameTypeValue == 3){
			FrameTypeString = "B";
			SetFrameTypeString(FrameTypeString);

		} else {
			//std::cout << "UNKNOWN FRAMETAG NUMBER" << std::endl;
			SetFrameTypeString("UNKNOWN FRAMETAG NUMBER" );
		}	
	}
	
	//Check QoS Tag
	//=============
	QosTag qosTag;
	uint8_t qosTid;
	
	if (packet_copy->PeekPacketTag(qosTag)){
		qosTid = qosTag.GetTid();
		SetQosTid(qosTid);
	
	}
	
	//Check NodeId Tag
	//=================
	NodeIdTag nodeIdTag;
	uint32_t NodeId=-1;
	
	if (packet_copy->PeekPacketTag(nodeIdTag)){
		NodeId = nodeIdTag.GetNodeId();
		SetNodeId(NodeId);
	}
	
	//Check DevId Tag
	//===============
	DevIdTag devIdTag;
	uint32_t DevId=-1;
	
	if (packet_copy->PeekPacketTag(devIdTag)){
		DevId = devIdTag.GetDevId();
		SetDevId(DevId);
	}
	
	//Check AppId Tag
	//===============
	AppIdTag appIdTag;
	uint32_t AppId=-1;
	
	if (packet_copy->PeekPacketTag(appIdTag)){
		AppId = appIdTag.GetAppId();
		SetAppId(AppId);
	}
	
}


//Setter Getter Start
//===================

//SourceIP Setter/ Getter
void WifiMacQueue::SetSourceIP(Ipv4Address SourceIP){
	m_SourceIP = SourceIP;
}

Ipv4Address WifiMacQueue::GetSourceIP(){
	return m_SourceIP;
}

//DestinationIP Setter/ Getter
void WifiMacQueue::SetDestinationIP(Ipv4Address DestinationIP){
	m_DestinationIP = DestinationIP;
}

Ipv4Address WifiMacQueue::GetDestinationIP(){
	return m_DestinationIP;
}

//SourcePort Setter/ Getter
void WifiMacQueue::SetSourcePort(uint32_t SourcePort){
	m_SourcePort = SourcePort;
}

uint32_t WifiMacQueue::GetSourcePort(){
	return m_SourcePort;
}

//SeqId Setter/ Getter
void WifiMacQueue::SetSeqId(uint32_t SeqId){
	m_SeqId = SeqId;
}

uint32_t WifiMacQueue::GetSeqId(){
	return m_SeqId;
}

//FrameTypeValue Setter/ Getter
void WifiMacQueue::SetFrameTypeValue(uint32_t FrameTypeValue){
	m_FrameTypeValue = FrameTypeValue;
}

uint32_t WifiMacQueue::GetFrameTypeValue(){
	return m_FrameTypeValue;
}

//FrameType Setter/ Getter
void WifiMacQueue::SetFrameTypeString(std::string FrameTypeString){
	m_FrameTypeString = FrameTypeString;
}

std::string WifiMacQueue::GetFrameTypeString(){
	return m_FrameTypeString;
}

//QoSTid Setter/ Getter
void WifiMacQueue::SetQosTid(uint8_t QosTid){
	m_QosTid = QosTid;
}

uint8_t WifiMacQueue::GetQosTid(){
	return m_QosTid;
}

//NodeId Setter/ Getter
void WifiMacQueue::SetNodeId(uint32_t NodeId){
	m_NodeId = NodeId;
}

uint32_t WifiMacQueue::GetNodeId(){
	return m_NodeId;
}

//DeviceId Setter/ Getter
void WifiMacQueue::SetDevId(uint32_t DevId){
	m_DevId = DevId;
}

uint32_t WifiMacQueue::GetDevId(){
	return m_DevId;
}

//AppId Setter/ Getter
void WifiMacQueue::SetAppId(uint32_t AppId){
	m_AppId = AppId;
}

uint32_t WifiMacQueue::GetAppId(){
	return m_AppId;
}

//Added by Najwan [-END-]

} //namespace ns3
