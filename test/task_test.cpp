#include <cassert>
#include <iostream>

#include "../src/include/taskBase.hpp"

struct MyMessage {
  std::string text;
};

class MyTask : public messaging::TaskBase {
 public:
  MyTask() : messaging::TaskBase() {}

  bool isReadyToStart() noexcept override {
    return true;
  }

  void run(std::stop_token stopToken) override {
    while (!stopToken.stop_requested()) {
      auto msg = messageQueue().wait_for(std::chrono::milliseconds(100));
      if (msg) {
        if (auto wrapped = std::dynamic_pointer_cast<messaging::MessageWrapper<MyMessage>>(msg)) {
          std::cout << "Task received: " << wrapped->contents_.text << std::endl;
          receivedCount++;
        } else {
          std::cout << "Task received UNKNOWN message type" << std::endl;
        }
      }
    }
  }

  void onPostMessageReceived(const std::type_info& /*messageTypeInfo*/,
                             const std::shared_ptr<messaging::MessageBase>& /*message*/) override {
    // Notification received
    notificationCount++;
  }

  int receivedCount = 0;
  int notificationCount = 0;
};

int main() {
  MyTask task;

  // Task IS a Sender.
  // By default Sender() has no queue. Let's bind it to its own receiver queue.
  static_cast<messaging::Sender&>(task) = task.makeSender();

  assert(task.start());

  task.Send(MyMessage{"Hello from Task's Sender"});

  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  task.stopAndWait();

  std::cout << "Received count: " << task.receivedCount << std::endl;
  std::cout << "Notification count: " << task.notificationCount << std::endl;

  assert(task.receivedCount == 1);
  assert(task.notificationCount == 1);

  // Test new concrete Task class
  int lambdaReceivedCount = 0;
  messaging::Task lambdaTask([&lambdaReceivedCount](messaging::Task& t, std::stop_token stopToken) {
    while (!stopToken.stop_requested()) {
      auto msg = t.messageQueue().wait_for(std::chrono::milliseconds(10));
      if (msg) {
        lambdaReceivedCount++;
      }
    }
  });

  assert(lambdaTask.start());
  lambdaTask.Send(MyMessage{"Hello from lambda"});
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  lambdaTask.stopAndWait();

  std::cout << "Lambda received count: " << lambdaReceivedCount << std::endl;
  assert(lambdaReceivedCount == 1);

  std::cout << "Task test passed!" << std::endl;

  return 0;
}
