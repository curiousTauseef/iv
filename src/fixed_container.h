#ifndef IV_FIXED_CONTAINE_H_
#define IV_FIXED_CONTAINE_H_
#include <cstddef>
#include <iterator>
#include <tr1/type_traits>
namespace iv {
namespace core {

template<typename T>
class FixedContainer {
 public:
  typedef T value_type;
  typedef typename std::tr1::add_pointer<T>::type pointer;
  typedef typename std::tr1::add_const<pointer>::type const_pointer;
  typedef pointer iterator;
  typedef typename std::tr1::add_const<iterator>::type const_iterator;
  typedef typename std::tr1::add_reference<T>::type  reference;
  typedef typename std::tr1::add_const<reference>::type const_reference;
  typedef std::reverse_iterator<iterator> reverse_iterator;
  typedef std::reverse_iterator<const_iterator> const_reverse_iterator;
  typedef std::size_t size_type;
  typedef std::ptrdiff_t difference_type;

  FixedContainer(pointer buffer, size_type size)
    : buf_(buffer),
      size_(size) { }

  iterator begin() {
    return buf_;
  }

  iterator end() {
    return buf_ + size_;
  }

  const_iterator begin() const {
    return buf_;
  }

  const_iterator end() const {
    return buf_ + size_;
  }

  reverse_iterator rbegin() {
    reverse_iterator(end());
  }

  const_reverse_iterator rbegin() const {
    const_reverse_iterator(end());
  }

  reverse_iterator rend() {
    reverse_iterator(begin());
  }

  const_reverse_iterator rend() const {
    const_reverse_iterator(begin());
  }

  reference operator[](size_type n) {
    assert(n < size_);
    return buf_[n];
  }

  const_reference operator[](size_type n) const {
    assert(n < size_);
    return buf_[n];
  }

  reference front() {
    return buf_[0];
  }

  const_reference front() const {
    return buf_[0];
  }

  reference back() {
    return buf_[size_ - 1];
  }

  const_reference back() const {
    return buf_[size_ - 1];
  }

  size_type size() const {
    return size_;
  }

  bool empty() const {
    return size_ == 0;
  }

  size_type max_size() const {
    return size_;
  }

  pointer data() {
    return buf_;
  }

  const_pointer data() const {
    return buf_;
  }

  void assign(const T& value) {
    std::fill_n(begin(), size(), value);
  }

  void swap(FixedContainer<T>& rhs) {
    using std::swap;
    swap(buf_, rhs.buf_);
    swap(size_, rhs.size_);
  }

  friend inline void swap(FixedContainer<T>& lhs, FixedContainer<T> rhs) {
    lhs.swap(rhs);
  }

 private:
  pointer buf_;
  size_type size_;
};

} }  // namespace iv::core
#endif  // IV_FIXED_CONTAINER_H_
