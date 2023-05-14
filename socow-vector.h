#pragma once
#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <memory>

template <typename T, std::size_t SMALL_SIZE>
struct socow_vector {
  using size_type = std::size_t;
  using value_type = T;
  using iterator = value_type*;
  using const_iterator = value_type const*;
  using reference = value_type&;
  using const_reference = value_type const&;

  socow_vector() noexcept : type(vector_type::SMALL), size_(0) {}

  socow_vector(socow_vector const& other)
      : type((other.size() <= SMALL_SIZE) ? vector_type::SMALL
                                          : vector_type::BIG),
        size_(other.size()) {
    if (other.size() <= SMALL_SIZE) {
      std::uninitialized_copy(other.data(), other.data() + other.size(),
                              static_storage);
    } else {
      new (&dynamic_storage) my_shared_ptr(other.dynamic_storage);
    }
  }

  constexpr socow_vector& operator=(socow_vector const& other) {
    if (this != &other) {
      socow_vector(other).swap(*this);
    }
    return *this;
  }

  ~socow_vector() {
    destroy_storage();
    if (!is_small()) {
      dynamic_storage.~my_shared_ptr();
    }
  }

  void swap(socow_vector& other) {
    using std::swap;
    if (is_small() && other.is_small()) {
      swap_static_storages(other);
    } else if (is_small() && !other.is_small()) {
      swap_data_small_to_big(other);
    } else if (!is_small() && other.is_small()) {
      other.swap_data_small_to_big(*this);
    } else {
      dynamic_storage.swap(other.dynamic_storage);
    }
    swap(size_, other.size_);
    swap(type, other.type);
  }

  constexpr void reserve(size_type new_capacity) {
    reserve(new_capacity, nullptr);
  }

  constexpr void push_back(const_reference value) {
    if (size() + 1 > capacity()) {
      enlarge_capacity(size() + 1, &value);
    } else {
      new (data() + size_++) value_type(value);
    }
  }

  constexpr iterator insert(const_iterator pos, const_reference value) {
    size_type const id = get_index(pos);
    push_back(value);
    for (size_type i = id; i < size() - 1; ++i) {
      std::swap(back(), (*this)[i]);
    }
    return begin() + id;
  }

  constexpr iterator erase(const_iterator pos) {
    return erase(pos, pos + 1);
  }

  constexpr iterator erase(const_iterator first, const_iterator last) {
    size_type const first_id = get_index(first);
    size_type const last_id = get_index(last);
    size_type const size_segment = last_id - first_id;
    make_master();
    for (size_type i = first_id; i + size_segment < size(); ++i) {
      std::swap((*this)[i], (*this)[i + size_segment]);
    }
    for (size_type i = 0; i < size_segment; ++i) {
      pop_back();
    }
    return begin() + first_id;
  };

  constexpr void pop_back() noexcept {
    if (!make_master(capacity(), size() - 1)) {
      (*this)[--size_].~value_type();
    }
  }

  constexpr reference front() noexcept {
    make_master();
    return (data())[0];
  }

  constexpr const_reference front() const noexcept {
    return (data())[0];
  }

  constexpr reference back() {
    make_master();
    return (data())[size() - 1];
  }

  constexpr const_reference back() const {
    return (data())[size() - 1];
  }

  [[nodiscard]] constexpr bool empty() const noexcept {
    return size() == 0;
  }

  [[nodiscard]] size_type size() const noexcept {
    return size_;
  }

  [[nodiscard]] constexpr size_type capacity() const noexcept {
    return is_small() ? SMALL_SIZE : dynamic_storage.get_capacity();
  }

  [[nodiscard]] constexpr iterator data() noexcept {
    make_master();
    return (is_small() ? static_storage : dynamic_storage.get());
  }

  [[nodiscard]] constexpr const_iterator data() const noexcept {
    return (is_small() ? static_storage : dynamic_storage.get());
  }

  [[nodiscard]] constexpr iterator begin() noexcept {
    return data();
  }

  [[nodiscard]] constexpr const_iterator begin() const noexcept {
    return data();
  }

  [[nodiscard]] constexpr iterator end() noexcept {
    return data() + size();
  }

  [[nodiscard]] constexpr const_iterator end() const noexcept {
    return data() + size();
  }

  constexpr void shrink_to_fit() {
    if (capacity() > size()) {
      if (size() <= SMALL_SIZE) {
        socow_vector(*this).swap(*this);
      } else {
        change_dynamic_storage(size(), size());
      }
    }
  }

  constexpr void clear() noexcept {
    destroy_storage();
    if (!is_master()) {
      dynamic_storage.reset(capacity());
    }
    size_ = 0;
  }

  constexpr reference operator[](size_type pos) noexcept {
    return (data())[pos];
  }
  constexpr const_reference operator[](size_type pos) const noexcept {
    return (data())[pos];
  }

private:
  enum class vector_type : bool { SMALL, BIG };

  static constexpr std::uint32_t EXPANSION_FACTOR = 2;

  vector_type type;
  size_type size_;

  struct my_shared_ptr {
  private:
    struct buffer {
      size_type cnt_references;
      size_type capacity;
      value_type arr[0];

      explicit buffer(size_type capacity_) noexcept
          : cnt_references(1), capacity(capacity_) {}
    };

  public:
    buffer* data;

    explicit my_shared_ptr(size_type array_capacity) {
      size_type const bytes =
          sizeof(buffer) + array_capacity * sizeof(value_type);
      auto* buf = new std::byte[bytes];
      data = new (buf) buffer(array_capacity);
    }

    my_shared_ptr(my_shared_ptr const& other) : data(other.data) {
      ++data->cnt_references;
    }

    constexpr void swap(my_shared_ptr& other) noexcept {
      std::swap(data, other.data);
    }

    my_shared_ptr& operator=(my_shared_ptr const& other) {
      if (this != &other) {
        my_shared_ptr(other).swap(*this);
      }
      return *this;
    }

    constexpr reference operator[](size_type pos) noexcept {
      return *((data->arr) + pos);
    }

    constexpr const_reference operator[](size_type pos) const noexcept {
      return *((data->arr) + pos);
    }

    constexpr void reset(size_type array_capacity) {
      my_shared_ptr(array_capacity).swap(*this);
    }

    [[nodiscard]] constexpr size_type use_count() const noexcept {
      return data->cnt_references;
    }

    [[nodiscard]] constexpr iterator get() noexcept {
      return data->arr;
    }

    [[nodiscard]] constexpr size_type get_capacity() const noexcept {
      return data->capacity;
    }

    [[nodiscard]] constexpr const_iterator get() const noexcept {
      return data->arr;
    }

    constexpr void destroy(size_type size) noexcept {
      for (size_type i = size; i-- > 0;) {
        (*this)[i].~value_type();
      }
    }

    ~my_shared_ptr() {
      if (use_count() != 1) {
        --data->cnt_references;
      } else {
        delete[] reinterpret_cast<std::byte*>(data);
      }
    }
  };

  union {
    my_shared_ptr dynamic_storage;
    value_type static_storage[SMALL_SIZE];
  };

  [[nodiscard]] bool is_small() const noexcept {
    return type == vector_type::SMALL;
  }

  void swap_data_small_to_big(socow_vector& big_vector) {
    assert(type == vector_type::SMALL && big_vector.type == vector_type::BIG);
    my_shared_ptr new_dynamic_storage(big_vector.dynamic_storage);
    big_vector.dynamic_storage.~my_shared_ptr();
    try {
      std::uninitialized_copy(static_storage, static_storage + size(),
                              big_vector.static_storage);
    } catch (...) {
      new (&big_vector.dynamic_storage) my_shared_ptr(new_dynamic_storage);
      throw;
    }
    destroy_storage();
    new (&dynamic_storage) my_shared_ptr(new_dynamic_storage);
  }

  void swap_static_storages(socow_vector& other) {
    size_type const min_size = std::min(size(), other.size());
    size_type const max_size = std::max(size(), other.size());
    for (size_type i = 0; i < min_size; ++i) {
      std::swap(static_storage[i], other.static_storage[i]);
    }
    iterator const copy_to =
        (size() < other.size()) ? static_storage : other.static_storage;
    iterator const copy_from =
        (size() >= other.size()) ? static_storage : other.static_storage;
    std::uninitialized_copy(copy_from + min_size, copy_from + max_size,
                            copy_to + min_size);
    for (size_type i = min_size; i < max_size; ++i) {
      copy_from[i].~value_type();
    }
  }

  void change_static_storage_to_dynamic(size_type new_capacity,
                                        const_iterator value = nullptr) {
    my_shared_ptr temp(new_capacity);
    std::uninitialized_copy(static_storage, static_storage + size(),
                            temp.get());
    size_type new_size = size();
    if (value) {
      try {
        new (temp.get() + new_size++) value_type(*value);
      } catch (...) {
        temp.destroy(size());
        throw;
      }
    }

    destroy_storage();
    new (&dynamic_storage) my_shared_ptr(temp);
    type = vector_type::BIG;
    size_ = new_size;
  }

  constexpr void change_dynamic_storage(size_type new_capacity,
                                        size_type new_size,
                                        const_iterator value = nullptr) {
    assert(!is_small());
    my_shared_ptr new_data(new_capacity);
    std::uninitialized_copy(dynamic_storage.get(),
                            dynamic_storage.get() + new_size, new_data.get());
    if (value) {
      try {
        new (new_data.get() + new_size) value_type(*value);
      } catch (...) {
        new_data.destroy(new_size);
        throw;
      }
    }
    destroy_storage();
    dynamic_storage = new_data;
    size_ = new_size + (value != nullptr);
  }

  constexpr bool make_master() {
    return make_master(capacity(), size());
  }

  constexpr bool make_master(size_type new_capacity, size_type new_size) {
    if (!is_master()) {
      change_dynamic_storage(new_capacity, new_size);
      return true;
    }
    return false;
  }

  [[nodiscard]] size_type get_index(const_iterator pos) const noexcept {
    return pos - begin();
  }

  constexpr void destroy_storage() noexcept {
    if (is_master()) {
      for (size_t i = size(); i-- > 0;) {
        (*this)[i].~value_type();
      }
    }
  }

  [[nodiscard]] constexpr bool is_master() const noexcept {
    return is_small() || dynamic_storage.use_count() == 1;
  }

  constexpr void reserve(size_type new_capacity, const_iterator value) {
    if (new_capacity > capacity() || (!is_master() && new_capacity > size())) {
      if (is_small()) {
        change_static_storage_to_dynamic(new_capacity, value);
      } else {
        change_dynamic_storage(new_capacity, size(), value);
      }
    }
  }

  void enlarge_capacity(size_type min_capacity,
                        const_iterator value = nullptr) {
    if (capacity() < min_capacity) {
      reserve(std::max(capacity() * EXPANSION_FACTOR, min_capacity), value);
    }
  }
};