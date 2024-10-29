#pragma once

#include "byte_stream.hh"
#include "tcp_receiver_message.hh"
#include "tcp_sender_message.hh"

#include <cstdint>
#include <functional>
#include <queue>

class RetryTimer
{
public:
  explicit RetryTimer( uint64_t initial_RTO_ms ) : RTO_duration_ms_( initial_RTO_ms ) {}

  bool is_timer_active() const { return timer_active_; }

  bool has_timer_expired() const
  {
    if ( !timer_active_ )
      return false;
    return timer_elapsed_ms_ >= RTO_duration_ms_;
  }

  void reset_timer() { timer_elapsed_ms_ = 0; }

  void apply_exponential_backoff() { RTO_duration_ms_ <<= 1; }

  void reload_timer( uint64_t initial_RTO_ms )
  {
    RTO_duration_ms_ = initial_RTO_ms;
    reset_timer();
  }

  void activate_timer()
  {
    if ( !timer_active_ ) {
      timer_active_ = true;
      reset_timer();
    }
  }

  void deactivate_timer()
  {
    if ( timer_active_ ) {
      timer_active_ = false;
      reset_timer();
    }
  }

  RetryTimer& advance_timer( uint64_t elapsed_ms )
  {
    if ( timer_active_ ) {
      timer_elapsed_ms_ += elapsed_ms;
    }
    return *this;
  }

private:
  bool timer_active_ {};
  uint64_t RTO_duration_ms_ {};
  uint64_t timer_elapsed_ms_ {};
};

class TCPSender
{
public:
  /* Construct TCP sender with given default Retransmission Timeout and possible ISN */
  TCPSender( ByteStream&& input, Wrap32 isn, uint64_t initial_RTO_ms )
    : input_( std::move( input ) ), isn_( isn ), initial_RTO_ms_( initial_RTO_ms ), retrans_timer_( initial_RTO_ms )
  {}

  /* Generate an empty TCPSenderMessage */
  TCPSenderMessage make_empty_message() const;

  /* Receive and process a TCPReceiverMessage from the peer's receiver */
  void receive( const TCPReceiverMessage& msg );

  /* Type of the `transmit` function that the push and tick methods can use to send messages */
  using TransmitFunction = std::function<void( const TCPSenderMessage& )>;

  /* Push bytes from the outbound stream */
  void push( const TransmitFunction& transmit );

  /* Time has passed by the given # of milliseconds since the last time the tick() method was called */
  void tick( uint64_t ms_since_last_tick, const TransmitFunction& transmit );

  // Accessors
  uint64_t sequence_numbers_in_flight() const;  // How many sequence numbers are outstanding?
  uint64_t consecutive_retransmissions() const; // How many consecutive *re*transmissions have happened?
  Writer& writer() { return input_.writer(); }
  const Writer& writer() const { return input_.writer(); }

  // Access input stream reader, but const-only (can't read from outside)
  const Reader& reader() const { return input_.reader(); }

private:
  // Variables initialized in constructor
  ByteStream input_;
  Wrap32 isn_;
  uint64_t initial_RTO_ms_;
  // added variables
  uint64_t next_seq_number_ {};
  uint64_t ack_sequence_number_ {};
  uint16_t window_capacity_ { 1 }; // start from 1
  std::queue<TCPSenderMessage> pending_messages_ {};
  uint64_t total_outgoing_seq_ {};
  uint64_t retrans_count_ {};
  bool SYN_sent_flag_ {};
  bool FIN_sent_flag_ {};
  RetryTimer retrans_timer_;
};
