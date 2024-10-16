#include "wrapping_integers.hh"

using namespace std;

Wrap32 Wrap32::wrap( uint64_t n, Wrap32 zero_point )
{
  return zero_point + static_cast<uint32_t>( n );
}

uint64_t Wrap32::unwrap( Wrap32 zero_point, uint64_t checkpoint ) const
{
  uint64_t uint_lowhalf = 0x0000'0000'FFFF'FFFF;
  uint64_t uint_highhalf = 0xFFFF'FFFF'0000'0000;
  uint64_t begin = uint_lowhalf + 1;
  uint64_t n_low32 = this->raw_value_ - zero_point.raw_value_;
  uint64_t c_low32 = checkpoint & uint_lowhalf;
  uint64_t result = ( checkpoint & uint_highhalf ) | n_low32;
  if ( result >= begin && n_low32 > c_low32 && ( n_low32 - c_low32 ) > ( begin / 2 ) ) {
    return result - begin;
  }
  if ( result < uint_highhalf && c_low32 > n_low32 && ( c_low32 - n_low32 ) > ( begin / 2 ) ) {
    return result + begin;
  }
  return result;
}
