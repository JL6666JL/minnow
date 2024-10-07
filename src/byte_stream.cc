#include "byte_stream.hh"

using namespace std;

ByteStream::ByteStream( uint64_t capacity ) : capacity_( capacity ) {}

bool Writer::is_closed() const
{
  // Your code here.
  return closed_;
}

void Writer::push( string data )
{
  // Your code here.
  if ( data.empty() || available_capacity() == 0 || is_closed() )
    return;
  if ( data.size() > available_capacity() ) {
    data.resize( available_capacity() );
  }
  pushed_sum_ = pushed_sum_ + data.size();
  buffered_sum_ = buffered_sum_ + data.size();
  stream_q.emplace( move( data ) );
}

void Writer::close()
{
  // Your code here.
  closed_ = true;
}

uint64_t Writer::available_capacity() const
{
  // Your code here.
  return capacity_ - buffered_sum_;
}

uint64_t Writer::bytes_pushed() const
{
  // Your code here.
  return pushed_sum_;
}

bool Reader::is_finished() const
{
  // Your code here.
  return closed_ && buffered_sum_ == 0;
}

uint64_t Reader::bytes_popped() const
{
  // Your code here.
  return popped_sum_;
}

string_view Reader::peek() const
{
  // Your code here.
  return stream_q.empty() ? string_view {} : string_view { stream_q.front() }.substr( prefix_len_ );
}

void Reader::pop( uint64_t len )
{
  // Your code here.
  buffered_sum_ -= len;
  popped_sum_ += len;
  while ( len != 0 ) {
    uint64_t size = stream_q.front().size() - prefix_len_;
    if ( len < size ) {
      prefix_len_ += len;
      break;
    }
    stream_q.pop();
    prefix_len_ = 0;
    len -= size;
  }
}

uint64_t Reader::bytes_buffered() const
{
  // Your code here.
  return buffered_sum_;
}
