#ifndef _RINGQUEUE_H
#define _RINGQUEUE_H

#ifndef __cplusplus
#error "C++ only"
#endif

#include <utility>
#include <cassert>
#include <type_traits>

/******************************************************************************************
Queue template class

Implement a queue with a ring buffer. Currently, LWIP is running in Polling mode. But
compatiblity with background mode (interrupt driven) is desired. The class does not manage the 
lifetime of stored data by move sematic. 

The data type being stored must support move semantics.

Usage:
QUEUESIZE is the capacity of the queue. QUEUESIZE must be a power of 2 to improve the 
performance. ( %QUEUESIZE should be optimized to bitwise AND).

To add an item to the queue, first check if the queue is full by full(). If it is not,
call push() method to add the item.

To read from the queue, empty() method must be called first. Wait until empty() returns
false. Then, read the first element at the queue by front() method. It returns a reference to
the first packet. After processing the packet data, call pop() method to remove it from
the queue.

The implementation uses a flag to determine if the buffer is full. It works pretty well in
single provider-single consumer model. For example, the udp_recv_callback() is the only
data provider. It only modifies head index and the index is updated after data are stored
in the queue. Similary, event loop is the only consumer. It only modifies tail index. They
don't have concurrency problems.

But the full flag is updated by both provider and consumer. If the buffer is full or empty,
either provider or consumer is forbidden to access the buffer. So, there is no concurrency
problem. If the buffer is neither full nor empty, it means head!=tail. Assume Add() and
Remove() operations are overlapped.  The full flag may not have the correct value due to
lack of locking. Bit when both operations are completed, the buffer is still neither full
nor empty. Since (head==tail) is checked in full() and empty() methods, both methods
will return true. So, no lock or synchroniztion is needed even LWIP is in interrupt-driven
mode.

volatile vs atomic
Due to the mechanism stated above, head,tail and full variables are volatile instead of
atomic.

Reference:
https://embeddedartistry.com/blog/2017/05/17/creating-a-circular-buffer-in-c-and-c/
https://www.downtowndougbrown.com/2013/01/microcontrollers-interrupt-safe-ring-buffers/
******************************************************************************************/
template<typename  T, size_t QUEUESIZE>
class ringqueue {
    static_assert((QUEUESIZE&(QUEUESIZE - 1)) == 0, "QUEUESIZE should be power of 2");
    static_assert(std::is_move_assignable<T>::value, "Type T must be move assignable");
public:
  ringqueue():head_(0), tail_(0), full_(false) {}

  //Disable copy and move semantics
  ringqueue(const ringqueue&) = delete;
  ringqueue& operator=(const ringqueue&) = delete;
  ringqueue(ringqueue&&) = delete;
  ringqueue& operator=(ringqueue&&) = delete;

  //Push new item with move semantics
  //Caller must ensure full() is false before calling
  void push(T&& newitem) {
    assert(!full());

    auto head = head_;  //to reduce reading from volatile variable
    buf_[head] = std::move(newitem);
    head = (head + 1) % QUEUESIZE;  //calculate newhead
    head_ = head;
    full_ = (head == tail_);
  }

  //Returns a reference to the first element in the queue
  //If the queue is empty, it returns garbage.
  T& front() {
    assert(!empty());
    return buf_[tail_];
  }
  
  const T& front() const {
    assert(!empty());
    return buf_[tail_];
  }

  //Remove the first element in the queue.
  //Caller must ensure empty() is false before calling
  void pop() {
    assert(!empty());

    //Move the first element to a dummy variable to destroy the content
    //if the type is not trivially move assignable.
    if constexpr(!std::is_trivially_move_assignable<T>::value){
      T destroy_me = std::move(buf_[tail_]);
    }

    tail_ = (tail_ + 1) % QUEUESIZE;
    full_ = false;
  }

  bool empty() const {
    return (head_ == tail_) && !full_; //Check both conditions to make the buffer lock-free
  }

  bool full() const {
    return (head_ == tail_) && full_;  //Check both conditions to make the buffer lock-free
  }

private:
  T buf_[QUEUESIZE];
  volatile size_t head_;
  volatile size_t tail_;
  volatile bool full_;

public:
  //A RAII helper class to call pop() method.
  //When the object goes out of scope, the pop() method of the queue is called.
  //Example:
  //
  // PacketQueue::Popper popper(mypacketqueue); 
  //    mypacketqueue.pop() is called when popper goes out of scope
  //
  class Popper {
  public:
      Popper(ringqueue& q) :q_(q) {}
      ~Popper() { if (!q_.empty()) q_.pop(); }
  private:
      ringqueue& q_;
  };
};


#endif
