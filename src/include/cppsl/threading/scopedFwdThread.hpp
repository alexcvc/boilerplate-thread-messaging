/* SPDX-License-Identifier: MIT */

/*************************************************************************/ /**
 * @file
 * @brief  contains scoped thread class.
 * @ingroup C++ support library
 *****************************************************************************/

#pragma once

#include <memory>
#include <thread>

namespace cppsl::threading {

/**
 * @brief A scoped thread class.
 *
 * The ScopedForwardThread class provides a simple wrapper around std::thread that joins the thread in the destructor.
 * This ensures that the thread is joined when the ScopedForwardThread object goes out of scope.
 */
template <typename Func, typename... Args>
class ScopedFwdThread {
 public:
  explicit ScopedFwdThread(Func&& func, Args&&... args)
      : m_thread(std::forward<Func>(func), std::forward<Args>(args)...) {}

  ~ScopedFwdThread() {
    join();
  }

  /**
   * @brief Retrieves the ID of the associated thread.
   * @return The ID of the thread.
   */
  auto getThreadId() const {
    return m_thread.get_id();
  }

  /**
    * @brief Check if the thread is terminated.
    * @return True if the thread is terminated, false otherwise.
    */
  [[nodiscard]] bool isTerminated() const {
    return m_terminated;
  }

  /**
   * @brief Joins the thread if it is running and marks the thread as terminated.
   * This function joins the underlying thread if it is running, using `std::thread::join()`.
   * After joining, it sets the `m_terminated` flag to true.
   * If the thread is already terminated or not joinable, this function does nothing.
   */
  void join() {
    if (!m_terminated) {
      if (m_thread.joinable()) {
        m_thread.join();
      }
      m_terminated = true;
    }
  }

 private:
  bool m_terminated{false};  ///< flag to indicate if the thread has been terminated.
  std::thread m_thread;      ///< thread object associated with this object.
};

/**
 * @brief Extracts a function to create a ScopedForwardThread object.
 *
 * The `MakeScopedFwdThread` function is used to create a `ScopedForwardThread` object. This function takes a
 * callable object (`Func`) and its arguments (`Args`) and forwards them to the constructor of `ScopedForwardThread`.
 * The created `ScopedForwardThread` object is then returned.
 * @return A `ScopedForwardThread` object.
 */
template <typename Func, typename... Args>
ScopedFwdThread<Func, Args...> ConstructScopedFwdThread(Func&& func, Args&&... args) {
  return ScopedFwdThread<Func, Args...>(std::forward<Func>(func), std::forward<Args>(args)...);
}

/**
 * function template instantiateScopedFwdThread creates a std::unique_ptr to a ScopedFwdThread object.
 * @param takes a function and a variadic list of arguments (using perfect forwarding).
 * @return Returns a std::unique_ptr to a new ScopedFwdThread object instantiated with the provided function
 * and arguments.
 */
template <typename FunctionType, typename... ArgumentTypes>
std::unique_ptr<ScopedFwdThread<FunctionType, ArgumentTypes...>> MakeUniqueScopedFwdThread(
    FunctionType&& function, ArgumentTypes&&... arguments) {
  return std::make_unique<ScopedFwdThread<FunctionType, ArgumentTypes...>>(std::forward<FunctionType>(function),
                                                                           std::forward<ArgumentTypes>(arguments)...);
}

}  // namespace cppsl::threading