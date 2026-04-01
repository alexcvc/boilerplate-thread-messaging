/*************************************************************************/ /**
 * @file
 * @brief  contains scoped thread class.
 * @ingroup C++ support library
 *****************************************************************************/

#pragma once

#include <thread>

namespace cppsl::threading {

/**
 * @brief A scoped thread class.
 *
 * The ScopedThread class provides a simple wrapper around std::thread that joins the thread in the destructor.
 * This ensures that the thread is joined when the ScopedThread object goes out of scope.
 */
class ScopedThreadForwarding {
 public:
  /**
   * @brief constructor
   * @tparam Args Variadic template parameter representing the types of arguments passed to std::thread constructor.
   */
  template <class... Args>
  explicit ScopedThreadForwarding(Args&&... args) : m_thread(std::forward<Args>(args)...) {}

  /**
   * @brief move constructor.
   */
  ScopedThreadForwarding(ScopedThreadForwarding&& other) noexcept {
    m_thread = std::move(other.m_thread);
  }

  /**
   * @brief assignment operator
   * @param other The ScopedThread object to be assigned from.
   * @return A reference to the current ScopedThread object after assignment.
   */
  ScopedThreadForwarding& operator=(ScopedThreadForwarding&& other) {
    m_thread = std::move(other.m_thread);
    return *this;
  }

  /**
   * @brief Retrieves the reference to the underlying std::thread object.
   * @return A reference to the std::thread object.
   */
  std::thread& operator*() {
    return m_thread;
  }

  /**
   * @brief Access the underlying `std::thread` object.
   * @return A const reference to the underlying `std::thread` object.
   */
  std::thread const& operator*() const {
    return m_thread;
  }

  /**
   * @brief Overloaded arrow operator that returns a pointer to the std::thread object.
   * The arrow operator (->) is overloaded to return a pointer to the std::thread object.
   * This allows accessing the functions and member variables of the std::thread object 
   * directly from a ScopedThread object.
   * @return A pointer to the std::thread object.
   */
  std::thread* operator->() {
    return &operator*();
  }

  /**
   * @brief Returns a pointer to the underlying std::thread object.
   * This function returns a const pointer to the underlying std::thread object associated with this ScopedThread object.
   * It allows accessing the member functions and data of the std::thread object.
   * @return A pointer to the underlying std::thread object.
   */
  std::thread const* operator->() const {
    return &operator*();
  }

  /**
   * @brief Get the thread identifier of the ScopedThread object.
   * @return The thread identifier of the underlying thread.
   */
  [[nodiscard]] auto get_id() const {
    return m_thread.get_id();
  }

  /**
   * @brief Joins the associated thread.
   *
   * The join() method checks if the thread is joinable and if so, it joins the thread. This means that the calling thread will wait until the associated thread completes its execution. If the thread is not joinable, no action is taken.
   */
  auto join() {
    if (m_thread.joinable())
      m_thread.join();
  }

  /**
    * @brief Destructor for the ScopedThread class.
    *
    * This destructor joins the thread if it is joinable.
    */
  ~ScopedThreadForwarding() {
    join();
  }

 private:
  std::thread m_thread;  ///< The std::thread object associated with the ScopedThread object.
};

}  // namespace cppsl::threading