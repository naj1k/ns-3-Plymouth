/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 *	Added by Najwan to tag the dev ID
 */
#include "dev-id-tag.h"
#include "ns3/tag.h"
#include "ns3/uinteger.h"

namespace ns3 {

NS_OBJECT_ENSURE_REGISTERED (DevIdTag);

TypeId
DevIdTag::GetTypeId (void)
{
  static TypeId devid = TypeId ("ns3::DevIdTag")
    .SetParent<Tag> ()
    .AddConstructor<DevIdTag> ()
    .AddAttribute ("devid", "The devid that indicates AC which packet belongs",
                   UintegerValue (0),
                   MakeUintegerAccessor (&DevIdTag::GetDevId),
                   MakeUintegerChecker<uint32_t> ())
  ;
  return devid;
}

TypeId
DevIdTag::GetInstanceTypeId (void) const
{
  return GetTypeId ();
}

DevIdTag::DevIdTag ()
  : m_devid (0)
{
}
DevIdTag::DevIdTag (uint32_t devid)
  : m_devid (devid)
{
}

void
DevIdTag::SetDevId (uint32_t devid)
{
  m_devid = devid;
}

//void
//DevIdTag::SetUserPriority (UserPriority up)
//{
  //m_did = up;
//}

uint32_t
DevIdTag::GetSerializedSize (void) const
{
  return 1;
}

void
DevIdTag::Serialize (TagBuffer i) const
{
  i.WriteU8 (m_devid);
}

void
DevIdTag::Deserialize (TagBuffer i)
{
  //m_did = (UserPriority) i.ReadU8 ();
  m_devid = i.ReadU8 ();
}

uint32_t
DevIdTag::GetDevId () const
{
  return m_devid;
}

void
DevIdTag::Print (std::ostream &os) const
{
  os << "DevId=" << m_devid;
}

} // namespace ns3
