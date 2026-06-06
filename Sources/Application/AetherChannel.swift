//===----------------------------------------------------------------------===//
// Runtime V20 Swift-facing async channel helpers.
//
// AetherChannelU64 is a tiny Swift wrapper over the fixed C-owned mailbox
// queues. It gives application tasks an async receive shape without moving the
// bounded storage or accounting out of the kernel substrate.
//===----------------------------------------------------------------------===//
import Support
import _Concurrency

struct AetherChannelU64: Sendable {
  let mailboxID: UInt32

  func send(_ value: UInt64) -> Bool {
    kernel_mailbox_send_u64(mailboxID, UInt(value)) != 0
  }

  func tryReceive() -> (Bool, UInt64) {
    var value: UInt = 0
    if kernel_mailbox_recv_u64(mailboxID, &value) != 0 {
      return (true, UInt64(value))
    }
    return (false, 0)
  }

  func receive() async -> UInt64 {
    while true {
      let result = tryReceive()
      if result.0 {
        return result.1
      }
      await timerSleepMillis(25)
    }
  }

  func depth() -> UInt32 {
    kernel_mailbox_depth(mailboxID)
  }
}

func aetherChannelSelftest() -> Int32 {
  let channel = AetherChannelU64(mailboxID: MAILBOX_SELFTEST_ID)
  let value: UInt64 = 0x0000_0000_0000_c020

  kernel_mailbox_clear(MAILBOX_SELFTEST_ID)
  if !channel.send(value) {
    return 0
  }
  if channel.depth() != 1 {
    return 0
  }
  let received = channel.tryReceive()
  if !received.0 || received.1 != value {
    return 0
  }
  if channel.depth() != 0 {
    return 0
  }
  return kernel_mailbox_selftest()
}
