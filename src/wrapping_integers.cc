#include "wrapping_integers.hh"

using namespace std;

Wrap32 Wrap32::wrap( uint64_t n, Wrap32 zero_point )
{
  return zero_point + static_cast<uint32_t>( n );
}

uint64_t Wrap32::unwrap( Wrap32 zero_point, uint64_t checkpoint ) const
{
  uint64_t high_mask = 0xFFFFFFFF00000000;
  uint64_t bias = 0x0000000100000000;

  uint64_t seqno = raw_value_ - zero_point.raw_value_;
  uint64_t abs_seqno_t0 = ( checkpoint & high_mask ) | seqno;

  if ( abs_seqno_t0 <= checkpoint ) {
    uint64_t abs_seqno_t1 = ( ( checkpoint & high_mask ) + bias ) | seqno;
    return checkpoint - abs_seqno_t0 < abs_seqno_t1 - checkpoint ? abs_seqno_t0 : abs_seqno_t1;
  }

  if ( ( checkpoint & high_mask ) == 0 ) {
    return abs_seqno_t0;
  }

  uint64_t abs_seqno_t1 = ( ( checkpoint & high_mask ) - bias ) | seqno;
  return abs_seqno_t0 - checkpoint < checkpoint - abs_seqno_t1 ? abs_seqno_t0 : abs_seqno_t1;
}
