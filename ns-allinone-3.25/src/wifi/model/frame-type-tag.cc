/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 *	Added by Najwan to tag the app ID at Evalvid Server
 */
#include "frame-type-tag.h"
#include "ns3/tag.h"
#include "ns3/uinteger.h"

namespace ns3 {

NS_OBJECT_ENSURE_REGISTERED (FrameTypeTag);

TypeId
FrameTypeTag::GetTypeId (void)
{
  static TypeId frametype = TypeId ("ns3::FrameTypeTag")
    .SetParent<Tag> ()
    .AddConstructor<FrameTypeTag> ()
    .AddAttribute ("frametype", "The frametype indicates whether it is I/P/B frame",
                   UintegerValue (0),
                   MakeUintegerAccessor (&FrameTypeTag::GetFrameType),
                   MakeUintegerChecker<uint32_t> ())
  ;
  return frametype;
}

TypeId
FrameTypeTag::GetInstanceTypeId (void) const
{
  return GetTypeId ();
}

FrameTypeTag::FrameTypeTag ()
  : m_frametype (0)
{
}
FrameTypeTag::FrameTypeTag (uint32_t frametype)
  : m_frametype (frametype)
{
}

void
FrameTypeTag::SetFrameType (uint32_t frametype)
{
  m_frametype = frametype;
}

//void
//AppIdTag::SetUserPriority (UserPriority up)
//{
  //m_aid = up;
//}

uint32_t
FrameTypeTag::GetSerializedSize (void) const
{
  return 1;
}

void
FrameTypeTag::Serialize (TagBuffer i) const
{
  i.WriteU8 (m_frametype);
}

void
FrameTypeTag::Deserialize (TagBuffer i)
{
  //m_aid = (UserPriority) i.ReadU8 ();
  m_frametype = i.ReadU8 ();
}

uint32_t
FrameTypeTag::GetFrameType () const
{
  return m_frametype;
}

void
FrameTypeTag::Print (std::ostream &os) const
{
  os << "FrameType=" << m_frametype;
}

} // namespace ns3
