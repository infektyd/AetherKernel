//===----------------------------------------------------------------------===//
// Runtime V19 structured task spawn helpers.
//
// The Swift side still uses the Embedded Swift Task runtime, but all kernel
// observability goes through this small Aether-owned surface.
//===----------------------------------------------------------------------===//
import Support
import _Concurrency

func registerAetherTask(
  _ taskID: UInt32,
  _ name: StaticString,
  _ periodMS: UInt32,
  _ parentTaskID: UInt32,
  _ supervisorDeadlineMS: UInt32
) {
  _ = kernel_task_register_with_parent(
    taskID,
    name.utf8Start,
    UInt32(name.utf8CodeUnitCount),
    periodMS,
    parentTaskID
  )
  _ = kernel_supervisor_register_task(
    taskID,
    supervisorDeadlineMS,
    KERNEL_SUPERVISOR_POLICY_OBSERVE
  )
}

func spawnAetherTask(
  _ taskID: UInt32,
  _ parentTaskID: UInt32,
  _ body: @escaping @Sendable () async -> Void
) {
  kernel_task_record_spawn(taskID, parentTaskID)
  Task {
    await body()
    kernel_task_record_completion(taskID)
  }
}

func aetherTaskSpawnSelftest() -> Int32 {
  let active = kernel_task_count()
  if active == 0 {
    return 0
  }

  var spawned: UInt32 = 0
  var i: UInt32 = 0
  while i < kernel_task_capacity() {
    if kernel_task_object_id(i) != 0 {
      if kernel_task_handle(i) == KERNEL_OBJECT_HANDLE_INVALID {
        return 0
      }
      if kernel_task_parent_id(i) != KERNEL_TASK_ROOT_PARENT &&
        kernel_task_parent_id(i) >= kernel_task_capacity() {
        return 0
      }
      if kernel_task_spawn_count(i) > 0 {
        spawned += 1
      }
    }
    i += 1
  }

  return spawned > 0 ? 1 : 0
}
