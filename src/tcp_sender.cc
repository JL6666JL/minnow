#include "tcp_sender.hh"
#include "tcp_config.hh"
#include "wrapping_integers.hh"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <utility>

using namespace std;

uint64_t TCPSender::sequence_numbers_in_flight() const
{
  return total_outgoing_seq_;
}

uint64_t TCPSender::consecutive_retransmissions() const
{
  return retrans_count_;
}

void TCPSender::push( const TransmitFunction& transmit )
{
  while ( ( window_capacity_ == 0 ? 1 : window_capacity_ ) > total_outgoing_seq_ ) {
    if ( FIN_sent_flag_ )
      break;
    auto msg = make_empty_message();
    if ( not SYN_sent_flag_ ) {
      msg.SYN = true;
      SYN_sent_flag_ = true;
    }

    uint64_t remaining_capacity = ( window_capacity_ == 0 ? 1 : window_capacity_ ) - total_outgoing_seq_;
    size_t payload_len = min( TCPConfig::MAX_PAYLOAD_SIZE, remaining_capacity - msg.sequence_length() );
    auto&& payload_data = msg.payload;

    while ( reader().bytes_buffered() != 0 and payload_data.size() < payload_len ) {
      string_view data_view = reader().peek();
      data_view = data_view.substr( 0, payload_len - payload_data.size() );
      payload_data += data_view;
      input_.reader().pop( data_view.size() );
    }

    if ( !FIN_sent_flag_ && remaining_capacity > msg.sequence_length() && reader().is_finished() ) {
      msg.FIN = true;
      FIN_sent_flag_ = true;
    }

    if ( msg.sequence_length() == 0 )
      break;

    transmit( msg );
    if ( !retrans_timer_.is_timer_active() ) {
      retrans_timer_.activate_timer();
    }
    next_seq_number_ += msg.sequence_length();
    total_outgoing_seq_ += msg.sequence_length();
    pending_messages_.emplace( move( msg ) );
  }
}

TCPSenderMessage TCPSender::make_empty_message() const
{
  return { Wrap32::wrap( next_seq_number_, isn_ ), false, {}, false, input_.has_error() };
}

void TCPSender::receive( const TCPReceiverMessage& msg )
{
  if ( input_.has_error() || msg.RST ) {
    if ( msg.RST ) {
      input_.set_error();
    }
    return;
  }

  window_capacity_ = msg.window_size;
  if ( !msg.ackno.has_value() )
    return;

  const uint64_t received_ack_seq = msg.ackno->unwrap( isn_, next_seq_number_ );
  if ( received_ack_seq > next_seq_number_ )
    return;

  bool acknowledged = false;
  while ( !pending_messages_.empty() ) {
    const auto& front_msg = pending_messages_.front();
    if ( ack_sequence_number_ + front_msg.sequence_length() > received_ack_seq ) {
      break;
    }
    ack_sequence_number_ += front_msg.sequence_length();
    total_outgoing_seq_ -= front_msg.sequence_length();
    pending_messages_.pop();
    acknowledged = true;
  }

  if ( acknowledged ) {
    retrans_count_ = 0;
    retrans_timer_.reload_timer( initial_RTO_ms_ );
    if ( pending_messages_.empty() ) {
      retrans_timer_.deactivate_timer();
    } else {
      retrans_timer_.activate_timer();
    }
  }
}

void TCPSender::tick( uint64_t ms_since_last_tick, const TransmitFunction& transmit )
{
  if ( retrans_timer_.advance_timer( ms_since_last_tick ).has_timer_expired() ) {
    if ( pending_messages_.empty() ) {
      return;
    }
    transmit( pending_messages_.front() );
    if ( window_capacity_ != 0 ) {
      retrans_count_ += 1;
      retrans_timer_.apply_exponential_backoff();
    }
    retrans_timer_.reset_timer();
  }
}
