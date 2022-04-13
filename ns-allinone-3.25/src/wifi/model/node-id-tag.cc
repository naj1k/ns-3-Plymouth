/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 *	Added by Najwan to tag the node ID at WifiNetDevice
 */
#include "node-id-tag.h"
#include "ns3/tag.h"
#include "ns3/uinteger.h"

namespace ns3 {

NS_OBJECT_ENSURE_REGISTERED (NodeIdTag);

TypeId
NodeIdTag::GetTypeId (void)
{
  static TypeId nodeid = TypeId ("ns3::NodeIdTag")
    .SetParent<Tag> ()
    .AddConstructor<NodeIdTag> ()
    .AddAttribute ("nodeid", "The nodeid that indicates AC which packet belongs",
                   UintegerValue (0),
                   MakeUintegerAccessor (&NodeIdTag::GetNodeId),
                   MakeUintegerChecker<uint32_t> ())
  ;
  return nodeid;
}

TypeId
NodeIdTag::GetInstanceTypeId (void) const
{
  return GetTypeId ();
}

NodeIdTag::NodeIdTag ()
  : m_nodeid (0)
{
}
NodeIdTag::NodeIdTag (uint32_t nodeid)
  : m_nodeid (nodeid)
{
}

void
NodeIdTag::SetNodeId (uint32_t nodeid)
{
  m_nodeid = nodeid;
}

//void
//NodeIdTag::SetUserPriority (UserPriority up)
//{
  //m_nid = up;
//}

uint32_t
NodeIdTag::GetSerializedSize (void) const
{
  return 1;
}

void
NodeIdTag::Serialize (TagBuffer i) const
{
  i.WriteU8 (m_nodeid);
}

void
NodeIdTag::Deserialize (TagBuffer i)
{
  //m_nid = (UserPriority) i.ReadU8 ();
  m_nodeid = i.ReadU8 ();
}

uint32_t
NodeIdTag::GetNodeId () const
{
  return m_nodeid;
}

void
NodeIdTag::Print (std::ostream &os) const
{
  os << "NodeId=" << m_nodeid;
}

} // namespace ns3
