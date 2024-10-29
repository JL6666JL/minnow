#include "tcp_receiver.hh"
#include "wrapping_integers.hh"

#include <cstdint>

using namespace std;

void TCPReceiver::receive( TCPSenderMessage message )
{
  if ( writer().has_error() )
    return;
  if ( message.RST ) {
    reader().set_error();
    return;
  }
  if ( !base_seqno_.has_value() ) {
    if ( !message.SYN ) {
      return;
    }
    base_seqno_ = message.seqno;
  }
  uint64_t expected_seq = writer().bytes_pushed() + 1;
  uint64_t absolute_seq = message.seqno.unwrap( *base_seqno_, expected_seq );
  uint64_t index_in_stream = absolute_seq + ( message.SYN ? 1 : 0 ) - 1;

  reassembler_.insert( index_in_stream, move( message.payload ), message.FIN );
}

TCPReceiverMessage TCPReceiver::send() const
{
  uint16_t window;
  if ( writer().available_capacity() > UINT16_MAX )
    window = UINT16_MAX;
  else
    window = static_cast<uint16_t>( writer().available_capacity() );

  if ( base_seqno_.has_value() ) {
    const uint64_t ack_seq = writer().bytes_pushed() + 1 + ( writer().is_closed() ? 1 : 0 );
    return { Wrap32::wrap( ack_seq, *base_seqno_ ), window, writer().has_error() };
  }

  return { nullopt, window, writer().has_error() };
}
